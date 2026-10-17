#include "MacFocusTests.hpp"
#include "support/MacWindowSession.hpp"
#include "MacApp.hpp"
#include "MacObjCCompat.hpp"
#include "MacWindow.hpp"
#include "MacScenePlatformController.hpp"
#include "context/MacEditTextContext.hpp"
#include "context/MacTextEditorContext.hpp"
#include "app/nodes/boundary/StdComposition.hpp"
#include "app/nodes/controls/EditText.hpp"
#include "app/nodes/controls/TextEditor.hpp"
#include "app/nodes/nestable/RowColumn.hpp"
#include "app/scene/Scene.hpp"
#include "platform/null/NullPlatformContext.hpp"
#include "support/Headless.hpp"
#include "support/LifecycleFactTestAccess.hpp"
#include "support/TestVerify.hpp"
#include "testing/MacWindowTestAccess.hpp"
#include "testing/scene/SceneTestFlow.hpp"
#include <AppKit/AppKit.h>
#include <ApplicationServices/ApplicationServices.h>
#include <cstdio>

namespace
{
  using namespace loka::app;
  using namespace loka::app::scene;
  using namespace loka::core;
  typedef Focused<unsigned int> Fact;
  typedef loka::dsl::testing::MacWindowTestAccess NativeAccess;
  typedef loka::dsl::testing::SceneTestAccess SceneAccess;

  struct Facts : HeadlessStateOwner
  {
    Focus<unsigned int> focus;
    Reported<LineCursor> cursor;
    NodeState<String> firstText, secondText;
    ObservableList<String> lines;
    Facts()
    {
      StateBatchBase::CreateImmediateState(this, this->cursor, LineCursor::None());
      StateBatchBase::CreateImmediateState(this, this->firstText, String("first"));
      StateBatchBase::CreateImmediateState(this, this->secondText, String("second"));
      LOKA_VERIFY(this->lines.attach(this->tracker()->asPushTracker(), 4) == ATTACH_OK);
      LOKA_VERIFY(this->lines.insert(0, String("editor")) == EDIT_OK);
    }
  };
  // Scoped by fixture construction, matching the Win32 initial-focus pin.
  unsigned int composingInitialFocus = 0;

  class FocusRoot;
  struct FocusTag
  {
  };
  struct FocusProps : NodePropsBase<FocusProps>
  {
    typedef FocusRoot NodeType;
    typedef FocusTag TypeTag;
    Facts *facts;
    explicit FocusProps(Facts *value)
        : facts(value)
    {
    }
    bool operator<(const PropsBase &rhs) const
    {
      if (rhs.propsTypeId() != this->propsTypeId())
        return this->propsTypeId() < rhs.propsTypeId();
      return this->facts < static_cast<const FocusProps &>(rhs).facts;
    }
  };
  class FocusRoot : public StdCompositionBoundaryNodeBase<FocusProps>
  {
    typedef StdCompositionBoundaryNodeBase<FocusProps> Base;

  public:
    typedef FocusTag TypeTag;
    explicit FocusRoot(const FocusProps &props)
        : Base(props)
    {
      if (composingInitialFocus)
        this->state(this->props.facts->focus, composingInitialFocus);
      else
        this->state(this->props.facts->focus);
    }
    virtual void composeNode(NodeComposition &composition)
    {
      // A composition declares one root; the three participants share a column.
      composition.declare(
          VStack() << EditText(EditTextProps(this->props.facts->firstText).focusedAs(this->props.facts->focus, 1u))
                   << EditText(EditTextProps(this->props.facts->secondText).focusedAs(this->props.facts->focus, 2u))
                   << TextEditor(TextEditorProps(this->props.facts->lines, this->props.facts->cursor)
                                     .focusedAs(this->props.facts->focus, 3u)));
    }
  };
  typedef BoundaryDefinition<FocusProps, FocusRoot> FocusDefinition;
  class FocusApp : public MacApp
  {
  public:
    FocusApp()
        : MacApp(0)
    {
    }
    void install(MacWindow *window)
    {
      this->group_ = new AppComponentGroup(std::vector<AppComponent *>(1, window));
      window->setApp(this);
    }
  };
  NSTextField *field(Node *node)
  {
    LOKA_VERIFY(node && node->asEditTextNode() && node->getContext());
    return (NSTextField *)static_cast<MacEditTextContext *>(node->getContext())->nativeField();
  }
  NSTextView *editor(NSView *parent, NodeContext *context)
  {
    if (MacTextEditorContext::fromNativeFocus(parent) == context)
      return (NSTextView *)parent;
    for (NSUInteger i = 0; i < [[parent subviews] count]; ++i)
      if (NSTextView *found = editor([[parent subviews] objectAtIndex:i], context))
        return found;
    return nil;
  }

