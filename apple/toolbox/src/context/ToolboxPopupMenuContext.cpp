#include "platform/ToolboxPascalText.hpp"
#include "app/layout/ControlWidth.hpp"
#include "ToolboxPropsRefresh.hpp"
#include "context/ToolboxPaintSupport.hpp"
#include "context/ToolboxPopupMenuContext.hpp"
#include "ToolboxScenePlatformController.hpp"
#include "ToolboxLayoutMetrics.hpp"
#include "app/scene/projection/RetainedNodeHandler.hpp"
#include "platform/StringUTF8.hpp"
#include <cstring>
#include <string>

namespace
{
  class ToolboxPopupMenuNodeHandler
      : public loka::app::scene::RetainedNodeHandler<ToolboxPopupMenuNodeHandler,
                                                     loka::app::PopupMenuNode,
                                                     ToolboxPopupMenuContext>
  {
  public:
    static loka::app::PopupMenuNode *cast(loka::app::scene::Node *node)
    {
      return node ? node->asPopupMenuNode() : 0;
    }

    static ToolboxPopupMenuContext *create(loka::app::PopupMenuNode *node,
                                           loka::app::scene::IPlatformController *controller,
                                           const loka::app::scene::LayoutState &state)
    {
      (void)state;
      return new ToolboxPopupMenuContext(node, static_cast<ToolboxScenePlatformController *>(controller));
    }
  };

  ToolboxPopupMenuNodeHandler gToolboxPopupMenuNodeHandler;

  bool DrawStringAt(short x, short y, const loka::core::String &value)
  {
    Str255 text;
    if (!ToolboxEncodePascal(value, text))
      return false;
    MoveTo(x, y);
    DrawString(text);
    return true;
  }
} // namespace

ToolboxPopupMenuContext::ToolboxPopupMenuContext(loka::app::PopupMenuNode *node,
                                                 ToolboxScenePlatformController *controller)
    : ToolboxProjectedNodeContext(controller),
      node_(node),
      rect_(),
      paintRect_(),
      lineHeight_(0),
      items_(0),
      selectedIndex_(0),
      onChange_(0),
      enabled_(0)
{
}

ToolboxPopupMenuContext::~ToolboxPopupMenuContext() {}

loka::app::scene::PaintAnswer ToolboxPopupMenuContext::queryPaintDamage(
    const loka::app::scene::PaintQuery &query) const
{
  using namespace loka::app::scene;
  if (query.placement != PLACEMENT_ELIGIBLE || query.scope != ToolboxPaintScope())
    return PaintAnswer::refused(PAINT_REFUSED_PLACEMENT_UNSETTLED);
  if (!this->node_ || this->items_ != this->node_->props.items_
      || this->selectedIndex_ != this->node_->props.selectedIndex_.state()
      || this->enabled_ != this->node_->props.enabled_)
    return PaintAnswer::refused(PAINT_REFUSED_PROPS_UNRECONCILED);
  if (ToolboxPaintIsClippedOut(this->rect_, this->paintRect_, this->deliveredFact()))
    return ToolboxExactPaint(this->paintRect_, false);
  if (EmptyRect(&this->paintRect_))
    return PaintAnswer::refused(PAINT_REFUSED_PLACEMENT_UNSETTLED);
  if (!this->presented_.isKnown())
    return PaintAnswer::refused(PAINT_REFUSED_HISTORY_UNKNOWN);
  return ToolboxExactPaint(this->paintRect_, !this->faceValue().equals(this->presented_.value()));
}

void ToolboxPopupMenuContext::onFactChanged(loka::app::scene::NodeLifecycleFact previous,
                                            loka::app::scene::NodeLifecycleFact next)
{
  if (next != loka::app::scene::NODE_FACT_ATTACHED)
  {
    this->presented_.invalidate();
    SetRect(&this->paintRect_, 0, 0, 0, 0);
  }
  ToolboxProjectedNodeContext::onFactChanged(previous, next);
}

