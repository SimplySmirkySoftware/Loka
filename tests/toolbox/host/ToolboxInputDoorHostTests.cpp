#include "ToolboxInputDoor.hpp"
#include "support/TestVerify.hpp"
#include "testing/core/StateTrackerTestAccess.hpp"
#include "support/WindowAdmissionTestApp.hpp"
#include "support/StandaloneMountTestSupport.hpp"
#include "testing/scene/SceneTestFlow.hpp"
#include "app/nodes/boundary/StdComposition.hpp"
#include "app/nodes/nestable/Show.hpp"
#include "app/nodes/nestable/RowColumn.hpp"
#include "ToolboxActivationPhase.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "ToolboxPresentHost.hpp"
namespace
{
  using namespace loka::app;
  using namespace loka::app::scene;
  using namespace loka::core;
  enum InputKind
  {
    EDIT_INPUT,
    POPUP_INPUT,
    CELL_INPUT,
    SCROLL_INPUT,
    BUTTON_INPUT,
    EXTENT_INPUT
  };

  /** Fixture owns input/extent storage and the sequence beyond Scene teardown;
      visibility is owned by the selected Boundary. */
  struct InputFacts
  {
    PushStateTracker tracker;
    NodeState<bool> *shown;
    MutableState<String> text;
    MutableState<int> selection, offset;
    MutableState<Frame> firstExtent, secondExtent;
    NodeState<String> textFact;
    NodeState<int> selectionFact, offsetFact;
    NodeState<Frame> firstFact, secondFact;
    EmitterState change;
    const InputKind kind;
    const bool parentVisibility;
    bool cancelHide;
    ToolboxApp *app;
    Window *closeOnExtent;
    ToolboxScenePlatformController *controller;
    unsigned sequence, destroyed, returned, writes, emits, secondWrites;
    explicit InputFacts(InputKind value, bool child)
        : shown(0),
          text(String("before")),
          selection(0),
          offset(0),
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
      LOKA_VERIFY(f.writes == 1);
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
      this->change.bind((this->kind == CELL_INPUT || this->kind == BUTTON_INPUT) ? &changed : &emitted, this, false);
    }
    void unbind()
    {
      this->text.unbind(&changed, this);
      this->selection.unbind(&changed, this);
      this->offset.unbind(&changed, this);
      this->firstExtent.unbind(&changed, this);
      this->secondExtent.unbind(&secondPublished, this);
      this->change.unbind((this->kind == CELL_INPUT || this->kind == BUTTON_INPUT) ? &changed : &emitted, this);
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
    case BUTTON_INPUT:
      composition.declare(seat << Button(ButtonProps().text("click").onClick(&facts.change)));
      break;
    case SCROLL_INPUT:
      composition.declare(seat << ScrollBar(ScrollBarProps().value(facts.offsetFact).range(0, 100).onChange(&facts.change)));
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
        || (kind == CELL_INPUT && node->asCellNode()) || (kind == SCROLL_INPUT && node->asScrollBarNode())
        || (kind == BUTTON_INPUT && node->asButtonNode()))
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