  /** Bound on waiting for AppKit to report a key-status change. */
  const double kKeyStatusWaitSeconds = 2.0;

  bool isKey(NSWindow *window)
  {
    return [window isKeyWindow] ? true : false;
  }

  /** Activation and key changes arrive through the event queue: deliver queued
      events until the window's key status matches, for a bounded time. */
  bool waitForKeyStatus(NSWindow *window, bool key)
  {
#if defined(MAC_OS_X_VERSION_MAX_ALLOWED) && (MAC_OS_X_VERSION_MAX_ALLOWED >= 101200)
    const NSEventMask anyEvent = NSEventMaskAny;
#else
    const NSUInteger anyEvent = NSAnyEventMask;
#endif
    NSDate *limit = [NSDate dateWithTimeIntervalSinceNow:kKeyStatusWaitSeconds];
    while (isKey(window) != key && [limit timeIntervalSinceNow] > 0)
    {
      NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
      NSEvent *event = [NSApp nextEventMatchingMask:anyEvent
                                          untilDate:[NSDate dateWithTimeIntervalSinceNow:0.05]
                                             inMode:NSDefaultRunLoopMode
                                            dequeue:YES];
      if (event)
        [NSApp sendEvent:event];
      [pool drain];
    }
    return isKey(window) == key;
  }

