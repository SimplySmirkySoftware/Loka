#include "platform/null/context/NullRectSurfaceContext.hpp"

#include "app/RectSurface.hpp"
#include "app/scene/projection/RetainedNodeHandler.hpp"
#include "platform/null/NullScenePlatformController.hpp"

namespace
{
  class NullRectSurfaceNodeHandler : public loka::app::scene::RetainedNodeHandler<NullRectSurfaceNodeHandler,
                                                                                  loka::app::RectSurfaceNode,
                                                                                  NullRectSurfaceContext>
  {
  public:
    static loka::app::RectSurfaceNode *cast(loka::app::scene::Node *node)
    {
      return node ? node->asRectSurfaceNode() : 0;
    }

    static NullRectSurfaceContext *create(loka::app::RectSurfaceNode *surface,
                                          loka::app::scene::IPlatformController *controller,
                                          const loka::app::scene::LayoutState &state)
    {
      (void)state;
      return new NullRectSurfaceContext(surface, static_cast<NullScenePlatformController *>(controller));
    }
  };

  NullRectSurfaceNodeHandler gNullRectSurfaceNodeHandler;
} // namespace

NullRectSurfaceContext::NullRectSurfaceContext(loka::app::RectSurfaceNode *node,
                                               NullScenePlatformController *controller)
    : loka::app::scene::NativeNodeContext(),
      node_(node),
      controller_(controller)
{
}

NullRectSurfaceContext::~NullRectSurfaceContext()
{
  // No controller access here: a test may destroy the controller before the
  // nodes that own their contexts. Cancellation rides the lifecycle facts.
  this->node_ = 0;
  this->controller_ = 0;
}

void NullRectSurfaceContext::onFactChanged(loka::app::scene::NodeLifecycleFact previous,
                                           loka::app::scene::NodeLifecycleFact next)
{
  (void)previous;
  // Detached or retired, the surface is no longer placed: its pending seat
  // rows go first.
  if (next != loka::app::scene::NODE_FACT_ATTACHED)
  {
    this->presented_.invalidate();
    this->placement_.invalidate();
  }
  if (next != loka::app::scene::NODE_FACT_ATTACHED && this->controller_)
  {
    this->controller_->cancelRectSurfaceExtent(this->node_);
  }
}

void NullRectSurfaceContext::readLifecycleFactOnAttach()
{
  // Presentation is completed by the synchronous Null presenter.
}

short NullRectSurfaceContext::layout(loka::app::scene::IPlatformController *, loka::app::scene::LayoutState &state)
{
  if (!this->node_)
  {
    return state.y;
  }
  const short height = this->node_->props.height_ > 0 ? this->node_->props.height_ : state.height;
  const short width = this->node_->props.width_ > 0 ? this->node_->props.width_ : state.width;
  loka::app::scene::PaintScope scope;
  this->placement_.invalidate();
  this->presented_.invalidate();
  if (this->controller_ && this->controller_->queryPaintProjectionScope(scope))
    this->placement_.complete(loka::core::Frame(state.x, state.y, width, height), scope);
  return static_cast<short>(state.y + height);
}

void RegisterNullRectSurfaceNodeHandler(NullScenePlatformController &controller)
{
  controller.registerNodeHandler(&gNullRectSurfaceNodeHandler);
}

namespace
{
  bool equalSprites(const loka::app::RectSurfaceModel &a, const loka::app::RectSurfaceModel &b)
  {
    const short count = loka::app::RectSurfaceModel::clampRectCount(a.rectCount);
    if (count != loka::app::RectSurfaceModel::clampRectCount(b.rectCount))
      return false;
    for (short i = 0; i < count; ++i)
      if (a.rects[i] != b.rects[i])
        return false;
    return true;
  }
} // namespace
loka::app::scene::PaintAnswer NullRectSurfaceContext::queryPaintDamage(const loka::app::scene::PaintQuery &query) const
{
  using namespace loka::app::scene;
  loka::core::Frame seat;
  if (query.placement != PLACEMENT_ELIGIBLE || !this->placement_.query(query.scope, seat))
    return PaintAnswer::refused(PAINT_REFUSED_PLACEMENT_UNSETTLED);
  if (!this->presented_.isKnown())
    return PaintAnswer::refused(PAINT_REFUSED_HISTORY_UNKNOWN);
  if (this->presented_.scope() != query.scope)
    return PaintAnswer::refused(PAINT_REFUSED_PLACEMENT_UNSETTLED);
  if (!this->node_ || !this->node_->props.model_)
    return PaintAnswer::refused(PAINT_REFUSED_PROPS_UNRECONCILED);
  const loka::app::RectSurfaceModel &current = this->node_->props.model_->get();
  PaintDamage damage = {
      query.scope, seat.x, seat.y, 0, 0, PAINT_COVERAGE_PAINT_ONLY};
  if (!equalSprites(current, this->presented_.value()))
  {
    int left = seat.width, top = seat.height, right = 0, bottom = 0;
    for (int pass = 0; pass < 2; ++pass)
    {
      const loka::app::RectSurfaceModel &model = pass ? current : this->presented_.value();
      for (short i = 0; i < loka::app::RectSurfaceModel::clampRectCount(model.rectCount); ++i)
      {
        const loka::app::RectSprite &r = model.rects[i];
        if (r.width <= 0 || r.height <= 0)
          continue;
        if (r.x < left)
          left = r.x;
        if (r.y < top)
          top = r.y;
        if (r.x + r.width > right)
          right = r.x + r.width;
        if (r.y + r.height > bottom)
          bottom = r.y + r.height;
      }
    }
    if (left < 0)
      left = 0;
    if (top < 0)
      top = 0;
    if (right > seat.width)
      right = seat.width;
    if (bottom > seat.height)
      bottom = seat.height;
    if (right > left && bottom > top)
    {
      damage.x += left;
      damage.y += top;
      damage.width = right - left;
      damage.height = bottom - top;
    }
  }
  return PaintAnswer::exact(damage);
}
bool NullRectSurfaceContext::commitPresented(const loka::app::RectSurfaceModel &value,
                                             const loka::app::scene::PaintScope &scope)
{
  loka::core::Frame seat;
  if (!this->placement_.query(scope, seat))
    return false;
  this->presented_.commit(value, scope);
  return true;
}

const void *NullRectSurfaceNodeHandlerKey()
{
  return gNullRectSurfaceNodeHandler.nodeTypeKey();
}
bool IsNullRectSurfaceNodeHandler(const loka::app::scene::IPlatformNodeHandler *handler)
{
  return handler == &gNullRectSurfaceNodeHandler;
}
