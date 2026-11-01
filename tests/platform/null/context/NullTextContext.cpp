#include "platform/null/context/NullTextContext.hpp"

#include <climits>
#include "support/NullLayoutRefusal.hpp"

#include "app/nodes/Text.hpp"
#include "app/scene/projection/RetainedNodeHandler.hpp"

namespace
{
  const int kDefaultFontSize = 12;
  short ClampCoordinateToShort(int value)
  {
    if (value < SHRT_MIN)
    {
      return SHRT_MIN;
    }
    if (value > SHRT_MAX)
    {
      return SHRT_MAX;
    }
    return static_cast<short>(value);
  }

  bool MeasureText(const loka::app::TextNode *node,
                   const loka::app::scene::LayoutState &state,
                   NullTextMeasurement &out,
                   const loka::core::String *rendered = 0)
  {
    const loka::core::String value =
        node && node->props.text_ ? (rendered ? *rendered : node->props.text_->get()) : loka::core::String();
    return MeasureNullText(node ? node->props.resolvedTextStyle() : loka::app::TextStyle(),
                           node ? node->props.blockStyle_ : loka::app::BlockStyle(),
                           node && node->props.text_ ? &value : 0,
                           state,
                           out);
  }

  bool FitsTextSeat(const loka::app::TextNode *node,
                    const loka::core::String &value,
                    const loka::core::Frame &seat,
                    const NullTextMeasurement &placed)
  {
    loka::app::scene::LayoutState measure;
    measure.width = static_cast<short>(seat.width);
    measure.lineHeight = placed.lineCount() > 0 ? placed.height() / placed.lineCount() : 0;
    NullTextMeasurement output;
    const bool materialized = MeasureText(node, measure, output, &value);
    return materialized && output.width() <= seat.width && output.height() <= seat.height;
  }

  class NullTextNodeHandler
      : public loka::app::scene::RetainedNodeHandler<NullTextNodeHandler,
                                                     loka::app::TextNode,
                                                     NullTextContext>
  {
  public:
    static loka::app::TextNode *cast(loka::app::scene::Node *node)
    {
      return node ? node->asTextNode() : 0;
    }

    static NullTextContext *create(loka::app::TextNode *text,
                                   loka::app::scene::IPlatformController *controller,
                                   const loka::app::scene::LayoutState &state)
    {
      (void)state;
      NullScenePlatformController *nullPlatform = static_cast<NullScenePlatformController *>(controller);
      (void)nullPlatform;
      return new NullTextContext(text);
    }
  };

  NullTextNodeHandler gNullTextNodeHandler;
} // namespace

NullTextContext::NullTextContext(loka::app::TextNode *node)
    : loka::app::scene::NativeNodeContext(),
      presented_(),
      placement_(),
      placedStyle_(),
      measurementBuilds_(0),
      node_(node),
      measurement_()
{
}

NullTextContext::~NullTextContext()
{
  this->node_ = 0;
}

void NullTextContext::readLifecycleFactOnAttach()
{
  // Presentation is completed by the synchronous Null presenter.
}

short NullTextContext::layout(loka::app::scene::IPlatformController *controller, loka::app::scene::LayoutState &state)
{
  bool materialized = true;
  if (state.inputs != loka::app::scene::NODE_DIRTY_NONE || !this->measurement_.reusable(state.width))
  {
    ++this->measurementBuilds_;
    if (loka::testing::declineNullTextMeasurement())
    {
      materialized = false;
      this->measurement_.invalidate();
    }
    else
      materialized = MeasureText(this->node_, state, this->measurement_);
  }
  if (!materialized && this->node_ && controller)
    controller->refuseTextMeasurement(this->node_, state);
  state.height = this->measurement_.height();
  this->placement_.invalidate();
  this->presented_.invalidate();
  loka::app::scene::PaintScope scope;
  // An unrenderable value has no placement: the next query refuses instead of
  // comparing against a seat that was measured from nothing.
  if (materialized && this->node_ && controller
      && static_cast<NullScenePlatformController *>(controller)->queryPaintProjectionScope(scope))
  {
    this->placement_.complete(loka::core::Frame(state.x, state.y, state.width, state.height), scope);
    this->placedStyle_ = NullTextPaintStyle(this->node_->props);
  }
  return ClampCoordinateToShort(state.y + state.height);
}

const NullTextMeasurement &NullTextContext::measurement() const
{
  return this->measurement_;
}

void RegisterNullTextNodeHandler(NullScenePlatformController &controller)
{
  controller.registerNodeHandler(&gNullTextNodeHandler);
}

