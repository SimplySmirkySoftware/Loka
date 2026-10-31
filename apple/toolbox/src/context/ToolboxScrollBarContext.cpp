#include "context/ToolboxScrollBarContext.hpp"
#include "ToolboxScenePlatformController.hpp"
#include "app/scene/projection/RetainedNodeHandler.hpp"

namespace
{
  class ToolboxScrollBarNodeHandler
      : public loka::app::scene::RetainedNodeHandler<ToolboxScrollBarNodeHandler,
                                                     loka::app::ScrollBarNode,
                                                     ToolboxScrollBarContext>
  {
  public:
    static loka::app::ScrollBarNode *cast(loka::app::scene::Node *node)
    {
      return node ? node->asScrollBarNode() : 0;
    }

    static ToolboxScrollBarContext *create(loka::app::ScrollBarNode *node,
                                           loka::app::scene::IPlatformController *controller,
                                           const loka::app::scene::LayoutState &state)
    {
      (void)state;
      return new ToolboxScrollBarContext(node, static_cast<ToolboxScenePlatformController *>(controller));
    }
  };

  ToolboxScrollBarNodeHandler gToolboxScrollBarNodeHandler;
} // namespace

ToolboxScrollBarContext::ToolboxScrollBarContext(loka::app::ScrollBarNode *node,
                                                 ToolboxScenePlatformController *controller)
    : ToolboxProjectedNodeContext(controller),
      node_(node),
      rect_(),
      resourceId_(0)
{
}

ToolboxScrollBarContext::~ToolboxScrollBarContext() {}

void ToolboxScrollBarContext::retireNativeProjection()
{
  if (this->controller())
  {
    // The terminal fact delivery just refreshed the hint snapshot, so the
    // retire flush decides on the freshest value -- not the last render's.
    this->controller()->destroyScrollBarControl(resourceId_, this->lifetimeHint());
  }
  this->node_ = 0;
}

void ToolboxScrollBarContext::updateRect(const Rect &rect)
{
  rect_ = rect;
}

short ToolboxScrollBarContext::layout(loka::app::scene::IPlatformController *controller,
                                      loka::app::scene::LayoutState &state)
{
  (void)controller;
  if (!node_)
  {
    return 0;
  }
  const short thickness = static_cast<short>(loka::app::SCROLL_BAR_THICKNESS);
  Rect rect;
  rect.left = state.x;
  rect.top = state.y;
  short usedWidth = thickness;
  if (node_->props.orientation_ == loka::app::SCROLL_BAR_HORIZONTAL)
  {
    // Length comes from the layout pass, thickness never does: a scroll bar
    // that grew on its cross axis would stop looking like the system's.
    short length = state.width > 0 ? state.width : static_cast<short>(thickness * 4);
    rect.right = static_cast<short>(rect.left + length);
    rect.bottom = static_cast<short>(rect.top + thickness);
    usedWidth = length;
  }
  else
  {
    short length = state.height > 0 ? state.height : static_cast<short>(state.lineHeight * 4);
    if (length < thickness)
    {
      length = thickness;
    }
    rect.right = static_cast<short>(rect.left + thickness);
    rect.bottom = static_cast<short>(rect.top + length);
  }
  updateRect(rect);
  state.y = static_cast<short>(rect.bottom);
  return usedWidth;
}

void ToolboxScrollBarContext::draw(ToolboxScenePlatformController *controller)
{
  if (!node_)
  {
    LOKA_PAINT_REFUSE(ToolboxPaintRefusals(this->controller()), TOOLBOX_PAINT_REFUSAL_OBLIGATION_UNMET);
    return;
  }
  if (controller && resourceId_ <= 0)
  {
    // Identity rule: the auto id stays with this context for its lifetime,
    // so the controller's binding table keeps pointing at the same control
    // across recomposes.
    resourceId_ = controller->allocateControlId();
  }
  if (controller && resourceId_ > 0)
  {
    if (controller->ensureScrollBarControl(resourceId_, rect_, node_->props, this, lifetimeHint()))
    {
      controller->reachNativeBar(resourceId_);
      controller->drawScrollBarControl(resourceId_);
      return;
    }
  }
  // No Control Manager control: outline the band so the layout stays legible
  // instead of leaving a hole, the same fallback the button arm uses.
  LOKA_PAINT_REFUSE(ToolboxPaintRefusals(this->controller()), TOOLBOX_PAINT_REFUSAL_NATIVE_ACQUISITION);
  FrameRect(&rect_);
}

void ToolboxScrollBarContext::render(loka::app::scene::IPlatformController *controller)
{
  ToolboxScenePlatformController *toolbox = static_cast<ToolboxScenePlatformController *>(controller);
  draw(toolbox);
}

bool RegisterToolboxScrollBarNodeHandler(loka::app::scene::PlatformNodeHandlerRegistry &registry)
{
  return registry.registerHandler(&gToolboxScrollBarNodeHandler);
}