  template <class Context> class ObservedContext : public Context
  {
    InputFacts &facts_;
  public:
    explicit ObservedContext(InputFacts &facts) : facts_(facts) {}
    virtual void onFactChanged(NodeLifecycleFact oldFact, NodeLifecycleFact newFact)
    {
      Context::onFactChanged(oldFact, newFact);
      if (newFact == NODE_FACT_RETIRED)
        this->facts_.controller->retireEditTextControl(this, NATIVE_HINT_EAGER_RELEASE);
    }
    virtual ~ObservedContext() { this->facts_.destroyed = ++this->facts_.sequence; }
  };
  class InputWindow : public Window
  {
    ToolboxWindow &native_;
    InputFacts &facts_;
  public:
    static WindowProps props(InputFacts &facts)
    {
      return WindowProps().scene(new Scene(Boundary<InputRoot>(InputProps<InputRoot>(&facts))));
    }
    InputWindow(PlatformContext *platform, ToolboxScenePlatformController &controller, InputFacts &facts)
        : Window(platform, props(facts)), native_(*controller.window_), facts_(facts)
    { this->scene()->mount(&controller); }
    virtual ~InputWindow()
    {
      this->unmountSceneForTeardown(*this->scene());
      if (this->facts_.closeOnExtent) this->facts_.destroyed = ++this->facts_.sequence;
    }
    virtual bool hasLiveScenePlatform() const { return true; }
    virtual ToolboxWindow *asToolboxWindow() { return &this->native_; }
  };
  struct InputFixture
  {
    InputFacts facts;
    ToolboxWindow native;
    ToolboxScenePlatformController controller;
    loka::testing::StandaloneMountTestPlatformContext platform;
    InputWindow window;
    ToolboxApp app;
    explicit InputFixture(InputKind kind, bool parent)
        : facts(kind, parent), controller(&native), window(&platform, controller, facts), app(window)
    {
      app.flush();
      facts.app = &app;
      facts.controller = &controller;
      controller.rootNode_ = root();
    }
    Node *root() { return loka::dsl::testing::SceneTestAccess::rootBoundary(*window.scene()); }
    template <class Context> Context *observe(Node *node)
    {
      LOKA_VERIFY(node);
      Context *context = new ObservedContext<Context>(facts);
      context->setOwner(node);
      node->setContext(context);
      return context;
    }
    void input()
    {
      Node *node = findField(root(), facts.kind);
      const Rect rect = {0, 0, 40, 120};
      const Point point = {5, 5};
      switch (facts.kind)
      {
      case EDIT_INPUT:
      {
        ToolboxEditTextContext *context = observe<ToolboxEditTextContext>(node);
        LOKA_VERIFY(controller.ensureEditTextControl(context, rect, facts.textFact.state(), NATIVE_HINT_DEFAULT).te);
        controller.publishFixturePressOrder();
        LOKA_VERIFY(ToolboxInputDoor::mouseDown(controller, point));
        facts.bind();
        LOKA_VERIFY(ToolboxInputDoor::keyDown(controller, 'x'));
        break;
      }
      case POPUP_INPUT:
      {
        ToolboxPopupMenuContext *context = observe<ToolboxPopupMenuContext>(node);
        context->rect_ = rect;
        context->items_ = node->asPopupMenuNode()->props.items_;
        context->selectedIndex_ = facts.selectionFact.state();
        context->selectedIndexSeat_ = node->asPopupMenuNode()->props.selectedIndex_;
        context->onChange_ = &facts.change;
        ToolboxHitLedger::PopupHit hit;
        hit.rect = rect; hit.context = context; hit.enabled = 0;
        controller.installHit(hit);
        facts.bind();
        controller.publishFixturePressOrder();
        ToolboxInputDoor::mouseDown(controller, point);
        LOKA_VERIFY(facts.selection.get() == 1 && facts.emits == 1);
        break;
      }
      case SCROLL_INPUT:
      {
        observe<NativeNodeContext>(node);
        LOKA_VERIFY(controller.ensureScrollBarControl(
            1, rect, node->asScrollBarNode()->props, node->getContext(), NATIVE_HINT_DEFAULT));
        controller.scrollControl(0).control->tracking = toolbox_host::TrackScript(129, 129, 42);
        const unsigned tracks = toolbox_host::tracks;
        facts.bind();
        controller.publishFixturePressOrder();
        ToolboxInputDoor::mouseDown(controller, point);
        LOKA_VERIFY(toolbox_host::tracks == tracks + 1 && facts.offset.get() == 42 && facts.emits == 1);
        break;
      }
      case BUTTON_INPUT:
      {
        ToolboxButtonContext *context = observe<ToolboxButtonContext>(node);
        context->rect_ = rect; context->emitter_ = &facts.change;
        ToolboxHitLedger::ButtonHit hit;
        hit.rect = rect; hit.context = context; hit.emitter = &facts.change; hit.enabled = 0; hit.boundary = 0;
        controller.installHit(hit);
        facts.bind();
        controller.publishFixturePressOrder();
        ToolboxInputDoor::mouseDown(controller, point);
        break;
      }
      case CELL_INPUT:
      {
        ToolboxCellContext *context = observe<ToolboxCellContext>(node);
        context->rect_ = rect; context->node_ = node->asCellNode();
        ToolboxHitLedger::CellHit hit;
        hit.rect = rect; hit.context = context; hit.emitter = &facts.change; hit.boundary = 0; hit.text = 0;
        controller.installHit(hit);
        facts.bind();
        controller.publishFixturePressOrder();
        ToolboxInputDoor::mouseDown(controller, point);
        break;
      }
      case EXTENT_INPUT:
        break;
      }
    }
    void returned()
    {
      facts.returned = ++facts.sequence;
      LOKA_VERIFY(facts.writes == 1 && facts.destroyed == 0);
      LOKA_VERIFY(!controller.borrowPhase().open());
      facts.unbind();
      {
        loka::core::Operation turn;
        app.present(ACTIVATION_FOREGROUND, turn);
        LOKA_VERIFY(!loka::core::testing::OperationTestAccess::active());
      }
      LOKA_VERIFY(!loka::core::testing::OperationTestAccess::active());
      LOKA_VERIFY(!loka::core::Operation::isSettling());
      LOKA_VERIFY(facts.destroyed > facts.returned);
      LOKA_VERIFY(!findField(root(), facts.kind));
    }
  };
  class TrackerOwner;
  typedef BoundaryPropsFor<TrackerOwner> TrackerOwnerProps;
  class TrackerOwner : public StdCompositionBoundaryNodeBase<TrackerOwnerProps>
  {
    struct DoubleValue : DerivedState<int>::EvalFn
    {
      const NodeState<int> &source;
      explicit DoubleValue(const NodeState<int> &value) : source(value) {}
      virtual int operator()() { return this->source.get() * 2; }
    };