  /** An unbundled test process is not a regular application until it asks, as
      MacApp::run() does (apple/macos/src/MacApp.mm); only then can activation
      make its window key. */
  bool requestKeyWindow(NSWindow *window)
  {
    if ([NSApp respondsToSelector:@selector(setActivationPolicy:)])
      [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    [NSApp activateIgnoringOtherApps:YES];
    [window makeKeyAndOrderFront:nil];
    return waitForKeyStatus(window, true);
  }

  /** Diagnostic only; the discriminating condition is the window's key status. */
  const char *keyRefusalReason()
  {
    if (loka::testing::MacConsoleScreenIsLocked())
      return "the console screen is locked";
    return [NSApp isActive] ? "the application is active but the window is not key"
                            : "the application was not activated";
  }
} // namespace

void testMacFocusReadAndCompletion()
{
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  [NSApplication sharedApplication];
  {
    Facts facts;
    NullPlatformContext context;
    FocusApp app;
    WindowProps props;
    props.scene(new Scene(FocusDefinition(FocusProps(&facts))));
    MacWindow *window = new MacWindow(&context, props);
    app.install(window);
    NSWindow *native = (NSWindow *)NativeAccess::nativeWindow(*window);
    LOKA_VERIFY(native != nil);
    // Key status decides which pins can run; the branch taken is always printed.
    const bool key = requestKeyWindow(native);
    Scene &scene = *window->scene();
    Node *root = SceneAccess::rootNode(scene);
    LOKA_VERIFY(root && root->asNestable() && root->asNestable()->childrenCount() == 1);
    Node *column = root->asNestable()->childrenHead();
    LOKA_VERIFY(column && column->asNestable() && column->asNestable()->childrenCount() == 3);
    loka::dsl::CompositionCursor<Node> children(column->asNestable()->childrenHead(),
                                                column->asNestable()->childrenCount());
    Node *firstNode = children.next();
    Node *secondNode = children.next();
    Node *editorNode = children.next();
    NSTextField *first = field(firstNode);
    NSTextField *second = field(secondNode);
    // editor() itself resolves the TextEditor view through its class-checked hop.
    NSTextView *textEditor = editor((NSView *)NativeAccess::contentView(*window), editorNode->getContext());
    LOKA_VERIFY(textEditor && editorNode && editorNode->getContext());
    MacScenePlatformController *rail =
        static_cast<MacScenePlatformController *>(SceneAccess::platformController(scene));
    NodeContext *read = 0;

    // Class-checked hops need no key status: each resolves only a marked native
    // participant through its typed delegate owner.
    LOKA_VERIFY(MacEditTextContext::fromNativeFocus(first) == firstNode->getContext());
    id firstDelegate = [first delegate];
    [first setDelegate:(id)native];
    LOKA_VERIFY(MacEditTextContext::fromNativeFocus(first) == 0);
    [first setDelegate:firstDelegate];
    // A foreign field cannot acquire the participant mark by borrowing a delegate.
    NSTextField *foreignField = [[NSTextField alloc] initWithFrame:NSZeroRect];
    [foreignField setDelegate:firstDelegate];
    LOKA_VERIFY(MacEditTextContext::fromNativeFocus(foreignField) == 0);
    [foreignField setDelegate:nil];
    [foreignField release];
    id editorDelegate = [textEditor delegate];
    [textEditor setDelegate:(id)native];
    LOKA_VERIFY(MacTextEditorContext::fromNativeFocus(textEditor) == 0);
    [textEditor setDelegate:editorDelegate];
    // A foreign view carrying the genuine delegate still lacks the typed mark.
    NSTextView *foreignView = [[NSTextView alloc] initWithFrame:NSMakeRect(0, 0, 80, 24)];
    [foreignView setDelegate:editorDelegate];
    LOKA_VERIFY(MacTextEditorContext::fromNativeFocus(foreignView) == 0);
    [foreignView setDelegate:nil];
    [foreignView release];
    // An editing field's first responder is the field editor, whose delegate is the field.
    LOKA_VERIFY([native makeFirstResponder:first]);
    LOKA_VERIFY([first currentEditor] != nil);
    LOKA_VERIFY(MacEditTextContext::fromNativeFocus([native firstResponder]) == firstNode->getContext());

    if (key)
    {
      // Only completion publishes: the raw read answers before the fact moves.
      LOKA_VERIFY(rail->readNativeFocus(read) && read == firstNode->getContext());
      LOKA_VERIFY(!(facts.focus.state()->get() != Fact::none()));
      app.flushInvalidationsTick();
      LOKA_VERIFY(!(facts.focus.state()->get() != Fact(1u)));

      // Drive the field editor's Tab command through AppKit's key-view loop.
      [first setNextKeyView:second];
      [(NSTextView *)[first currentEditor] insertTab:nil];
      LOKA_VERIFY([second currentEditor] != nil);
      LOKA_VERIFY(!(facts.focus.state()->get() != Fact(1u)));
      app.flushInvalidationsTick();
      LOKA_VERIFY(!(facts.focus.state()->get() != Fact(2u)));

      // Exercise today's native capture/restore without replacing the Scene.
      LOKA_VERIFY([native makeFirstResponder:first]);
      app.flushInvalidationsTick();
      LOKA_VERIFY(!(facts.focus.state()->get() != Fact(1u)));
      LOKA_VERIFY([native makeFirstResponder:second]);
      rail->onChange(root, NODE_DIRTY_LAYOUT, true);
      LOKA_VERIFY([field(secondNode) currentEditor] != nil);
      app.flushInvalidationsTick();
      LOKA_VERIFY(!(facts.focus.state()->get() != Fact(2u)));

      // Another window of this application takes key status. The Loka window
      // cannot answer, so its fact is HELD while its first responder changes.
      NSWindow *other = [[NSWindow alloc] initWithContentRect:NSMakeRect(40, 40, 160, 80)
                                                    styleMask:LOKA_MAC_WINDOW_STYLE_TITLED
                                                      backing:NSBackingStoreBuffered
                                                        defer:NO];
      [other makeKeyAndOrderFront:nil];
      LOKA_VERIFY(waitForKeyStatus(native, false));
      LOKA_VERIFY([native makeFirstResponder:nil]);
      LOKA_VERIFY(!rail->readNativeFocus(read));
      app.flushInvalidationsTick();
      LOKA_VERIFY(!(facts.focus.state()->get() != Fact(2u)));
      [other orderOut:nil];
      [other release];
      [native makeKeyWindow];
      LOKA_VERIFY(waitForKeyStatus(native, true));
      // Regaining key status answers again; with no participant focused, the answer is none.
      LOKA_VERIFY([native makeFirstResponder:nil]);
      app.flushInvalidationsTick();
      LOKA_VERIFY(!(facts.focus.state()->get() != Fact::none()));

      textEditor = editor((NSView *)NativeAccess::contentView(*window), editorNode->getContext());
      LOKA_VERIFY(textEditor && [native makeFirstResponder:textEditor]);
      LOKA_VERIFY(rail->readNativeFocus(read) && read == editorNode->getContext());
      app.flushInvalidationsTick();
      LOKA_VERIFY(!(facts.focus.state()->get() != Fact(3u)));

      // A first responder without the typed mark answers none, genuine delegate or not.
      NSTextView *foreign = [[NSTextView alloc] initWithFrame:NSMakeRect(0, 0, 80, 24)];
      [[native contentView] addSubview:foreign];
      [foreign setDelegate:[textEditor delegate]];
      LOKA_VERIFY([native makeFirstResponder:foreign]);
      LOKA_VERIFY(rail->readNativeFocus(read) && read == 0);
      [foreign setDelegate:nil];
      app.flushInvalidationsTick();
      LOKA_VERIFY(!(facts.focus.state()->get() != Fact::none()));
      [native makeFirstResponder:nil];
      [foreign removeFromSuperview];
      [foreign release];

      // Native mouse delivery selects the field; only completion publishes.
#if defined(MAC_OS_X_VERSION_MAX_ALLOWED) && (MAC_OS_X_VERSION_MAX_ALLOWED >= 101200)
      const NSEventType mouseUpType = NSEventTypeLeftMouseUp;
      const NSEventType mouseDownType = NSEventTypeLeftMouseDown;
#else
      const NSEventType mouseUpType = NSLeftMouseUp;
      const NSEventType mouseDownType = NSLeftMouseDown;
#endif
      first = field(firstNode);
      const NSRect bounds = [first bounds];
      LOKA_VERIFY(!NSIsEmptyRect(bounds));
      const NSPoint point = [first convertPoint:NSMakePoint(NSMidX(bounds), NSMidY(bounds)) toView:nil];
      NSEvent *up = [NSEvent mouseEventWithType:mouseUpType
                                       location:point
                                  modifierFlags:0
                                      timestamp:0
                                   windowNumber:[native windowNumber]
                                        context:nil
                                    eventNumber:1
                                     clickCount:1
                                       pressure:0];
      [NSApp postEvent:up atStart:YES];
      NSEvent *down = [NSEvent mouseEventWithType:mouseDownType
                                         location:point
                                    modifierFlags:0
                                        timestamp:0
                                     windowNumber:[native windowNumber]
                                          context:nil
                                      eventNumber:0
                                       clickCount:1
                                         pressure:1];
      [native sendEvent:down];
      LOKA_VERIFY([first currentEditor] != nil);
      LOKA_VERIFY(!(facts.focus.state()->get() != Fact::none()));
      app.flushInvalidationsTick();
      LOKA_VERIFY(!(facts.focus.state()->get() != Fact(1u)));
      std::printf("[pin] macOS focus with a key window: raw read and publication at completion, Tab, "
                  "rebuild, HELD across key loss, TextEditor publication, unmarked responder, click.\n");
      std::fflush(stdout);
    }
    else
    {
      std::printf("[skip] macOS focus pins that need a key window (raw read and publication at "
                  "completion, Tab, rebuild, HELD across key loss, TextEditor publication, "
                  "unmarked responder, click): window not key after %.1f s because %s; the "
                  "class-checked hops and the non-key read still run.\n",
                  kKeyStatusWaitSeconds, keyRefusalReason());
      std::fflush(stdout);
      // HELD: a window that is not key cannot answer, whoever is its first responder.
      LOKA_VERIFY(!rail->readNativeFocus(read));
      app.flushInvalidationsTick();
      LOKA_VERIFY(!(facts.focus.state()->get() != Fact::none()));
    }

    // AppKit may outlive the logical contexts; retained native objects lose owners.
    first = field(firstNode);
    [first retain];
    [textEditor retain];
    {
      StateTrackerGuard guard(window->getTracker());
      window->visibilityState().set(false);
    }
    app.flushInvalidationsTick();
    LOKA_VERIFY(NativeAccess::nativeWindow(*window) == 0);
    LOKA_VERIFY(MacEditTextContext::fromNativeFocus(first) == 0);
    LOKA_VERIFY(MacTextEditorContext::fromNativeFocus(textEditor) == 0);
    // The Focus is declared by FocusRoot inside this Scene (#960), so closing
    // the window reclaims it with the Scene: there is no fact left to read here.
    // Clearing on leave is pinned headless in FocusPublisherTests.
    [first release];
    [textEditor release];
  }
  [pool drain];
}

namespace
{
  /** Owns the same three-participant screen as the read/completion pin. */
  struct WriteFixture
  {
    Facts facts;
    NullPlatformContext context;
    FocusApp app;
    MacWindow *window;
    NSWindow *native;
    MacScenePlatformController *rail;
    Node *firstNode;
    Node *secondNode;
    Node *editorNode;

