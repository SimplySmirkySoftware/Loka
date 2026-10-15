#include "platform/ToolboxPascalText.hpp"
#include "app/layout/ControlWidth.hpp"
#include "ToolboxPropsRefresh.hpp"
#include "context/ToolboxButtonContext.hpp"
#include "ToolboxScenePlatformController.hpp"
#include "ToolboxLayoutMetrics.hpp"
#include "context/ToolboxLayoutUtil.hpp"
#include "app/scene/projection/RetainedNodeHandler.hpp"
#include "platform/StringUTF8.hpp"
#include <cstring>
#include <string>

namespace
{
  class ToolboxButtonNodeHandler
      : public loka::app::scene::RetainedNodeHandler<ToolboxButtonNodeHandler,
                                                     loka::app::ButtonNode,
                                                     ToolboxButtonContext>
  {
  public:
    static loka::app::ButtonNode *cast(loka::app::scene::Node *node)
    {
      return node ? node->asButtonNode() : 0;
    }

    static ToolboxButtonContext *create(loka::app::ButtonNode *node,
                                        loka::app::scene::IPlatformController *controller,
                                        const loka::app::scene::LayoutState &state)
    {
      (void)state;
      return new ToolboxButtonContext(node, static_cast<ToolboxScenePlatformController *>(controller));
    }
  };

  ToolboxButtonNodeHandler gToolboxButtonNodeHandler;

  void DrawStringAt(short x, short y, const loka::core::String &value)
  {
    Str255 text;
    if (!ToolboxEncodePascal(value, text))
    {
      return;
    }
    MoveTo(x, y);
    DrawString(text);
  }
} // namespace

ToolboxButtonContext::ToolboxButtonContext(loka::app::ButtonNode *node, ToolboxScenePlatformController *controller)
    : ToolboxProjectedNodeContext(controller),
      node_(node),
      rect_(),
      paintRect_(),
      widthFromText_(false),
      label_(loka::core::String::Literal("Button")),
      emitter_(0),
      enabled_(0),
      resourceId_(0)
{
}

ToolboxButtonContext::~ToolboxButtonContext() {}

loka::app::scene::PaintAnswer ToolboxButtonContext::queryPaintDamage(const loka::app::scene::PaintQuery &query) const
{
  using namespace loka::app::scene;
  if (query.placement != PLACEMENT_ELIGIBLE || query.scope != ToolboxPaintScope())
    return PaintAnswer::refused(PAINT_REFUSED_PLACEMENT_UNSETTLED);
  if (!this->node_)
    return PaintAnswer::refused(PAINT_REFUSED_HISTORY_UNKNOWN);
  const ToolboxButtonPaintValue current(
      this->node_->props.text_ ? this->node_->props.text_->get() : loka::core::String::Literal("Button"),
      !this->node_->props.enabled_ || this->node_->props.enabled_->get());
  if (ToolboxPaintIsClippedOut(this->rect_, this->paintRect_, this->deliveredFact()))
  {
    // No presented value is required offscreen, but the laid-out title width
    // still constrains placement. A changed width can move visible siblings.
    if (this->widthFromText_ && (!this->controller()
        || this->naturalWidth(current.label()) != this->rect_.right - this->rect_.left))
      return PaintAnswer::refused(PAINT_REFUSED_PLACEMENT_UNSETTLED);
    return ToolboxExactPaint(this->paintRect_, false);
  }
  if (!this->presented_.isKnown())
    return PaintAnswer::refused(PAINT_REFUSED_HISTORY_UNKNOWN);
  // Only a text-sized control can move its Row siblings when the title
  // changes. A container-sized control retains its laid-out rectangle.
  if (this->widthFromText_ && (!this->controller()
      || this->naturalWidth(current.label()) != this->rect_.right - this->rect_.left))
    return PaintAnswer::refused(PAINT_REFUSED_PLACEMENT_UNSETTLED);
  return ToolboxExactPaint(this->paintRect_, !(current == this->presented_.value()));
}

void ToolboxButtonContext::onFactChanged(loka::app::scene::NodeLifecycleFact previous,
                                        loka::app::scene::NodeLifecycleFact next)
{
  if (next != loka::app::scene::NODE_FACT_ATTACHED)
    this->presented_.invalidate();
  ToolboxProjectedNodeContext::onFactChanged(previous, next);
}

void ToolboxButtonContext::retireNativeProjection()
{
  if (this->controller())
  {
    // The terminal fact delivery just refreshed the hint snapshot, so the
    // retire flush decides on the freshest value — not the last render's.
    this->controller()->destroyButtonControl(resourceId_, this->lifetimeHint());
  }
  this->node_ = 0;
}

void ToolboxButtonContext::updateData(const loka::core::String &label,
                                      loka::core::EmitterState *emitter,
                                      loka::core::State<bool> *enabled,
                                      short resourceId,
                                      int controlTag)
{
  label_ = label;
  emitter_ = emitter;
  enabled_ = enabled;
  if (resourceId > 0)
  {
    resourceId_ = resourceId;
  }
  else if (controlTag > 0 && controlTag <= 32767)
  {
    resourceId_ = static_cast<short>(controlTag);
  }
}

void ToolboxButtonContext::updateRect(const Rect &rect)
{
  Rect paintRect = rect;
  if (this->controller() && !this->controller()->intersectWithProjectionClip(rect, paintRect))
    SetRect(&paintRect, 0, 0, 0, 0);
  if (!EqualRect(&this->rect_, &rect) || !EqualRect(&this->paintRect_, &paintRect))
    this->presented_.invalidate();
  this->rect_ = rect;
  this->paintRect_ = paintRect;
}

