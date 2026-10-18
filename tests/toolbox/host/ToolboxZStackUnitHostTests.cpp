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
    LOKA_VERIFY(controller.unitLedger_.size() == 1);
    {
      StateTrackerGuard guard(root->tracker());
      root->visible.set(false);
    }
    Access::flushInvalidation(scene);
    ToolboxInputDoor::render(controller);
    LOKA_VERIFY(controller.unitLedger_.empty());
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
    LOKA_VERIFY(controller.unitLedger_.size() == 1);
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
    LOKA_VERIFY(controller.unitLedger_.size() == 1);
    const Rect row = controller.unitLedger_[0].rect;
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
    LOKA_VERIFY(controller.unitLedger_.empty());
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
    LOKA_VERIFY(controller.unitLedger_.empty());
    LOKA_VERIFY(state.y == 60);
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
      stack.addChild(inner);
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
    const Rect outer = {25, 15, 55, 90};
    ClipRect(&outer);
    const bool projected = std::strcmp(mode, "UnitInsideViewportRowIsProjected") == 0;
    const bool refused = std::strcmp(mode, "RefusedChildHasNoRowAndNoRender") == 0;
    const bool inherited = std::strcmp(mode, "UnitFrameInheritsParentClip") == 0;
    if (projected || refused || inherited)
    {
      ProjectionParentScope viewport(window.window(), -7, -9, Frame(15, 25, 75, 30));
      ProjectionViewportScopeGuard guard(controller.projectionParentScopes_, viewport);
      upper->refuse = refused;
      if (inherited)
        state.height = 0;
      LayoutNode(&stack, state, &controller, 0);
      if (refused)
      {
        LOKA_VERIFY(controller.unitLedger_.empty());
        RenderNode(&stack, &controller);
        LOKA_VERIFY(upper->renders == 0);
      }
      else
      {
        LOKA_VERIFY(controller.unitLedger_.size() == 1);
        const Rect expected = {25, 15, 51, 90};
        equal(controller.unitLedger_[0].rect, expected);
        // A heightless seat must inherit the viewport; clip=seat erases this row.
        LOKA_VERIFY(upper->layouts == 1);
      }
      return;
    }
    LayoutNode(&stack, state, &controller, 0);
    toolbox_host::paintOperations.clear();
    toolbox_host::erasedRects.clear();
    RenderNode(&stack, &controller);
    if (nested || scroll)
    {
      LOKA_VERIFY(upper->layouts == 0 && upper->renders == 0);
      LOKA_VERIFY(controller.unitLedger_.empty());
      return;
    }
    LOKA_VERIFY(controller.unitLedger_.size() == 1);
    const Rect expected = {20, 10, 60, 110};
    equal(controller.unitLedger_[0].rect, expected);
    LOKA_VERIFY(controller.unitLedger_[0].stack == &stack && controller.unitLedger_[0].index == 1);
    LOKA_VERIFY(state.y == 90 && upper->layouts == 1 && ground->layouts == 1);
    LOKA_VERIFY(toolbox_host::paintOperations.size() == 3);
    LOKA_VERIFY(toolbox_host::paintOperations[0] == "ground");
    LOKA_VERIFY(toolbox_host::paintOperations[1] == "erase");
    LOKA_VERIFY(toolbox_host::paintOperations[2] == "upper");
    equal(toolbox_host::erasedRects[0], expected);
    equal(upper->clip, outer);
    if (std::strcmp(mode, "RefusedChildHasNoRowAndNoRender") != 0)
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
    if (std::strcmp(mode, "LedgerRebuiltEachPass") == 0)
    {
      controller.rootNode_ = ground;
      ToolboxInputDoor::render(controller);
      LOKA_VERIFY(controller.unitLedger_.empty());
      controller.rootNode_ = 0;
    }
  }
} // namespace
int main(int argc, char **argv)
{
  LOKA_VERIFY(argc == 2);
  if (std::strcmp(argv[1], "LedgerRebuiltEachPass") == 0)
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
      refusedControlCleanup();
  }
  return 0;
}
