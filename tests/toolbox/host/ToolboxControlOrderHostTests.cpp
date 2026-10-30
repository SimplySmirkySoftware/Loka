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
  EmitterState &firstAction();
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
      composition.declare(Column() << Button("First").onClick(&firstAction()).TEST_ID("button")
                                   << RectSurface().size(100, 20));
    }
  };
  unsigned &firstPresses()
  {
    static unsigned presses = 0;
    return presses;
  }
  void countFirstPress(void *)
  {
    ++firstPresses();
  }
  EmitterState &firstAction()
  {
    static EmitterState action;
    static bool bound = false;
    if (!bound)
    {
      action.bind(&countFirstPress, 0, false);
      bound = true;
    }
    return action;
  }
  void verifyOneDraw(ControlRef control, const char *operation = "draw")
  {
    unsigned painted = 0;
    for (std::size_t i = 0; i < control->calls.size(); ++i)
      if (control->calls[i].painted)
      {
        ++painted;
        LOKA_VERIFY(control->calls[i].operation == operation);
      }
    LOKA_VERIFY(painted == 1);
  }
  /** Declared before the controller so its check runs after queue/pool drain. */
  class ControlCleanupCheck
  {
  public:
    explicit ControlCleanupCheck(ToolboxWindow &window)
        : window_(window)
    {
    }
    ~ControlCleanupCheck()
    {
      LOKA_VERIFY(this->window_.window()->native.controls.empty());
    }

  private:
    ToolboxWindow &window_;
  };
  void button(const char *mode)
  {
    ToolboxWindow window;
    HostPortScope port(window.window());
    ControlCleanupCheck cleanup(window);
    ToolboxScenePlatformController controller(&window);
    State<bool> disabled(false);
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
    verifyOneDraw(control, "show");
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
        LOKA_VERIFY(Button(ButtonProps().text(refused).onClick(&firstAction())).applyPropsToNode(node));
      }
      const bool retainedProps = std::strcmp(mode, "RetainedPropsReconcileDoesNotPaint") == 0;
      if (retainedProps)
      {
        toolbox_host::controlTitles.clear();
        LOKA_VERIFY(Button("Retained").enabled(&disabled).applyPropsToNode(node));
        LOKA_VERIFY(toolbox_host::controlCalls.draws == 0);
        LOKA_VERIFY(toolbox_host::controlTitles.size() == 1);
        LOKA_VERIFY(toolbox_host::controlTitles[0] == "Retained");
        LOKA_VERIFY(control->hilite == 255);
        LOKA_VERIFY(controller.buttonControls_[0].label == "Retained");
        for (std::size_t i = 0; i < control->calls.size(); ++i)
          LOKA_VERIFY(!control->calls[i].painted);
        control->calls.clear();
      }
      ToolboxInputDoor::render(controller);
      verifyOneDraw(control);
      if (retainedProps)
      {
        LOKA_VERIFY(toolbox_host::controlCalls.draws == 1);
        LOKA_VERIFY(toolbox_host::controlTitles.size() == 1);
        LOKA_VERIFY(controller.buttonControls_[0].label == "Retained" && control->hilite == 255);
      }
      LOKA_VERIFY(toolbox_host::paintOperations.size() == 2);
      LOKA_VERIFY(toolbox_host::paintOperations[0] == "bar");
      LOKA_VERIFY(toolbox_host::paintOperations[1] == "erase");
      LOKA_VERIFY(controller.debugStats_.totalControlDrawCount == 2);
      if (refusedLabel)
      {
        const PaintQuery query = {ToolboxPaintScope(), PLACEMENT_ELIGIBLE};
        LOKA_VERIFY(context->queryPaintDamage(query).kind == PAINT_ANSWER_REFUSED);
        LOKA_VERIFY(control->visible && controller.buttonControls_[0].label == "First");
        // The retained control is still on screen, so the press door still reaches it.
        Rect rect;
        LOKA_VERIFY(controller.buttonControls_[0].placement.queryVisible(rect));
        const Point inside = {static_cast<short>((rect.top + rect.bottom) / 2),
                              static_cast<short>((rect.left + rect.right) / 2)};
        const unsigned before = firstPresses();
        LOKA_VERIFY(!ToolboxInputDoor::mouseDown(controller, inside));
        LOKA_VERIFY(firstPresses() == before + 1);
      }
    }
    // Strip borrowed contexts while their nodes are still alive.
    controller.clearControls();
    controller.rootNode_ = 0;
    Access::unmount(scene);
  }
  void verifyVisibleHide(ControlRef control)
  {
    LOKA_VERIFY(control->visible);
  }
  void buttonHiddenMove(const char *mode)
  {
    ToolboxWindow window;
    HostPortScope port(window.window());
    ControlCleanupCheck cleanup(window);
    ToolboxScenePlatformController controller(&window);
    ButtonNode node(ButtonProps().text("Moved title"));
    ToolboxButtonContext context(&node, &controller);
    const Rect before = {10, 10, 30, 110};
    const Rect after = {20, 20, 45, 140};
    const Rect covered = {0, 0, 200, 200};
    const bool created = std::strcmp(mode, "ButtonCreatedPaintsOnce") == 0;
    const bool admitted = std::strcmp(mode, "ButtonMoveWithoutErasedPass") != 0;
    context.updateData(String::Literal("Moved title"), 0, 0, controller.allocateControlId(), 0);
    context.updateRect(before);
    if (!created)
    {
      context.render(&controller);
      controller.buttonControls_[0].control->calls.clear();
      context.updateRect(after);
    }
    const int draws = controller.debugStats_.controlDrawCount;
    const int total = controller.debugStats_.totalControlDrawCount;
    toolbox_host::controlCalls = toolbox_host::ControlCalls();
    if (!created)
      toolbox_host::beforeHide = verifyVisibleHide;
    if (admitted)
    {
      ToolboxErasedPass::Guard pass(controller.erasedPass(), covered);
      context.render(&controller);
    }
    else
      context.render(&controller);
    toolbox_host::beforeHide = 0;
    ControlRef control = controller.buttonControls_[0].control;
    const std::size_t start = created ? 2 : 1;
    if (created)
    {
      LOKA_VERIFY(control->calls[0].operation == "new" && !control->calls[0].painted);
      LOKA_VERIFY(control->calls[1].operation == "hide" && !control->calls[1].painted);
    }
    else
    {
      LOKA_VERIFY(control->calls[0].operation == "hide");
      LOKA_VERIFY(EmptyRect(&control->calls[0].ownerClip) == admitted);
      LOKA_VERIFY(control->calls[0].painted == !admitted);
    }
    LOKA_VERIFY(control->calls[start].operation == "move" && !control->calls[start].painted);
    LOKA_VERIFY(control->calls[start + 1].operation == "size" && !control->calls[start + 1].painted);
    for (std::size_t i = start; i + 1 < control->calls.size(); ++i)
    {
      LOKA_VERIFY(!control->calls[i].painted);
      LOKA_VERIFY(control->calls[i].operation != "draw");
    }
    LOKA_VERIFY(control->calls.back().operation == "show" && control->calls.back().painted);
    LOKA_VERIFY(toolbox_host::controlCalls.draws == 0 && toolbox_host::controlCalls.shows == 1);
    LOKA_VERIFY(controller.debugStats_.controlDrawCount == draws + 1);
    LOKA_VERIFY(controller.debugStats_.totalControlDrawCount == total + 1);
    controller.clearControls();
  }
  void scrollBar()
  {
    ToolboxWindow window;
    HostPortScope port(window.window());
    ControlCleanupCheck cleanup(window);
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
    // Strip borrowed contexts while their nodes are still alive.
    controller.clearControls();
  }
  void viewport()
  {
    ToolboxWindow window;
    HostPortScope port(window.window());
    ControlCleanupCheck cleanup(window);
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
    LOKA_VERIFY(toolbox_host::controlCalls.draws == 2);
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
    // Strip borrowed contexts while their nodes are still alive.
    controller.clearControls();
  }
} // namespace
#include "ToolboxParkedPressHost.hpp"
#include "ToolboxPressDoorHost.hpp"
#include "ToolboxNativeDoorPins.hpp"
#include "ToolboxFullControlPins.hpp"
int main(int argc, char **argv)
{
  LOKA_VERIFY(argc == 2);
  if (std::strncmp(argv[1], "FullControl", 11) == 0)
    full_control::run(argv[1]);
  else if (std::strncmp(argv[1], "NativeDoor", 10) == 0)
    native_door::run(argv[1]);
  else if (std::strcmp(argv[1], "ButtonHiddenMove") == 0 || std::strcmp(argv[1], "ButtonCreatedPaintsOnce") == 0
           || std::strcmp(argv[1], "ButtonMoveWithoutErasedPass") == 0)
    buttonHiddenMove(argv[1]);
  else if (std::strncmp(argv[1], "PressDoor", 9) == 0)
    press_door::run(argv[1]);
  else if (std::strcmp(argv[1], "ParkedNativeButton") == 0)
  {
    parked_press::pin(parked_press::NATIVE_BUTTON);
    for (int kind = parked_press::NATIVE_BUTTON; kind <= parked_press::VIEWPORT_HORIZONTAL; ++kind)
    {
      parked_press::disabledNative(static_cast<parked_press::Kind>(kind));
      parked_press::nativeVisibility(static_cast<parked_press::Kind>(kind), false);
      parked_press::nativeVisibility(static_cast<parked_press::Kind>(kind), true);
    }
  }
  else if (std::strcmp(argv[1], "ViewportBarsDrawnByRenderScrollView") == 0)
    viewport();
  else if (std::strcmp(argv[1], "ScrollBarDrawnAtRender") == 0)
    scrollBar();
  else
    button(argv[1]);
  return 0;
}