    explicit WriteFixture(unsigned int initial = 0)
    {
      WindowProps props;
      props.scene(new Scene(FocusDefinition(FocusProps(&this->facts))));
      composingInitialFocus = initial;
      this->window = new MacWindow(&this->context, props);
      composingInitialFocus = 0;
      this->app.install(this->window);
      this->native = (NSWindow *)NativeAccess::nativeWindow(*this->window);
      LOKA_VERIFY(this->native != nil);
      Scene &scene = *this->window->scene();
      this->rail = static_cast<MacScenePlatformController *>(SceneAccess::platformController(scene));
      Node *root = SceneAccess::rootNode(scene);
      LOKA_VERIFY(root && root->asNestable());
      Node *column = root->asNestable()->childrenHead();
      LOKA_VERIFY(column && column->asNestable() && column->asNestable()->childrenCount() == 3);
      loka::dsl::CompositionCursor<Node> children(column->asNestable()->childrenHead(),
                                                  column->asNestable()->childrenCount());
      this->firstNode = children.next();
      this->secondNode = children.next();
      this->editorNode = children.next();
    }

    bool key(const char *pin)
    {
      if (requestKeyWindow(this->native))
        return true;
      std::printf("[skip] %s: window not key after %.1f s because %s.\n",
                  pin, kKeyStatusWaitSeconds, keyRefusalReason());
      std::fflush(stdout);
      return false;
    }

