#include "support/LifecycleFactTestAccess.hpp"
#include "ToolboxScrollViewDecisions.hpp"

/** Shared host fixture: every press enters the production input door. */
namespace parked_press
{
  using namespace loka::app;
  using namespace loka::app::scene;
  using namespace loka::core;

  enum Kind
  {
    NATIVE_BUTTON,
    DSL_BAR,
    VIEWPORT_VERTICAL,
    VIEWPORT_HORIZONTAL,
    NATIVE_EDIT,
    EDIT_HIT,
    POPUP_HIT,
    CELL_HIT,
    BUTTON_HIT,
    NULL_HIT,
    KIND_COUNT
  };

  struct Fixture
  {
    MutableState<String> text;
    MutableState<int> value;
    NodeState<int> valueSeat;
    EmitterState action, beneathAction;
    unsigned actions, beneathActions;
    ToolboxWindow window;
    HostPortScope port;
    ToolboxScenePlatformController controller;
    EditTextNode focused;
    Node *target;
    ButtonNode beneath;
    const Rect rect;
    Point point;

    static void count(void *data)
    {
      ++*static_cast<unsigned *>(data);
    }
    Fixture()
        : text(String("field")),
          value(0),
          valueSeat(&value, 0),
          actions(0),
          beneathActions(0),
          port(window.window()),
          controller(&window),
          focused(EditTextProps()),
          target(0),
          beneath(ButtonProps()),
          rect(makeRect()),
          point(makePoint())
    {
      this->action.bind(&count, &this->actions, false);
      this->beneathAction.bind(&count, &this->beneathActions, false);
      NotifySubtreeNodeAttached(&this->focused);
      ToolboxEditTextContext *context = this->editContext(&this->focused);
      this->focused.setContext(context);
      const Rect field = {150, 0, 170, 100};
      LOKA_VERIFY(this->controller.ensureEditTextControl(context, field, &this->text, NATIVE_HINT_DEFAULT).te);
      LOKA_VERIFY(this->controller.applyNativeFocus(*context));
    }
    ~Fixture()
    {
      this->controller.clearControls();

      this->controller.rootNode_ = 0;
      delete this->target;
      this->action.unbind(&count, &this->actions);
      this->beneathAction.unbind(&count, &this->beneathActions);
    }
    static Rect makeRect()
    {
      const Rect r = {0, 0, 100, 120};
      return r;
    }
    static Point makePoint()
    {
      const Point p = {5, 5};
      return p;
    }
    ToolboxEditTextContext *editContext(EditTextNode *node)
    {
#ifdef LOKA_HOST_CONTROL_ORDER
      return new ToolboxEditTextContext(node, &this->controller);
#else
      return new ToolboxEditTextContext(node);
#endif
    }
    void expectFocus(NodeContext *expected)
    {
      NodeContext *actual = 0;
      LOKA_VERIFY(this->controller.readNativeFocus(actual));
      LOKA_VERIFY(actual == expected);
    }
    void attach(Node *node, NodeContext *context)
    {
      this->target = node;
      NotifySubtreeNodeAttached(node);
      node->setContext(context);
    }
    void installBeneath()
    {
      NotifySubtreeNodeAttached(&this->beneath);
#ifdef LOKA_HOST_CONTROL_ORDER
      ToolboxButtonContext *context = new ToolboxButtonContext(&this->beneath, &this->controller);
      this->beneath.setContext(context);
      context->updateData(String("beneath"), &this->beneathAction, 0, 0, 0);
      context->updateRect(this->rect);
#else
      ToolboxButtonContext *context = new ToolboxButtonContext();
      this->beneath.setContext(context);
      context->rect_ = this->rect;
      context->emitter_ = &this->beneathAction;
#endif
      ToolboxHitLedger::ButtonHit hit;
      hit.rect = this->rect;
      hit.context = context;
      hit.emitter = &this->beneathAction;
      hit.enabled = 0;
      hit.boundary = 0;
      this->controller.installHit(hit);
    }
    void install(Kind kind)
    {
      this->installBeneath();
      switch (kind)
      {
      case NATIVE_BUTTON:
#ifdef LOKA_HOST_CONTROL_ORDER
      {
        ButtonNode *node = new ButtonNode(ButtonProps().text("park").onClick(&this->action));
        ToolboxButtonContext *context = new ToolboxButtonContext(node, &this->controller);
        this->attach(node, context);
        ToolboxScenePlatformController::ButtonControlBinding *binding = 0;
        LOKA_VERIFY(this->controller.ensureButtonControl(this->controller.allocateControlId(),
                                                         this->rect,
                                                         String("park"),
                                                         &this->action,
                                                         0,
                                                         NATIVE_HINT_DEFAULT,
                                                         context,
                                                         binding));
        LOKA_VERIFY(binding && binding->control);
        break;
      }
#else
        LOKA_VERIFY(false);
        break;
#endif
      case DSL_BAR:
      {
        ScrollBarNode *node =
            new ScrollBarNode(ScrollBarProps().value(this->valueSeat).range(0, 100).onChange(&this->action));
#ifdef LOKA_HOST_CONTROL_ORDER
        ToolboxScrollBarContext *context = new ToolboxScrollBarContext(node, &this->controller);
        this->attach(node, context);
        context->updateRect(this->rect);
        context->render(&this->controller);
        LOKA_VERIFY(this->controller.scrollControls().size() == 1);
#else
        this->attach(node, new NativeNodeContext());
        LOKA_VERIFY(this->controller.ensureScrollBarControl(
            this->controller.allocateControlId(), this->rect, node->props, node->getContext(), NATIVE_HINT_DEFAULT));
#endif
        this->controller.scrollControl(0).control->tracking = toolbox_host::TrackScript(129, 129, 42);
        break;
      }
      case VIEWPORT_VERTICAL:
      case VIEWPORT_HORIZONTAL:
      {
        ScrollViewNode *node = new ScrollViewNode(ScrollViewProps());
        this->attach(node, 0);
        this->controller.ensureViewportScrollBarControl(
            this->rect, node, 300, 84, 0, ToolboxScrollViewMetrics(300, 0), true);
        const unsigned index = kind == VIEWPORT_VERTICAL ? 0 : 1;
        LOKA_VERIFY(this->controller.scrollControls().size() == 2);
        const Rect bar = this->controller.scrollControl(index).rect;
        this->point.v = bar.top + 2;
        this->point.h = bar.left + 2;
        this->controller.scrollControl(index).control->tracking = toolbox_host::TrackScript(129, 129, 42);
        break;
      }
      case NATIVE_EDIT:
      case EDIT_HIT:
      case NULL_HIT:
      {
        EditTextNode *node = new EditTextNode(EditTextProps());
        ToolboxEditTextContext *context = this->editContext(node);
        this->attach(node, context);
        if (kind == NATIVE_EDIT)
          LOKA_VERIFY(this->controller.ensureEditTextControl(context, this->rect, &this->text, NATIVE_HINT_DEFAULT).te);
        else
          this->controller.recordEditHit(this->rect, &this->text, 0, kind == NULL_HIT ? 0 : context);
        break;
      }
      case POPUP_HIT:
#ifndef LOKA_HOST_CONTROL_ORDER
      {
        const char *items[] = {"one", "two"};
        PopupMenuNode *node = new PopupMenuNode(PopupMenuProps().items(items, 2).selectedIndex(this->valueSeat));
        ToolboxPopupMenuContext *context = new ToolboxPopupMenuContext();
        this->attach(node, context);
        context->rect_ = this->rect;
        context->items_ = node->props.items_;
        context->selectedIndex_ = &this->value;
        context->selectedIndexSeat_ = node->props.selectedIndex_;
        context->onChange_ = &this->action;
        ToolboxHitLedger::PopupHit hit;
        hit.rect = this->rect;
        hit.context = context;
        hit.enabled = 0;
        this->controller.installHit(hit);
        break;
      }
#else
        LOKA_VERIFY(false);
        break;
#endif
      case CELL_HIT:
#ifndef LOKA_HOST_CONTROL_ORDER
      {
        CellNode *node = new CellNode(CellProps().text("cell").onClick(&this->action));
        ToolboxCellContext *context = new ToolboxCellContext();
        this->attach(node, context);
        context->rect_ = this->rect;
        context->node_ = node;
        ToolboxHitLedger::CellHit hit;
        hit.rect = this->rect;
        hit.context = context;
        hit.emitter = &this->action;
        hit.boundary = 0;
        hit.text = 0;
        this->controller.installHit(hit);
        break;
      }
#else
        LOKA_VERIFY(false);
        break;
#endif
      case BUTTON_HIT:
#ifndef LOKA_HOST_CONTROL_ORDER
      {
        ButtonNode *node = new ButtonNode(ButtonProps().onClick(&this->action));
        ToolboxButtonContext *context = new ToolboxButtonContext();
        this->attach(node, context);
        context->rect_ = this->rect;
        context->emitter_ = &this->action;
        ToolboxHitLedger::ButtonHit hit;
        hit.rect = this->rect;
        hit.context = context;
        hit.emitter = &this->action;
        hit.enabled = 0;
        hit.boundary = 0;
        this->controller.installHit(hit);
        break;
      }
#else
        LOKA_VERIFY(false);
        break;
#endif
      case KIND_COUNT:
        LOKA_VERIFY(false);
        break;
      }
      this->controller.publishFixturePressOrder();
    }
  };

