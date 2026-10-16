#include "MacInputDoorTests.hpp"
#include "testing/core/StateTrackerTestAccess.hpp"
#include <AppKit/AppKit.h>
#include "testing/MacWindowTestAccess.hpp"
#include "app/nodes/controls/Cell.hpp"
#include "MacInputDoor.hpp"
#include "support/TestVerify.hpp"
#include "MacApp.hpp"
#include "platform/null/NullPlatformContext.hpp"
#include "testing/scene/SceneTestFlow.hpp"
#include "app/nodes/boundary/StdComposition.hpp"
#include "app/nodes/controls/EditText.hpp"
#include "app/nodes/nestable/Show.hpp"
#include "app/nodes/nestable/Box.hpp"
#include "app/nodes/nestable/RowColumn.hpp"
#include "app/nodes/Text.hpp"
#include "app/RectSurface.hpp"
#include "core/util/StateTrackerGuard.hpp"
#include <cstdio>

namespace
{
  using namespace loka::app;
  using namespace loka::app::scene;
  using namespace loka::core;

  /** Cocoa resources outlive every native fixture object. */
  struct CocoaHost
  {
    NSAutoreleasePool *pool;
    CocoaHost()
        : pool([[NSAutoreleasePool alloc] init])
    {
      [NSApplication sharedApplication];
    }
    ~CocoaHost()
    {
      [this->pool drain];
    }
  };

  /** Same borrowed-window fixture ownership as WindowAdmissionTestApp, with
      the actual macOS rail pump available for completion assertions. */
  class InputTestApp : public MacApp
  {
  public:
    explicit InputTestApp(Window &window)
        : MacApp(0)
    {
      this->group_ = new AppComponentGroup(std::vector<AppComponent *>(1, &window));
    }
    virtual ~InputTestApp()
    {
      this->group_->build();
    }
    virtual void quit() {}
    void flush()
    {
      this->flushWindowInvalidations();
    }
  };

  enum InputKind
  {
    EDIT_INPUT,
    POPUP_INPUT,
    CELL_INPUT,
    SCROLL_INPUT,
    EXTENT_INPUT
  };