    FocusBinding binding(unsigned int key)
    {
      return EditTextProps().focusedAs(this->facts.focus, key).focus_;
    }

    NSTextView *document() const
    {
      return editor((NSView *)NativeAccess::contentView(*this->window), this->editorNode->getContext());
    }

  private:
    WriteFixture(const WriteFixture &);
    WriteFixture &operator=(const WriteFixture &);
  };

  void verifyFieldFocus(WriteFixture &f, NSTextField *target, unsigned int key)
  {
    NSText *fieldEditor = [target currentEditor];
    LOKA_VERIFY(fieldEditor != nil);
    LOKA_VERIFY([f.native firstResponder] == fieldEditor);
    LOKA_VERIFY([(NSTextView *)fieldEditor delegate] == (id)target);
    LOKA_VERIFY(f.facts.focus.state()->get().is(key));
    LOKA_VERIFY(!f.binding(key).requested());
    LOKA_VERIFY([f.native isKeyWindow]);
  }

  /** Immutable native snapshot: AppKit owns the shared field editor; take the
      EditText selection while it is editing, before leaving and returning. */
  class TextSnapshot
  {
    NSString *const text_;
    const NSRange selection_;
    const NSPoint scroll_;

  public:
    explicit TextSnapshot(NSTextView *view)
        : text_([[view string] copy]), selection_([view selectedRange]),
          scroll_([[view enclosingScrollView] contentView] ? [[[view enclosingScrollView] contentView] bounds].origin
                                                         : NSZeroPoint)
    {
    }
    ~TextSnapshot()
    {
      [this->text_ release];
    }
    void verify(NSTextView *view) const
    {
      LOKA_VERIFY([[view string] isEqualToString:this->text_]);
      LOKA_VERIFY(NSEqualRanges([view selectedRange], this->selection_));
      NSClipView *clip = [[view enclosingScrollView] contentView];
      const NSPoint scroll = clip ? [clip bounds].origin : NSZeroPoint;
      LOKA_VERIFY(NSEqualPoints(scroll, this->scroll_));
    }