void ToolboxPopupMenuContext::updateData(const loka::Vector<loka::core::String> *items,
                                         loka::core::State<int> *selectedIndex,
                                         const loka::app::scene::WriteSeat<int> &selectedIndexSeat,
                                         loka::core::EmitterState *onChange,
                                         loka::core::State<bool> *enabled)
{
  items_ = items;
  selectedIndex_ = selectedIndex;
  selectedIndexSeat_ = selectedIndexSeat;
  onChange_ = onChange;
  enabled_ = enabled;
}

void ToolboxPopupMenuContext::updateRect(const Rect &rect, short lineHeight)
{
  const Rect previousRect = this->rect_;
  const Rect previousPaintRect = this->paintRect_;

  this->rect_ = rect;
  // The gray shadow occupies the right/bottom pixels outside the face's
  // half-open geometry. Only the paint bounds include that extra pixel.
  Rect paintBounds = rect;
  ++paintBounds.right;
  ++paintBounds.bottom;
  this->paintRect_ = paintBounds;
  if (this->controller() && !this->controller()->intersectWithProjectionClip(paintBounds, this->paintRect_))
    SetRect(&this->paintRect_, 0, 0, 0, 0);
  if (!EqualRect(&previousRect, &this->rect_) || !EqualRect(&previousPaintRect, &this->paintRect_) || this->lineHeight_ != lineHeight)
    this->presented_.invalidate();
  lineHeight_ = lineHeight;
}

short ToolboxPopupMenuContext::clampIndex(int index) const
{
  if (!items_ || items_->size() == 0)
  {
    return 0;
  }
  if (index < 0)
  {
    return 0;
  }
  if (static_cast<std::size_t>(index) >= items_->size())
  {
    return static_cast<short>(items_->size() - 1);
  }
  return static_cast<short>(index);
}

void ToolboxPopupMenuContext::draw()
{
  ToolboxPaintClip clip(this->paintRect_);
  if (!clip.isActive())
    LOKA_PAINT_REFUSE(ToolboxPaintRefusals(this->controller()), TOOLBOX_PAINT_REFUSAL_CLIP_ALLOCATION);
  this->paintFace(clip);
}

void ToolboxPopupMenuContext::repaint()
{
  ToolboxPaintClip clip(this->paintRect_);
  if (!clip.isActive())
    LOKA_PAINT_REFUSE(ToolboxPaintRefusals(this->controller()), TOOLBOX_PAINT_REFUSAL_CLIP_ALLOCATION);
  if (clip.isActive() && !clip.touches(this->paintRect_))
    return;
  // Erase under whichever clip is in force: the intersected one, or the
  // caller's when region allocation was refused, so a shorter label does
  // not leave the previous face's glyphs behind.
  EraseRect(&this->paintRect_);
  this->paintFace(clip);
}

ToolboxPopupMenuContext::FaceValue ToolboxPopupMenuContext::faceValue() const
{
  const int selectedIndex = this->selectedIndex_ ? this->selectedIndex_->get() : 0;
  const loka::core::String label = this->items_ && this->items_->size() > 0
      ? (*this->items_)[this->clampIndex(selectedIndex)] : loka::core::String::Literal("Select");
  return FaceValue(label, selectedIndex, !this->enabled_ || this->enabled_->get());
}