  /** Fixture owns input/extent storage and the sequence beyond Scene teardown;
      visibility is owned by the selected Boundary. */
  struct InputFacts
  {
    PushStateTracker tracker;
    NodeState<bool> *shown;
    MutableState<String> text;
    MutableState<int> selection;
    MutableState<Frame> offset;
    MutableState<Frame> firstExtent, secondExtent;
    NodeState<String> textFact;
    NodeState<int> selectionFact;
    NodeState<Frame> offsetFact;
    NodeState<Frame> firstFact, secondFact;
    EmitterState change;
    const InputKind kind;
    const bool parentVisibility;
    bool cancelHide;
    InputTestApp *app;
    MacWindow *closeOnExtent;
    MacScenePlatformController *controller;
    unsigned sequence, destroyed, returned, writes, emits, secondWrites;
    explicit InputFacts(InputKind value, bool child)
        : shown(0),
          text(String("before")),
          selection(0),
          offset(Frame(0, 0, 0, 0)),
          firstExtent(Frame()),
          secondExtent(Frame()),
          textFact(&text, &tracker),
          selectionFact(&selection, &tracker),
          offsetFact(&offset, &tracker),
          firstFact(&firstExtent, &tracker),
          secondFact(&secondExtent, &tracker),
          kind(value),
          parentVisibility(child),
          cancelHide(false),
          app(0),
          closeOnExtent(0),
          controller(0),
          sequence(0),
          destroyed(0),
          returned(0),
          writes(0),
          emits(0),
          secondWrites(0)
    {
      this->tracker.addState(&this->text);
      this->tracker.addState(&this->selection);
      this->tracker.addState(&this->offset);
      this->tracker.addState(&this->firstExtent);
      this->tracker.addState(&this->secondExtent);
    }
    static void changed(void *data)
    {
      InputFacts &f = *static_cast<InputFacts *>(data);
      LOKA_VERIFY(loka::core::testing::OperationTestAccess::active());
      ++f.writes;
      if (f.closeOnExtent)
      {
        f.app->requestWindowClose(f.closeOnExtent);
        f.app->flush();
        LOKA_VERIFY(f.destroyed == 0);
        return;
      }
      // No explicit pump: the natural State/Scene path is the discriminator.
      if (f.writes == 1)
      {
        f.shown->set(false);
        f.shown->set(true);
        if (!f.cancelHide)
          f.shown->set(false);
      }
      LOKA_VERIFY(f.destroyed == 0);
    }
    static void emitted(void *data)
    {
      InputFacts &f = *static_cast<InputFacts *>(data);
      ++f.emits;
      LOKA_VERIFY(f.writes == 1 && f.selection.get() == 1);
      LOKA_VERIFY(f.destroyed == 0);
    }
    static void secondPublished(void *data)
    {
      InputFacts &f = *static_cast<InputFacts *>(data);
      ++f.secondWrites;
      LOKA_VERIFY(f.writes == 1 && f.controller->borrowPhase().open());
    }
    void bind()
    {
      if (this->kind == EDIT_INPUT)
        this->text.bind(&changed, this, false);
      if (this->kind == POPUP_INPUT)
        this->selection.bind(&changed, this, false);
      if (this->kind == SCROLL_INPUT)
        this->offset.bind(&changed, this, false);
      if (this->kind == EXTENT_INPUT)
      {
        this->firstExtent.bind(&changed, this, false);
        this->secondExtent.bind(&secondPublished, this, false);
      }
      this->change.bind(this->kind == CELL_INPUT ? &changed : &emitted, this, false);
    }
    void unbind()
    {
      this->text.unbind(&changed, this);
      this->selection.unbind(&changed, this);
      this->offset.unbind(&changed, this);
      this->firstExtent.unbind(&changed, this);
      this->secondExtent.unbind(&secondPublished, this);
      this->change.unbind(this->kind == CELL_INPUT ? &changed : &emitted, this);
    }
  };

  class InputRoot;
  class InputChild;
  template <class T> struct InputProps : NodePropsBase<InputProps<T> >
  {
    typedef T NodeType;
    struct TypeTag
    {
    };
    InputFacts *facts;
    explicit InputProps(InputFacts *value = 0)
        : facts(value)
    {
    }
    bool operator<(const PropsBase &rhs) const
    {
      return this->facts < static_cast<const InputProps &>(rhs).facts;
    }
  };

  void declareField(NodeComposition &composition, InputFacts &facts)
  {
    ShowDefinition seat = Show(*facts.shown->state()).destroyOnDetach();
    const char *items[] = {"before", "after"};
    switch (facts.kind)
    {
    case EDIT_INPUT:
      composition.declare(seat << EditText(EditTextProps(facts.textFact)));
      break;
    case POPUP_INPUT:
      composition.declare(
          seat << PopupMenu(
              PopupMenuProps().items(items, 2).selectedIndex(facts.selectionFact).onChange(&facts.change)));
      break;
    case CELL_INPUT:
      composition.declare(seat << Cell(CellProps().text("click").onClick(&facts.change)));
      break;
    case SCROLL_INPUT:
      composition.declare(seat << (Box().size(240, 80) << ScrollView(facts.offsetFact)));
      break;
    case EXTENT_INPUT:
      break;
    }
  }
  class InputChild : public StdCompositionBoundaryNodeBase<InputProps<InputChild> >
  {
    typedef StdCompositionBoundaryNodeBase<InputProps<InputChild> > Base;

  public:
    NodeState<bool> shown;
    explicit InputChild(const InputProps<InputChild> &props)
        : Base(props)
    {
      this->state(this->shown, true);
      if (!this->props.facts->parentVisibility)
        this->props.facts->shown = &this->shown;
    }
    virtual void composeNode(NodeComposition &composition)
    {
      declareField(composition, *this->props.facts);
    }
  };
  class InputRoot : public StdCompositionBoundaryNodeBase<InputProps<InputRoot> >
  {
    typedef StdCompositionBoundaryNodeBase<InputProps<InputRoot> > Base;

