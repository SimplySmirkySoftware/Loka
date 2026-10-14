#include "ToolboxPaintGround.hpp"
#include "context/ToolboxRectSurfaceContext.hpp"
#include "ToolboxScenePlatformController.hpp"
#include "context/RectSurfaceRepaintPlan.hpp"

void EnsureToolboxRectSurfaceContext(loka::app::RectSurfaceNode *node, ToolboxScenePlatformController *controller)
{
  if (!node)
  {
    return;
  }
  ToolboxRectSurfaceContext *ctx = static_cast<ToolboxRectSurfaceContext *>(node->getContext());
  if (!ctx)
  {
    ctx = new ToolboxRectSurfaceContext(node, controller);
    node->setContext(ctx);
    if (ctx && controller)
    {
      controller->requestStructurePresent();
    }
  }
}

ToolboxRectSurfaceContext::ToolboxRectSurfaceContext(loka::app::RectSurfaceNode *node,
                                                     ToolboxScenePlatformController *controller)
    : ToolboxProjectedNodeContext(controller),
      node_(node),
      rect_(),
      paintRect_(),
      dirtyRgn_(NewRgn()),
      tempRgn_(NewRgn()),
      savedClipRgn_(NewRgn())
{
  SetRect(&rect_, 0, 0, 0, 0);
}

loka::app::scene::PaintAnswer ToolboxRectSurfaceContext::queryPaintDamage(
    const loka::app::scene::PaintQuery &query) const
{
  using namespace loka::app::scene;
  if (query.placement != PLACEMENT_ELIGIBLE || query.scope != ToolboxPaintScope())
    return PaintAnswer::refused(PAINT_REFUSED_PLACEMENT_UNSETTLED);
  if (!this->node_ || !this->node_->props.model_)
    return PaintAnswer::refused(PAINT_REFUSED_PROPS_UNRECONCILED);
  if (!this->presented_.isKnown())
    return PaintAnswer::refused(PAINT_REFUSED_HISTORY_UNKNOWN);
  return ToolboxExactPaint(this->paintRect_, this->presented_.value() != this->node_->props.model_->get());
}

void ToolboxRectSurfaceContext::onPropsApplied()
{
  this->presented_.invalidate();
}

void ToolboxRectSurfaceContext::onFactChanged(loka::app::scene::NodeLifecycleFact previous,
                                              loka::app::scene::NodeLifecycleFact next)
{
  // Detached or retired, the surface is no longer placed: its pending seat
  // rows go first, while the controller back-pointer is still intact (the
  // base clears it on RETIRED).
  if (next != loka::app::scene::NODE_FACT_ATTACHED)
  {
    this->presented_.invalidate();
    SetRect(&this->paintRect_, 0, 0, 0, 0);
    if (this->controller())
      this->controller()->cancelRectSurfaceExtent(node_);
  }
  ToolboxProjectedNodeContext::onFactChanged(previous, next);
}

ToolboxRectSurfaceContext::~ToolboxRectSurfaceContext()
{
  if (dirtyRgn_)
  {
    DisposeRgn(dirtyRgn_);
    dirtyRgn_ = 0;
  }
  if (tempRgn_)
  {
    DisposeRgn(tempRgn_);
    tempRgn_ = 0;
  }
  if (savedClipRgn_)
  {
    DisposeRgn(savedClipRgn_);
    savedClipRgn_ = 0;
  }
}

short ToolboxRectSurfaceContext::layout(loka::app::scene::IPlatformController *, loka::app::scene::LayoutState &state)
{
  const Rect previousRect = this->rect_;
  const Rect previousPaintRect = this->paintRect_;

  if (!node_)
  {
    return 0;
  }
  // LayoutNode validates both far edges and the spaced result before dispatch.
  rect_.left = state.x;
  rect_.top = static_cast<short>(state.y);
  rect_.right = static_cast<short>(state.x + state.width);
  rect_.bottom = static_cast<short>(state.y + state.height);
  this->paintRect_ = this->rect_;
  if (this->controller() && !this->controller()->intersectWithProjectionClip(this->rect_, this->paintRect_))
    SetRect(&this->paintRect_, 0, 0, 0, 0);
  if (!EqualRect(&previousRect, &this->rect_) || !EqualRect(&previousPaintRect, &this->paintRect_))
    this->presented_.invalidate();
  state.y = static_cast<short>(rect_.bottom);
  return state.width;
}