  public:
    NodeState<int> value;
    DerivedNodeState<int> doubled;
    EmitterState click;
    explicit TrackerOwner(const TrackerOwnerProps &props)
        : StdCompositionBoundaryNodeBase<TrackerOwnerProps>(props)
    {
      this->click.bind(&TrackerOwner::guardedWrite, this, false);
      this->state(this->value, 0);
      this->derived(this->doubled, this->value, new DoubleValue(this->value));
    }
    virtual ~TrackerOwner() { this->click.unbind(&TrackerOwner::guardedWrite, this); }
    static void guardedWrite(void *data)
    {
      TrackerOwner &owner = *static_cast<TrackerOwner *>(data);
      StateTrackerGuard guard(owner.tracker());
      owner.value.set(3);
    }
    virtual void composeNode(NodeComposition &composition)
    {
      const char *items[] = {"zero", "one", "two", "three"};
      composition.declare(Column()
          << PopupMenu(PopupMenuProps().items(items, 4).selectedIndex(this->value))
          << ScrollBar(ScrollBarProps().value(this->value).range(0, 10))
          << Button(ButtonProps().text("guarded").onClick(&this->click)));
    }
  };

  class TrackerWindow : public Window
  {
    ToolboxWindow &native_;
  public:
    unsigned admissions;
    TrackerWindow(ToolboxWindow &native, ToolboxScenePlatformController &controller)
        : Window(0, WindowProps().scene(new Scene(Boundary<TrackerOwner>(TrackerOwnerProps())))),
          native_(native), admissions(0)
    { this->scene()->mount(&controller); }
    virtual ~TrackerWindow() { this->unmountSceneForTeardown(*this->scene()); }
    virtual bool hasLiveScenePlatform() const { return true; }
    virtual ToolboxWindow *asToolboxWindow() { return &this->native_; }
    virtual void applyNativeVisibility() { ++this->admissions; }
  };

