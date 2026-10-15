#include "platform/null/context/NullAttributedTextContext.hpp"

#include <climits>
#include "support/NullLayoutRefusal.hpp"
#include "app/scene/projection/RetainedNodeHandler.hpp"
#include "platform/null/NullScenePlatformController.hpp"

namespace
{
  int extent(int value)
  {
    return value > SHRT_MAX ? SHRT_MAX : value;
  }

  NullTextMeasurement emptyMeasurement()
  {
    return NullTextMeasurement(0, 12, 1);
  }

  NullTextMeasurement measure(const loka::app::AttributedString &value,
                              const loka::app::BlockStyle &block,
                              const loka::app::scene::LayoutState &state,
                              bool &materialized,
                              const NullScenePlatformController &controller)
  {
    if (RefuseNullTextMeasurement())
    {
      materialized = false;
      return emptyMeasurement();
    }
    const loka::app::SyntheticTextWidthSource synthetic(value);
    const loka::app::TextWidthSource *source = &synthetic;
    {
      switch (controller.textShaping())
      {
      case loka::app::PER_RUN:
        source = &controller.textWidthSource(loka::app::PER_RUN, synthetic);
        break;
      case loka::app::WHOLE_LINE:
        source = &controller.textWidthSource(loka::app::WHOLE_LINE, synthetic);
        break;
      }
    }
    const loka::app::TextLineBreaker result(*source, block, state.width);
    materialized = result.valid();
    if (!materialized)
      return emptyMeasurement();
    return MeasureNullTextLines(result, block, state.width);
  }

  bool fits(const loka::app::AttributedString &value,
            const loka::app::BlockStyle &block,
            const loka::core::Frame &seat,
            const NullScenePlatformController &controller)
  {
    loka::app::scene::LayoutState state;
    state.width = static_cast<short>(seat.width);
    bool materialized = false;
    const NullTextMeasurement output = measure(value, block, state, materialized, controller);
    return materialized && output.width() <= seat.width && output.height() <= seat.height;
  }

  class NullAttributedTextNodeHandler : public loka::app::scene::RetainedNodeHandler<NullAttributedTextNodeHandler,
                                                                                     loka::app::AttributedTextNode,
                                                                                     NullAttributedTextContext>
  {
  public:
    static loka::app::AttributedTextNode *cast(loka::app::scene::Node *node)
    {
      return node ? node->asAttributedTextNode() : 0;
    }
    static NullAttributedTextContext *create(loka::app::AttributedTextNode *node,
                                             loka::app::scene::IPlatformController *controller,
                                             const loka::app::scene::LayoutState &)
    {
      return new NullAttributedTextContext(node, *static_cast<NullScenePlatformController *>(controller));
    }
  };
  NullAttributedTextNodeHandler handler;
} // namespace

NullAttributedTextContext::NullAttributedTextContext(loka::app::AttributedTextNode *node,
                                                     NullScenePlatformController &controller)
    : measurementBuilds_(0),
      node_(node),
      controller_(controller)
{
}

short NullAttributedTextContext::layout(loka::app::scene::IPlatformController *controller,
                                        loka::app::scene::LayoutState &state)
{
  bool materialized = true;
  if (state.inputs != loka::app::scene::NODE_DIRTY_NONE || !this->measurement_.reusable(state.width))
  {
    ++this->measurementBuilds_;
    const loka::app::AttributedString empty;
    if (loka::testing::declineNullTextMeasurement())
    {
      materialized = false;
      this->measurement_.invalidate();
    }
    else
      this->measurement_ = measure(this->node_->props.text_ ? this->node_->props.text_->get() : empty,
                                 this->node_->props.blockStyle_,
                                 state,
                                 materialized,
                                 this->controller_);
  }
  if (!materialized && this->node_ && controller)
    controller->refuseTextMeasurement(this->node_, state);
  state.height = this->measurement_.height();
  this->invalidatePresentation();
  loka::app::scene::PaintScope scope;
  if (materialized && controller
      && static_cast<NullScenePlatformController *>(controller)->queryPaintProjectionScope(scope))
  {
    this->placement_.complete(loka::core::Frame(state.x, state.y, state.width, state.height), scope);
    this->placedBlock_ = this->node_->props.blockStyle_;
  }
  const int bottom = state.y + state.height;
  return static_cast<short>(bottom < SHRT_MIN ? SHRT_MIN : extent(bottom));
}

void NullAttributedTextContext::invalidatePresentation()
{
  this->placement_.invalidate();
  this->presented_.invalidate();
}

void NullAttributedTextContext::onFactChanged(loka::app::scene::NodeLifecycleFact,
                                              loka::app::scene::NodeLifecycleFact next)
{
  if (next == loka::app::scene::NODE_FACT_RETIRED)
    this->measurement_ = NullTextMeasurement();
  if (next != loka::app::scene::NODE_FACT_ATTACHED)
    this->invalidatePresentation();
}

loka::app::scene::PaintAnswer
NullAttributedTextContext::queryPaintDamage(const loka::app::scene::PaintQuery &query) const
{
  using namespace loka::app::scene;
  if (!this->node_->props.text_ || !this->node_->props.text_->get().valid())
    return PaintAnswer::refused(PAINT_REFUSED_PROPS_UNRECONCILED);
  loka::core::Frame seat;
  if (query.placement != PLACEMENT_ELIGIBLE || !this->placement_.query(query.scope, seat)
      || !(this->placedBlock_ == this->node_->props.blockStyle_))
    return PaintAnswer::refused(PAINT_REFUSED_PLACEMENT_UNSETTLED);
  if (!this->presented_.isKnown())
    return PaintAnswer::refused(PAINT_REFUSED_HISTORY_UNKNOWN);
  if (this->presented_.scope() != query.scope)
    return PaintAnswer::refused(PAINT_REFUSED_PLACEMENT_UNSETTLED);
  const loka::app::AttributedString &current = this->node_->props.text_->get();
  PaintDamage damage = {query.scope, seat.x, seat.y, 0, 0, PAINT_COVERAGE_ERASE_AND_PAINT};
  if (current == this->presented_.value())
    return PaintAnswer::exact(damage);
  if (!fits(current, this->node_->props.blockStyle_, seat, this->controller_))
    return PaintAnswer::refused(PAINT_REFUSED_PLACEMENT_UNSETTLED);
  damage.width = seat.width;
  damage.height = seat.height;
  return PaintAnswer::exact(damage);
}

bool NullAttributedTextContext::commitPresented(const loka::app::AttributedString &value,
                                                const loka::app::scene::PaintScope &scope)
{
  loka::core::Frame seat;
  if (!this->placement_.query(scope, seat) || !fits(value, this->node_->props.blockStyle_, seat, this->controller_))
  {
    this->presented_.invalidate();
    return false;
  }
  this->placedBlock_ = this->node_->props.blockStyle_;
  this->presented_.commit(value, scope);
  return true;
}

void RegisterNullAttributedTextNodeHandler(NullScenePlatformController &controller)
{
  controller.registerNodeHandler(&handler);
}
const void *NullAttributedTextNodeHandlerKey()
{
  return handler.nodeTypeKey();
}
bool IsNullAttributedTextNodeHandler(const loka::app::scene::IPlatformNodeHandler *candidate)
{
  return candidate == &handler;
}

unsigned loka::app::testing::NullTextMeasurementAccess::builds(const NullAttributedTextContext &context)
{
  return context.measurementBuilds_;
}