  public:
    NodeState<bool> shown;
    explicit InputRoot(const InputProps<InputRoot> &props)
        : Base(props)
    {
      this->state(this->shown, true);
      this->props.facts->shown = &this->shown;
    }
    virtual void composeNode(NodeComposition &composition)
    {
      InputFacts &facts = *this->props.facts;
      if (facts.kind == EXTENT_INPUT)
        composition.declare(Column() << RectSurface().laidOutExtent(facts.firstFact)
                                     << (Show(*facts.shown->state()).destroyOnDetach()
                                         << RectSurface().laidOutExtent(facts.secondFact)));
      else
        composition.declare(Boundary<InputChild>(InputProps<InputChild>(&facts)));
    }
  };

  Node *findField(Node *node, InputKind kind)
  {
    if (!node)
      return 0;
    if ((kind == EDIT_INPUT && node->asEditTextNode()) || (kind == POPUP_INPUT && node->asPopupMenuNode())
        || (kind == CELL_INPUT && node->asCellNode()) || (kind == SCROLL_INPUT && node->asScrollViewNode()))
      return node;
    INestable *nestable = node->asNestable();
    if (nestable)
      for (Node *child = nestable->childrenHead(); child; child = child->nextInComposition)
      {
        Node *found = findField(child, kind);
        if (found)
          return found;
      }
    return 0;
  }

  unsigned surfaceCount(Node *node)
  {
    if (!node)
      return 0;
    unsigned count = node->asRectSurfaceNode() ? 1u : 0u;
    INestable *nestable = node->asNestable();
    for (Node *child = nestable ? nestable->childrenHead() : 0; child; child = child->nextInComposition)
      count += surfaceCount(child);
    return count;
  }

  template <class Context, class Logical> class ObservedContext : public Context
  {
    InputFacts &facts_;

  public:
    ObservedContext(MacScenePlatformController &rail, void *parent, Logical *node, InputFacts &facts)
        : Context(&rail, parent, 0, 0, 240, 80, node),
          facts_(facts)
    {
    }
    virtual ~ObservedContext()
    {
      this->facts_.destroyed = ++this->facts_.sequence;
    }
  };

  struct InputFixture
  {
    CocoaHost host;
    InputFacts facts;
    NullPlatformContext platform;
    MacWindow window;
    InputTestApp app;
    static WindowProps props(InputFacts &facts)
    {
      return WindowProps()
          .frame(50, 50, 360, 260)
          .visible(true)
          .scene(new Scene(Boundary<InputRoot>(InputProps<InputRoot>(&facts))));
    }
    InputFixture(InputKind kind, bool child)
        : facts(kind, child),
          window(&platform, props(facts)),
          app(window)
    {
      this->app.flush();
      this->window.setApp(&this->app);
      LOKA_VERIFY(this->nativeWindow());
      this->facts.app = &this->app;
      this->facts.controller = static_cast<MacScenePlatformController *>(
          loka::dsl::testing::SceneTestAccess::platformController(*this->window.scene()));
      LOKA_VERIFY(this->facts.controller);
    }
    ~InputFixture()
    {
      // The borrowed stack Window outlives its App; revoke its Scene first.
      loka::dsl::testing::SceneTestAccess::unmount(*this->window.scene());
      this->window.setApp(0);
    }
    NSWindow *nativeWindow()
    {
      return (NSWindow *)loka::dsl::testing::MacWindowTestAccess::nativeWindow(this->window);
    }
    NSView *nativeRoot()
    {
      return (NSView *)loka::dsl::testing::MacWindowTestAccess::contentView(this->window);
    }
    Node *root()
    {
      return loka::dsl::testing::SceneTestAccess::rootBoundary(*this->window.scene());
    }
    template <class Context, class Logical> NSView *observe(Logical *node)
    {
      LOKA_VERIFY(node && node->getContext());
      // Fixture-only resource retirement before replacing a live projection.
      // setContext alone is silent on a live node and cannot retire a native view.
      // The ScrollView is empty, so there are no child native handles to move.
      node->getContext()->onFactChanged(NODE_FACT_ATTACHED, NODE_FACT_RETIRED);
      node->setContext(0);
      ObservedContext<Context, Logical> *context =
          new ObservedContext<Context, Logical>(*this->facts.controller, this->nativeRoot(), node, this->facts);
      node->setContext(context);
      context->readLifecycleFactOnAttach();
      this->facts.controller->relayout(360, 260);
      this->app.flush(); // Drain the replaced native projection before input observation.
      return [[this->nativeRoot() subviews] lastObject];
    }
    void returned()
    {
      LOKA_VERIFY(!loka::core::testing::OperationTestAccess::active());
      this->facts.returned = ++this->facts.sequence;
      LOKA_VERIFY(this->facts.writes > 0 && this->facts.destroyed == 0);
      LOKA_VERIFY(!this->facts.controller->borrowPhase().open());
      this->facts.unbind();
      this->app.flushInvalidationsTick();
      LOKA_VERIFY(this->facts.destroyed > this->facts.returned);
      LOKA_VERIFY(!findField(this->root(), this->facts.kind));
    }
  };
} // namespace

