#include "MacTextDocumentViewTests.hpp"
#include "MacApp.hpp"
#include "MacInputDoor.hpp"
#include "testing/app/TextInputCommitTestHooks.hpp"
#include <algorithm>
#include <cstdlib>
#include <unistd.h>
#include "MacWindow.hpp"
#include "MacScenePlatformController.hpp"
#include "context/MacEditTextContext.hpp"
#include "context/MacTextDocumentViewContext.hpp"
#include "text/MacTextStore.hpp"
#include "app/nodes/boundary/StdComposition.hpp"
#include "app/nodes/controls/EditText.hpp"
#include "app/nodes/nestable/RowColumn.hpp"
#include "app/nodes/nestable/Show.hpp"
#include "app/text/TextStoreSource.hpp"
#include "core/util/StateTrackerGuard.hpp"
#include "support/LifecycleFactTestAccess.hpp"
#include "support/MacSyntheticMark.hpp"
#include "support/MacCompositionProbe.hpp"
#include "support/MacWindowSession.hpp"
#include "support/TextDocumentAccess.hpp"
#include "support/TextStoreConformance.hpp"
#include "testing/MacScenePlatformTestAccess.hpp"
#include "testing/MacWindowTestAccess.hpp"
#include "testing/app/AppTestAccess.hpp"
#include "testing/scene/SceneTestFlow.hpp"
#include "platform/null/NullPlatformContext.hpp"
#include <cstdio>
#include <cstring>

using namespace loka::app;
using namespace loka::app::scene;
using namespace loka::macos::testing;

@protocol LokaHandInputSource
- (NSString *)selectedKeyboardInputSource;
@end

@protocol LokaPhaseRowOwner
- (MacTextStore *)owner;
@end

struct PhaseCounts
{
  MacTextStore const *row;
  NSTextView *view;
  NSTextStorage *storage;
  unsigned characters, attributes, frames, calls, marks;
};

/** One synchronous ledger for native character/attribute processing, geometry and delegate entries.
    The observer is independent of the row's own delegate. */
@interface LokaDocumentPhase : NSObject
{
  const char *phase_;
  PhaseCounts objects_[2];
  unsigned discards_, alternateUndos_;
}
- (void)begin:(const char *)phase row:(MacTextStore *)row;
- (void)finish;
- (void)recordCall:(const MacTextStore *)row kind:(StoreCall)call;
- (unsigned)characters;
- (unsigned)notes;
- (unsigned)discards;
- (void)discardMarkedText;
- (void)alternateUndo:(id)sender;
- (unsigned)alternateUndos;
@end
namespace
{
  LokaDocumentPhase *activePhase = nil;
  void phaseCall(const MacTextStore *row, StoreCall call)
  {
    [activePhase recordCall:row kind:call];
  }
} // namespace
@implementation LokaDocumentPhase
- (void)watch:(const MacTextStore *)row at:(unsigned)index
{
  PhaseCounts &entry = objects_[index];
  entry.row = row;
  entry.view = [row->view() retain];
  entry.storage = [[entry.view textStorage] retain];
}
- (void)begin:(const char *)phase row:(MacTextStore *)row
{
  LOKA_VERIFY(!activePhase);
  phase_ = phase;
  discards_ = 0;
  [self watch:row at:0];
  activePhase = self;
  observeStoreCalls(row, phaseCall);
  NSNotificationCenter *center = [NSNotificationCenter defaultCenter];
  [center addObserver:self selector:@selector(processed:) name:NSTextStorageDidProcessEditingNotification object:nil];
  [center addObserver:self selector:@selector(frameChanged:) name:NSViewFrameDidChangeNotification object:nil];
}
- (void)recordCall:(const MacTextStore *)row kind:(StoreCall)call
{
  if (row != objects_[0].row && !objects_[1].row && std::strcmp(phase_, "replacement") == 0)
    [self watch:row at:1];
  for (unsigned i = 0; i < 2; ++i)
    if (objects_[i].row == row)
    {
      ++objects_[i].calls;
      if (call == NOTE_EDIT)
        ++objects_[i].marks;
    }
}
- (void)processed:(NSNotification *)note
{
  NSTextStorage *storage = [note object];
  // AppKit may notify observers before invoking its storage delegate during birth.
  if (!objects_[1].row && storage != objects_[0].storage && std::strcmp(phase_, "replacement") == 0)
  {
    id delegate = [storage delegate];
    if ([delegate isKindOfClass:NSClassFromString(@"LokaTextStoreDelegate")])
    {
      MacTextStore *row = [(id<LokaPhaseRowOwner>)delegate owner];
      if (row)
        [self watch:row at:1];
    }
  }
  for (unsigned i = 0; i < 2; ++i)
    if (objects_[i].storage == storage)
    {
      NSUInteger mask = [storage editedMask];
      if (mask & NSTextStorageEditedCharacters)
        ++objects_[i].characters;
      if (mask & NSTextStorageEditedAttributes)
        ++objects_[i].attributes;
    }
}
- (void)frameChanged:(NSNotification *)note
{
  for (unsigned i = 0; i < 2; ++i)
    if (objects_[i].view == [note object])
      ++objects_[i].frames;
}
- (void)alternateUndo:(id)sender
{
  (void)sender;
  ++alternateUndos_;
}
- (unsigned)alternateUndos
{
  return alternateUndos_;
}
- (void)discardMarkedText
{
  ++discards_;
}
- (unsigned)discards
{
  return discards_;
}
- (unsigned)notes
{
  return objects_[0].marks;
}
- (unsigned)characters
{
  return objects_[0].characters;
}
- (void)finish
{
  observeStoreCalls(0);
  activePhase = nil;
  [[NSNotificationCenter defaultCenter] removeObserver:self];
  if (std::strcmp(phase_, "replacement") == 0)
    LOKA_VERIFY(objects_[1].row && objects_[1].characters && objects_[1].marks);
  for (unsigned i = 0; i < 2; ++i)
  {
    PhaseCounts &c = objects_[i];
    if (!c.row)
      continue;
    std::printf("%s row=%p view=%p storage=%p characters=%u attributes=%u frames=%u marks=%u rowCalls=%u\n",
                phase_,
                (const void *)c.row,
                (void *)c.view,
                (void *)c.storage,
                c.characters,
                c.attributes,
                c.frames,
                c.marks,
                c.calls);
    [c.storage release];
    [c.view release];
    c = PhaseCounts();
  }
}
@end

namespace
{
  typedef loka::dsl::testing::SceneTestAccess SceneAccess;
  bool initialWrap = true;
  class ViewRoot : public BoundaryNodeFor<ViewRoot>
  {
  public:
    TextDocument text;
    Focus<int> focus;
    NodeState<loka::core::String> field;
    NodeState<bool> shown;
    NodeState<TextViewAttachment> incomingAttachment;
    explicit ViewRoot(const BoundaryPropsFor<ViewRoot> &props)
        : BoundaryNodeFor<ViewRoot>(props)
    {
      this->document(this->text);
      this->state(this->focus);
      this->state(this->field, loka::core::String("field"));
      this->state(this->shown, true);
      this->state(this->incomingAttachment, TextViewAttachment());
    }
    virtual void composeNode(NodeComposition &c)
    {
      c.declare(VStack() << EditText(EditTextProps(this->field).focusedAs(this->focus, 1))
                         << (Show(*this->shown.state())
                             << TextDocumentEditor(this->text)
                                    .focusedAs(this->focus, 2)
                                    .presentation(TextViewPresentation(TextStyle(), 3, false, initialWrap))));
    }
  };
  class ViewApp : public MacApp
  {
  public:
    ViewApp()
        : MacApp(0)
    {
    }
    void install(MacWindow *window)
    {
      if (this->group_)
        this->group_->adopt(window);
      else
        this->group_ = new AppComponentGroup(std::vector<AppComponent *>(1, window));
      window->setApp(this);
    }
    void flush()
    {
      loka::app::testing::AppTestAccess::flushWindowInvalidations(*this);
      this->reconcileAtCompletion();
    }
  };
  class Pool
  {
    NSAutoreleasePool *pool_;