NullTextPaintStyle::NullTextPaintStyle(const loka::app::TextProps &props)
    : fontSize(kDefaultFontSize),
      weight(loka::app::TEXT_WEIGHT_NORMAL),
      color(loka::app::TEXT_COLOR_UNSET),
      italic(false),
      underline(loka::app::TEXT_TOGGLE_OFF),
      wrap(loka::app::TEXT_WRAP_NONE),
      truncation(loka::app::TEXT_TRUNCATION_NONE),
      align(loka::app::TEXT_ALIGN_LEFT)
{
  const loka::app::TextStyle textStyle = props.resolvedTextStyle();
  this->color = textStyle.color_;
  this->family = textStyle.family_;
  this->fontSize = loka::app::SyntheticTextSize(textStyle.size_);
  loka::app::TextWeight declaredWeight = loka::app::TEXT_WEIGHT_NORMAL;
  if (textStyle.queryWeight(declaredWeight))
    weight = declaredWeight;
  bool on = false;
  if (textStyle.queryItalic(on))
    italic = on;
  on = false;
  textStyle.queryUnderline(on);
  this->underline = loka::app::TextToggleValue(on ? loka::app::TEXT_TOGGLE_ON : loka::app::TEXT_TOGGLE_OFF);
  if (props.blockStyle_.hasWrap_)
    wrap = props.blockStyle_.wrap_;
  if (props.blockStyle_.hasTruncation_)
    truncation = props.blockStyle_.truncation_;
  if (props.blockStyle_.hasAlign_)
    align = props.blockStyle_.align_;
}
void NullTextContext::onFactChanged(loka::app::scene::NodeLifecycleFact, loka::app::scene::NodeLifecycleFact next)
{
  if (next == loka::app::scene::NODE_FACT_RETIRED)
    this->measurement_ = NullTextMeasurement();
  if (next != loka::app::scene::NODE_FACT_ATTACHED)
  {
    this->presented_.invalidate();
    this->placement_.invalidate();
  }
}
loka::app::scene::PaintAnswer NullTextContext::queryPaintDamage(const loka::app::scene::PaintQuery &query) const
{
  using namespace loka::app::scene;
  loka::core::Frame seat;
  if (query.placement != PLACEMENT_ELIGIBLE || !this->placement_.query(query.scope, seat))
    return PaintAnswer::refused(PAINT_REFUSED_PLACEMENT_UNSETTLED);
  if (!this->presented_.isKnown())
    return PaintAnswer::refused(PAINT_REFUSED_HISTORY_UNKNOWN);
  if (this->presented_.scope() != query.scope || !(this->placedStyle_ == NullTextPaintStyle(this->node_->props)))
    return PaintAnswer::refused(PAINT_REFUSED_PLACEMENT_UNSETTLED);
  if (!this->node_->props.text_)
    return PaintAnswer::refused(PAINT_REFUSED_PROPS_UNRECONCILED);
  const loka::core::String &current = this->node_->props.text_->get();
  PaintDamage damage = {query.scope, seat.x, seat.y, 0, 0, PAINT_COVERAGE_ERASE_AND_PAINT};
  loka::core::StringCompareResult equal = current.compare(this->presented_.value(), false);
  if (equal == loka::core::StringCompareBufferRequired)
    equal = current.compare(this->presented_.value(), true);
  if (equal == loka::core::StringCompareBufferRequired)
  {
    // The current value cannot be materialized, so it cannot be measured or
    // placed; this is a placement refusal, not a stale binding.
    return PaintAnswer::refused(PAINT_REFUSED_PLACEMENT_UNSETTLED);
  }
  if (equal == loka::core::StringCompareEqual)
    return PaintAnswer::exact(damage);
  if (!FitsTextSeat(this->node_, current, seat, this->measurement_))
    return PaintAnswer::refused(PAINT_REFUSED_PLACEMENT_UNSETTLED);
  damage.width = seat.width;
  damage.height = seat.height;
  return PaintAnswer::exact(damage);
}
bool NullTextContext::commitPresented(const loka::core::String &value, const loka::app::scene::PaintScope &scope)
{
  loka::core::Frame seat;
  if (!this->placement_.query(scope, seat))
    return false;
  const NullTextPaintStyle current(this->node_->props);
  const bool restyled = !(this->placedStyle_ == current);
  if (restyled)
  {
    // A props-only apply can change the resolved style without a layout pass.
    // The widened presentation reconstructs the whole seat under the current
    // style, so adopt it here and verify coverage below; refusing forever would
    // leave the history UNKNOWN until an unrelated layout.
    this->presented_.invalidate();
    this->placedStyle_ = current;
  }
  if (restyled || !this->presented_.isKnown()
      || value.compare(this->presented_.value(), false) != loka::core::StringCompareEqual)
  {
    if (!FitsTextSeat(this->node_, value, seat, this->measurement_))
    {
      this->presented_.invalidate();
      return false;
    }
  }
  this->presented_.commit(value, scope);
  return true;
}

const void *NullTextNodeHandlerKey()
{
  return gNullTextNodeHandler.nodeTypeKey();
}
bool IsNullTextNodeHandler(const loka::app::scene::IPlatformNodeHandler *handler)
{
  return handler == &gNullTextNodeHandler;
}

unsigned loka::app::testing::NullTextMeasurementAccess::builds(const NullTextContext &context)
{
  return context.measurementBuilds_;
}