void testMacInputDoorEditTextLifetime()
{
  for (int child = 0; child != 2; ++child)
  {
    InputFixture fixture(EDIT_INPUT, child != 0);
    NSTextField *field =
        (NSTextField *)fixture.observe<MacEditTextContext>(findField(fixture.root(), EDIT_INPUT)->asEditTextNode());
    fixture.facts.bind();
    [field setStringValue:@"after"];
    [[field delegate] controlTextDidChange:[NSNotification notificationWithName:NSControlTextDidChangeNotification
                                                                         object:field]];
    LOKA_VERIFY(fixture.facts.text.get().equals(String("after")));
    fixture.returned();
  }
}

void testMacInputDoorPopupLifetime()
{
  for (int child = 0; child != 2; ++child)
  {
    InputFixture fixture(POPUP_INPUT, child != 0);
    NSPopUpButton *field = (NSPopUpButton *)fixture.observe<MacPopupMenuContext>(
        findField(fixture.root(), POPUP_INPUT)->asPopupMenuNode());
    fixture.facts.bind();
    [field selectItemAtIndex:1];
    [field sendAction:[field action] to:[field target]];
    LOKA_VERIFY(fixture.facts.emits == 1);
    fixture.returned();
  }
}

void testMacInputDoorScrollLifetime()
{
  for (int child = 0; child != 2; ++child)
  {
    InputFixture fixture(SCROLL_INPUT, child != 0);
    NSScrollView *field = (NSScrollView *)fixture.observe<MacScrollViewContext>(
        findField(fixture.root(), SCROLL_INPUT)->asScrollViewNode());
    static_cast<MacScrollViewContext *>(findField(fixture.root(), SCROLL_INPUT)->getContext())
        ->commitScrollMetrics(0, 2000, 80, 0, 0, false);
    fixture.facts.bind();
    [[field contentView] setBoundsOrigin:NSMakePoint(0, 100)];
    LOKA_VERIFY(fixture.facts.offset.get().y > 0);
    fixture.returned();
  }
}

void testMacInputDoorCellLifetime()
{
  for (int child = 0; child != 2; ++child)
  {
    InputFixture fixture(CELL_INPUT, child != 0);
    NSView *field = fixture.observe<MacCellContext>(findField(fixture.root(), CELL_INPUT)->asCellNode());
    fixture.facts.bind();
#if defined(MAC_OS_X_VERSION_MAX_ALLOWED) && MAC_OS_X_VERSION_MAX_ALLOWED >= 101200
    const NSEventType mouseDownType = NSEventTypeLeftMouseDown;
#else
    const NSEventType mouseDownType = NSLeftMouseDown;
#endif
    NSEvent *event = [NSEvent mouseEventWithType:mouseDownType
                                        location:NSMakePoint(4, 4)
                                   modifierFlags:0
                                       timestamp:0
                                    windowNumber:[fixture.nativeWindow() windowNumber]
                                         context:nil
                                     eventNumber:1
                                      clickCount:1
                                        pressure:1.0];
    [field mouseDown:event];
    fixture.returned();
  }
}