  public:
    Pool()
        : pool_([[NSAutoreleasePool alloc] init])
    {
      [NSApplication sharedApplication];
    }
    ~Pool()
    {
      [this->pool_ drain];
    }
  };
  Node *find(Node *node, const void *token)
  {
    if (node->nodeTypeKey() == token)
      return node;
    INestable *parent = node->asNestable();
    for (Node *child = parent ? parent->childrenHead() : 0; child; child = child->nextInComposition)
      if (Node *hit = find(child, token))
        return hit;
    return 0;
  }
  TextReplacement replacement(const char *body)
  {
    loka::core::resource::Blob bytes = loka::core::resource::Blob::Create();
    LOKA_VERIFY(bytes.tryAssign(reinterpret_cast<const unsigned char *>(body), std::strlen(body)));
    bytes.sealBytes();
    TextReplacement result;
    LOKA_VERIFY(TextReplacement::FromUtf8(bytes, TextPlacement::AtUtf8Offset(1), result) == TEXT_DOCUMENT_OK);
    return result;
  }
  struct Fixture
  {
    Pool pool;
    NullPlatformContext platform;
    ViewApp app;
    MacWindow *window;
    LokaDocumentPhase *phase;
    explicit Fixture(bool wrap = true)
    {
      initialWrap = wrap;
      WindowProps props;
      props.frame(40, 40, 320, 240).visible(true).scene(new Scene(Boundary<ViewRoot>()));
      window = new MacWindow(&platform, props);
      app.install(window);
      app.flush();
      phase = [[LokaDocumentPhase alloc] init];
    }
    ~Fixture()
    {
      [phase release];
    }
    ViewRoot &root()
    {
      return *static_cast<ViewRoot *>(SceneAccess::rootBoundary(*window->scene()));
    }
    TextDocumentEditorNode &node()
    {
      Node *n = find(&root(), NodeTypeToken<TextDocumentEditorNode>());
      LOKA_VERIFY(n);
      return *static_cast<TextDocumentEditorNode *>(n);
    }
    MacTextDocumentViewContext &ctx()
    {
      LOKA_VERIFY(node().getContext());
      return *static_cast<MacTextDocumentViewContext *>(node().getContext());
    }
    MacTextStore &row()
    {
      TextStoreRow *r = loka::app::testing::TextDocumentAccess::registration(root().text)->store();
      LOKA_VERIFY(r);
      return *static_cast<MacTextStore *>(r);
    }
    MacScenePlatformController &rail()
    {
      return *static_cast<MacScenePlatformController *>(SceneAccess::platformController(*window->scene()));
    }
    NSWindow *native()
    {
      return (NSWindow *)loka::dsl::testing::MacWindowTestAccess::nativeWindow(*window);
    }
    NSTextField *field()
    {
      Node *n = find(&root(), NodeTypeToken<EditTextNode>());
      LOKA_VERIFY(n && n->getContext());
      return (NSTextField *)static_cast<MacEditTextContext *>(n->getContext())->nativeField();
    }
    void verifyKeyFocus()
    {
      NSResponder *actual = [native() firstResponder];
      NodeContext *expected = 0;
      Focused<int> fact;
      if ([field() currentEditor] && actual == [field() currentEditor])
      {
        expected = find(&root(), NodeTypeToken<EditTextNode>())->getContext();
        fact = Focused<int>(1);
      }
      else if (ctx().store() && actual == row().view())
      {
        expected = &ctx();
        fact = Focused<int>(2);
      }
      NodeContext *read = 0;
      LOKA_VERIFY(rail().readNativeFocus(read) && read == expected);
      app.reconcileAtCompletion();
      LOKA_VERIFY(!(root().focus.state()->get() != fact));
    }
    bool key(const char *pin)
    {
      if (loka::testing::MacRequestKeyWindow(native()))
        return true;
      std::printf("[skip] %s: %s\n", pin, loka::testing::MacKeyRefusalReason());
      return false;
    }
    void replace(const char *s)
    {
      LOKA_VERIFY(root().text.replace(replacement(s)) == POST_ACCEPTED);
      app.reconcileAtCompletion();
    }
    void detach()
    {
      ctx().onFactChanged(NODE_FACT_ATTACHED, NODE_FACT_DETACHED_RETAINED);
    }
    void attach()
    {
      ctx().readLifecycleFactOnAttach();
    }
    void show(bool value)
    {
      {
        loka::core::StateTrackerGuard guard(root().tracker());
        root().shown.set(value);
      }
      app.flush();
    }
    void present(bool readOnly)
    {
      TextDocumentEditor next(
          TextDocumentEditorProps(node().props)
              .presentation(TextViewPresentation(TextStyle() + Monospace, 7, readOnly, initialWrap)));
      LOKA_VERIFY(next.applyPropsToNode(&node()));
    }
  };
  void type(NSTextView *view, NSString *text)
  {
    [view insertText:text replacementRange:NSMakeRange(NSNotFound, 0)];
  }
  NSMenuItem *verb(SEL action)
  {
    return [[[NSMenuItem alloc] initWithTitle:@"" action:action keyEquivalent:@""] autorelease];
  }
  void send(NSTextView *view, SEL action)
  {
    [view performSelector:action withObject:nil];
  }
#if defined(MAC_OS_X_VERSION_MAX_ALLOWED) && MAC_OS_X_VERSION_MAX_ALLOWED >= 101200
  const NSEventType keyDownType = NSEventTypeKeyDown;
  const NSUInteger commandKey = NSEventModifierFlagCommand, controlKey = NSEventModifierFlagControl,
                   shiftKey = NSEventModifierFlagShift;
#else
  const NSEventType keyDownType = NSKeyDown;
  const NSUInteger commandKey = NSCommandKeyMask, controlKey = NSControlKeyMask, shiftKey = NSShiftKeyMask;
#endif
  id discardWitness = nil;
  id substituteInputContext(id, SEL)
  {
    return discardWitness;
  }
  NSEvent *keyEvent(NSWindow *window, NSString *chars, NSUInteger flags, unsigned short code)
  {
    return [NSEvent keyEventWithType:keyDownType
                            location:NSZeroPoint
                       modifierFlags:flags
                           timestamp:0
                        windowNumber:[window windowNumber]
                             context:nil
                          characters:chars
         charactersIgnoringModifiers:chars
                           isARepeat:NO
                             keyCode:code];
  }
} // namespace

void testMacTextViewAttachDetach()
{
  Fixture f;
  NSTextView *v = f.row().view();
  NSScrollView *s = f.ctx().scroll();
  LOKA_VERIFY([s superview] && [s window] == f.native() && [v superview] == [s contentView]);
  type(v, @"abcd");
  [v setSelectedRange:NSMakeRange(1, 2)];
  for (unsigned i = 0; i < 2; ++i)
  {
    LOKA_VERIFY(f.row().pending() && [f.row().undoManager() canUndo]);
    [f.phase begin:i ? "attach" : "detach" row:&f.row()];
    if (i)
    {
      f.attach();
      LOKA_VERIFY([s documentView] == v && [v superview] == [s contentView]);
    }
    else
    {
      f.detach();
      LOKA_VERIFY(![v superview] && ![v window]);
    }
    LOKA_VERIFY([[v string] isEqual:@"abcd"] && NSEqualRanges([v selectedRange], NSMakeRange(1, 2)));
    LOKA_VERIFY(f.row().pending() && [f.row().undoManager() canUndo] && ![f.phase characters]);
    [f.phase finish];
  }
}
void testMacTextViewPresentation()
{
  Fixture f;
  f.replace("body");
  NSTextView *v = f.row().view();
  if ([v respondsToSelector:@selector(inputContext)])
  {
    // Exercise the unconditional discard separately from AppKit's own unmark implementation.
    [f.phase begin:"settle-unmarked" row:&f.row()];
    Method getter = class_getInstanceMethod(object_getClass(v), @selector(inputContext));
    discardWitness = f.phase;
    IMP original = method_setImplementation(getter, reinterpret_cast<IMP>(&substituteInputContext));
    f.rail().settleTextComposition(f.row());
    method_setImplementation(getter, original);
    discardWitness = nil;
    LOKA_VERIFY([f.phase discards] == 1);
    [f.phase finish];
  }
  else
    std::printf("[skip] input context unavailable on this OS\n");
  loka::testing::MacSendMark(v);
  TextStoreReading marked;
  LOKA_VERIFY(f.row().read(marked) && marked.marked());
  [f.phase begin:"settle-marked" row:&f.row()];
  f.rail().settleTextComposition(f.row());
  LOKA_VERIFY(![v hasMarkedText] && f.row().pending());
  [f.phase finish];
  [f.phase begin:"presentation-editable" row:&f.row()];
  type(v, @"X");
  NSRange selected = [v selectedRange];
  NSString *body = [[v string] copy];
  [f.phase finish];
  [f.phase begin:"presentation-compatible" row:&f.row()];
  f.present(false);
  LOKA_VERIFY([[v string] isEqual:body] && NSEqualRanges([v selectedRange], selected) && f.row().pending() &&
              [f.row().undoManager() canUndo] && ![f.phase characters]);
  [body release];
  [f.phase finish];
  loka::testing::MacSendMark(v);
  NSString *committed = [[v string] copy];
  [f.phase begin:"presentation" row:&f.row()];
  f.present(true);
  LOKA_VERIFY(![v hasMarkedText] && ![v isEditable] && [v isSelectable]);
  NSFont *expected = (NSFont *)f.rail().textFont(TextStyle() + Monospace);
  LOKA_VERIFY([[v font] isEqual:expected] &&
              [[[v typingAttributes] objectForKey:NSFontAttributeName] isEqual:expected]);
  LOKA_VERIFY(NSEqualSizes([v textContainerInset], NSMakeSize(7, 7)));
  type(v, @"X");
  LOKA_VERIFY([[v string] isEqual:committed]);
  f.app.reconcileAtCompletion();
  TextPosition caret;
  LOKA_VERIFY(f.root().text.queryPositionAtUtf8(2, caret));
  f.root().text.post(caret);
  f.app.reconcileAtCompletion();
  LOKA_VERIFY([v selectedRange].location == 2 && f.root().text.fact()->get().caret() == caret);
  [committed release];
  [f.phase finish];
  f.present(false);
  LOKA_VERIFY([v isEditable]);
  loka::testing::MacSendMark(v);
  f.detach();
  LOKA_VERIFY(![v hasMarkedText] && ![v window]);
}
namespace
{
  void countWrite(void *data)
  {
    ++*static_cast<unsigned *>(data);
  }
  void markAndLeave(Fixture &f)
  {
    NSTextView *v = f.row().view();
    LOKA_VERIFY([f.native() makeFirstResponder:v]);
    loka::testing::MacSendMark(v);
    LOKA_VERIFY([v markedRange].length && [f.native() makeFirstResponder:f.field()]);
    LOKA_VERIFY([f.field() currentEditor] && [v markedRange].length);
  }
} // namespace
void testMacTextViewComposition()
{
  Fixture f;
  NSTextView *v = f.row().view();
  bool key = f.key("composition with first responder");
  TextDocument &doc = f.root().text;
  for (unsigned selector = 0; selector < 2; ++selector)
  {
    f.present(false);
    if (key)
      LOKA_VERIFY([f.native() makeFirstResponder:v]);
    TextPosition caret = doc.fact()->get().caret();
    [f.phase begin:"mark" row:&f.row()];
    unsigned writes = 0;
    doc.fact()->bind(&countWrite, &writes, false);
    loka::testing::MacSendMark(v, selector != 0);
    doc.post(caret);
    LOKA_VERIFY([v hasMarkedText] && [v markedRange].length && f.row().pending() && !writes);
    f.app.reconcileAtCompletion();
    LOKA_VERIFY(doc.fact()->get().state() == TEXT_DOCUMENT_MARKED && writes == 1 && [f.phase characters] &&
                [f.phase notes]);
    LOKA_VERIFY(loka::app::testing::TextDocumentAccess::pending(doc));
    TextStoreReading reading;
    LOKA_VERIFY(f.row().read(reading) && reading.marked() && !reading.changed());
    LOKA_VERIFY(f.app.commitTextInput() == TEXT_INPUT_SETTLED && ![v hasMarkedText]);
    doc.fact()->unbind(&countWrite, &writes);
    [f.phase finish];
    f.present(true);
    for (unsigned focus = 0; focus < (key ? 2u : 1u); ++focus)
    {
      LOKA_VERIFY([f.native() makeFirstResponder:focus ? (NSResponder *)v : f.field()]);
      LOKA_VERIFY(focus ? [f.native() firstResponder] == v : [f.native() firstResponder] != v);
      NSString *body = [[v string] copy];
      [f.phase begin:"read-only mark" row:&f.row()];
      std::printf("read-only selector=%u first=%d inputContext=%p\n",
                  selector,
                  [f.native() firstResponder] == v,
                  [v respondsToSelector:@selector(inputContext)] ? (void *)[v inputContext] : 0);
      loka::testing::MacSendMark(v, selector != 0);
      LOKA_VERIFY([[v string] isEqual:body] && ![v hasMarkedText] && ![v markedRange].length && ![f.phase characters]
                  && !f.row().pending());
      [f.phase finish];
      [body release];
    }
  }
}

