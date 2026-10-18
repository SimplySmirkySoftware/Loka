#include "ToolboxHost.hpp"
#include "context/ToolboxScrollBarContext.hpp"
#include "ToolboxInputDoor.hpp"
#include "ToolboxDirtyReplay.hpp"
#include "ToolboxPaintSuppressScope.hpp"
#include "platform/String.hpp"
#include "ToolboxScrollViewDecisions.hpp"
#include "app/nodes/boundary/StdComposition.hpp"
#include "testing/scene/SceneTestFlow.hpp"
#include <cstring>

using namespace loka::app;
using namespace loka::app::scene;
using namespace loka::core;
namespace
{
  class RefusingUtf8 : public loka::platform::String
  {
  public:
    virtual bool appendUtf8(std::string &) const
    {
      return false;
    }
  };
  class OrderNode;
  typedef BoundaryPropsFor<OrderNode> OrderProps;
  class OrderNode : public StdCompositionBoundaryNodeBase<OrderProps>
  {
  public:
    typedef OrderProps::TypeTag TypeTag;
    explicit OrderNode(const OrderProps &props)
        : StdCompositionBoundaryNodeBase<OrderProps>(props)
    {
    }
    virtual void composeNode(NodeComposition &composition)
    {
      composition.declare(Column() << Button("First").TEST_ID("button") << RectSurface().size(100, 20));
    }
  };
  void verifyOneDraw(ControlRef control)
  {
    unsigned painted = 0;
    for (std::size_t i = 0; i < control->calls.size(); ++i)
      if (control->calls[i].painted)
      {
        ++painted;
        LOKA_VERIFY(control->calls[i].operation == "draw");
      }
    LOKA_VERIFY(painted == 1);
  }
  void button(const char *mode)
  {
    ToolboxWindow window;
    HostPortScope port(window.window());
    ToolboxScenePlatformController controller(&window);
    Scene scene((Boundary<OrderNode>(OrderProps())));
    scene.mount(&controller);
    typedef loka::dsl::testing::SceneTestAccess Access;
    Access::updateAttached(scene, true);
    Node *node = 0;
    loka::dsl::FlowError error;
    LOKA_VERIFY(loka::dsl::testing::LookupNodeById<Node>(&scene, "button", node, error));
    ToolboxButtonContext *context = new ToolboxButtonContext(node->asButtonNode(), &controller);
    node->setContext(context);
    LayoutState seat;
    seat.x = 10;
    seat.y = 10;
    seat.width = 100;
    seat.height = 20;
    seat.lineHeight = 16;
    seat.spacing = 0;
    context->layout(&controller, seat);
    controller.rootNode_ = Access::rootBoundary(scene);
    toolbox_host::reset();
    toolbox_host::paintOperations.clear();
    toolbox_host::controlCalls = toolbox_host::ControlCalls();
    ToolboxInputDoor::render(controller);
    LOKA_VERIFY(controller.buttonControls_.size() == 1);
    ControlRef control = controller.buttonControls_[0].control;
    verifyOneDraw(control);
    LOKA_VERIFY(toolbox_host::paintOperations.size() == 2);
    LOKA_VERIFY(toolbox_host::paintOperations[0] == "bar");
    LOKA_VERIFY(toolbox_host::paintOperations[1] == "erase");
    control->calls.clear();
    toolbox_host::reset();
    toolbox_host::paintOperations.clear();
    toolbox_host::controlCalls = toolbox_host::ControlCalls();
    if (std::strcmp(mode, "CompositionReplayDrawsControlsInWalk") == 0)
    {
      const Rect dirty = {0, 0, 100, 120};
      ToolboxRenderDirtyInCompositionOrder(controller, dirty);
      verifyOneDraw(control);
      LOKA_VERIFY(toolbox_host::paintOperations.size() == 3);
      LOKA_VERIFY(toolbox_host::paintOperations[0] == "erase");
      LOKA_VERIFY(toolbox_host::paintOperations[1] == "bar");
      LOKA_VERIFY(toolbox_host::paintOperations[2] == "erase");
      control->calls.clear();
      toolbox_host::reset();
      toolbox_host::paintOperations.clear();
      toolbox_host::controlCalls = toolbox_host::ControlCalls();
      const Rect miss = {80, 0, 100, 120};
      ToolboxRenderDirtyInCompositionOrder(controller, miss);
      LOKA_VERIFY(toolbox_host::controlCalls.draws == 0);
      for (std::size_t i = 0; i < control->calls.size(); ++i)
        LOKA_VERIFY(!control->calls[i].painted);
    }
    else
    {
      const bool refusedLabel = std::strcmp(mode, "ButtonRefusedLabelDrawsInstalledControl") == 0;
      if (refusedLabel)
      {
        const String refused = String::FromPlatform(Managed<loka::platform::String>::Wrap(new RefusingUtf8()));
        LOKA_VERIFY(Button(ButtonProps().text(refused)).applyPropsToNode(node));
      }
      ToolboxInputDoor::render(controller);
      verifyOneDraw(control);
      LOKA_VERIFY(toolbox_host::paintOperations.size() == 2);
      LOKA_VERIFY(toolbox_host::paintOperations[0] == "bar");
      LOKA_VERIFY(toolbox_host::paintOperations[1] == "erase");
      LOKA_VERIFY(controller.debugStats_.totalControlDrawCount == 2);
      if (refusedLabel)
      {
        const PaintQuery query = {ToolboxPaintScope(), PLACEMENT_ELIGIBLE};
        LOKA_VERIFY(context->queryPaintDamage(query).kind == PAINT_ANSWER_REFUSED);
        LOKA_VERIFY(control->visible && controller.buttonControls_[0].label == "First");
      }
    }
    // Host retirement uses the shared bulk teardown while the contexts live.
    controller.clearControls();
    controller.rootNode_ = 0;
    Access::unmount(scene);
  }
  void scrollBar()
  {
    ToolboxWindow window;
    HostPortScope port(window.window());
    ToolboxScenePlatformController controller(&window);
    ScrollBarNode node(ScrollBarProps().range(0, 100));
    ToolboxScrollBarContext context(&node, &controller);
    const Rect rect = {0, 0, 100, 16};
    context.updateRect(rect);
    for (unsigned reach = 0; reach < 2; ++reach)
    {
      if (reach)
        controller.scrollControl(0).control->calls.clear();
      toolbox_host::controlCalls = toolbox_host::ControlCalls();
      context.render(&controller);
      LOKA_VERIFY(toolbox_host::controlCalls.draws == 1);
      verifyOneDraw(controller.scrollControl(0).control);
    }
    const Rect beforeSuppression = toolbox_host::currentClip();
    toolbox_host::controlCalls = toolbox_host::ControlCalls();
    {
      ToolboxPaintSuppressScope suppress(controller.paintSuppressClipRgn_);
      controller.drawScrollBarControl(controller.scrollControl(0).resourceId);
      LOKA_VERIFY(toolbox_host::controlCalls.draws == 0);
    }
    const Rect afterSuppression = toolbox_host::currentClip();
    LOKA_VERIFY(EqualRect(&beforeSuppression, &afterSuppression));
    const Rect miss = {0, 30, 100, 100};
    ClipRect(&miss);
    toolbox_host::controlCalls = toolbox_host::ControlCalls();
    context.render(&controller);
    LOKA_VERIFY(toolbox_host::controlCalls.draws == 0);
  }
  void viewport()
  {
    ToolboxWindow window;
    HostPortScope port(window.window());
    ToolboxScenePlatformController controller(&window);
    ScrollViewNode scroll((ScrollViewProps()));
    scroll.addChild(new RectSurfaceNode(RectSurfaceProps()));
    ButtonNode *button = new ButtonNode(ButtonProps().text("Inside"));
    ToolboxButtonContext *context = new ToolboxButtonContext(button, &controller);
    button->setContext(context);
    LayoutState seat;
    seat.x = 10;
    seat.y = 10;
    seat.width = 100;
    seat.height = 20;
    seat.lineHeight = 16;
    seat.spacing = 0;
    context->layout(&controller, seat);
    scroll.addChild(button);
    const Rect viewport = {0, 0, 120, 180};
    controller.ensureViewportScrollBarControl(viewport, &scroll, 300, 104, 0, ToolboxScrollViewMetrics(200, 0), true);
    controller.viewportScroll().contentRect = Rect();
    controller.viewportScroll().contentRect.bottom = 104;
    controller.viewportScroll().contentRect.right = 164;
    LOKA_VERIFY(controller.projectionParentScopes_.resetRoot(window.window(), Frame(0, 0, 300, 300)));
    const Rect outer = {0, 0, 120, 180};
    ClipRect(&outer);
    toolbox_host::reset();
    toolbox_host::paintOperations.clear();
    toolbox_host::controlCalls = toolbox_host::ControlCalls();
    controller.renderScrollView(&scroll);
    LOKA_VERIFY(toolbox_host::paintOperations.size() == 4);
    LOKA_VERIFY(toolbox_host::paintOperations[0] == "erase");
    LOKA_VERIFY(toolbox_host::paintOperations[1] == "bar");
    LOKA_VERIFY(toolbox_host::paintOperations[2] == "bar");
    LOKA_VERIFY(toolbox_host::paintOperations[3] == "bar");
    LOKA_VERIFY(toolbox_host::controlCalls.draws == 3);
    const Rect restored = toolbox_host::currentClip();
    LOKA_VERIFY(EqualRect(&restored, &outer));
    for (std::size_t i = 0; i < controller.scrollControls().size(); ++i)
    {
      const toolbox_host::ControlCall &call = controller.scrollControl(i).control->calls.back();
      LOKA_VERIFY(call.operation == "draw" && call.painted);
      LOKA_VERIFY(call.ownerClip.right > 164 || call.ownerClip.bottom > 104);
    }
    const Rect miss = {80, 0, 100, 120};
    ClipRect(&miss);
    toolbox_host::controlCalls = toolbox_host::ControlCalls();
    controller.renderScrollView(&scroll);
    LOKA_VERIFY(toolbox_host::controlCalls.draws == 0);
    const Rect touch = {10, 10, 30, 110};
    ClipRect(&touch);
    controller.renderScrollView(&scroll);
    LOKA_VERIFY(toolbox_host::controlCalls.draws == 1);
    for (int refused = 1; refused <= 2; ++refused)
    {
      ClipRect(&outer);
      toolbox_host::paintOperations.clear();
      toolbox_host::refuseRegionAttempt = toolbox_host::regions + refused;
      controller.renderScrollView(&scroll);
      toolbox_host::refuseRegionAttempt = 0;
      LOKA_VERIFY(toolbox_host::paintOperations.empty());
      const Rect restoredOnRefusal = toolbox_host::currentClip();
      LOKA_VERIFY(EqualRect(&restoredOnRefusal, &outer));
    }
    controller.clearControls();
  }
} // namespace
int main(int argc, char **argv)
{
  LOKA_VERIFY(argc == 2);
  if (std::strcmp(argv[1], "ViewportBarsDrawnByRenderScrollView") == 0)
    viewport();
  else if (std::strcmp(argv[1], "ScrollBarDrawnAtRender") == 0)
    scrollBar();
  else
    button(argv[1]);
  return 0;
}
