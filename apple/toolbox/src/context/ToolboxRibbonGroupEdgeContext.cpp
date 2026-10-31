#include "context/ToolboxRibbonGroupEdgeContext.hpp"
#include "ToolboxScenePlatformController.hpp"
#include "ToolboxPaintGradient.hpp"
#include "ToolboxPaintGround.hpp"
#include "context/ToolboxPaintSupport.hpp"
#include "app/scene/projection/RetainedNodeHandler.hpp"

namespace
{
  class ToolboxRibbonGroupEdgeNodeHandler
      : public loka::app::scene::RetainedNodeHandler<ToolboxRibbonGroupEdgeNodeHandler,
                                                     loka::app::RibbonGroupEdgeNode,
                                                     ToolboxRibbonGroupEdgeContext>
  {
  public:
    static loka::app::RibbonGroupEdgeNode *cast(loka::app::scene::Node *node)
    {
      return node ? node->asRibbonGroupEdgeNode() : 0;
    }
    static ToolboxRibbonGroupEdgeContext *create(loka::app::RibbonGroupEdgeNode *node,
                                                 loka::app::scene::IPlatformController *controller,
                                                 const loka::app::scene::LayoutState &)
    {
      return new ToolboxRibbonGroupEdgeContext(node, static_cast<ToolboxScenePlatformController *>(controller));
    }
  };
  ToolboxRibbonGroupEdgeNodeHandler gToolboxRibbonGroupEdgeNodeHandler;
} // namespace

ToolboxRibbonGroupEdgeContext::ToolboxRibbonGroupEdgeContext(loka::app::RibbonGroupEdgeNode *node,
                                                             ToolboxScenePlatformController *controller)
    : ToolboxProjectedNodeContext(controller),
      node_(node),
      rect_(),
      paintRect_()
{
}

void ToolboxRibbonGroupEdgeContext::readLifecycleFactOnAttach()
{
  if (this->controller())
    this->controller()->registerCompositionReplay(this->replay_);
}

void ToolboxRibbonGroupEdgeContext::onFactChanged(loka::app::scene::NodeLifecycleFact previous,
                                                  loka::app::scene::NodeLifecycleFact next)
{
  if (next == loka::app::scene::NODE_FACT_ATTACHED)
    this->readLifecycleFactOnAttach();
  else
  {
    this->replay_.clear();
    SetRect(&this->paintRect_, 0, 0, 0, 0);
  }
  ToolboxProjectedNodeContext::onFactChanged(previous, next);
}

short ToolboxRibbonGroupEdgeContext::layout(loka::app::scene::IPlatformController *,
                                            loka::app::scene::LayoutState &state)
{
  SetRect(&this->rect_, state.x, state.y, state.x + state.width, state.y + state.height);
  this->paintRect_ = this->rect_;
  if (this->controller() && !this->controller()->intersectWithProjectionClip(this->rect_, this->paintRect_))
    SetRect(&this->paintRect_, 0, 0, 0, 0);
  state.y += state.height;
  return state.width;
}

loka::app::scene::PaintAnswer
ToolboxRibbonGroupEdgeContext::queryPaintDamage(const loka::app::scene::PaintQuery &query) const
{
  using namespace loka::app::scene;
  if (query.placement != PLACEMENT_ELIGIBLE || query.scope != ToolboxPaintScope()
      || this->deliveredFact() != NODE_FACT_ATTACHED)
    return PaintAnswer::refused(PAINT_REFUSED_PLACEMENT_UNSETTLED);
  return ToolboxExactPaint(this->paintRect_, false);
}

void ToolboxRibbonGroupEdgeContext::render(loka::app::scene::IPlatformController *)
{
  ToolboxPaintClip clip(this->paintRect_);
  if (!clip.isActive())
    LOKA_PAINT_REFUSE(ToolboxPaintRefusals(this->controller()), TOOLBOX_PAINT_REFUSAL_CLIP_ALLOCATION);
  if (EmptyRect(&this->paintRect_) || (clip.isActive() && !clip.touches(this->paintRect_)))
    return;
  loka::toolbox::ToolboxApplyWindowGround();
  loka::toolbox::ToolboxTextInkScope ink(blackColor);
  static const unsigned char side[] = {8, 6, 4, 2};
  static const unsigned char bottom[] = {8};
  switch (this->node_->props.edge)
  {
  case loka::app::RIBBON_GROUP_EDGE_LEFT:
  case loka::app::RIBBON_GROUP_EDGE_RIGHT:
    loka::toolbox::ToolboxPaintGradient(this->rect_, loka::toolbox::BOTTOM_TO_TOP, side, 4);
    break;
  case loka::app::RIBBON_GROUP_EDGE_BOTTOM:
    loka::toolbox::ToolboxPaintGradient(this->rect_, loka::toolbox::BOTTOM_TO_TOP, bottom, 1);
    break;
  }
}

bool RegisterToolboxRibbonGroupEdgeNodeHandler(loka::app::scene::PlatformNodeHandlerRegistry &registry)
{
  return registry.registerHandler(&gToolboxRibbonGroupEdgeNodeHandler);
}