// Observe the native entry before AppKit can change selection, discard a mark or remove the view.
namespace
{
  class TransitionWitness;
  TransitionWitness *transition = 0;
  class TransitionWitness
  {
    Method setter_, removal_, discard_, sizing_;
    IMP set_, remove_, discardImpl_, sizeImpl_;
    NSTextView *view_;
    unsigned setters_, removals_;
    bool attaching_, retiring_;
    static void sizeForAttach(id view, SEL cmd, NSSize size)
    {
      TransitionWitness &w = *transition;
      reinterpret_cast<void (*)(id, SEL, NSSize)>(w.sizeImpl_)(view, cmd, size);
      if (view == w.view_ && w.setters_ == 1 && [view superview] && ![view isEditable]
          && !MacTextStore::attachedRow(view))
      {
        // Last native attach operation, before the kernel links the claim. Re-arm one existing character
        // so skipping post-link settlement is visible at the second read-only setter.
        [view setEditable:YES];
        NSRange first = NSMakeRange(0, 1);
        NSString *same = [[view string] substringWithRange:first];
        [view setMarkedText:same selectedRange:NSMakeRange(1, 0) replacementRange:first];
        LOKA_VERIFY([view markedRange].length);
      }
    }
    static void discardMark(id context, SEL cmd)
    {
      TransitionWitness &w = *transition;
      if ([w.view_ inputContext] == context)
        LOKA_VERIFY(![w.view_ hasMarkedText]);
      reinterpret_cast<void (*)(id, SEL)>(w.discardImpl_)(context, cmd);
    }
    static void setEditable(id view, SEL cmd, BOOL editable)
    {
      TransitionWitness &w = *transition;
      if (view == w.view_ && !editable)
      {
        std::printf("setEditable:NO #%u linked=%d marked=%d\n",
                    w.setters_ + 1,
                    MacTextStore::attachedRow(view) != 0,
                    [view hasMarkedText]);
        LOKA_VERIFY(![view hasMarkedText]);
        if (w.attaching_)
        {
          LOKA_VERIFY((MacTextStore::attachedRow(view) != 0) == (w.setters_ != 0));
          if (w.setters_)
            LOKA_VERIFY([view isEditable]);
        }
        ++w.setters_;
      }
      reinterpret_cast<void (*)(id, SEL, BOOL)>(w.set_)(view, cmd, editable);
    }
    static void removeView(id view, SEL cmd)
    {
      TransitionWitness &w = *transition;
      if (view == w.view_ && [view superview])
      {
        std::printf("removeFromSuperview marked=%d\n", [view hasMarkedText]);
        // Window teardown can withdraw the root document before its child's context is peeled.
        // Direct retirement only cuts routes. A surviving parked row must have settled here.
        LOKA_VERIFY(w.retiring_ || ![view hasMarkedText]);
        MacTextStore *row = MacTextStore::attachedRow(view);
        if (row && !w.retiring_)
        {
          NSRange selection = [view selectedRange];
          BOOL undo = [row->undoManager() canUndo];
          bool pending = row->pending();
          reinterpret_cast<void (*)(id, SEL)>(w.remove_)(view, cmd);
          LOKA_VERIFY(NSEqualRanges(selection, [view selectedRange]) && [row->undoManager() canUndo] == undo
                      && row->pending() == pending);
          ++w.removals_;
          return;
        }
        ++w.removals_;
      }
      reinterpret_cast<void (*)(id, SEL)>(w.remove_)(view, cmd);
    }