  private:
    TextSnapshot(const TextSnapshot &);
    TextSnapshot &operator=(const TextSnapshot &);
  };
}

void testMacFocusPostedRequest()
{
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  [NSApplication sharedApplication];
  {
    WriteFixture f;
    if (f.key("testMacFocusPostedRequest"))
    {
      NSTextField *first = field(f.firstNode);
      NSTextField *second = field(f.secondNode);
      // Prime the target's field-editor selection with its native select-all
      // range. The shared field editor has no inactive per-field selection API.
      LOKA_VERIFY([f.native makeFirstResponder:second]);
      NSTextView *editing = (NSTextView *)[second currentEditor];
      LOKA_VERIFY(editing != nil);
      [editing setSelectedRange:NSMakeRange(0, [[editing string] length])];
      const TextSnapshot beforeField(editing);
      const String beforeText = f.facts.secondText.get();
      LOKA_VERIFY([f.native makeFirstResponder:first]);
      f.app.flushInvalidationsTick();
      LOKA_VERIFY(f.facts.focus.state()->get().is(1u));
      f.facts.focus.post(2u);
      f.app.flushInvalidationsTick();
      verifyFieldFocus(f, second, 2u);
      beforeField.verify((NSTextView *)[second currentEditor]);
      LOKA_VERIFY(f.facts.secondText.get().equals(beforeText));

      // A repeated post also leaves a nontrivial selection untouched.
      editing = (NSTextView *)[second currentEditor];
      [editing setSelectedRange:NSMakeRange(1, 2)];
      const TextSnapshot selectedField(editing);
      f.facts.focus.post(2u);
      f.app.flushInvalidationsTick();
      verifyFieldFocus(f, second, 2u);
      selectedField.verify((NSTextView *)[second currentEditor]);

      NSTextView *document = f.document();
      LOKA_VERIFY(document != nil);
      LOKA_VERIFY(static_cast<MacTextEditorContext *>(f.editorNode->getContext())->nativeFocusView() == document);
      [document setSelectedRange:NSMakeRange(1, 2)];
      const TextSnapshot beforeEditor(document);
      const LineCursor beforeCursor = f.facts.cursor.state()->get();
      f.facts.focus.post(3u);
      f.app.flushInvalidationsTick();
      LOKA_VERIFY([f.native firstResponder] == document);
      LOKA_VERIFY(f.facts.focus.state()->get().is(3u));
      LOKA_VERIFY(!f.binding(3u).requested());
      LOKA_VERIFY([f.native isKeyWindow]);
      beforeEditor.verify(document);
      LOKA_VERIFY(!(f.facts.cursor.state()->get() != beforeCursor));
    }
  }
  [pool drain];
}

void testMacFocusInactiveRequest()
{
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  [NSApplication sharedApplication];
  {
    WriteFixture f;
    if (f.key("testMacFocusInactiveRequest"))
    {
      LOKA_VERIFY([f.native makeFirstResponder:field(f.firstNode)]);
      f.app.flushInvalidationsTick();
      NSWindow *other = [[NSWindow alloc] initWithContentRect:NSMakeRect(40, 40, 160, 80)
                                                    styleMask:LOKA_MAC_WINDOW_STYLE_TITLED
                                                      backing:NSBackingStoreBuffered defer:NO];
      [other makeKeyAndOrderFront:nil];
      LOKA_VERIFY(waitForKeyStatus(f.native, false));
      NSResponder *const before = [f.native firstResponder];
      // Direct admission must decline too, independently of the common read gate.
      LOKA_VERIFY(!f.rail->applyNativeFocus(*f.secondNode->getContext()));
      LOKA_VERIFY([f.native firstResponder] == before);
      f.facts.focus.post(2u);
      f.app.flushInvalidationsTick();
      LOKA_VERIFY(f.binding(2u).requested());
      LOKA_VERIFY(f.facts.focus.state()->get().is(1u));
      LOKA_VERIFY([f.native firstResponder] == before);
      LOKA_VERIFY(![f.native isKeyWindow]);
      [other orderOut:nil];
      [other release];
      [f.native makeKeyWindow];
      LOKA_VERIFY(waitForKeyStatus(f.native, true));
      f.app.flushInvalidationsTick();
      verifyFieldFocus(f, field(f.secondNode), 2u);
    }
  }
  [pool drain];
}

void testMacFocusWriteAdmission()
{
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  [NSApplication sharedApplication];
  {
    WriteFixture f;
    WriteFixture foreign;
    if (f.key("testMacFocusWriteAdmission"))
    {
      LOKA_VERIFY([f.native makeFirstResponder:field(f.firstNode)]);
      NSResponder *const before = [f.native firstResponder];
      NodeContext *const target = f.secondNode->getContext();
      NSTextField *second = field(f.secondNode);
      id delegate = [second delegate];
      [second setDelegate:nil];
      LOKA_VERIFY(MacEditTextContext::fromNativeFocus(second) == 0);
      LOKA_VERIFY(!f.rail->applyNativeFocus(*target));
      LOKA_VERIFY([f.native firstResponder] == before);
      [second setDelegate:delegate];
      // Owner mismatch is tested without freeing the installed context.
      target->setOwner(f.firstNode);
      LOKA_VERIFY(!f.rail->applyNativeFocus(*target));
      LOKA_VERIFY([f.native firstResponder] == before);
      target->setOwner(f.secondNode);
      NotifySubtreeNodeDetached(f.secondNode);
      LifecycleFactTestAccess::DeliverFacts(f.secondNode);
      LOKA_VERIFY(!f.rail->applyNativeFocus(*target));
      LOKA_VERIFY([f.native firstResponder] == before);
      NotifySubtreeNodeAttached(f.secondNode);
      LifecycleFactTestAccess::DeliverFacts(f.secondNode);
      // A genuine mark/current owner from another window must still decline.
      LOKA_VERIFY(MacEditTextContext::fromNativeFocus(field(foreign.secondNode)) == foreign.secondNode->getContext());
      NSResponder *const foreignBefore = [foreign.native firstResponder];
      LOKA_VERIFY(!f.rail->applyNativeFocus(*foreign.secondNode->getContext()));
      LOKA_VERIFY([f.native firstResponder] == before);
      LOKA_VERIFY([foreign.native firstResponder] == foreignBefore);
      LOKA_VERIFY([f.native isKeyWindow]);

      // The document view has its own typed delegate mark.
      NSTextView *document = f.document();
      LOKA_VERIFY(document != nil);
      id editorDelegate = [document delegate];
      [document setDelegate:nil];
      LOKA_VERIFY(!f.rail->applyNativeFocus(*f.editorNode->getContext()));
      LOKA_VERIFY([f.native firstResponder] == before);
      [document setDelegate:editorDelegate];
    }
  }
  [pool drain];
}

void testMacFocusInitialRequest()
{
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  [NSApplication sharedApplication];
  {
    WriteFixture f(2u);
    LOKA_VERIFY(f.binding(2u).requested());
    if (f.key("testMacFocusInitialRequest"))
    {
      f.app.flushInvalidationsTick();
      verifyFieldFocus(f, field(f.secondNode), 2u);
    }
  }
  [pool drain];
}

/** Deterministic IME-commit stand-in: actual AppKit end-editing notification,
    followed by the existing controlTextDidChange bridge, without an input method. */
@interface MacFocusEndEditingCommit : NSObject
- (void)ended:(NSNotification *)notification;
@end
@implementation MacFocusEndEditingCommit
- (void)ended:(NSNotification *)notification
{
  NSTextField *field = (NSTextField *)[notification object];
  [field setStringValue:@"committed on resign"];
  [[field delegate] controlTextDidChange:
      [NSNotification notificationWithName:NSControlTextDidChangeNotification object:field]];
}
@end

namespace
{
  class RetireOnText
  {
    State<String> *const text_;
    Node *const target_;
    unsigned int calls_;