void ToolboxButtonContext::draw(ToolboxScenePlatformController *controller)
{
  ToolboxPaintClip clip(this->paintRect_);
  const bool paints = !clip.isActive() || clip.touches(this->paintRect_);
  const ToolboxButtonPaintValue current(this->label_, !this->enabled_ || this->enabled_->get());
  const bool completes = ToolboxPaintCompletes(clip, this->paintRect_, this->presented_,
      this->presented_.isKnown() && current == this->presented_.value());
  if (paints)
    this->presented_.invalidate();
  // ensureButtonControl also marks the native control used by this render.
  // Keep that registration even when its pixels are outside the caller's clip.
  if (controller && resourceId_ <= 0)
  {
    resourceId_ = controller->allocateControlId();
  }
  if (controller && resourceId_ > 0)
  {
    if (controller->ensureButtonControl(resourceId_, rect_, label_, emitter_, enabled_, lifetimeHint(), this))
    {
      if (paints && completes)
        this->presented_.commit(current, ToolboxPaintScope());
      return;
    }
  }
  if (paints)
  {
    FrameRect(&rect_);
    DrawStringAt(static_cast<short>(rect_.left + 4), static_cast<short>(rect_.bottom - ToolboxLayoutMetrics::kControlDescent), label_);
  }
  if (controller)
  {
    controller->recordButtonHit(rect_, emitter_, enabled_, boundary_, this);
  }
}

bool ReconcileToolboxButtonControl(ControlRef control, const loka::core::String &label,
                                   loka::core::State<bool> *enabled, std::string &installedLabel)
{
  if (!control)
    return false;
  std::string labelUtf8;
  if (!loka::platform::CollectUtf8(label, labelUtf8))
    return false;
  if (installedLabel != labelUtf8)
  {
    Str255 title;
    if (!ToolboxEncodePascal(label, title))
      return false;
    SetControlTitle(control, title);
    installedLabel = labelUtf8;
  }
  HiliteControl(control, enabled && !enabled->get() ? 255 : 0);
  return true;
}

void ToolboxButtonContext::repaint(ControlRef control, std::string &installedLabel)
{
  ToolboxPaintClip clip(this->paintRect_);
  if (clip.isActive() && !clip.touches(this->paintRect_))
    return;
  if (!this->node_ || !control)
  {
    this->presented_.invalidate();
    return;
  }
  const loka::core::String label = this->node_->props.text_
      ? this->node_->props.text_->get() : loka::core::String::Literal("Button");
  const ToolboxButtonPaintValue current(label, !this->node_->props.enabled_ || this->node_->props.enabled_->get());
  const bool completes = ToolboxPaintCompletes(clip, this->paintRect_, this->presented_,
      this->presented_.isKnown() && current == this->presented_.value());
  this->presented_.invalidate();
  const bool submitted = ReconcileToolboxButtonControl(control, label, this->node_->props.enabled_, installedLabel);
  Draw1Control(control);
  if (submitted && completes)
    this->presented_.commit(current, ToolboxPaintScope());
}

void ToolboxButtonContext::forgetPresentedControl()
{
  this->presented_.invalidate();
}

short ToolboxButtonContext::layout(loka::app::scene::IPlatformController *controller,
                                   loka::app::scene::LayoutState &state)
{
  (void)controller;
  if (!node_)
  {
    return 0;
  }
  this->captureProps();
  this->widthFromText_ = state.width <= 0;
  const short width = static_cast<short>(loka::app::layout::offeredOrNaturalWidth(
      state.width, this->widthFromText_ ? this->naturalWidth(this->label_) : 0));
  Rect rect;
  rect.left = state.x;
  rect.top = state.y;
  rect.right = static_cast<short>(state.x + width);
  rect.bottom = static_cast<short>(state.y + state.lineHeight - ToolboxLayoutMetrics::kControlAscentInset
                                   + ToolboxLayoutMetrics::kControlDescent);
  updateRect(rect);
  // Advance by the painted box, as the other rails do: y is the top edge.
  state.y = static_cast<short>(rect.bottom);
  return width;
}

short ToolboxButtonContext::naturalWidth(const loka::core::String &label) const
{
  if (!this->controller())
    return 0;
  return this->controller()->measurePushButtonNaturalWidth(label);
}

void ToolboxButtonContext::render(loka::app::scene::IPlatformController *controller)
{
  ToolboxScenePlatformController *toolbox = static_cast<ToolboxScenePlatformController *>(controller);
  draw(toolbox);
}

#include "ToolboxButtonInput.cpp"

bool RegisterToolboxButtonNodeHandler(loka::app::scene::PlatformNodeHandlerRegistry &registry)
{
  return registry.registerHandler(&gToolboxButtonNodeHandler);
}

bool ToolboxButtonContext::captureProps()
{
  if (!this->node_)
    return false;
  const loka::core::String label =
      this->node_->props.text_ ? this->node_->props.text_->get() : loka::core::String::Literal("Button");
  const bool changed = ToolboxButtonProjectionChanged(
      this->label_, label, this->enabled_, this->node_->props.enabled_,
      this->emitter_, this->node_->props.onClick_);
  this->updateData(label,
                   this->node_->props.onClick_,
                   this->node_->props.enabled_,
                   this->resourceId_,
                   this->node_->props.controlTag_);
  return changed;
}

void ToolboxButtonContext::onPropsApplied()
{
  this->presented_.invalidate();
  const bool changed = this->captureProps();
  if (changed && this->controller() && this->node_)
  {
    this->controller()->refreshContextProps(this->node_, this->resourceId_);
  }
}