  public:
    TransitionWitness(NSTextView *view, bool attaching, bool retiring)
        : discard_(0),
          sizing_(0),
          view_(view),
          setters_(0),
          removals_(0),
          attaching_(attaching),
          retiring_(retiring)
    {
      LOKA_VERIFY(!transition);
      transition = this;
      id context = [view respondsToSelector:@selector(inputContext)] ? [view inputContext] : nil;
      if (context && !retiring)
      {
        discard_ = class_getInstanceMethod([context class], @selector(discardMarkedText));
        discardImpl_ = method_setImplementation(discard_, reinterpret_cast<IMP>(&discardMark));
      }
      if (attaching)
      {
        sizing_ = class_getInstanceMethod([view class], @selector(setMinSize:));
        sizeImpl_ = method_setImplementation(sizing_, reinterpret_cast<IMP>(&sizeForAttach));
      }
      setter_ = class_getInstanceMethod([view class], @selector(setEditable:));
      removal_ = class_getInstanceMethod([view class], @selector(removeFromSuperview));
      set_ = method_setImplementation(setter_, reinterpret_cast<IMP>(&setEditable));
      remove_ = method_setImplementation(removal_, reinterpret_cast<IMP>(&removeView));
    }
    ~TransitionWitness()
    {
      if (sizing_)
        method_setImplementation(sizing_, sizeImpl_);
      if (discard_)
        method_setImplementation(discard_, discardImpl_);
      method_setImplementation(setter_, set_);
      method_setImplementation(removal_, remove_);
      transition = 0;
    }
    void expect(unsigned setters, unsigned removals) const
    {
      LOKA_VERIFY(setters_ == setters && removals_ == removals);
    }
  };
} // namespace
void testMacTextViewCompositionTransitions()
{
  enum Transition
  {
    COMPATIBLE,
    READ_ONLY_PROPS,
    HIDE_RETURN,
    REFUSED,
    EVICTED,
    RETIRED_FACT,
    CLEAR_CONTEXT,
    DESTROY_WINDOW,
    READ_ONLY_ATTACH
  };
  const char *names[] = {"compatible",
                         "read-only",
                         "retained hide/return",
                         "occupied refusal",
                         "eviction",
                         "retired delivery",
                         "setContext(0)",
                         "window destruction",
                         "read-only attach"};
  for (unsigned mode = 0; mode < sizeof names / sizeof names[0]; ++mode)
  {
    Fixture f;
    if (!f.key(names[mode]))
      continue;
    TextDocument &doc = f.root().text;
    NSTextView *v = f.row().view();
    const MacTextStore *const watched = &f.row();
    TextDocumentEditorNode incoming(TextDocumentEditorProps(doc).attachment(f.root().incomingAttachment));
    type(v, @"body");
    f.app.reconcileAtCompletion();
    [f.phase begin:names[mode] row:&f.row()];
    markAndLeave(f);
    NSString *body = [[v string] copy];
    NSRange range = [v markedRange], selection = [v selectedRange];
    unsigned writes = 0;
    unsigned long revision = doc.fact()->get().textRevision();
    std::printf("%s before revision=%lu selection=%s marked=%s body=%s notes=%u\n",
                names[mode],
                revision,
                [NSStringFromRange(selection) UTF8String],
                [NSStringFromRange(range) UTF8String],
                [body UTF8String],
                [f.phase notes]);
    LOKA_VERIFY([f.phase notes] && f.row().pending());
    doc.fact()->bind(&countWrite, &writes, false);
    TransitionWitness witness(v, mode == READ_ONLY_ATTACH, mode == DESTROY_WINDOW);
    if (mode == COMPATIBLE || mode == READ_ONLY_PROPS)
      f.present(mode == READ_ONLY_PROPS);
    else if (mode == HIDE_RETURN)
      f.show(false);
    else if (mode == REFUSED || mode == EVICTED)
    {
      if (mode == EVICTED)
        NotifySubtreeNodeDetached(&f.node());
      MacTextDocumentViewContext *ctx = f.rail().createTextViewContext(incoming, f.rail().projectionParentView());
      LOKA_VERIFY(ctx && incoming.adoptContext(ctx));
      ctx->readLifecycleFactOnAttach();
      LOKA_VERIFY(ctx->store() == (mode == EVICTED ? &f.row() : 0));
      LOKA_VERIFY(f.ctx().store() == (mode == REFUSED ? &f.row() : 0));
      if (mode == EVICTED)
        LifecycleFactTestAccess::DeliverFacts(&f.node());
    }
    else if (mode == RETIRED_FACT)
    {
      LifecycleFactTestAccess::MarkSubtreeRetired(&f.node());
      LifecycleFactTestAccess::DeliverFacts(&f.node());
    }
    else if (mode == CLEAR_CONTEXT)
      f.node().setContext(0);
    else if (mode == DESTROY_WINDOW)
    {
      // Destruction withdraws the publisher. The recorder owns the native view across reclaim.
      doc.fact()->unbind(&countWrite, &writes);
      [f.native() close];
      f.app.flush();
    }
    else
    {
      f.detach();
      f.present(true);
      // A deliberately marked, editable parked row exercises the first read-only setter on attach.
      loka::testing::MacSendMark(v);
      LOKA_VERIFY([v markedRange].length);
      NodeContext *focus = 0;
      LOKA_VERIFY(f.rail().readNativeFocus(focus) && focus != &f.ctx() && !f.rail().applyNativeFocus(f.ctx()));
      LOKA_VERIFY([v markedRange].length);
      [body release];
      body = [[v string] copy];
      f.attach();
    }
    const bool survives = mode == COMPATIBLE || mode == REFUSED;
    witness.expect(mode == READ_ONLY_PROPS    ? 1
                   : mode == READ_ONLY_ATTACH ? 2
                                              : 0,
                   survives || mode == READ_ONLY_PROPS ? 0 : 1);
    if (mode == DESTROY_WINDOW)
    {
      LOKA_VERIFY(![v delegate] && ![v superview]);
      observeStoreCalls(watched, phaseCall);
      [v insertText:@"late" replacementRange:NSMakeRange(NSNotFound, 0)];
      LOKA_VERIFY(!observedStoreCalls());
    }
    else
      LOKA_VERIFY([[v string] isEqual:body] && ([v hasMarkedText] != NO) == survives);
    if (survives)
      LOKA_VERIFY(NSEqualRanges([v markedRange], range) && NSEqualRanges([v selectedRange], selection));
    if (mode != DESTROY_WINDOW)
    {
      if (mode != HIDE_RETURN)
        LOKA_VERIFY(f.row().pending() && [f.row().undoManager() canUndo]);
      f.app.reconcileAtCompletion();
      if (mode == REFUSED || mode == EVICTED)
        LOKA_VERIFY(
            f.root().incomingAttachment.get().value()
            == (mode == REFUSED ? TextViewAttachment::VIEW_REFUSED_OCCUPIED : TextViewAttachment::VIEW_ATTACHED));
      LOKA_VERIFY(writes == 1 && doc.fact()->get().textRevision() == revision + 1);
      LOKA_VERIFY(doc.fact()->get().state() == (survives ? TEXT_DOCUMENT_MARKED : TEXT_DOCUMENT_READY));
      std::printf("%s revision=%lu writes=%u selection=%s marked=%s body=%s\n",
                  names[mode],
                  doc.fact()->get().textRevision(),
                  writes,
                  [NSStringFromRange([v selectedRange]) UTF8String],
                  [NSStringFromRange([v markedRange]) UTF8String],
                  [[v string] UTF8String]);
      if (mode == HIDE_RETURN)
      {
        f.show(true);
        LOKA_VERIFY(f.ctx().store() == &f.row() && ![v hasMarkedText] && [[v string] isEqual:body]);
      }
      doc.fact()->unbind(&countWrite, &writes);
    }
    [body release];
    [f.phase finish];
  }
}
void testMacTextViewUndo()
{
  Fixture f;
  bool key = f.key("Edit click, Cmd-Z and nil-target redo");
  NSTextView *v = [f.row().view() retain];
  NSMenuItem *undo = verb(@selector(undo:)), *redo = verb(@selector(redo:));
  NSUndoManager *manager = f.row().undoManager();
  // Separate groups so both histories have a live step at the read-only transition.
  [manager setGroupsByEvent:NO];
  [manager beginUndoGrouping];
  type(v, @"A");
  [manager endUndoGrouping];
  if ([v respondsToSelector:@selector(breakUndoCoalescing)])
    [v breakUndoCoalescing];
  [manager beginUndoGrouping];
  type(v, @"B");
  [manager endUndoGrouping];
  NSUndoManager *alternate = [f.native() undoManager];
  LOKA_VERIFY(alternate && alternate != manager);
  [alternate registerUndoWithTarget:f.phase selector:@selector(alternateUndo:) object:nil];
  // Each history query answers its own verb: only Undo before undoing, only Redo once exhausted.
  LOKA_VERIFY([v validateMenuItem:undo] && ![v validateMenuItem:redo]);
  // Two groups were typed; the bound keeps a regressed undo from looping forever.
  unsigned steps = 0;
  while (steps < 2 && [v validateMenuItem:undo])
  {
    send(v, @selector(undo:));
    ++steps;
  }
  LOKA_VERIFY(steps == 2 && [[v string] isEqual:@""] && ![v validateMenuItem:undo] && [v validateMenuItem:redo]);
  while (steps--)
    send(v, @selector(redo:));
  LOKA_VERIFY([[v string] isEqual:@"AB"] && [v validateMenuItem:undo] && ![v validateMenuItem:redo]);
  // Other verbs reach NSTextView's own validation.
  if ([NSTextView instancesRespondToSelector:@selector(validateMenuItem:)])
    std::printf("[skip] validateUserInterfaceItem: fallback: this AppKit's NSTextView answers validateMenuItem:\n");
  NSMenuItem *copy = verb(@selector(copy:));
  [v setSelectedRange:NSMakeRange(0, 0)];
  LOKA_VERIFY(![v validateMenuItem:copy]);
  [v setSelectedRange:NSMakeRange(0, 1)];
  LOKA_VERIFY([v validateMenuItem:copy]);
  [v setSelectedRange:NSMakeRange(2, 0)];
  MenuBarDefinition bar;
  bar << EditMenu();
  const MenuSources sources = {&bar, 0, 0};
  if (key)
  {
    LOKA_VERIFY([f.native() makeFirstResponder:v]);
    LOKA_VERIFY(f.app.menuAttachment().project(sources));
    NSMenu *menu = [[[NSApp mainMenu] itemAtIndex:0] submenu];
    LOKA_VERIFY([NSApp targetForAction:@selector(undo:)] == v);
    LOKA_VERIFY([NSApp sendAction:@selector(undo:) to:nil from:undo] && [[v string] isEqual:@"A"]);
    LOKA_VERIFY([NSApp sendAction:@selector(redo:) to:nil from:redo] && [[v string] isEqual:@"AB"]);
    [menu performActionForItemAtIndex:0];
    LOKA_VERIFY([[v string] isEqual:@"A"]);
    LOKA_VERIFY([NSApp sendAction:@selector(redo:) to:nil from:redo] && [[v string] isEqual:@"AB"]);
    LOKA_VERIFY([[NSApp mainMenu] performKeyEquivalent:keyEvent(f.native(), @"z", commandKey, 6)]);
  }
  else
    send(v, @selector(undo:));
  LOKA_VERIFY([[v string] isEqual:@"A"] && [v validateMenuItem:undo] && [v validateMenuItem:redo]);
  NSTextView *impostor = [[[v class] alloc] initWithFrame:NSZeroRect];
  [impostor setEditable:YES];
  [impostor setDelegate:[v delegate]];
  LOKA_VERIFY(![impostor validateMenuItem:undo]);
  send(impostor, @selector(undo:));
  LOKA_VERIFY([[v string] isEqual:@"A"]);
  [impostor setDelegate:nil];
  [impostor release];
  LOKA_VERIFY([v validateMenuItem:undo]);
  f.present(true);
  send(v, @selector(undo:));
  LOKA_VERIFY([[v string] isEqual:@"A"] && [manager canUndo] && [manager canRedo]);
  send(v, @selector(redo:));
  LOKA_VERIFY([[v string] isEqual:@"A"] && ![v validateMenuItem:undo] && ![v validateMenuItem:redo]);
  LOKA_VERIFY([manager canUndo] && [manager canRedo] && [alternate canUndo] && ![f.phase alternateUndos]);
  f.present(false);
  f.detach();
  send(v, @selector(undo:));
  LOKA_VERIFY([[v string] isEqual:@"A"]);
  send(v, @selector(redo:));
  LOKA_VERIFY(![v validateMenuItem:undo] && ![v validateMenuItem:redo] && [[v string] isEqual:@"A"]);
  f.attach();
  f.replace("fresh");
  for (unsigned drain = 0; drain < 2; ++drain)
  {
    if (drain)
      f.rail().drainNativeRetirements();
    LOKA_VERIFY(![v validateMenuItem:undo] && ![v validateMenuItem:redo]);
    send(v, @selector(undo:));
    send(v, @selector(redo:));
    LOKA_VERIFY([[f.row().view() string] isEqual:@"fresh"] && [alternate canUndo]);
  }
  f.app.menuAttachment().disconnect();
  [v release];
}
void testMacTextViewFocus()
{
  Fixture f;
  if (!f.key("focus read/apply and key-window transfer"))
    return;
  NSWindow *w = f.native();
  NSTextView *v = f.row().view();
  NodeContext *read = 0;
  LOKA_VERIFY([v window] == w && f.rail().applyNativeFocus(f.ctx()) && [w firstResponder] == v);
  LOKA_VERIFY(f.rail().readNativeFocus(read) && read == &f.ctx());
  f.detach();
  LOKA_VERIFY(!f.rail().applyNativeFocus(f.ctx()) && !MacTextStore::fromNativeFocus(v));
  LOKA_VERIFY(!MacTextStore::fromNativeFocus(w));
  f.verifyKeyFocus();
  f.attach();
  Fixture other;
  if (other.key("foreign key window"))
  {
    LOKA_VERIFY(other.rail().applyNativeFocus(other.ctx()));
    LOKA_VERIFY(!f.rail().readNativeFocus(read) && !f.rail().applyNativeFocus(f.ctx()));
  }
}