  public:
    RetireOnText(State<String> *text, Node *target)
        : text_(text), target_(target), calls_(0)
    {
      this->text_->bind(&changed, this, false, true);
    }
    ~RetireOnText()
    {
      this->text_->unbind(&changed, this);
    }
    unsigned int calls() const { return this->calls_; }
    static void changed(void *data)
    {
      RetireOnText &self = *static_cast<RetireOnText *>(data);
      ++self.calls_;
      LifecycleFactTestAccess::MarkSubtreeRetired(self.target_);
      LifecycleFactTestAccess::DeliverFacts(self.target_);
      // Reclaim the context while makeFirstResponder is on the stack. Terminal
      // delivery queued the native view; this does not drain the native clock.
      self.target_->setContext(0);
    }

  private:
    RetireOnText(const RetireOnText &);
    RetireOnText &operator=(const RetireOnText &);
  };
}

void testMacFocusWriteReentry()
{
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  [NSApplication sharedApplication];
  {
    WriteFixture f;
    if (f.key("testMacFocusWriteReentry"))
    {
      NSTextField *first = field(f.firstNode);
      LOKA_VERIFY([f.native makeFirstResponder:first]);
      LOKA_VERIFY([first currentEditor] != nil);
      f.app.flushInvalidationsTick();
      LOKA_VERIFY(f.facts.focus.state()->get().is(1u));
      const FocusBinding pending = f.binding(2u);
      RetireOnText retirement(f.facts.firstText.state(), f.secondNode);
      MacFocusEndEditingCommit *commit = [[MacFocusEndEditingCommit alloc] init];
      [[NSNotificationCenter defaultCenter] addObserver:commit selector:@selector(ended:)
                                                   name:NSControlTextDidEndEditingNotification object:first];
      f.facts.focus.post(2u);
      f.app.flushInvalidationsTick();
      [[NSNotificationCenter defaultCenter] removeObserver:commit];
      [commit release];
      // Positive control: no notification/observer is a failure, not a quiet pass.
      LOKA_VERIFY(retirement.calls() == 1);
      LOKA_VERIFY(f.facts.firstText.get().equals(String("committed on resign")));
      LOKA_VERIFY(f.secondNode->lifecycleFact() == NODE_FACT_RETIRED);
      LOKA_VERIFY(f.secondNode->getContext() == 0);
      LOKA_VERIFY(!f.facts.focus.state()->get().is(2u));
      LOKA_VERIFY(!pending.requested());
      LOKA_VERIFY([f.native isKeyWindow]);
    }
  }
  [pool drain];
}
