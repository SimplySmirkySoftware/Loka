#include "app/layout/ControlWidth.hpp"
#include "ToolboxPropsRefresh.hpp"
#include "context/ToolboxEditTextContext.hpp"
#include "context/ToolboxPaintSupport.hpp"
#include "ToolboxScenePlatformController.hpp"
#include "ToolboxLayoutMetrics.hpp"
#include "app/scene/projection/RetainedNodeHandler.hpp"
#include "platform/ToolboxPascalText.hpp"
#include "platform/String.hpp"
#include "core/StringAccess.hpp"
#include <cstring>
#include <string>

namespace
{
  class ToolboxEditTextNodeHandler
      : public loka::app::scene::RetainedNodeHandler<ToolboxEditTextNodeHandler,
                                                     loka::app::EditTextNode,
                                                     ToolboxEditTextContext>
  {
  public:
    static loka::app::EditTextNode *cast(loka::app::scene::Node *node)
    {
      return node ? node->asEditTextNode() : 0;
    }

    static ToolboxEditTextContext *create(loka::app::EditTextNode *node,
                                          loka::app::scene::IPlatformController *controller,
                                          const loka::app::scene::LayoutState &state)
    {
      (void)state;
      return new ToolboxEditTextContext(node, static_cast<ToolboxScenePlatformController *>(controller));
    }
  };

  ToolboxEditTextNodeHandler gToolboxEditTextNodeHandler;

  void DrawStringAt(short x, short y, const loka::core::String &value)
  {
    Str255 text;
    if (!ToolboxEncodePascal(value, text)) return;
    MoveTo(x, y);
    DrawString(text);
  }
} // namespace

ToolboxEditTextContext::ToolboxEditTextContext(loka::app::EditTextNode *node,
                                               ToolboxScenePlatformController *controller)
    : ToolboxProjectedNodeContext(controller),
      node_(node),
      rect_(),
      paintRect_(),
      textRect_(),
      textX_(0),
      textY_(0),
      text_(0)
{
}

ToolboxEditTextContext::~ToolboxEditTextContext() {}

loka::app::scene::PaintAnswer ToolboxEditTextContext::queryPaintDamage(
    const loka::app::scene::PaintQuery &query) const
{
  using namespace loka::app::scene;
  if (query.placement != PLACEMENT_ELIGIBLE || query.scope != ToolboxPaintScope())
    return PaintAnswer::refused(PAINT_REFUSED_PLACEMENT_UNSETTLED);
  if (!this->node_ || !this->text_ || this->node_->props.text_.state() != this->text_)
    return PaintAnswer::refused(PAINT_REFUSED_PROPS_UNRECONCILED);
  if (ToolboxPaintIsClippedOut(this->rect_, this->paintRect_, this->deliveredFact()))
    return ToolboxExactPaint(this->paintRect_, false);
  if (EmptyRect(&this->paintRect_))
    return PaintAnswer::refused(PAINT_REFUSED_PLACEMENT_UNSETTLED);
  // Only repaint of an installed TE can establish this fact. Native retirement
  // revokes it, including retirement without a logical detach.
  if (!this->presented_.isKnown())
    return PaintAnswer::refused(PAINT_REFUSED_HISTORY_UNKNOWN);
  return ToolboxExactPaint(this->paintRect_, !this->text_->get().equals(this->presented_.value()));
}

void ToolboxEditTextContext::onFactChanged(loka::app::scene::NodeLifecycleFact previous,
                                          loka::app::scene::NodeLifecycleFact next)
{
  if (next != loka::app::scene::NODE_FACT_ATTACHED)
  {
    this->presented_.invalidate();
    SetRect(&this->paintRect_, 0, 0, 0, 0);
  }
  ToolboxProjectedNodeContext::onFactChanged(previous, next);
}

void ToolboxEditTextContext::updateData(loka::core::State<loka::core::String> *text)
{
  text_ = text;
}