// During a focused swap AppKit may change selection and cause storage processing.
// Inject both effects before the rail restores placement and takes birth edits.
// A category method exchanged with NSWindow's for the swap only: AppKit may already have
// moved the window to a KVO subclass, so the window's class itself is left alone.
@interface NSWindow (LokaSwapWindow)
- (BOOL)lokaSwapMakeFirstResponder:(NSResponder *)responder;
@end
namespace
{
  NSTextView *swappingOld = nil;
  bool refuseFocus = false;
  unsigned focusOffers = 0;
} // namespace
@implementation NSWindow (LokaSwapWindow)
- (BOOL)lokaSwapMakeFirstResponder:(NSResponder *)responder
{
  if (swappingOld && responder != swappingOld && [responder isKindOfClass:[NSTextView class]]
      && MacTextStore::fromNativeFocus(responder))
  {
    LOKA_VERIFY(![swappingOld hasMarkedText]);
    ++focusOffers;
    NSTextView *fresh = (NSTextView *)responder;
    [[fresh textStorage] replaceCharactersInRange:NSMakeRange(0, 1) withString:@"n"];
    [fresh setSelectedRange:NSMakeRange(0, 0)];
    if (refuseFocus)
      return NO;
  }
  return [self lokaSwapMakeFirstResponder:responder]; // the exchanged original
}
@end
namespace
{
  void exchangeSwapWindow()
  {
    method_exchangeImplementations(class_getInstanceMethod([NSWindow class], @selector(makeFirstResponder:)),
                                   class_getInstanceMethod([NSWindow class], @selector(lokaSwapMakeFirstResponder:)));
  }
} // namespace
void testMacTextViewReplacement()
{
  for (unsigned mode = 0; mode < 3; ++mode)
  {
    Fixture f(false);
    if (mode && !f.key("focused replacement"))
      continue;
    if (mode)
    {
      TextDocument &doc = f.root().text;
      TextPosition caret = doc.fact()->get().caret();
      TextSplice splice;
      LOKA_VERIFY(TextSplice::FromUtf8(caret, caret, "x", 1, TextPlacement::End(), splice) == TEXT_DOCUMENT_OK);
      markAndLeave(f);
      MacTextStore *original = &f.row();
      NSTextView *marked = original->view();
      NSString *body = [[marked string] copy];
      NSRange range = [marked markedRange];
      doc.post(caret);
      f.replace("refused");
      LOKA_VERIFY(doc.replaceReply()->get().reason() == TEXT_REPLACE_COMPOSING);
      unsigned long length;
      LOKA_VERIFY(!doc.queryLength(length) && loka::app::testing::TextDocumentAccess::pending(doc));
      LOKA_VERIFY(&f.row() == original && f.ctx().store() == original && f.row().view() == marked &&
                  [[marked string] isEqual:body] && NSEqualRanges(range, [marked markedRange]));
      LOKA_VERIFY(f.app.commitTextInput() == TEXT_INPUT_SETTLED && ![marked hasMarkedText]);
      // Exercise the independent splice decision inside a test-only editable interval.
      f.present(true);
      [marked setEditable:YES];
      markAndLeave(f);
      LOKA_VERIFY(doc.splice(splice) == POST_ACCEPTED);
      f.app.reconcileAtCompletion();
      LOKA_VERIFY(doc.spliceReply()->get().kind() == Reply<TextSplice>::REFUSED
                  && doc.spliceReply()->get().reason() == TEXT_SPLICE_COMPOSING && [marked markedRange].length);
      LOKA_VERIFY(f.app.commitTextInput() == TEXT_INPUT_SETTLED && ![marked hasMarkedText]);
      [marked setEditable:NO];
      [body release];
    }
    f.present(true);
    NSWindow *w = f.native();
    NSTextView *old = [f.row().view() retain];
    if (mode)
      LOKA_VERIFY([w makeFirstResponder:old]);
    else
      LOKA_VERIFY([w makeFirstResponder:nil]);
    LOKA_VERIFY([w class] == [NSWindow class]);
    exchangeSwapWindow();
    swappingOld = old;
    refuseFocus = mode == 2;
    focusOffers = 0;
    const unsigned long beforeRevision = f.root().text.fact()->get().textRevision();
    [f.phase begin:"replacement" row:&f.row()];
    f.replace("new body");
    swappingOld = nil;
    exchangeSwapWindow();
    [f.phase finish];
    NSTextView *fresh = f.row().view();
    LOKA_VERIFY([f.ctx().scroll() documentView] == fresh && fresh != old && ![old superview] && ![old window]);
    LOKA_VERIFY(!MacTextStore::fromNativeFocus(old) && MacTextStore::fromNativeFocus(fresh) == &f.ctx());
    LOKA_VERIFY([[fresh string] isEqual:@"new body"] && NSEqualRanges([fresh selectedRange], NSMakeRange(1, 0)));
    LOKA_VERIFY(![fresh isEditable] && [[fresh font] isEqual:[old font]]
                && NSEqualSizes([fresh textContainerInset], [old textContainerInset]) &&
                [fresh isHorizontallyResizable]);
    LOKA_VERIFY((focusOffers != 0) == (mode != 0) && ([w firstResponder] == fresh) == (mode == 1));
    TextStoreReading reading;
    LOKA_VERIFY(!f.row().pending() && f.row().read(reading) && !reading.changed() && !reading.marked()
                && reading.caret() == 1);
    unsigned long revision = f.root().text.fact()->get().textRevision();
    LOKA_VERIFY(revision == beforeRevision + 1);
    if (mode)
      f.verifyKeyFocus();
    f.app.reconcileAtCompletion();
    LOKA_VERIFY(f.root().text.fact()->get().textRevision() == revision);
    [old release];
  }
  // Inspect the birth result at the rail boundary before a publisher can consume it.
  Fixture f;
  MacTextStore &old = f.row();
  TextStoreSource source(replacement("born"));
  TextStoreRow *fresh = 0;
  TextStoreReading birth;
  LOKA_VERIFY(f.rail().createTextStore(source, &old, fresh, birth) == TEXT_STORE_BORN && fresh);
  LOKA_VERIFY(f.ctx().store() == fresh && !birth.changed() && !birth.marked() && birth.caret() == 1
              && birth.anchor() == 1);
  TextChangedRange range;
  LOKA_VERIFY(birth.queryRange(range) && range == TextChangedRange(0, 0, 4) && !f.rail().peekTextStore(*fresh));
  fresh->retire();
  f.attach();
  LOKA_VERIFY(f.ctx().store() == &old);
}
void testMacTextViewReplacementRollback()
{
  Fixture f;
  bool key = f.key("rollback first responder");
  MacTextStore &old = f.row();
  NSTextView *v = old.view();
  if (key)
    LOKA_VERIFY([f.native() makeFirstResponder:v]);
  type(v, @"old");
  [v setSelectedRange:NSMakeRange(1, 2)];
  for (unsigned i = 0; i <= STORE_ALLOCATION_COUNT; ++i)
  {
    bool preparation = i == STORE_ALLOCATION_COUNT;
    if (preparation)
      failTextViewPreparation(1);
    else
      failStoreAllocation(static_cast<StoreAllocation>(i), 1);
    TextStoreSource source(replacement("fresh"));
    TextStoreRow *fresh = &old;
    TextStoreReading birth;
    LOKA_VERIFY(f.rail().createTextStore(source, &old, fresh, birth)
                == (preparation ? TEXT_STORE_REFUSED_UNSUPPORTED : TEXT_STORE_REFUSED_ALLOCATION));
    LOKA_VERIFY(!fresh && f.ctx().store() == &old && [f.ctx().scroll() documentView] == v);
    LOKA_VERIFY([[v string] isEqual:@"old"] && NSEqualRanges([v selectedRange], NSMakeRange(1, 2)) &&
                [old.undoManager() canUndo] && old.pending());
    LOKA_VERIFY(!key || [f.native() firstResponder] == v);
    LOKA_VERIFY(loka::app::testing::TextStoreTestAccess::retiredCount(old) == (i >= STORAGE ? 1u : 0u));
    f.rail().drainNativeRetirements();
  }
}
void testMacTextViewLayout()
{
  for (unsigned wrap = 0; wrap < 2; ++wrap)
  {
    Fixture f(wrap != 0);
    f.detach();
    f.rail().relayout(320, 240);
    LOKA_VERIFY(!loka::dsl::testing::MacScenePlatformTestAccess::hasPendingRelayout(f.rail()));
    f.attach();
    LOKA_VERIFY(loka::dsl::testing::MacScenePlatformTestAccess::hasPendingRelayout(f.rail()));
    LayoutState state;
    state.x = 7;
    state.y = 11;
    state.width = 140;
    state.height = 60;
    LOKA_VERIFY(f.ctx().layout(&f.rail(), state) == 71);
    NSTextView *v = f.row().view();
    NSScrollView *s = f.ctx().scroll();
    LOKA_VERIFY(NSEqualRects([s frame], f.rail().projection().projectFrame(loka::core::Frame(7, 11, 140, 60)).r));
    LOKA_VERIFY(NSEqualSizes([v minSize], [s contentSize]));
    LOKA_VERIFY(([[v textContainer] widthTracksTextView] != NO) == (wrap != 0)
                && ([v isHorizontallyResizable] != NO) == !wrap);
    if (wrap)
    {
      // A same-size reattach with a margin keeps the tracked width (view width less both insets).
      // On Tahoe 26.7.1 the presentation's setTextContainerInset: after attach re-tracks the width.
      const CGFloat inset = [v textContainerInset].width;
      LOKA_VERIFY(inset > 0 && [[v textContainer] containerSize].width == [v bounds].size.width - 2 * inset);
      f.detach();
      f.attach();
      LOKA_VERIFY([[v textContainer] containerSize].width == [v bounds].size.width - 2 * inset);
    }
  }
}
void testMacTextViewKeyViewLoop()
{
  Fixture f;
  if (!f.key("key-view commands, Tab into and Control-Tab out of the document view"))
    return;
  NSWindow *w = f.native();
  NSTextField *field = f.field();
  for (unsigned stage = 0; stage < 3; ++stage)
  {
    if (stage == 1)
      f.replace("replacement");
    if (stage == 2)
    {
      f.detach();
      LOKA_VERIFY(![f.row().view() window]);
      f.node().setContext(0);
      f.rail().relayout(320, 240);
    }
    NSTextView *v = f.row().view();
    LOKA_VERIFY([w makeFirstResponder:field]);
    // Traversal commands name the field: while it edits, the first responder is its field editor.
    [w selectKeyViewFollowingView:field];
    LOKA_VERIFY([w firstResponder] == v);
    [w selectKeyViewPrecedingView:v];
    LOKA_VERIFY([field currentEditor]);
    [w sendEvent:keyEvent(w, @"\t", 0, 48)];
    LOKA_VERIFY([w firstResponder] == v);
    // A document text view keeps plain and Option-Tab as text input (Tahoe 26.7.1:
    // Option-Tab inserts a tab, Shift-Tab does nothing); Control-Tab and
    // Control-Shift-Tab are the AppKit keys that leave a multi-line text view.
    NSString *body = [[v string] copy];
    [w sendEvent:keyEvent(w, @"\x19", controlKey | shiftKey, 48)];
    LOKA_VERIFY([field currentEditor]);
    [w sendEvent:keyEvent(w, @"\t", 0, 48)];
    LOKA_VERIFY([w firstResponder] == v);
    [w sendEvent:keyEvent(w, @"\t", controlKey, 48)];
    LOKA_VERIFY([[v string] isEqual:body]);
    [body release];
    LOKA_VERIFY([field currentEditor]);
  }
}

