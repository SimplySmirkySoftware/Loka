#include "MacTextDocumentViewTests.hpp"
#include "MacApp.hpp"
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
    explicit ViewRoot(const BoundaryPropsFor<ViewRoot> &props)
        : BoundaryNodeFor<ViewRoot>(props)
    {
      this->document(this->text);
      this->state(this->focus);
      this->state(this->field, loka::core::String("field"));
      this->state(this->shown, true);
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
  loka::testing::MacSyntheticMark(v);
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
  loka::testing::MacSyntheticMark(v);
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
  loka::testing::MacSyntheticMark(v);
  f.detach();
  LOKA_VERIFY(![v hasMarkedText] && ![v window]);
}
void testMacTextViewCompositionRefused()
{
  Fixture f;
  if (!f.key("composition first responder"))
    return;
  NSTextView *v = f.row().view();
  for (unsigned readOnly = 0; readOnly < 2; ++readOnly)
  {
    f.present(readOnly != 0);
    LOKA_VERIFY([f.native() makeFirstResponder:v] && [f.native() firstResponder] == v);
    for (unsigned selector = 0; selector < 2; ++selector)
    {
      loka::testing::MacVerifyMarkRefused(v, selector != 0);
      LOKA_VERIFY(!f.row().pending());
    }
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
  unsigned steps = 0;
  while ([v validateMenuItem:undo])
  {
    send(v, @selector(undo:));
    ++steps;
  }
  LOKA_VERIFY(steps && ![v validateMenuItem:undo] && [v validateMenuItem:redo]);
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