void testMacInputDoorExtentOrdering()
{
  for (int cancel = 0; cancel != 2; ++cancel)
  {
    InputFixture fixture(EXTENT_INPUT, false);
    fixture.facts.cancelHide = cancel != 0;
    LOKA_VERIFY(surfaceCount(fixture.root()) == 2);
    // Change bounds before installing observers, then drive the real delegate.
    [fixture.nativeRoot() setFrameSize:NSMakeSize(480, 320)];
    fixture.facts.bind();
    [[fixture.nativeWindow() delegate]
        windowDidResize:[NSNotification notificationWithName:NSWindowDidResizeNotification
                                                      object:fixture.nativeWindow()]];
    LOKA_VERIFY(fixture.facts.writes == 1 && fixture.facts.secondWrites == 1);
    LOKA_VERIFY(surfaceCount(fixture.root()) == 2);
    LOKA_VERIFY(!fixture.facts.controller->borrowPhase().open());
    fixture.facts.unbind();
    fixture.app.flushInvalidationsTick();
    LOKA_VERIFY(fixture.facts.shown->get() == (cancel != 0));
    LOKA_VERIFY(surfaceCount(fixture.root()) == (cancel ? 2u : 1u));
  }
}

namespace
{
  class ClosingInputWindow : public MacWindow
  {
    InputFacts &facts_;

  public:
    ClosingInputWindow(PlatformContext *platform, InputFacts &facts)
        : MacWindow(platform, InputFixture::props(facts)),
          facts_(facts)
    {
    }
    virtual ~ClosingInputWindow()
    {
      this->facts_.destroyed = ++this->facts_.sequence;
    }
  };
} // namespace

void testMacInputDoorCloseDuringPendingLayout()
{
  CocoaHost host;
  InputFacts facts(EXTENT_INPUT, false);
  NullPlatformContext platform;
  ClosingInputWindow *window = new ClosingInputWindow(&platform, facts);
  InputTestApp app(*window);
  window->setApp(&app);
  app.flush();
  facts.app = &app;
  facts.closeOnExtent = window;
  facts.controller = static_cast<MacScenePlatformController *>(
      loka::dsl::testing::SceneTestAccess::platformController(*window->scene()));
  LOKA_VERIFY(facts.controller);
  NSView *root = (NSView *)loka::dsl::testing::MacWindowTestAccess::contentView(*window);
  [root setFrameSize:NSMakeSize(480, 320)];
  // Keep another live map row pending: closing one window must not pump or
  // invalidate the walk, and the other controller must still make progress.
  InputFixture other(EXTENT_INPUT, false);
  other.facts.cancelHide = true;
  [other.nativeRoot() setFrameSize:NSMakeSize(490, 330)];
  other.facts.bind();
  other.facts.controller->requestRelayout();
  facts.bind();
  facts.controller->requestRelayout();
  MacScenePlatformController::flushPendingRelayouts();
  LOKA_VERIFY(other.facts.writes == 1 && other.facts.secondWrites == 1);
  LOKA_VERIFY(!other.facts.controller->borrowPhase().open());
  other.facts.unbind();
  facts.returned = ++facts.sequence;
  LOKA_VERIFY(facts.writes == 1 && facts.secondWrites == 1 && facts.destroyed == 0);
  LOKA_VERIFY(!facts.controller->borrowPhase().open());
  facts.unbind();
  app.flushInvalidationsTick();
  LOKA_VERIFY(facts.destroyed > facts.returned);
}