void ToolboxRectSurfaceContext::render(loka::app::scene::IPlatformController *)
{
  ToolboxPaintClip clip(this->paintRect_);
  // The #412 clipped full walk may exclude this surface entirely. Preserve
  // its last painted sprite positions so a pending dirty flush can erase them.
  if (clip.isActive() && !clip.touches(this->paintRect_))
    return;
  this->presented_.invalidate();
  if (!this->node_ || !this->node_->props.model_)
    return;
  loka::toolbox::ToolboxPaintGround(
      loka::app::GroundForKind(loka::app::scene::NODE_KIND_RECT_SURFACE), rect_);
  const loka::app::RectSurfaceModel model = node_->props.model_->get();
  for (short i = 0; i < model.rectCount; ++i)
  {
    Rect spriteRect = rectForSprite(model.rects[i]);
    PaintRect(&spriteRect);
  }
  if (clip.isActive() && clip.covers(this->paintRect_))
    this->presented_.commit(model, ToolboxPaintScope());
}

void ToolboxRectSurfaceContext::renderDirty(const Rect &requestedDirtyRect)
{
  if (!node_ || !node_->props.model_)
  {
    return;
  }
  Rect dirtyRect;
  if (!SectRect(&requestedDirtyRect, &this->paintRect_, &dirtyRect))
    return;

  const loka::app::RectSurfaceModel model = node_->props.model_->get();
  bool useRegionClip = false;
  useRegionClip =
      this->presented_.isKnown() && node_->props.useRegionClip_
      && buildDirtyRegion(dirtyRect, model) && dirtyRgn_ != 0 && savedClipRgn_ != 0;
  if (useRegionClip)
  {
    GetClip(savedClipRgn_);
    SectRgn(dirtyRgn_, savedClipRgn_, dirtyRgn_);
    SetClip(dirtyRgn_);
  }
  const loka::toolbox::RectSurfaceRepaintPlan plan(
      this->presented_.isKnown() ? &this->presented_.value() : 0, model,
      loka::core::Frame(this->rect_.left, this->rect_.top,
                        this->rect_.right - this->rect_.left, this->rect_.bottom - this->rect_.top),
      loka::core::Frame(dirtyRect.left, dirtyRect.top,
                        dirtyRect.right - dirtyRect.left, dirtyRect.bottom - dirtyRect.top));
  for (short i = 0; i < plan.eraseCount(); ++i)
  {
    const loka::core::Frame &frame = plan.eraseRect(i);
    Rect rect;
    SetRect(&rect, frame.x, frame.y, frame.x + frame.width, frame.y + frame.height);
    loka::toolbox::ToolboxPaintGround(
        loka::app::GroundForKind(loka::app::scene::NODE_KIND_RECT_SURFACE), rect);
  }
  for (short i = 0; i < plan.paintCount(); ++i)
  {
    const loka::core::Frame &frame = plan.paintRect(i);
    Rect rect;
    SetRect(&rect, frame.x, frame.y, frame.x + frame.width, frame.y + frame.height);
    PaintRect(&rect);
  }
  if (useRegionClip)
  {
    SetClip(savedClipRgn_);
  }
  this->presented_.invalidate();
  if (this->tempRgn_)
  {
    GetClip(this->tempRgn_);
    if (ToolboxPaintClipCovers(this->tempRgn_, this->paintRect_)
        && dirtyRect.left <= this->paintRect_.left && dirtyRect.top <= this->paintRect_.top
        && dirtyRect.right >= this->paintRect_.right && dirtyRect.bottom >= this->paintRect_.bottom)
      this->presented_.commit(model, ToolboxPaintScope());
  }
}

