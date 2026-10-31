#include "ToolboxPaintGround.hpp"
#include "platform/ToolboxPascalText.hpp"
#include "app/layout/ControlWidth.hpp"
#include "ToolboxPropsRefresh.hpp"
#include "context/ToolboxPaintSupport.hpp"
#include "context/ToolboxCellContext.hpp"
#include "ToolboxScenePlatformController.hpp"
#include "context/ToolboxLayoutUtil.hpp"
#include "app/scene/projection/RetainedNodeHandler.hpp"
#include "platform/StringUTF8.hpp"
#include <cstring>
#include <string>

namespace
{
  class ToolboxCellNodeHandler
      : public loka::app::scene::RetainedNodeHandler<ToolboxCellNodeHandler,
                                                     loka::app::CellNode,
                                                     ToolboxCellContext>
  {
  public:
    static loka::app::CellNode *cast(loka::app::scene::Node *node)
    {
      return node ? node->asCellNode() : 0;
    }

    static ToolboxCellContext *create(loka::app::CellNode *node,
                                      loka::app::scene::IPlatformController *controller,
                                      const loka::app::scene::LayoutState &state)
    {
      (void)state;
      return new ToolboxCellContext(node, static_cast<ToolboxScenePlatformController *>(controller));
    }
  };

  ToolboxCellNodeHandler gToolboxCellNodeHandler;

} // namespace

ToolboxCellContext::ToolboxCellContext(loka::app::CellNode *node, ToolboxScenePlatformController *controller)
    : ToolboxProjectedNodeContext(controller),
      node_(node),
      rect_(),
      paintRect_(),
      widthFromText_(false),
      text_(0)
{
}

ToolboxCellContext::~ToolboxCellContext() {}

void ToolboxCellContext::onFactChanged(loka::app::scene::NodeLifecycleFact previous,
                                       loka::app::scene::NodeLifecycleFact next)
{
  if (next != loka::app::scene::NODE_FACT_ATTACHED)
  {
    this->presented_.invalidate();
    SetRect(&this->paintRect_, 0, 0, 0, 0);
  }
  ToolboxProjectedNodeContext::onFactChanged(previous, next);
}

void ToolboxCellContext::updateData(loka::core::State<loka::core::String> *text)
{
  text_ = text;
}

loka::app::scene::PaintAnswer ToolboxCellContext::queryPaintDamage(const loka::app::scene::PaintQuery &query) const
{
  using namespace loka::app::scene;
  if (query.placement != PLACEMENT_ELIGIBLE || query.scope != ToolboxPaintScope())
    return PaintAnswer::refused(PAINT_REFUSED_PLACEMENT_UNSETTLED);
  if (!this->node_ || this->node_->props.text_ != this->text_)
    return PaintAnswer::refused(PAINT_REFUSED_PROPS_UNRECONCILED);
  const loka::core::String current = this->text_ ? this->text_->get() : loka::core::String();
  // Like Button, a text-sized Cell can move Row siblings. Grid/fixed-width
  // Cells keep their placement even when the new text measures differently.
  if (this->widthFromText_ && (!this->controller()
      || this->controller()->measureTextWidth(current) != this->rect_.right - this->rect_.left))
    return PaintAnswer::refused(PAINT_REFUSED_PLACEMENT_UNSETTLED);
  if (ToolboxPaintIsClippedOut(this->rect_, this->paintRect_, this->deliveredFact()))
    return ToolboxExactPaint(this->paintRect_, false);
  if (!this->presented_.isKnown())
    return PaintAnswer::refused(PAINT_REFUSED_HISTORY_UNKNOWN);
  return ToolboxExactPaint(this->paintRect_, !current.equals(this->presented_.value()));
}

void ToolboxCellContext::updateRect(const Rect &rect)
{
  Rect paintRect = rect;
  if (this->controller() && !this->controller()->intersectWithProjectionClip(rect, paintRect))
    SetRect(&paintRect, 0, 0, 0, 0);
  if (!EqualRect(&this->rect_, &rect) || !EqualRect(&this->paintRect_, &paintRect))
    this->presented_.invalidate();
  this->rect_ = rect;
  this->paintRect_ = paintRect;
}