  void controlWriteUsesOwnerTracker(InputKind kind, bool clocked)
  {
    ToolboxWindow window;
    ToolboxScenePlatformController controller(&window);
    TrackerWindow logical(window, controller);
    ToolboxApp app(logical);
    app.flush();
    Scene &scene = *logical.scene();
    typedef loka::dsl::testing::SceneTestAccess Access;
    Access::updateAttached(scene, true);
    TrackerOwner *owner = static_cast<TrackerOwner *>(Access::rootBoundary(scene));
    controller.rootNode_ = owner;
    LOKA_VERIFY(owner->tracker() != 0);
    LOKA_VERIFY(window.getTracker() != 0);
    LOKA_VERIFY(owner->tracker() != window.getTracker());
    LOKA_VERIFY(owner->tracker()->phase() == TRACKER_IDLE);
    LOKA_VERIFY(owner->value.get() == 0);
    LOKA_VERIFY(owner->doubled.get() == 0);
    Node *node = findField(owner, kind);
    LOKA_VERIFY(node != 0);
    const Rect rect = {0, 0, 40, 120};
    const Point point = {5, 5};
    const short previousPopupItem = toolbox_host::popupItem;
    if (kind == BUTTON_INPUT)
    {
      ToolboxButtonContext *context = new ToolboxButtonContext();
      context->setOwner(node);
      node->setContext(context);
      context->rect_ = rect;
      context->emitter_ = &owner->click;
      ToolboxHitLedger::ButtonHit hit;
      hit.rect = rect; hit.context = context; hit.emitter = &owner->click;
      hit.enabled = 0; hit.boundary = owner;
      controller.installHit(hit);
    }
    else if (kind == POPUP_INPUT)
    {
      const PopupMenuProps &props = node->asPopupMenuNode()->props;
      LOKA_VERIFY(props.selectedIndex_.usesTracker(owner->tracker()));
      ToolboxPopupMenuContext *context = new ToolboxPopupMenuContext();
      context->setOwner(node);
      node->setContext(context);
      context->rect_ = rect;
      context->items_ = props.items_;
      context->selectedIndex_ = props.selectedIndex_.state();
      context->selectedIndexSeat_ = props.selectedIndex_;
      context->boundary_ = owner;
      ToolboxHitLedger::PopupHit hit;
      hit.rect = rect; hit.context = context; hit.enabled = 0;
      controller.installHit(hit);
      toolbox_host::popupItem = 4; // Toolbox menu items are one-based.
    }
    else
    {
      const ScrollBarProps &props = node->asScrollBarNode()->props;
      LOKA_VERIFY(props.value_.usesTracker(owner->tracker()));
      node->setContext(new NativeNodeContext());
      LOKA_VERIFY(controller.ensureScrollBarControl(1, rect, props, node->getContext(), NATIVE_HINT_DEFAULT));
      controller.scrollControl(0).control->tracking = toolbox_host::TrackScript(129, 129, 3);
    }
    if (clocked)
    {
      Operation turn;
      const unsigned admissions = logical.admissions;
      controller.publishFixturePressOrder();
      ToolboxInputDoor::mouseDown(controller, point);
      // PR C1/C2 (#1057): Seat/guard source is immediate, owner-derived values
      // move from before present to present's settle. The no-clock twin stays.
      LOKA_VERIFY(owner->value.get() == 3 && owner->doubled.get() == 0);
      LOKA_VERIFY(owner->tracker()->phase() == TRACKER_PRECOMMIT);
      app.present(ACTIVATION_FOREGROUND, turn);
      LOKA_VERIFY(logical.admissions == admissions + 1);
      LOKA_VERIFY(!Operation::hasActive());
    }
    else
    {
      controller.publishFixturePressOrder();
      ToolboxInputDoor::mouseDown(controller, point);
    }
    // No-clock #366 control; clock variant has now completed present.
    std::printf("[pin] %s owner tracker (%s): source=%d derived=%d\n",
        kind == POPUP_INPUT ? "popup" : (kind == BUTTON_INPUT ? "guarded-button" : "scroll"), clocked ? "clock" : "legacy", owner->value.get(), owner->doubled.get());
    std::fflush(stdout);
    LOKA_VERIFY(owner->value.get() == 3);
    LOKA_VERIFY(owner->doubled.get() == 6);
    LOKA_VERIFY(owner->tracker()->phase() == TRACKER_IDLE);
    toolbox_host::popupItem = previousPopupItem;
  }
  void controlWriteUsesOwnerTracker(InputKind kind)
  {
    controlWriteUsesOwnerTracker(kind, false);
    controlWriteUsesOwnerTracker(kind, true);
  }
  struct PresentSettleProbe
  {
    Scene *scene;
    unsigned calls;
    static void invalidate(void *data)
    {
      PresentSettleProbe &probe = *static_cast<PresentSettleProbe *>(data);
      ++probe.calls;
      LOKA_VERIFY(Operation::isSettling());
      probe.scene->requestInvalidate();
      LOKA_VERIFY(!probe.scene->flushInvalidation());
    }
  };
  void presentSettlesBeforeApply(ActivationPhase phase)
  {
    InputFixture fixture(EDIT_INPUT, false);
    MutableState<int> value(0);
    PushStateTracker ledger;
    ledger.addState(&value);
    PresentSettleProbe probe = { fixture.window.scene(), 0 };
    ledger.setInvalidateCallback(&PresentSettleProbe::invalidate, &probe);
    Operation turn;
    LOKA_VERIFY(turn.open(&ledger) == OPEN_OK);
    value.set(1);
    fixture.app.present(phase, turn);
    LOKA_VERIFY(probe.calls == 1);
    LOKA_VERIFY(!probe.scene->hasPendingInvalidation());
    LOKA_VERIFY(!loka::core::testing::OperationTestAccess::active());
  }
  void lifetime(InputKind kind, bool parentOnly)
  {
    for (int parent = parentOnly ? 1 : 0; parent != 2; ++parent)
    {
      InputFixture fixture(kind, parent != 0);
      fixture.input();
      fixture.returned();
    }
  }
  void extentOrdering()
  {
    for (int cancel = 0; cancel != 2; ++cancel)
    {
      InputFixture fixture(EXTENT_INPUT, false);
      fixture.facts.cancelHide = cancel != 0;
      fixture.facts.bind();
      ToolboxInputDoor::render(fixture.controller);
      LOKA_VERIFY(fixture.facts.writes == 1 && fixture.facts.secondWrites == 1);
      LOKA_VERIFY(surfaceCount(fixture.root()) == 2);
      LOKA_VERIFY(!fixture.controller.borrowPhase().open());
      fixture.facts.unbind();
      {
        loka::core::Operation turn;
        fixture.app.present(ACTIVATION_FOREGROUND, turn);
        LOKA_VERIFY(!loka::core::testing::OperationTestAccess::active());
      }
      LOKA_VERIFY(!loka::core::testing::OperationTestAccess::active());
      LOKA_VERIFY(!loka::core::Operation::isSettling());
      LOKA_VERIFY(surfaceCount(fixture.root()) == (cancel ? 2u : 1u));
    }
  }
  void closeDuringRender(ActivationPhase phase)
  {
    InputFacts facts(EXTENT_INPUT, false);
    ToolboxWindow native;
    ToolboxScenePlatformController controller(&native);
    loka::testing::StandaloneMountTestPlatformContext platform;
    InputWindow *window = new InputWindow(&platform, controller, facts);
    ToolboxApp app(*window);
    app.flush();
    facts.app = &app; facts.closeOnExtent = window; facts.controller = &controller;
    controller.rootNode_ = loka::dsl::testing::SceneTestAccess::rootBoundary(*window->scene());
    facts.bind();
    ToolboxInputDoor::render(controller);
    facts.returned = ++facts.sequence;
    LOKA_VERIFY(facts.writes == 1 && facts.secondWrites == 1 && facts.destroyed == 0);
    LOKA_VERIFY(!controller.borrowPhase().open());
    facts.unbind();
    {
      loka::core::Operation turn;
      app.present(phase, turn);
      LOKA_VERIFY(!loka::core::testing::OperationTestAccess::active());
    }
    LOKA_VERIFY(!loka::core::testing::OperationTestAccess::active());
    LOKA_VERIFY(!loka::core::Operation::isSettling());
    LOKA_VERIFY(facts.destroyed > facts.returned);
  }
}
#include "ToolboxParkedPressHost.hpp"
#include "ToolboxParkedDialogHost.hpp"
int main(int argc, char **argv)
{
  if (argc == 1 || std::strcmp(argv[1], "press-door") == 0)
  {
    parked_press::barParts();
    parked_press::erasedHit();
    std::puts("[skip] native ordering/unit pins run in LokaToolboxControlOrderTests PressDoor");
  }
  if (argc == 1 || std::strcmp(argv[1], "parked-dialog") == 0)
    parked_dialog::pin();
  if (argc == 1 || std::strcmp(argv[1], "parked-typing") == 0)
    parked_press::typing();
  if (argc == 1 || std::strcmp(argv[1], "parked") == 0)
  {
    if (argc == 3)
      parked_press::pin(static_cast<parked_press::Kind>(std::atoi(argv[2])));
    else
      for (int kind = parked_press::DSL_BAR; kind != parked_press::KIND_COUNT; ++kind)
        parked_press::pin(static_cast<parked_press::Kind>(kind));
  }
  if (argc == 1 || std::strcmp(argv[1], "turn") == 0)
  {
    presentSettlesBeforeApply(ACTIVATION_FOREGROUND);
    presentSettlesBeforeApply(ACTIVATION_BACKGROUND);
  }
  if (argc == 1 || std::strcmp(argv[1], "popup-owner-tracker") == 0)
    controlWriteUsesOwnerTracker(POPUP_INPUT);
  if (argc == 1 || std::strcmp(argv[1], "scroll-owner-tracker") == 0)
    controlWriteUsesOwnerTracker(SCROLL_INPUT);
  if (argc == 1 || std::strcmp(argv[1], "guarded-owner-tracker") == 0)
    controlWriteUsesOwnerTracker(BUTTON_INPUT);
  const char *names[] = {"edit", "popup", "cell", "scroll", "button"};
  for (int i = 0; i < 5; ++i)
    if (argc == 1 || std::strcmp(argv[1], names[i]) == 0)
    {
      lifetime(static_cast<InputKind>(i), argc == 3);
      std::printf("[pin] %s same/parent lifetime passed\n", names[i]);
    }
  if (argc == 1 || std::strcmp(argv[1], "extent") == 0) extentOrdering();
  if (argc == 1 || std::strcmp(argv[1], "close") == 0)
  {
    closeDuringRender(ACTIVATION_FOREGROUND);
    closeDuringRender(ACTIVATION_BACKGROUND);
  }
}