// At the actual removal edge the base destructor has not had a second chance to unlink.
@interface LokaRemovalScroll : NSScrollView
@end
namespace
{
  MacTextDocumentViewContext *removing = 0;
  unsigned removals = 0;
  id refuseScrollInit(id scroll, SEL, NSRect)
  {
    [scroll release];
    return nil;
  }
} // namespace
@implementation LokaRemovalScroll
- (void)viewWillMoveToSuperview:(NSView *)parent
{
  if (!parent && removing)
  {
    ++removals;
    LOKA_VERIFY(!removing->store());
  }
  [super viewWillMoveToSuperview:parent];
}
@end
void testMacTextViewLifecycle()
{
  for (unsigned mode = 0; mode < 2; ++mode)
  {
    Fixture f;
    RefusedNodeHandler refused((NodeTypeToken<TextDocumentEditorNode>()));
    LOKA_VERIFY(!f.rail().registerNodeHandler(&refused));
    f.node().setContext(0);
    f.rail().relayout(320, 240);
    LOKA_VERIFY(!refused.refusalCount() && f.ctx().store() == &f.row());
    unsigned claims = loka::app::testing::TextDocumentAccess::claimCount(f.window->scene()->textDocuments());
    Method init = class_getInstanceMethod([NSScrollView class], @selector(initWithFrame:));
    IMP normal = method_setImplementation(init, reinterpret_cast<IMP>(&refuseScrollInit));
    MacTextDocumentViewContext *failed = f.rail().createTextViewContext(f.node(), f.rail().projectionParentView());
    method_setImplementation(init, normal);
    LOKA_VERIFY(!failed
                && loka::app::testing::TextDocumentAccess::claimCount(f.window->scene()->textDocuments()) == claims);
    MacScenePlatformController orphan(0, RailMetrics());
    LOKA_VERIFY(!orphan.createTextViewContext(f.node(), 0));
    f.show(false);
    LOKA_VERIFY(![f.row().view() superview]);
    f.show(true);
    NSTextView *v = [f.row().view() retain];
    NSScrollView *s = [f.ctx().scroll() retain];
    LOKA_VERIFY(object_getClass(s) == [NSScrollView class]);
    Class original = object_setClass(s, [LokaRemovalScroll class]);
    removing = &f.ctx();
    removals = 0;
    if (mode)
      f.node().setContext(0);
    else
    {
      LifecycleFactTestAccess::MarkSubtreeRetired(&f.node());
      LifecycleFactTestAccess::DeliverFacts(&f.node());
    }
    removing = 0;
    object_setClass(s, original);
    LOKA_VERIFY(removals == 1 && ![s superview] && ![v superview]);
    [f.phase begin:"retire/drain" row:&f.row()];
    // The ordinary window teardown delivers RETIRED to the sibling field before clearing contexts.
    LifecycleFactTestAccess::MarkSubtreeRetired(&f.root());
    LifecycleFactTestAccess::DeliverFacts(&f.root());
    f.rail().destroy();
    f.rail().destroy();
    send(v, @selector(undo:));
    send(v, @selector(redo:));
    LOKA_VERIFY(![v delegate] && !MacTextStore::fromNativeFocus(v) && !observedStoreCalls());
    [f.phase finish];
    [s release];
    [v release];
  }
}
void testMacTextViewClaimAuthority()
{
  Fixture f;
  NSTextView *v = f.row().view();
  type(v, @"X");
  NSScrollView *s = f.ctx().scroll();
  NSMenuItem *undo = verb(@selector(undo:));
  [v removeFromSuperview];
  LOKA_VERIFY(MacTextStore::fromNativeFocus(v) == &f.ctx() && [v validateMenuItem:undo]);
  [s setDocumentView:v];
  f.detach();
  [s setDocumentView:v];
  LOKA_VERIFY(!MacTextStore::fromNativeFocus(v) && ![v validateMenuItem:undo]);
  [v removeFromSuperview];
  f.attach();
  Fixture foreign;
  NSTextView *impostor = foreign.row().view();
  id delegate = [impostor delegate];
  [impostor setDelegate:[v delegate]];
  LOKA_VERIFY(!MacTextStore::fromNativeFocus(impostor) && ![impostor validateMenuItem:undo]);
  [impostor setDelegate:delegate];
}

namespace
{
  struct CommitProbe
  {
    Fixture &fixture;
    unsigned step;
    static void unmarkCallback(void *data)
    {
      CommitProbe &probe = *static_cast<CommitProbe *>(data);
      Fixture &f = probe.fixture;
      Scene *original = f.window->scene();
      LOKA_VERIFY(++probe.step == 1 && original->isBorrowOpen() && original->textDocuments().isPublishing());
      std::puts("NOTE_EDIT enter -> editTextChange enter");
      MacEditTextContext *field =
          static_cast<MacEditTextContext *>(find(&f.root(), NodeTypeToken<EditTextNode>())->getContext());
      [f.field() setStringValue:@"nested"];
      MacInputDoor::editTextChange(*field);
      LOKA_VERIFY(f.root().field.get().equals(loka::core::String("nested")));
      std::puts("editTextChange exit -> inner commit");
      LOKA_VERIFY(f.app.commitTextInput() == TEXT_INPUT_UNAVAILABLE);
      LOKA_VERIFY(f.window->sceneManager()->requestReplacement(new Scene(Boundary<ViewRoot>())));
      loka::app::testing::AppTestAccess::flushWindowInvalidations(f.app);
      LOKA_VERIFY(f.window->scene() == original);
      original->requestInvalidate(NODE_DIRTY_PROPS);
      LOKA_VERIFY(!original->flushInvalidation());
      ++probe.step;
      std::puts("NOTE_EDIT exit");
    }
  };
} // namespace
void testMacTextViewCompositionCallback()
{
  Fixture f;
  NSTextView *v = f.row().view();
  loka::testing::MacSendMark(v);
  f.app.reconcileAtCompletion();
  TextDocument &doc = f.root().text;
  LOKA_VERIFY([v markedRange].length && doc.fact()->get().state() == TEXT_DOCUMENT_MARKED);
  unsigned writes = 0;
  doc.fact()->bind(&countWrite, &writes, false);
  [f.phase begin:"command callback" row:&f.row()];
  CommitProbe probe = {f, 0};
  loka::testing::observeTextInputCommits(0, 0);
  onNextCharacterEdit(&f.row(), &CommitProbe::unmarkCallback, &probe);
  LOKA_VERIFY(f.app.commitTextInput() == TEXT_INPUT_SETTLED);
  onNextCharacterEdit(0, 0, 0);
  LOKA_VERIFY(probe.step == 2 && writes == 1 && loka::testing::textInputCommitCount() == 2);
  LOKA_VERIFY(doc.fact()->get().state() == TEXT_DOCUMENT_READY && ![v hasMarkedText]);
  doc.fact()->unbind(&countWrite, &writes);
  loka::testing::observeTextInputCommits(0, 0);
  [f.phase finish];
}

namespace
{
  struct ReadyCommand
  {
    TextDocument &document;
    NSTextView *view;
    unsigned calls;
    bool ready;
    static void handle(void *data)
    {
      ReadyCommand &cmd = *static_cast<ReadyCommand *>(data);
      cmd.ready = cmd.document.fact()->get().state() == TEXT_DOCUMENT_READY && ![cmd.view hasMarkedText];
      ++cmd.calls;
    }
  };
  bool runEditCommand(Fixture &f, NSTextView *marked)
  {
    ReadyCommand handler = {f.root().text, marked, 0, false};
    loka::core::EmitterState command;
    command.bind(&ReadyCommand::handle, &handler, false);
    MenuBarDefinition bar;
    bar << (Menu("Edit") << MenuItem("Check document").onClick(&command));
    const MenuSources sources = {&bar, 0, 0};
    if (f.app.menuAttachment().project(sources))
    {
      NSMenuItem *item = [[[[NSApp mainMenu] itemAtIndex:0] submenu] itemAtIndex:0];
      if ([item action] == @selector(handleMenuAction:) && [marked markedRange].length)
        [[item target] performSelector:[item action] withObject:item];
    }
    f.app.menuAttachment().disconnect();
    command.unbind(&ReadyCommand::handle, &handler);
    return handler.calls == 1 && handler.ready;
  }
  NSPanel *newPanel()
  {
    NSPanel *panel = [[NSPanel alloc] initWithContentRect:NSMakeRect(90, 90, 240, 120)
                                                styleMask:LOKA_MAC_WINDOW_STYLE_TITLED | LOKA_MAC_WINDOW_STYLE_CLOSABLE
                                                  backing:NSBackingStoreBuffered
                                                    defer:NO];
    [panel setReleasedWhenClosed:NO];
    return panel;
  }
  void beginSheet(NSWindow *parent, NSPanel *sheet)
  {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    [NSApp beginSheet:sheet modalForWindow:parent modalDelegate:nil didEndSelector:0 contextInfo:0];
#pragma clang diagnostic pop
  }
  void closePanel(NSPanel *panel)
  {
    if ([panel sheetParent])
    {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
      [NSApp endSheet:panel];
#pragma clang diagnostic pop
    }
    [panel orderOut:nil];
    [panel release];
  }
} // namespace
void testMacTextViewCompositionCommands()
{
  const char *names[] = {"background document window", "document sheet", "key panel"};
  for (unsigned mode = 0; mode < 3; ++mode)
  {
    Fixture f;
    NSWindow *other = nil;
    NSPanel *panel = nil;
    if (!mode)
    {
      WindowProps props;
      props.frame(400, 40, 320, 240).visible(true).scene(new Scene(Boundary<ViewRoot>()));
      MacWindow *second = new MacWindow(&f.platform, props);
      f.app.install(second);
      f.app.flush();
      other = (NSWindow *)loka::dsl::testing::MacWindowTestAccess::nativeWindow(*second);
      ViewRoot *root = static_cast<ViewRoot *>(SceneAccess::rootBoundary(*second->scene()));
      LOKA_VERIFY(loka::app::testing::TextDocumentAccess::registration(root->text)->store());
    }
    if (!f.key(names[mode]))
      continue;
    NSTextView *v = f.row().view();
    LOKA_VERIFY([f.native() makeFirstResponder:v]);
    loka::testing::MacSendMark(v);
    f.app.reconcileAtCompletion();
    if (mode)
    {
      panel = newPanel();
      other = panel;
      if (mode == 1)
        beginSheet(f.native(), panel);
    }
    bool key = mode == 1 ? [f.native() attachedSheet] == panel : loka::testing::MacRequestKeyWindow(other);
    if (key)
    {
      LOKA_VERIFY((mode == 1 || ([other isKeyWindow] && ![f.native() isKeyWindow])) && [v markedRange].length);
      LOKA_VERIFY(mode != 1 || [f.native() attachedSheet] == panel);
      TextDocument &doc = f.root().text;
      unsigned writes = 0;
      doc.fact()->bind(&countWrite, &writes, false);
      NSString *body = [[v string] copy];
      const unsigned long revision = doc.fact()->get().textRevision();
      [f.phase begin:names[mode] row:&f.row()];
      LOKA_VERIFY(runEditCommand(f, v) && writes == 1 && [f.phase characters] && [f.phase notes]);
      LOKA_VERIFY([[v string] isEqual:body] && revision + 1 == doc.fact()->get().textRevision());
      [body release];
      const unsigned notes = [f.phase notes];
      unsigned edits = [f.phase characters];
      LOKA_VERIFY(f.app.commitTextInput() == TEXT_INPUT_SETTLED && writes == 1 && edits == [f.phase characters]
                  && notes == [f.phase notes]);
      [f.phase finish];
      doc.fact()->unbind(&countWrite, &writes);
    }
    else
      std::printf("[skip] %s did not become key: %s\n", names[mode], loka::testing::MacKeyRefusalReason());
    closePanel(panel);
  }
}