void ToolboxCellContext::draw(ToolboxScenePlatformController *controller)
{
  (void)controller;
  ToolboxPaintClip clip(this->paintRect_);
  if (!clip.isActive())
    LOKA_PAINT_REFUSE(ToolboxPaintRefusals(this->controller()), TOOLBOX_PAINT_REFUSAL_CLIP_ALLOCATION);
  // Full/clipped render and dirty replay share the #763 history rule.
  if (clip.isActive() && !clip.touches(this->paintRect_))
    return;
  const loka::core::String current = this->text_ ? this->text_->get() : loka::core::String();
  const bool completes = ToolboxPaintCompletes(clip, this->paintRect_, this->presented_,
      this->presented_.isKnown() && current.equals(this->presented_.value()));
  this->presented_.invalidate();
  Rect drawRect = this->rect_;
  loka::toolbox::ToolboxPaintGround(
      loka::app::GroundForKind(loka::app::scene::NODE_KIND_CELL), drawRect);
  FrameRect(&drawRect);
  Str255 text;
  if (!ToolboxEncodePascal(current, text))
  {
    LOKA_PAINT_REFUSE(ToolboxPaintRefusals(this->controller()), TOOLBOX_PAINT_REFUSAL_TEXT_CONVERSION);
    return;
  }
  if (text[0] != 0)
  {
    short textWidth = StringWidth(text);
    FontInfo info;
    GetFontInfo(&info);
    short textHeight = static_cast<short>(info.ascent + info.descent);
    short rectWidth = static_cast<short>(drawRect.right - drawRect.left);
    short rectHeight = static_cast<short>(drawRect.bottom - drawRect.top);
    short textX = static_cast<short>(drawRect.left + (rectWidth - textWidth) / 2);
    short textY = static_cast<short>(drawRect.top + (rectHeight - textHeight) / 2 + info.ascent);
    MoveTo(textX, textY);
    loka::toolbox::ToolboxTextInkScope ink(loka::toolbox::ToolboxTextRoleColor(loka::app::SURFACE_GROUND_CONTROL));
    DrawString(text);
  }
  if (completes)
    this->presented_.commit(current, ToolboxPaintScope());
}

short ToolboxCellContext::layout(loka::app::scene::IPlatformController *controller,
                                 loka::app::scene::LayoutState &state)
{
  if (!node_)
  {
    return 0;
  }
  this->widthFromText_ = state.width <= 0;
  short naturalWidth = state.width;
  if (this->widthFromText_ && node_->props.text_)
  {
    ToolboxScenePlatformController *toolbox =
        static_cast<ToolboxScenePlatformController *>(controller);
    naturalWidth = toolbox ? toolbox->measureTextWidth(node_->props.text_->get()) : 0;
  }
  const short width = static_cast<short>(
      loka::app::layout::offeredOrNaturalWidth(state.width, naturalWidth));
  short height = state.height;
  if (height <= 0)
  {
    height = static_cast<short>(state.lineHeight + 6);
  }
  Rect rect;
  rect.left = state.x;
  rect.top = static_cast<short>(state.y);
  rect.right = static_cast<short>(state.x + width);
  rect.bottom = static_cast<short>(state.y + height);
  this->captureProps();
  updateRect(rect);
  state.y = static_cast<short>(state.y + height);
  return width;
}

void ToolboxCellContext::render(loka::app::scene::IPlatformController *controller)
{
  ToolboxScenePlatformController *toolbox = static_cast<ToolboxScenePlatformController *>(controller);
  // Registration happens here, on the tree-walk render, and only here. The
  // dirty replay re-invokes draw() on already-registered entries; a painter
  // that also registers would grow the very vector the replay iterates
  // (unbounded redraw + a dangling reference on reallocation).
  if (toolbox)
  {
    toolbox->recordCellHit(rect_, node_ ? node_->props.onClick_ : 0, boundary_, this, text_);
  }
  draw(toolbox);
}

#include "ToolboxCellInput.cpp"

bool RegisterToolboxCellNodeHandler(loka::app::scene::PlatformNodeHandlerRegistry &registry)
{
  return registry.registerHandler(&gToolboxCellNodeHandler);
}

bool ToolboxCellContext::captureProps()
{
  loka::core::State<loka::core::String> *text = this->node_ ? this->node_->props.text_ : 0;
  const bool changed = ToolboxTextProjectionChanged(this->text_, text);
  this->updateData(text);
  return changed;
}

void ToolboxCellContext::onPropsApplied()
{
  const bool changed = this->captureProps();
  if (changed && this->controller() && this->node_)
  {
    this->controller()->refreshContextProps(this->node_);
  }
}