bool ToolboxRectSurfaceContext::dirtyRect(Rect &outRect) const
{
  if (!node_ || !node_->props.model_)
  {
    return false;
  }
  if (!this->presented_.isKnown())
  {
    outRect = this->rect_;
    return outRect.left < outRect.right && outRect.top < outRect.bottom;
  }
  const loka::app::RectSurfaceModel model = node_->props.model_->get();
  bool hasBounds = false;
  const loka::app::RectSurfaceModel *models[] = {
      &model, &this->presented_.value()};
  for (unsigned m = 0; m < sizeof(models) / sizeof(models[0]); ++m)
  {
    for (short i = 0; i < models[m]->rectCount; ++i)
    {
      const Rect spriteRect = this->rectForSprite(models[m]->rects[i]);
      if (EmptyRect(&spriteRect))
        continue;
      if (!hasBounds)
      {
        outRect = spriteRect;
        hasBounds = true;
        continue;
      }
      if (spriteRect.left < outRect.left) outRect.left = spriteRect.left;
      if (spriteRect.top < outRect.top) outRect.top = spriteRect.top;
      if (spriteRect.right > outRect.right) outRect.right = spriteRect.right;
      if (spriteRect.bottom > outRect.bottom) outRect.bottom = spriteRect.bottom;
    }
  }
  if (!hasBounds)
  {
    outRect = this->rect_;
    // Preserve the empty-model fallback without repainting off-seat sprites.
    return model.rectCount == 0 && this->presented_.value().rectCount == 0;
  }
  return outRect.left < outRect.right && outRect.top < outRect.bottom;
}

Rect ToolboxRectSurfaceContext::rectForSprite(const loka::app::RectSprite &sprite) const
{
  const long left = static_cast<long>(this->rect_.left) + sprite.x;
  const long top = static_cast<long>(this->rect_.top) + sprite.y;
  const long right = left + sprite.width;
  const long bottom = top + sprite.height;
  const long clippedLeft = left > this->rect_.left ? left : this->rect_.left;
  const long clippedTop = top > this->rect_.top ? top : this->rect_.top;
  const long clippedRight = right < this->rect_.right ? right : this->rect_.right;
  const long clippedBottom = bottom < this->rect_.bottom ? bottom : this->rect_.bottom;
  Rect rect;
  if (clippedLeft >= clippedRight || clippedTop >= clippedBottom)
    SetRect(&rect, 0, 0, 0, 0);
  else
    SetRect(&rect, static_cast<short>(clippedLeft), static_cast<short>(clippedTop),
            static_cast<short>(clippedRight), static_cast<short>(clippedBottom));
  return rect;
}

bool ToolboxRectSurfaceContext::buildDirtyRegion(const Rect &dirtyRect, const loka::app::RectSurfaceModel &model)
{
  if (!dirtyRgn_ || !tempRgn_)
  {
    return false;
  }
  SetEmptyRgn(dirtyRgn_);
  unionSpriteRectsIntoRegion(model, dirtyRect);
  if (this->presented_.isKnown())
  {
    unionSpriteRectsIntoRegion(this->presented_.value(), dirtyRect);
  }
  Rect bounds = (**dirtyRgn_).rgnBBox;
  return bounds.left < bounds.right && bounds.top < bounds.bottom;
}

void ToolboxRectSurfaceContext::unionSpriteRectsIntoRegion(const loka::app::RectSurfaceModel &model,
                                                           const Rect &dirtyRect)
{
  if (!dirtyRgn_ || !tempRgn_)
  {
    return;
  }
  for (short i = 0; i < model.rectCount; ++i)
  {
    const Rect spriteRect = this->rectForSprite(model.rects[i]);
    Rect clippedRect = spriteRect;
    if (!SectRect(&clippedRect, &dirtyRect, &clippedRect))
    {
      continue;
    }
    RectRgn(tempRgn_, &clippedRect);
    UnionRgn(dirtyRgn_, tempRgn_, dirtyRgn_);
  }
}