// An independent undo action leaves the native mark in place while both live history branches execute.
@interface LokaHistoryWitness : NSObject
{
  NSUndoManager *manager_;
  unsigned calls_;
}
- (void)arm:(NSUndoManager *)manager;
- (void)step:(id)sender;
- (unsigned)calls;
@end
@implementation LokaHistoryWitness
- (void)arm:(NSUndoManager *)manager
{
  manager_ = manager;
  [manager_ registerUndoWithTarget:self selector:@selector(step:) object:nil];
}
- (void)step:(id)sender
{
  (void)sender;
  ++calls_;
  [self arm:manager_];
}
- (unsigned)calls
{
  return calls_;
}
@end
void testMacTextViewCompositionUndo()
{
  Fixture f;
  NSTextView *v = f.row().view();
  NSUndoManager *manager = f.row().undoManager();
  [manager setGroupsByEvent:NO];
  [manager beginUndoGrouping];
  loka::testing::MacSendMark(v);
  [manager endUndoGrouping];
  LokaHistoryWitness *history = [[LokaHistoryWitness alloc] init];
  [manager beginUndoGrouping];
  [history arm:manager];
  [manager endUndoGrouping];
  NSMenuItem *undo = verb(@selector(undo:)), *redo = verb(@selector(redo:));
  LOKA_VERIFY([v markedRange].length && [v validateMenuItem:undo] && ![v validateMenuItem:redo]);
  send(v, @selector(undo:));
  LOKA_VERIFY([history calls] == 1 && [v markedRange].length && [v validateMenuItem:redo]);
  send(v, @selector(redo:));
  LOKA_VERIFY([history calls] == 2 && [v markedRange].length);
  [manager removeAllActionsWithTarget:history];
  [history release];
  [manager setGroupsByEvent:YES];
  // Scope the real text change and its commit to an explicit native undo group.
  LOKA_VERIFY(f.app.commitTextInput() == TEXT_INPUT_SETTLED);
  f.replace("base");
  v = f.row().view();
  [v setSelectedRange:NSMakeRange(4, 0)];
  manager = f.row().undoManager();
  [manager setGroupsByEvent:NO];
  [manager beginUndoGrouping];
  loka::testing::MacSendMark(v);
  LOKA_VERIFY(f.app.commitTextInput() == TEXT_INPUT_SETTLED);
  [manager endUndoGrouping];
  LOKA_VERIFY([[v string] isEqual:@"basek"]);
  send(v, @selector(undo:));
  LOKA_VERIFY([[v string] isEqual:@"base"]);
  send(v, @selector(redo:));
  LOKA_VERIFY([[v string] isEqual:@"basek"]);
  if (f.key("menu commits before undo dispatch"))
  {
    LOKA_VERIFY([f.native() makeFirstResponder:v]);
    MenuBarDefinition bar;
    bar << EditMenu();
    const MenuSources sources = {&bar, 0, 0};
    LOKA_VERIFY(f.app.menuAttachment().project(sources));
    NSMenu *menu = [[[NSApp mainMenu] itemAtIndex:0] submenu];
    [manager setGroupsByEvent:YES];
    loka::testing::MacSendMark(v);
    LOKA_VERIFY([v markedRange].length);
    [menu performActionForItemAtIndex:0];
    LOKA_VERIFY(![v hasMarkedText] && [[v string] isEqual:@"basek"]);
    f.app.menuAttachment().disconnect();
  }
}

namespace
{
  enum HandCase
  {
    READ_ONLY,
    DETACH,
    REPLACE,
    RETRY_REPLACE,
    SHEET,
    PANEL,
    CLOSE,
    FIELD_FOCUS,
    APP_RETURN,
    HAND_COUNT
  };
  const char *const handNames[] = {"Read-only toggle",
                                   "Detach/Reattach",
                                   "Replace",
                                   "Commit+Replace retry",
                                   "Sheet",
                                   "Key panel",
                                   "Close window",
                                   "Field focus",
                                   "App switch/return"};
  enum Verdict
  {
    RETRY,
    PASS,
    FAIL
  };
  const char *const verdictNames[] = {"RETRY", "PASS", "FAIL"};
  /** A single interactive run owns its log and all observations until close or timeout. */
  class HandRun
  {
    Fixture &f_;
    FILE *log_;
    Verdict results_[HAND_COUNT];
    unsigned writes_, depth_, maximum_, callbacks_;
    loka::core::State<TextDocumentFact> *fact_;
    Method delegateMethod_;
    IMP original_;
    static HandRun *active_;
    static void edited(id delegate, SEL cmd, NSNotification *note)
    {
      HandRun &run = *active_;
      ++run.depth_;
      ++run.callbacks_;
      if (run.depth_ > run.maximum_)
        run.maximum_ = run.depth_;
      std::fprintf(run.log_,
                   "callback=%u enter depth=%u mask=%lu row=%p\n",
                   run.callbacks_,
                   run.depth_,
                   (unsigned long)[(NSTextStorage *)[note object] editedMask],
                   (void *)[(id<LokaPhaseRowOwner>)delegate owner]);
      reinterpret_cast<void (*)(id, SEL, NSNotification *)>(run.original_)(delegate, cmd, note);
      std::fprintf(run.log_, "callback exit depth=%u\n", run.depth_--);
    }
    void outcome(HandCase which, Verdict verdict)
    {
      if (results_[which] != FAIL && (results_[which] != PASS || verdict == FAIL))
        results_[which] = verdict;
      std::fprintf(log_, "%s %s\n", handNames[which], verdictNames[verdict]);
      std::printf("%s %s\n", handNames[which], verdictNames[verdict]);
      std::fflush(log_);
    }
    static void written(void *data)
    {
      HandRun &run = *static_cast<HandRun *>(data);
      ++run.writes_;
      run.snapshot("publication");
    }