void ToolboxPopupMenuContext::paintFace(const ToolboxPaintClip &clip)
{
  // Mirrors Text/EditText: disjoint replay preserves the completed fact;
  // allocation refusal still draws under the caller's clip, without a commit.
  if (clip.isActive() && !clip.touches(this->paintRect_))
    return;
  if (!this->node_)
  {
    LOKA_PAINT_REFUSE(ToolboxPaintRefusals(this->controller()), TOOLBOX_PAINT_REFUSAL_OBLIGATION_UNMET);
    this->presented_.invalidate();
    return;
  }
  const FaceValue face = this->faceValue();
  const bool completes = ToolboxPaintCompletes(clip, this->paintRect_, this->presented_,
      this->presented_.isKnown() && face.equals(this->presented_.value()));
  this->presented_.invalidate();
  PenState penState;
  GetPenState(&penState);
  FrameRect(&rect_);
  PenPat(&qd.gray);
  MoveTo(rect_.left + 2, rect_.bottom);
  LineTo(rect_.right, rect_.bottom);
  LineTo(rect_.right, rect_.top + 2);
  SetPenState(&penState);
  short textY = static_cast<short>(rect_.top + lineHeight_ - ToolboxLayoutMetrics::kControlAscentInset);
  const bool labelDrawn = DrawStringAt(static_cast<short>(rect_.left + 4), textY, face.label());
  short arrowRight = static_cast<short>(rect_.right - 4);
  short arrowTop = static_cast<short>(rect_.top + 4);
  short arrowBottom = static_cast<short>(rect_.bottom - 4);
  short arrowMidY = static_cast<short>((arrowTop + arrowBottom) / 2);
  MoveTo(static_cast<short>(arrowRight - 6), arrowMidY - 3);
  LineTo(arrowRight, arrowMidY - 3);
  LineTo(static_cast<short>(arrowRight - 3), arrowMidY + 3);
  LineTo(static_cast<short>(arrowRight - 6), arrowMidY - 3);
  if (!labelDrawn)
    LOKA_PAINT_REFUSE(ToolboxPaintRefusals(this->controller()), TOOLBOX_PAINT_REFUSAL_TEXT_CONVERSION);
  if (labelDrawn && completes)
    this->presented_.commit(face, ToolboxPaintScope());
}

short ToolboxPopupMenuContext::layout(loka::app::scene::IPlatformController *controller,
                                      loka::app::scene::LayoutState &state)
{
  if (!node_)
  {
    return 0;
  }
  const short width = static_cast<short>(loka::app::layout::offeredOrNaturalWidth(state.width, 120));
  Rect rect;
  rect.left = state.x;
  rect.top = state.y;
  // Preserve the legacy outer inset only for an unconstrained control.
  rect.right = static_cast<short>(state.x
      + loka::app::layout::offeredOrNaturalWidth(state.width, 120 + 8));
  rect.bottom = static_cast<short>(state.y + state.lineHeight - ToolboxLayoutMetrics::kControlAscentInset
                                   + ToolboxLayoutMetrics::kControlDescent);
  this->captureProps();
  updateRect(rect, state.lineHeight);
  ToolboxScenePlatformController *toolbox = static_cast<ToolboxScenePlatformController *>(controller);
  if (toolbox)
  {
    toolbox->recordPopupHit(this->rect_, this->enabled_, this);
  }
  // Advance by the painted box, as the other rails do: y is the top edge.
  state.y = static_cast<short>(rect.bottom);
  return width;
}

#include "ToolboxPopupMenuInput.cpp"

void ToolboxPopupMenuContext::render(loka::app::scene::IPlatformController *controller)
{
  if (controller)
    static_cast<ToolboxScenePlatformController *>(controller)->reachPopupHit(this);
  draw();
}

short ToolboxPopupMenuContext::menuId() const
{
  if (node_ && node_->props.controlTag_ > 0 && node_->props.controlTag_ <= 32767)
  {
    return static_cast<short>(node_->props.controlTag_);
  }
  return 2000;
}

bool RegisterToolboxPopupMenuNodeHandler(loka::app::scene::PlatformNodeHandlerRegistry &registry)
{
  return registry.registerHandler(&gToolboxPopupMenuNodeHandler);
}

bool ToolboxPopupMenuContext::captureProps()
{
  if (!this->node_)
    return false;
  const bool changed = ToolboxPopupProjectionChanged(
      this->items_, this->node_->props.items_,
      this->selectedIndex_, this->node_->props.selectedIndex_.state(),
      this->enabled_, this->node_->props.enabled_);
  this->updateData(this->node_->props.items_,
                   this->node_->props.selectedIndex_.state(), this->node_->props.selectedIndex_,
                   this->node_->props.onChange_,
                   this->node_->props.enabled_);
  return changed;
}

void ToolboxPopupMenuContext::onPropsApplied()
{
  const bool changed = this->captureProps();
  if (changed && this->controller() && this->node_)
  {
    this->controller()->refreshContextProps(this->node_);
  }
}