  void barParts()
  {
    Fixture f;
    f.install(DSL_BAR);
    ControlRef bar = f.controller.scrollControl(0).control;
    bar->tracking = toolbox_host::TrackScript(129, 129, 42);
    ToolboxInputDoor::mouseDown(f.controller, f.point);
    bool thumb = false;
    for (std::size_t i = 0; i < bar->calls.size(); ++i)
      if (bar->calls[i].operation == "track-no-action")
        thumb = true;
    LOKA_VERIFY(thumb && f.value.get() == 42);
    bar->calls.clear();
    bar->tracking = toolbox_host::TrackScript(21, 21, 0);
    bar->tracking.callbacks.assign(2, 21);
    ToolboxInputDoor::mouseDown(f.controller, f.point);
    bool arrow = false;
    for (std::size_t i = 0; i < bar->calls.size(); ++i)
      if (bar->calls[i].operation == "track-action")
        arrow = true;
    LOKA_VERIFY(arrow && f.value.get() == 44);
    std::puts("[pin] N6 thumb has no action UPP, arrow has action UPP passed");
  }
#ifndef LOKA_HOST_CONTROL_ORDER
  void erasedHit()
  {
    Fixture f;
    f.install(BUTTON_HIT);
    ButtonNode last((ButtonProps()));
    NotifySubtreeNodeAttached(&last);
    ToolboxButtonContext *lastContext = new ToolboxButtonContext();
    last.setContext(lastContext);
    const Rect away = {200, 200, 220, 240};
    lastContext->rect_ = away;
    lastContext->emitter_ = &f.beneathAction;
    ToolboxHitLedger::ButtonHit hit = f.controller.buttonHits()[0];
    hit.context = lastContext;
    hit.rect = away;
    f.controller.installHit(hit);
    // A shield between ground and target makes a missing shift observable
    // even when vector capacity leaves a readable copy past its new end.
    const ToolboxUnitLedger::UnitSeat unit = {0, 1, f.rect, f.rect};
    f.controller.unitLedger_.push(unit);
    f.controller.pressLedger_.begin();
    f.controller.pressLedger_.append(PressRow::BUTTON_HIT, 0);
    f.controller.pressLedger_.append(PressRow::UNIT, 0);
    f.controller.pressLedger_.append(PressRow::BUTTON_HIT, 1);
    f.controller.pressLedger_.append(PressRow::BUTTON_HIT, 2);
    f.controller.pressLedger_.commit();
    f.controller.retireNodeContext(f.beneath.getContext(), NATIVE_HINT_DEFAULT);
    LOKA_VERIFY(f.controller.buttonHits().size() == 2);
    ToolboxInputDoor::mouseDown(f.controller, f.point);
    LOKA_VERIFY(f.actions == 1 && f.beneathActions == 0);
    std::puts("[pin] N8 hit retirement shifts later index passed");
  }
  /** A drawn Button without onClick is a button too: the press stops on it. */
  void silentHit()
  {
    Fixture f;
    f.install(BUTTON_HIT);
    static_cast<ToolboxButtonContext *>(f.target->getContext())->emitter_ = 0;
    LOKA_VERIFY(ToolboxInputDoor::mouseDown(f.controller, f.point));
    LOKA_VERIFY(f.actions == 0 && f.beneathActions == 0);
    std::puts("[pin] N10 drawn button without onClick shields the button beneath passed");
  }
#endif