void ToolboxEditTextContext::updateRect(const Rect &outerRect, const Rect &textRect, short textX, short textY)
{
  const Rect previousRect = this->rect_;
  const Rect previousPaintRect = this->paintRect_;

  rect_ = outerRect;
  this->paintRect_ = outerRect;
  if (this->controller() && !this->controller()->intersectWithProjectionClip(outerRect, this->paintRect_))
    SetRect(&this->paintRect_, 0, 0, 0, 0);
  if (!EqualRect(&previousRect, &this->rect_) || !EqualRect(&previousPaintRect, &this->paintRect_) || !EqualRect(&this->textRect_, &textRect)
      || this->textX_ != textX || this->textY_ != textY)
    this->presented_.invalidate();
  textRect_ = textRect;
  textX_ = textX;
  textY_ = textY;
}

void ToolboxEditTextContext::repaint(const ToolboxEditPresentation &presentation)
{
  TEHandle te = presentation.te;
  ToolboxPaintClip clip(this->paintRect_);
  if (clip.isActive() && !clip.touches(this->paintRect_))
    return;
  const bool completes = ToolboxPaintCompletes(clip, this->paintRect_, this->presented_,
      this->presented_.isKnown() && this->text_ && this->text_->get().equals(this->presented_.value()));
  this->presented_.invalidate();
  if (!te || !*te || !this->text_)
    return;
  const Rect view = (**te).viewRect;
  TEUpdate(&view, te);
  FrameRect(&this->rect_);
  if (completes && presentation.installed.holds(this->text_->get()))
    this->presented_.commit(this->text_->get(), ToolboxPaintScope());
}

void ToolboxEditTextContext::draw(ToolboxScenePlatformController *controller)
{
  // The render walk rebuilds native usage even for disjoint painters.
  if (controller && this->text_)
  {
    const ToolboxEditPresentation presentation = controller->ensureEditTextControl(this, textRect_, text_, lifetimeHint());
    if (presentation.te)
    {
      this->repaint(presentation);
      return;
    }
  }
  ToolboxPaintClip clip(this->paintRect_);
  if (!clip.isActive() || clip.touches(this->paintRect_))
  {
    this->presented_.invalidate();
    FrameRect(&this->rect_);
    if (this->text_)
      DrawStringAt(this->textX_, this->textY_, this->text_->get());
  }
  if (controller && this->text_)
    controller->recordEditHit(rect_, text_, boundary_, this);
}

short ToolboxEditTextContext::layout(loka::app::scene::IPlatformController *controller,
                                     loka::app::scene::LayoutState &state)
{
  (void)controller;
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
      + loka::app::layout::offeredOrNaturalWidth(state.width, 120 + 3));
  rect.bottom = static_cast<short>(state.y + state.lineHeight - ToolboxLayoutMetrics::kControlAscentInset
                                   + ToolboxLayoutMetrics::kEditTextDescent);
  Rect textRect = rect;
  textRect.left = static_cast<short>(textRect.left + 1);
  textRect.top = static_cast<short>(textRect.top + 2);
  textRect.right = static_cast<short>(textRect.right - 1);
  textRect.bottom = static_cast<short>(textRect.bottom - 1);
  this->captureProps();
  updateRect(rect, textRect, static_cast<short>(state.x + 4),
             static_cast<short>(state.y + state.lineHeight - ToolboxLayoutMetrics::kControlAscentInset));
  // Advance by the painted box, as the other rails do: y is the top edge.
  state.y = static_cast<short>(rect.bottom);
  return width;
}

void ToolboxEditTextContext::render(loka::app::scene::IPlatformController *controller)
{
  ToolboxScenePlatformController *toolbox = static_cast<ToolboxScenePlatformController *>(controller);
  draw(toolbox);
}

bool RegisterToolboxEditTextNodeHandler(loka::app::scene::PlatformNodeHandlerRegistry &registry)
{
  return registry.registerHandler(&gToolboxEditTextNodeHandler);
}

bool ToolboxEditTextContext::captureProps()
{
  loka::core::State<loka::core::String> *text = this->node_ ? this->node_->props.text_.state() : 0;
  const bool changed = ToolboxTextProjectionChanged(this->text_, text);
  this->updateData(text);
  return changed;
}

void ToolboxEditTextContext::onPropsApplied()
{
  const bool changed = this->captureProps();
  if (changed && this->controller() && this->node_)
  {
    this->controller()->refreshContextProps(this->node_);
  }
}