  public:
    HandRun(Fixture &f, FILE *log)
        : f_(f),
          log_(log),
          writes_(0),
          depth_(0),
          maximum_(0),
          callbacks_(0)
    {
      std::fill(results_, results_ + HAND_COUNT, RETRY);
      active_ = this;
      delegateMethod_ =
          class_getInstanceMethod([[f.row().view() delegate] class], @selector(textStorageDidProcessEditing:));
      original_ = method_setImplementation(delegateMethod_, reinterpret_cast<IMP>(&edited));
      fact_ = f.root().text.fact();
      fact_->bind(&written, this, false);
    }
    ~HandRun()
    {
      unbind();
      method_setImplementation(delegateMethod_, original_);
      active_ = 0;
      for (unsigned i = 0; i < HAND_COUNT; ++i)
      {
        std::fprintf(log_, "final %s %s\n", handNames[i], verdictNames[results_[i]]);
        std::printf("final %s %s\n", handNames[i], verdictNames[results_[i]]);
      }
    }
    void unbind()
    {
      if (fact_)
        fact_->unbind(&written, this);
      fact_ = 0;
    }
    void snapshot(const char *stage)
    {
      TextDocument &doc = f_.root().text;
      MacTextStore &row = f_.row();
      NSTextView *view = row.view();
      NSWindow *window = [view window];
      NSString *source = nil;
      id context = [view respondsToSelector:@selector(inputContext)] ? [view inputContext] : nil;
      if ([context respondsToSelector:@selector(selectedKeyboardInputSource)])
        source = [(id<LokaHandInputSource>)context selectedKeyboardInputSource];
      std::fprintf(log_,
                   "%s row=%p view=%p window=%p key=%p responder=%p source=%s body=%s selection=%s mark=%s "
                   "pending=%d fact=%d revision=%lu writes=%u reply=%d reason=%d callbacks=%u maxDepth=%u\n",
                   stage,
                   (void *)&row,
                   (void *)view,
                   (void *)window,
                   (void *)[NSApp keyWindow],
                   (void *)[window firstResponder],
                   source ? [source UTF8String] : "(none)",
                   [[view string] UTF8String],
                   [NSStringFromRange([view selectedRange]) UTF8String],
                   [NSStringFromRange([view markedRange]) UTF8String],
                   row.pending(),
                   (int)doc.fact()->get().state(),
                   doc.fact()->get().textRevision(),
                   writes_,
                   (int)doc.replaceReply()->get().kind(),
                   doc.replaceReply()->get().kind() == Reply<TextReplacement>::REFUSED
                       ? (int)doc.replaceReply()->get().reason()
                       : -1,
                   callbacks_,
                   maximum_);
      std::fflush(log_);
    }
    void focusOutcome(HandCase which, NSString *priorBody, NSRange priorRange)
    {
      snapshot(handNames[which]);
      NSTextView *view = f_.row().view();
      bool marked = [view hasMarkedText] && [view markedRange].length;
      bool consistent = f_.root().text.fact()->get().state() == (marked ? TEXT_DOCUMENT_MARKED : TEXT_DOCUMENT_READY);
      std::fprintf(log_,
                   "before focus change body=%s mark=%s\n",
                   [priorBody UTF8String],
                   [NSStringFromRange(priorRange) UTF8String]);
      bool preserved = [[view string] isEqual:priorBody] && (!marked || NSEqualRanges([view markedRange], priorRange));
      outcome(which, consistent && maximum_ <= 1 ? (preserved ? PASS : RETRY) : FAIL);
    }
    void act(HandCase which)
    {
      NSWindow *window = f_.native();
      NSTextView *v = f_.row().view();
      snapshot(handNames[which]);
      if (![window isKeyWindow] || [window firstResponder] != v || ![v hasMarkedText] || ![v markedRange].length)
      {
        outcome(which, RETRY);
        return;
      }
      NSString *body = [[v string] copy];
      NSRange range = [v markedRange];
      TextDocument &doc = f_.root().text;
      bool passed = false;
      bool retry = false;
      if (which == READ_ONLY || which == DETACH)
      {
        if (which == READ_ONLY)
          f_.present(true);
        else
          f_.show(false);
        f_.app.reconcileAtCompletion();
        snapshot("settled");
        passed = ![v hasMarkedText] && [[v string] isEqual:body] && doc.fact()->get().state() == TEXT_DOCUMENT_READY
                 && (which == READ_ONLY ? ![v isEditable] : ![v superview]);
        if (which == READ_ONLY)
          f_.present(false);
        else
          f_.show(true);
        passed = passed && f_.ctx().store() == &f_.row() && [v isEditable] && [v window] == window;
      }
      else if (which == REPLACE || which == RETRY_REPLACE)
      {
        f_.replace("replacement");
        passed = doc.replaceReply()->get().kind() == Reply<TextReplacement>::REFUSED
                 && doc.replaceReply()->get().reason() == TEXT_REPLACE_COMPOSING && f_.row().view() == v &&
                 [[v string] isEqual:body] && NSEqualRanges([v markedRange], range);
        snapshot("replacement receipt");
        if (which == RETRY_REPLACE && passed)
        {
          passed = f_.app.commitTextInput() == TEXT_INPUT_SETTLED && ![v hasMarkedText] && [[v string] isEqual:body];
          snapshot("command commit");
          f_.replace("replacement");
          passed = passed && doc.replaceReply()->get().kind() == Reply<TextReplacement>::GRANTED &&
                   [[f_.row().view() string] isEqual:@"replacement"];
        }
      }
      else if (which == SHEET || which == PANEL)
      {
        NSPanel *panel = newPanel();
        if (which == SHEET)
          beginSheet(window, panel);
        else
          [panel makeKeyAndOrderFront:nil];
        retry = (which == SHEET ? [window attachedSheet] != panel : !loka::testing::MacWaitForKeyStatus(panel, true))
                || ![v markedRange].length;
        snapshot("auxiliary window before Edit command");
        if (!retry)
        {
          passed = runEditCommand(f_, v) && ![v hasMarkedText] && [[v string] isEqual:body];
        }
        closePanel(panel);
      }
      else
      {
        [v retain];
        const MacTextStore *retiring = &f_.row();
        observeStoreCalls(retiring);
        f_.row().undoManager();
        const unsigned control = observedStoreCalls();
        observeStoreCalls(0);
        [window performClose:nil];
        passed = ![window isVisible] && ![v hasMarkedText] && [[v string] isEqual:body]
                 && doc.fact()->get().state() == TEXT_DOCUMENT_READY;
        snapshot("native close returned");
        if (![window isVisible])
        {
          unbind();
          f_.app.flush();
          observeStoreCalls(retiring);
          unsigned nativeAttempts = 0;
          ++nativeAttempts;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
          [v insertText:@"synthetic late delivery"];
#pragma clang diagnostic pop
          unsigned calls = observedStoreCalls();
          observeStoreCalls(0);
          std::fprintf(log_,
                       "synthetic post-retirement view=%p nativeAttempts=%u lokaCalls=%u\n",
                       (void *)v,
                       nativeAttempts,
                       calls);
          std::fprintf(log_, "live delegate control calls=%u\n", control);
          passed = passed && control == 1 && ![v delegate] && calls == 0;
        }
        [v release];
      }
      [body release];
      if ([window isVisible])
        snapshot("action returned");
      outcome(which, retry ? RETRY : passed && maximum_ <= 1 ? PASS : FAIL);
    }
  };
  HandRun *HandRun::active_ = 0;
} // namespace
@interface LokaHandButton : NSButton
{
  HandRun *run_;
}
- (void)install:(HandRun *)run;
@end
@implementation LokaHandButton
- (BOOL)acceptsFirstResponder
{
  return NO;
}
- (void)install:(HandRun *)run
{
  run_ = run;
  [self setRefusesFirstResponder:YES];
  [self setTarget:self];
  [self setAction:@selector(performCase:)];
}
- (void)performCase:(id)sender
{
  (void)sender;
  run_->act(static_cast<HandCase>([self tag]));
}
@end

void testMacTextViewImeHandCheck()
{
  const char *enable = std::getenv("LOKA_HAND_CHECK");
  if (!enable || std::strcmp(enable, "1") != 0)
  {
    std::puts("[skip] hand check: set LOKA_HAND_CHECK=1");
    return;
  }
  Fixture f;
  char path[] = "/tmp/loka-ime-XXXXXX";
  int fd = mkstemp(path);
  LOKA_VERIFY(fd >= 0);
  FILE *log = fdopen(fd, "w");
  LOKA_VERIFY(log);
  std::printf("IME hand-check log %s\n", path);
  const char *candidate = std::getenv("LOKA_CANDIDATE");
  std::fprintf(log,
               "candidate=%s os=%s\n",
               candidate ? candidate : "(unspecified)",
               [[[NSProcessInfo processInfo] operatingSystemVersionString] UTF8String]);
  NSWindow *window = [f.native() retain];
  [window setTitle:@"Japanese IME hand check"];
  [window setContentSize:NSMakeSize(540, 350)];
  {
    HandRun run(f, log);
    for (unsigned i = 0; i <= CLOSE; ++i)
    {
      LokaHandButton *button =
          [[LokaHandButton alloc] initWithFrame:NSMakeRect(10 + (i % 3) * 176, 190 + (i / 3) * 42, 172, 36)];
      [button setTag:i];
      [button setTitle:[NSString stringWithUTF8String:handNames[i]]];
      [button setBezelStyle:LOKA_MAC_BUTTON_BEZEL_STYLE];
      [button install:&run];
      [[window contentView] addSubview:button];
      [button release];
    }
    f.key("interactive document window");
    [window makeFirstResponder:f.row().view()];
    NSDate *deadline = [NSDate dateWithTimeIntervalSinceNow:600];
    NSString *departedBody = nil;
    NSRange departedRange = NSMakeRange(NSNotFound, 0);
    while ([window isVisible] && [deadline timeIntervalSinceNow] > 0)
    {
      NSAutoreleasePool *iteration = [[NSAutoreleasePool alloc] init];
#if defined(MAC_OS_X_VERSION_MAX_ALLOWED) && MAC_OS_X_VERSION_MAX_ALLOWED >= 101200
      const NSEventMask mask = NSEventMaskAny;
#else
      const NSUInteger mask = NSAnyEventMask;
#endif
      NSTextView *document = f.row().view();
      bool markedAtEntry = [window isKeyWindow] && [window firstResponder] == document && [document markedRange].length;
      NSString *priorBody = markedAtEntry ? [[[document string] copy] autorelease] : nil;
      NSRange priorRange = [document markedRange];
      NSEvent *event = [NSApp nextEventMatchingMask:mask
                                          untilDate:[NSDate dateWithTimeIntervalSinceNow:0.1]
                                             inMode:NSDefaultRunLoopMode
                                            dequeue:YES];
      if (event)
        [NSApp sendEvent:event];
      if ([window isVisible])
      {
        f.app.flush();
        if (markedAtEntry && ![NSApp isActive])
        {
          run.snapshot("departed app while composing");
          [departedBody release];
          departedBody = [priorBody retain];
          departedRange = priorRange;
        }
        if (departedBody && [window isKeyWindow] && [NSApp isActive])
        {
          run.focusOutcome(APP_RETURN, departedBody, departedRange);
          [departedBody release];
          departedBody = nil;
        }
        if (markedAtEntry && document == f.row().view() && [f.field() currentEditor] &&
            [window firstResponder] == [f.field() currentEditor])
          run.focusOutcome(FIELD_FOCUS, priorBody, priorRange);
        if (event)
          run.snapshot("event completed");
        [NSApp updateWindows];
      }
      [iteration drain];
    }
    [departedBody release];
    NSArray *views = [[window contentView] subviews];
    for (NSUInteger i = 0; i < [views count]; ++i)
    {
      id view = [views objectAtIndex:i];
      if ([view isKindOfClass:[LokaHandButton class]])
        [view setTarget:nil];
    }
  }
  [window release];
  std::fclose(log);
  std::printf("IME hand-check log %s\n", path);
}
