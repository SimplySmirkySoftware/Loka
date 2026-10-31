#include "context/ToolboxAttributedTextContext.hpp"
#include "ToolboxScenePlatformController.hpp"
#include "app/scene/projection/RetainedNodeHandler.hpp"
#include <climits>

namespace
{
  class ToolboxAttributedTextNodeHandler
      : public loka::app::scene::RetainedNodeHandler<ToolboxAttributedTextNodeHandler,
                                                     loka::app::AttributedTextNode,
                                                     ToolboxAttributedTextContext>
  {
  public:
    static loka::app::AttributedTextNode *cast(loka::app::scene::Node *node)
    {
      return node ? node->asAttributedTextNode() : 0;
    }
    static ToolboxAttributedTextContext *create(loka::app::AttributedTextNode *node,
                                                loka::app::scene::IPlatformController *controller,
                                                const loka::app::scene::LayoutState &)
    {
      return new ToolboxAttributedTextContext(node, static_cast<ToolboxScenePlatformController *>(controller));
    }
  };
  ToolboxAttributedTextNodeHandler handler;
  short Coordinate(int value)
  {
    return static_cast<short>(value > SHRT_MAX ? SHRT_MAX : value < SHRT_MIN ? SHRT_MIN : value);
  }
} // namespace

ToolboxAttributedTextContext::ToolboxAttributedTextContext(loka::app::AttributedTextNode *node,
                                                           ToolboxScenePlatformController *controller)
    : ToolboxProjectedNodeContext(controller),
      node_(node),
      rect_(),
      paintRect_()
{
}

ToolboxAttributedTextContext::~ToolboxAttributedTextContext()
{
  assert(!this->table_.valid() && "retirement must drop derived text before reclaim");
}

void ToolboxAttributedTextContext::readLifecycleFactOnAttach()
{
  if (this->controller())
    this->controller()->registerCompositionReplay(this->replay_);
}

void ToolboxAttributedTextContext::retireNativeProjection()
{
  this->replay_.clear();
  this->table_.clear();
}

void ToolboxAttributedTextContext::onPropsApplied()
{
  this->table_.invalidateGeometry();
}

void ToolboxAttributedTextContext::onFactChanged(loka::app::scene::NodeLifecycleFact previous,
                                                 loka::app::scene::NodeLifecycleFact next)
{
  if (next != loka::app::scene::NODE_FACT_ATTACHED)
  {
    this->presented_.invalidate();
    SetRect(&this->paintRect_, 0, 0, 0, 0);
  }
  ToolboxProjectedNodeContext::onFactChanged(previous, next);
}

short ToolboxAttributedTextContext::placedWidth(short constraint) const
{
  return constraint > 0 ? constraint : this->table_.width();
}

bool ToolboxAttributedTextContext::reconcileProjection(short width, bool rebuildGeometry)
{
  if (!this->controller() || !this->node_ || !this->node_->props.text_)
  {
    this->table_.clear();
    this->presented_.invalidate();
    return false;
  }
  const loka::app::AttributedString &value = this->node_->props.text_->get();
  if (!rebuildGeometry && this->table_.valid() && this->table_.matches(value))
    return true;
  if (this->table_.build(value, this->node_->props.blockStyle_, width, *this->controller()))
  {
    // A rebuild under a logically equal value can still change pixels (font
    // metrics, wrapping, or segment boundaries splitting malformed bytes), so
    // equal-value history becomes unknown on every path that rebuilds.
    if (this->presented_.isKnown() && value == this->presented_.value())
      this->presented_.invalidate();
    return true;
  }
  this->presented_.invalidate();
  return false;
}

short ToolboxAttributedTextContext::layout(loka::app::scene::IPlatformController *controller,
                                           loka::app::scene::LayoutState &state)
{
  ToolboxScenePlatformController *toolbox = static_cast<ToolboxScenePlatformController *>(controller);
  if (!toolbox || !this->node_ || !this->node_->props.text_)
  {
    this->table_.clear();
    this->presented_.invalidate();
    return 0;
  }
  assert(toolbox->textShaping() == loka::app::PER_RUN);
  const loka::app::AttributedString &value = this->node_->props.text_->get();
  // The builder reads width, not lineHeight. Ambient fonts follow the frozen
  // environment contract on ToolboxTextMeasureScope.
  const bool reused = state.inputs == loka::app::scene::NODE_DIRTY_NONE && this->table_.reusable(state.width)
      && this->table_.valid() && this->table_.matches(value);
  if (!this->reconcileProjection(state.width, !reused))
  {
    LOKA_PAINT_REFUSE(ToolboxPaintRefusals(this->controller()), TOOLBOX_PAINT_REFUSAL_OBLIGATION_UNMET);
    controller->refuseTextMeasurement(this->node_, state);
    this->presented_.invalidate();
    return 0;
  }
  const short width = this->placedWidth(state.width);
  Rect rect;
  rect.left = state.x;
  rect.top = state.y;
  rect.right = Coordinate(state.x + width);
  rect.bottom = Coordinate(state.y + this->table_.height());
  Rect paintRect = rect;
  if (!toolbox->intersectWithProjectionClip(rect, paintRect))
    SetRect(&paintRect, 0, 0, 0, 0);
  // Keep the previous logical paint value on source changes: exact damage
  // compares it with the new projection. reconcileProjection already made
  // equal-value rebuilds unknown; a moved placement does the same here.
  if (!EqualRect(&this->rect_, &rect) || !EqualRect(&this->paintRect_, &paintRect))
    this->presented_.invalidate();
  this->rect_ = rect;
  this->paintRect_ = paintRect;
  state.y = Coordinate(this->rect_.bottom);
  return width;
}