  void pin(Kind kind)
  {
    for (int phase = 0; phase != 3; ++phase)
    {
      Fixture f;
      f.install(kind);
      if (phase)
      {
        NotifySubtreeNodeDetached(f.target);
        if (phase == 2)
          LifecycleFactTestAccess::DeliverFacts(f.target);
        LOKA_VERIFY(f.target->lifecycleFact() == NODE_FACT_DETACHED_RETAINED);
      }
      const unsigned tracks = toolbox_host::tracks;
      ToolboxInputDoor::mouseDown(f.controller, f.point);
      if (phase && kind != NULL_HIT)
      {
        f.expectFocus(f.focused.getContext());
        LOKA_VERIFY(f.actions == 0 && f.beneathActions == 0);
        LOKA_VERIFY(toolbox_host::tracks == tracks && f.value.get() == 0);
      }
      else if (kind == NATIVE_EDIT || kind == EDIT_HIT)
        f.expectFocus(f.target->getContext());
      else if (kind == DSL_BAR || kind == VIEWPORT_VERTICAL || kind == VIEWPORT_HORIZONTAL)
      {
        f.expectFocus(f.focused.getContext());
        LOKA_VERIFY(toolbox_host::tracks == tracks + 1);
      }
      else if (kind == NULL_HIT)
      {
        f.expectFocus(0);
        LOKA_VERIFY(f.beneathActions == 1);
      }
      else if (kind != EDIT_HIT)
      {
        if (kind == NATIVE_BUTTON)
          f.expectFocus(f.focused.getContext());
        LOKA_VERIFY(f.actions == 1 && f.beneathActions == 0);
      }
      std::printf("[pin] parked row kind=%d phase=%d passed\n", kind, phase);
    }
  }
  /** A parked focused EditText keeps native focus until its row's sweep; a key there does nothing. */
  void typing()
  {
    for (int phase = 0; phase != 3; ++phase)
    {
      Fixture f;
      const TEHandle te = f.controller.editControls_[0].te;
      const short length = (**te).teLength;
      if (phase)
      {
        NotifySubtreeNodeDetached(&f.focused);
        if (phase == 2)
          LifecycleFactTestAccess::DeliverFacts(&f.focused);
        LOKA_VERIFY(f.focused.lifecycleFact() == NODE_FACT_DETACHED_RETAINED);
      }
      LOKA_VERIFY(ToolboxInputDoor::keyDown(f.controller, 'x'));
      f.expectFocus(f.focused.getContext());
      LOKA_VERIFY((**te).teLength == length + (phase ? 0 : 1));
      std::printf("[pin] parked typing phase=%d passed\n", phase);
    }
  }
#ifdef LOKA_HOST_CONTROL_ORDER
  void disabledNative(Kind kind)
  {
    Fixture f;
    f.install(kind);
    NotifySubtreeNodeDetached(f.target);
    const unsigned index = kind == VIEWPORT_HORIZONTAL ? 1 : 0;
    ControlRef control =
        kind == NATIVE_BUTTON ? f.controller.buttonControls_[0].control : f.controller.scrollControl(index).control;
    HiliteControl(control, 255);
    if (kind != NATIVE_BUTTON)
      f.controller.scrollControl(index).active = false;
    LOKA_VERIFY((**control).contrlVis);
    const unsigned tracks = toolbox_host::tracks;
    ToolboxInputDoor::mouseDown(f.controller, f.point);
    f.expectFocus(f.focused.getContext());
    LOKA_VERIFY(f.actions == 0 && f.beneathActions == 0 && toolbox_host::tracks == tracks);
  }
  void nativeVisibility(Kind kind, bool refuseRender)
  {
    Fixture f;
    f.install(kind);
    NotifySubtreeNodeDetached(f.target);
    LifecycleFactTestAccess::DeliverFacts(f.target);
    RectSurfaceNode empty((RectSurfaceProps()));
    f.controller.rootNode_ = &empty;
    if (refuseRender)
    {
      f.window.window()->portRect.left = -20000;
      f.window.window()->portRect.right = 20000;
    }
    ToolboxInputDoor::render(f.controller);
    const unsigned index = kind == VIEWPORT_HORIZONTAL ? 1 : 0;
    ControlRef control =
        kind == NATIVE_BUTTON ? f.controller.buttonControls_[0].control : f.controller.scrollControl(index).control;
    const Rect oldRect =
        kind == NATIVE_BUTTON ? f.controller.buttonControls_[0].rect : f.controller.scrollControl(index).rect;
    LOKA_VERIFY(f.target->lifecycleFact() == NODE_FACT_DETACHED_RETAINED);
    if (kind == NATIVE_BUTTON)
      LOKA_VERIFY(f.controller.buttonControls_[0].context == f.target->getContext());
    else if (kind == DSL_BAR)
      LOKA_VERIFY(f.controller.scrollControl(index).ownerContext == f.target->getContext());
    else
      LOKA_VERIFY(f.controller.viewportScroll().scrollView == f.target);
    LOKA_VERIFY(PtInRect(f.point, &oldRect));
    LOKA_VERIFY((**control).contrlVis == refuseRender);
    if (refuseRender)
    {
      LOKA_VERIFY(kind == NATIVE_BUTTON ? !f.controller.buttonControls_[0].usedThisFrame
                                        : !f.controller.scrollControl(index).usedThisFrame);
      const unsigned tracks = toolbox_host::tracks;
      ToolboxInputDoor::mouseDown(f.controller, f.point);
      f.expectFocus(f.focused.getContext());
      LOKA_VERIFY(f.actions == 0 && toolbox_host::tracks == tracks);
    }
    else
    {
      NotifySubtreeNodeAttached(&f.beneath);
      f.beneath.props = ButtonProps().text("replacement").onClick(&f.beneathAction);
      ToolboxButtonContext *replacement = new ToolboxButtonContext(&f.beneath, &f.controller);
      f.beneath.setContext(replacement);
      replacement->onPropsApplied();
      replacement->updateRect(oldRect);
      replacement->render(&f.controller);
      f.controller.pressLedger_.commit();
      LOKA_VERIFY(!(**control).contrlVis);
      ToolboxInputDoor::mouseDown(f.controller, f.point);
      LOKA_VERIFY(f.actions == 0 && f.beneathActions == 1);
    }
    std::printf("[pin] native visibility kind=%d render-refusal=%d passed\n", kind, refuseRender);
  }
#endif

} // namespace parked_press
