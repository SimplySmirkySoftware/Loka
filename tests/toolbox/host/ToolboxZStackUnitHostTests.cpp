#include "ToolboxHost.hpp"
#include "ToolboxNodeDispatch.hpp"
#include "ToolboxInputDoor.hpp"
#include "ToolboxPlatformLayoutHandlers.hpp"
#include "app/nodes/nestable/Box.hpp"
#include "app/nodes/nestable/RowColumn.hpp"
#include "app/nodes/nestable/Show.hpp"
#include "app/nodes/boundary/StdComposition.hpp"
#include "testing/scene/SceneTestFlow.hpp"
#include <cstring>

using namespace loka::app;
using namespace loka::app::scene;
using namespace loka::core;
namespace
{
  /** Pins record operations and bounding-box clips, never pixel/region verdicts. */
  class Probe : public RectSurfaceNode, public IProjectedLayoutNode
  {
  public:
    const char *name;
    short height;
    unsigned layouts, renders;
    Rect clip;
    bool refuse;
    Probe(const char *value, short extent)
        : RectSurfaceNode(RectSurfaceProps()),
          name(value),
          height(extent),
          layouts(0),
          renders(0),
          clip(),
          refuse(false)
    {
    }
    virtual IProjectedLayoutNode *asProjectedLayoutNode()
    {
      return this;
    }
    virtual short layoutProjected(IPlatformController *platform, LayoutState &state)
    {
      ToolboxScenePlatformController &controller = *static_cast<ToolboxScenePlatformController *>(platform);
      ++layouts;
      if (!controller.projectLayoutState(state))
        return 0;
      if (refuse)
        controller.refuseScrollViewShortRange();
      state.y = static_cast<short>(state.y + height);
      return state.width;
    }
    virtual void render(IPlatformController *)
    {
      ++renders;
      clip = toolbox_host::currentClip();
      toolbox_host::paintOperations.push_back(name);
    }
  };
  LayoutState seat()
  {
    LayoutState s;
    s.x = 10;
    s.y = 20;
    s.width = 100;
    s.height = 80;
    s.spacing = 0;
    return s;
  }
  void equal(const Rect &a, const Rect &b)
  {
    LOKA_VERIFY(EqualRect(&a, &b));
  }
  class ShowingNode;
  typedef BoundaryPropsFor<ShowingNode> ShowingProps;
  class ShowingNode : public StdCompositionBoundaryNodeBase<ShowingProps>
  {
  public:
    typedef ShowingProps::TypeTag TypeTag;
    NodeState<bool> visible;
    explicit ShowingNode(const ShowingProps &props)
        : StdCompositionBoundaryNodeBase<ShowingProps>(props)
    {
      this->state(this->visible, true);
    }
    virtual void composeNode(NodeComposition &composition)
    {
      composition.declare(ZStack() << Box().size(100, 70) << (Show(*this->visible.state()) << Box().size(100, 40)));
    }
  };
  void rebuilt()
  {
    ToolboxWindow window;
    HostPortScope port(window.window());
    ToolboxScenePlatformController controller(&window);
    LOKA_VERIFY(RegisterToolboxPlatformLayoutHandlers(controller.gapLayoutRegistry));
    Scene scene((Boundary<ShowingNode>(ShowingProps())));
    scene.mount(&controller);
    typedef loka::dsl::testing::SceneTestAccess Access;
    Access::updateAttached(scene, true);
    ShowingNode *root = static_cast<ShowingNode *>(Access::rootBoundary(scene));
    controller.rootNode_ = root;
    ToolboxInputDoor::render(controller);
    LOKA_VERIFY(controller.unitLedger_.mark() == 1);
    BoundaryLocalApplyInfo info;
    info.paintKind = LOCAL_APPLY_PAINT_COMPOSITED;
    PlatformApplyPlan plan;
    plan.paintKind = PlatformApplyPlan::PAINT_LOCAL;
    plan.setPrimaryRoot(root);
    const unsigned before = toolbox_host::invalidations;
    controller.onBoundaryApply(root, root, info, plan);
    LOKA_VERIFY(toolbox_host::invalidations > before);
    {
      StateTrackerGuard guard(root->tracker());
      root->visible.set(false);
    }
    Access::flushInvalidation(scene);
    ToolboxInputDoor::render(controller);
    LOKA_VERIFY(controller.unitLedger_.mark() == 0);
    controller.rootNode_ = 0;
    Access::unmount(scene);
  }
  class PlacedButton : public ButtonNode
  {
  public:
    PlacedButton()
        : ButtonNode(ButtonProps().text("Inside"))
    {
    }
    virtual short layoutProjected(IPlatformController *platform, LayoutState &state)
    {
      ToolboxScenePlatformController *controller = static_cast<ToolboxScenePlatformController *>(platform);
      if (!controller->projectLayoutState(state))
        return 0;
      return this->getContext()->layout(platform, state);
    }
  };
  void inheritedButton()
  {
    ToolboxWindow window;
    HostPortScope port(window.window());
    ToolboxScenePlatformController controller(&window);
    ZStackNode stack((ZStackProps()));
    stack.addChild(new Probe("ground", 70));
    ButtonNode *button = new PlacedButton();
    button->setContext(new ToolboxButtonContext(button, &controller));
    stack.addChild(button);
    LOKA_VERIFY(controller.projectionParentScopes_.resetRoot(window.window(), Frame(0, 0, 300, 200)));
    const Rect outer = {25, 15, 55, 90};
    ClipRect(&outer);
    ProjectionParentScope viewport(window.window(), -7, -9, Frame(15, 25, 75, 30));
    ProjectionViewportScopeGuard guard(controller.projectionParentScopes_, viewport);
    LayoutState state = seat();
    state.height = 0;
    state.lineHeight = 40;
    LayoutNode(&stack, state, &controller, 0);
    LOKA_VERIFY(controller.unitLedger_.mark() == 1);
    RenderNode(&stack, &controller);
    LOKA_VERIFY(controller.buttonControls_.size() == 1);
    const Rect rect = controller.buttonControls_[0].rect;
    LOKA_VERIFY(rect.left == 15 && rect.right == 90 && rect.top == 25 && rect.bottom <= 55);
    const toolbox_host::ControlCall &draw = controller.buttonControls_[0].control->calls.back();
    LOKA_VERIFY(draw.operation == "draw" && draw.painted);
    LOKA_VERIFY(draw.ownerClip.left >= outer.left && draw.ownerClip.right <= outer.right);
    State<bool> enabled(false);
    controller.buttonControls_[0].enabled = &enabled;
    ControlRef native = controller.buttonControls_[0].control;
    native->calls.clear();
    const unsigned beforeInvalidation = toolbox_host::invalidations;
    {
      ToolboxWindow other;
      HostPortScope otherPort(other.window());
      controller.refreshButtonEnabled(controller.buttonControls_[0]);
      GrafPtr current;
      GetPort(&current);
      LOKA_VERIFY(current == other.window());
    }
    LOKA_VERIFY(toolbox_host::invalidations == beforeInvalidation + 1);
    LOKA_VERIFY(native->hilite == 255);
    for (size_t i = 0; i < native->calls.size(); ++i)
      LOKA_VERIFY(!native->calls[i].painted);
    controller.clearControls();
  }
  void refusedControlCleanup()
  {
    ToolboxWindow window;
    HostPortScope port(window.window());
    ToolboxScenePlatformController controller(&window);
    ZStackNode stack((ZStackProps()));
    stack.addChild(new Probe("ground", 70));
    PlacedButton *button = new PlacedButton();
    button->setContext(new ToolboxButtonContext(button, &controller));
    stack.addChild(button);
    stack.addChild(new Probe("later-unit", 40));
    controller.rootNode_ = &stack;
    ToolboxInputDoor::render(controller);
    LOKA_VERIFY(controller.buttonControls_.size() == 1);
    ControlRef control = controller.buttonControls_[0].control;
    LOKA_VERIFY(control->visible);
    control->calls.clear();
    toolbox_host::refuseRegionAttempt = toolbox_host::regions + 1;
    ToolboxInputDoor::render(controller);
    toolbox_host::refuseRegionAttempt = 0;
    LOKA_VERIFY(!control->visible);
    for (size_t i = 0; i < control->calls.size(); ++i)
      LOKA_VERIFY(!control->calls[i].painted);
    controller.clearControls();
    controller.rootNode_ = 0;
  }
  void viewportRow()
  {
    ToolboxWindow window;
    HostPortScope port(window.window());
    ToolboxScenePlatformController controller(&window);
    MutableState<Frame> origin(Frame(0, 9, 0, 0));
    NodeState<Frame> viewportState(&origin, 0);
    ScrollViewNode viewport((ScrollViewProps(viewportState)));
    ZStackNode *stack = new ZStackNode(ZStackProps());
    stack->addChild(new Probe("ground", 200));
    Probe *upper = new Probe("upper", 40);
    stack->addChild(upper);
    viewport.addChild(stack);
    LOKA_VERIFY(controller.projectionParentScopes_.resetRoot(window.window(), Frame(0, 0, 300, 200)));
    LayoutState state = seat();
    LayoutNode(&viewport, state, &controller, 0);
    LOKA_VERIFY(controller.unitLedger_.mark() == 1);
    const Rect row = controller.unitLedger_.find(stack, 1)->rect;
    const Rect clip = controller.viewportScroll().contentRect;
    LOKA_VERIFY(row.top == clip.top && row.bottom == 51);
    LOKA_VERIFY(row.left >= clip.left && row.right <= clip.right);
    LOKA_VERIFY(row.bottom != 60); // An unprojected seat would end here.
    Probe *refused = new Probe("refused-after-unit", 40);
    refused->refuse = true;
    stack->addChild(refused);
    controller.unitLedger_.clear();
    state = seat();
    LayoutNode(&viewport, state, &controller, 0);
    LOKA_VERIFY(controller.unitLedger_.mark() == 0);
    controller.clearControls();
  }
  void clippedUnitExtent()
  {
    ToolboxWindow window;
    ToolboxScenePlatformController controller(&window);
    ZStackNode stack((ZStackProps()));
    stack.addChild(new Probe("ground", 10));
    stack.addChild(new Probe("offscreen-unit", 40));
    LOKA_VERIFY(controller.projectionParentScopes_.resetRoot(window.window(), Frame(0, 0, 300, 300)));
    ProjectionParentScope viewport(window.window(), 0, 0, Frame(0, 100, 100, 100));
    ProjectionViewportScopeGuard guard(controller.projectionParentScopes_, viewport);
    LayoutState state = seat();
    LayoutNode(&stack, state, &controller, 0);
    LOKA_VERIFY(controller.unitLedger_.mark() == 0);
    LOKA_VERIFY(state.y == 60);
  }
  void rootOverflowRepro()
  {
    ToolboxWindow window;
    HostPortScope port(window.window());
    ToolboxScenePlatformController controller(&window);
    LOKA_VERIFY(RegisterToolboxPlatformLayoutHandlers(controller.gapLayoutRegistry));
    LOKA_VERIFY(controller.projectionParentScopes_.resetRoot(window.window(), Frame(0, 0, 300, 200)));
    ZStackNode stack((ZStackProps()));
    stack.addChild(new Probe("ground", 70));
    StackNode *column = new StackNode(Column().gap(0).props);
    column->addChild(new Probe("first", 10));
    column->addChild(new BoxNode(Box().size(100, SHRT_MAX).props));
    column->addChild(new Probe("after-refusal", 40));
    stack.addChild(column);
    LayoutState state = seat();
    LayoutNode(&stack, state, &controller, 0);
    LOKA_VERIFY(controller.unitLedger_.mark() == 0);
  }
  void toggleCaret(TEHandle te)
  {
    (**te).caretState = (**te).caretState ? 0 : -1;
  }
  void idleRepair()
  {
    ToolboxWindow window;
    HostPortScope port(window.window());
    ToolboxScenePlatformController controller(&window);
    ZStackNode stack((ZStackProps()));
    stack.addChild(new Probe("ground", 70));
    stack.addChild(new Probe("unit", 40));
    stack.addChild(new Probe("second-unit", 30));
    LOKA_VERIFY(controller.projectionParentScopes_.resetRoot(window.window(), Frame(0, 0, 300, 200)));
    LayoutState state = seat();
    LayoutNode(&stack, state, &controller, 0);
    ToolboxScenePlatformController::EditTextControlBinding binding;
    binding.ownerContext = 0;
    binding.editor = 0;
    binding.text = 0;
    binding.rect = Rect();
    binding.rect.left = 5;
    binding.rect.top = 25;
    binding.rect.right = 90;
    binding.rect.bottom = 75;
    binding.te = TENew(&binding.rect, &binding.rect);
    binding.usedThisFrame = true;
    binding.lifetimeHint = NATIVE_HINT_EAGER_RELEASE;
    controller.editControls_.add(binding);
    controller.editControls_.focus(0);
    toolbox_host::invalidatedRects.clear();
    ToolboxInputDoor::idleTextEdits(controller);
    LOKA_VERIFY(toolbox_host::invalidatedRects.empty());
    toolbox_host::afterIdle = toggleCaret;
    ToolboxInputDoor::idleTextEdits(controller);
    LOKA_VERIFY(toolbox_host::invalidatedRects.size() == 2);
    const Rect first = {25, 10, 60, 90};
    const Rect second = {25, 10, 50, 90};
    equal(toolbox_host::invalidatedRects[0], first);
    equal(toolbox_host::invalidatedRects[1], second);
    toolbox_host::invalidatedRects.clear();
    const Rect contained = {25, 15, 45, 90};
    controller.editControls_[0].rect = contained;
    ToolboxInputDoor::idleTextEdits(controller);
    LOKA_VERIFY(toolbox_host::invalidatedRects.empty());
    const Rect touching = {60, 10, 80, 90};
    controller.editControls_[0].rect = touching;
    ToolboxInputDoor::idleTextEdits(controller);
    LOKA_VERIFY(toolbox_host::invalidatedRects.empty());
    controller.rootNode_ = &stack;
    const unsigned before = controller.debugStats_.totalRenderCalls;
    ToolboxInputDoor::renderDirty(controller, touching);
    LOKA_VERIFY(controller.debugStats_.totalRenderCalls == before);
    controller.rootNode_ = 0;
    toolbox_host::afterIdle = 0;
    controller.clearControls();
    // A vanished window cannot supply a safe deactivation port. Its active
    // record must retire eagerly instead of entering the EditText churn pool.
    binding.te = TENew(&binding.rect, &binding.rect);
    TEActivate(binding.te);
    controller.window_ = 0;
    controller.retireEditTextBinding(binding, NATIVE_HINT_DEFAULT);
    LOKA_VERIFY(!binding.te);
    LOKA_VERIFY(controller.retiredTextEdits_.back().lifetimeHint == NATIVE_HINT_EAGER_RELEASE);
    controller.window_ = &window;
  }
  void textDirtyClip()
  {
    ToolboxWindow window;
    HostPortScope port(window.window());
    ToolboxScenePlatformController controller(&window);
    ZStackNode stack((ZStackProps()));
    stack.addChild(new Probe("ground", 70));
    stack.addChild(new Probe("unit", 40));
    LOKA_VERIFY(controller.projectionParentScopes_.resetRoot(window.window(), Frame(0, 0, 300, 200)));
    LayoutState state = seat();
    LayoutNode(&stack, state, &controller, 0);
    State<String> text(String("Ground text partly below unit"));
    ToolboxScenePlatformController::TextHit hit;
    hit.context = 0;
    hit.rect = Rect();
    hit.rect.left = 10;
    hit.rect.top = 50;
    hit.rect.right = 110;
    hit.rect.bottom = 80;
    hit.x = 10;
    hit.y = 65;
    hit.text = &text;
    hit.boundary = 0;
    hit.lastMeasuredWidth = 100;
    hit.needsRelayoutOnChange = false;
    controller.recordTextHit(hit.rect, hit.x, hit.y, hit.text, hit.boundary, false, hit.lastMeasuredWidth, 0);
    controller.rootNode_ = &stack;
    const Rect dirty = {65, 10, 80, 110};
    ClipRect(&dirty); // drawDirty's outer clip, supplied by the native window.
    toolbox_host::draws.clear();
    const unsigned before = controller.debugStats_.totalRenderCalls;
    ToolboxInputDoor::renderDirty(controller, dirty);
    LOKA_VERIFY(controller.debugStats_.totalRenderCalls == before);
    LOKA_VERIFY(toolbox_host::draws.size() == 1);
    equal(toolbox_host::draws[0].clip, dirty);
    toolbox_host::draws.clear();
    toolbox_host::refuseRegionAttempt = toolbox_host::regions + 1;
    ToolboxInputDoor::renderDirty(controller, dirty);
    toolbox_host::refuseRegionAttempt = 0;
    LOKA_VERIFY(toolbox_host::draws.empty());
    State<String> shorter(String("x"));
    hit.text = &shorter;
    hit.lastMeasuredWidth = 100;
    toolbox_host::refuseRegionAttempt = toolbox_host::regions + 1;
    controller.redrawTextHit(hit);
    toolbox_host::refuseRegionAttempt = 0;
    LOKA_VERIFY(hit.lastMeasuredWidth == 100);
    toolbox_host::erasedRects.clear();
    controller.redrawTextHit(hit);
    LOKA_VERIFY(toolbox_host::erasedRects.size() == 1);
    LOKA_VERIFY(toolbox_host::erasedRects[0].right == 110);
    LOKA_VERIFY(hit.lastMeasuredWidth < 100);
    controller.rootNode_ = 0;
  }
  void run(const char *mode)
  {
    ToolboxWindow window;
    HostPortScope port(window.window());
    ToolboxScenePlatformController controller(&window);
    LOKA_VERIFY(RegisterToolboxPlatformLayoutHandlers(controller.gapLayoutRegistry));
    LOKA_VERIFY(controller.projectionParentScopes_.resetRoot(window.window(), Frame(0, 0, 300, 200)));
    ZStackNode stack((ZStackProps()));
    Probe *ground = new Probe("ground", 70);
    Probe *upper = new Probe("upper", 40);
    stack.addChild(ground);
    const bool nested = std::strcmp(mode, "NestedZStackInUnitLaysOutNothing") == 0;
    const bool scroll = std::strcmp(mode, "ScrollViewRefusedInsideUnit") == 0;
    if (nested)
    {
      ZStackNode *inner = new ZStackNode(ZStackProps());
      inner->addChild(upper);
      StackNode *column = new StackNode(Column().gap(0).props);
      column->addChild(new Probe("unit-content", 10));
      column->addChild(inner);
      stack.addChild(column);
    }
    else if (scroll)
    {
      ScrollViewNode *inner = new ScrollViewNode(ScrollViewProps());
      inner->addChild(upper);
      stack.addChild(inner);
    }
    else
      stack.addChild(upper);
    LayoutState state = seat();
    const Rect outer = {25, 5, 75, 90};
    ClipRect(&outer);
    if (std::strcmp(mode, "RefusedChildHasNoRowAndNoRender") == 0)
    {
      upper->refuse = true;
      LayoutNode(&stack, state, &controller, 0);
      LOKA_VERIFY(controller.unitLedger_.mark() == 0);
      RenderNode(&stack, &controller);
      LOKA_VERIFY(upper->renders == 0);
      return;
    }
    LayoutNode(&stack, state, &controller, 0);
    toolbox_host::paintOperations.clear();
    toolbox_host::erasedRects.clear();
    RenderNode(&stack, &controller);
    if (nested || scroll)
    {
      LOKA_VERIFY(upper->layouts == 0 && upper->renders == 0);
      LOKA_VERIFY(controller.unitLedger_.mark() == (nested ? 1u : 0u));
      return;
    }
    LOKA_VERIFY(controller.unitLedger_.mark() == 1);
    const Rect expected = {20, 10, 60, 110};
    equal(controller.unitLedger_.find(&stack, 1)->rect, expected);
    LOKA_VERIFY(controller.unitLedger_.find(&stack, 1)->stack == &stack
                && controller.unitLedger_.find(&stack, 1)->index == 1);
    LOKA_VERIFY(state.y == 90 && upper->layouts == 1 && ground->layouts == 1);
    LOKA_VERIFY(toolbox_host::paintOperations.size() == 3);
    LOKA_VERIFY(toolbox_host::paintOperations[0] == "ground");
    LOKA_VERIFY(toolbox_host::paintOperations[1] == "erase");
    LOKA_VERIFY(toolbox_host::paintOperations[2] == "upper");
    equal(toolbox_host::erasedRects[0], expected);
    const Rect clipped = {25, 10, 60, 90};
    equal(upper->clip, clipped);
    {
      toolbox_host::refuseRegionAttempt = toolbox_host::regions + 1;
      const unsigned before = upper->renders;
      RenderNode(&stack, &controller);
      toolbox_host::refuseRegionAttempt = 0;
      LOKA_VERIFY(upper->renders == before);
    }
    if (std::strcmp(mode, "DirtyTouchingUnitTakesCompositionReplay") == 0)
    {
      controller.rootNode_ = &stack;
      const unsigned before = controller.debugStats_.totalRenderCalls;
      ToolboxInputDoor::renderDirty(controller, expected);
      LOKA_VERIFY(controller.debugStats_.totalRenderCalls == before + 1);
      LOKA_VERIFY(upper->renders >= 2);
      controller.rootNode_ = 0;
    }
  }
} // namespace
int main(int argc, char **argv)
{
  LOKA_VERIFY(argc == 2);
  if (std::strcmp(argv[1], "TextDirtyClipStaysBelowUnit") == 0)
    textDirtyClip();
  else if (std::strcmp(argv[1], "IdleRepairOnlyToggledOverlaps") == 0)
    idleRepair();
  else if (std::strcmp(argv[1], "LedgerRebuiltEachPass") == 0)
    rebuilt();
  else if (std::strcmp(argv[1], "UnitFrameInheritsParentClip") == 0)
    inheritedButton();
  else if (std::strcmp(argv[1], "UnitInsideViewportRowIsProjected") == 0)
    viewportRow();
  else
  {
    run(argv[1]);
    if (std::strcmp(argv[1], "LaterChildGetsUnitRow") == 0)
      clippedUnitExtent();
    if (std::strcmp(argv[1], "RefusedChildHasNoRowAndNoRender") == 0)
    {
      refusedControlCleanup();
      rootOverflowRepro();
    }
  }
  return 0;
}