loka::app::scene::PaintAnswer
ToolboxAttributedTextContext::queryPaintDamage(const loka::app::scene::PaintQuery &query) const
{
  using namespace loka::app::scene;
  if (query.placement != PLACEMENT_ELIGIBLE || query.scope != ToolboxPaintScope())
    return PaintAnswer::refused(PAINT_REFUSED_PLACEMENT_UNSETTLED);
  if (!this->table_.valid() || !this->node_ || !this->node_->props.text_
      || !this->table_.matches(this->node_->props.text_->get()))
    return PaintAnswer::refused(PAINT_REFUSED_PROPS_UNRECONCILED);
  if (ToolboxPaintIsClippedOut(this->rect_, this->paintRect_, this->deliveredFact()))
    return ToolboxExactPaint(this->paintRect_, false);
  if (!this->presented_.isKnown())
    return PaintAnswer::refused(PAINT_REFUSED_HISTORY_UNKNOWN);
  PaintAnswer answer = ToolboxExactPaint(this->paintRect_, !(this->table_.value() == this->presented_.value()));
  answer.damage.coverage = PAINT_COVERAGE_ERASE_AND_PAINT;
  return answer;
}

void ToolboxAttributedTextContext::render(loka::app::scene::IPlatformController *)
{
  // A paint-only rebuild keeps the last layout constraint (an intrinsic 0
  // stays unbounded) instead of the placement width derived from it.
  const short placementWidth = static_cast<short>(this->rect_.right - this->rect_.left);
  short constraint = placementWidth;
  this->table_.queryConstraint(constraint);
  if (!this->reconcileProjection(constraint, false))
  {
    LOKA_PAINT_REFUSE(ToolboxPaintRefusals(this->controller()), TOOLBOX_PAINT_REFUSAL_OBLIGATION_UNMET);
    return;
  }
  // Geometry layout would place differently waits for layout: paint what
  // fits, but never certify the value as presented.
  const bool fits = this->placedWidth(constraint) == placementWidth
      && this->table_.height() == this->rect_.bottom - this->rect_.top;
  // Layout already intersected the placement with the projection clip: an
  // empty paint rect owes no pixels, so leave before switching the port or
  // allocating clip regions (the resident-Column scroll cost, S1 lane).
  if (EmptyRect(&this->paintRect_))
  {
    this->presented_.invalidate();
    return;
  }
  ToolboxTextMeasureScope port(*this->controller());
  ToolboxPaintClip clip(this->paintRect_);
  if (!clip.isActive())
    LOKA_PAINT_REFUSE(ToolboxPaintRefusals(this->controller()), TOOLBOX_PAINT_REFUSAL_CLIP_ALLOCATION);
  // A missed replay changes no pixels and must preserve the previous history.
  if (clip.isActive() && !clip.touches(this->paintRect_))
    return;
  const bool completes = ToolboxPaintCompletes(clip, this->paintRect_, this->presented_,
      this->presented_.isKnown() && this->table_.value() == this->presented_.value());
  this->presented_.invalidate();
  // Classic low-memory fallback (same as Text): an inactive clip keeps the
  // caller's clip and still draws; covers() stays false, so history stays
  // unknown and the next pass repaints in full.
  const bool painted = this->table_.draw(this->rect_.left,
                                         this->rect_.top,
                                         *this->controller(),
                                         this->node_->props.blockStyle_,
                                         this->rect_.right - this->rect_.left);
  if (!fits)
    LOKA_PAINT_REFUSE(ToolboxPaintRefusals(this->controller()), TOOLBOX_PAINT_REFUSAL_OBLIGATION_UNMET);
  if (!painted)
    LOKA_PAINT_REFUSE(ToolboxPaintRefusals(this->controller()), TOOLBOX_PAINT_REFUSAL_PARTIAL_DRAW);
  if (painted && completes && fits)
    this->presented_.commit(this->table_.value(), ToolboxPaintScope());
}

bool RegisterToolboxAttributedTextNodeHandler(loka::app::scene::PlatformNodeHandlerRegistry &registry)
{
  return registry.registerHandler(&handler);
}

void *ToolboxAttributedTextContext::operator new(std::size_t size) throw()
{
  return loka::core::LokaAllocRaw(size, loka::core::LokaAllocationSite("ToolboxAttributedText", "Context"));
}
void ToolboxAttributedTextContext::operator delete(void *storage) throw()
{
  loka::core::LokaFreeRaw(storage, loka::core::LokaAllocationSite("ToolboxAttributedText", "Context"));
}
