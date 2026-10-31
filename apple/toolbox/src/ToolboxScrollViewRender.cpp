#include "context/ToolboxPaintSupport.hpp"

// Shared by the controller and host fixture.
void ToolboxScenePlatformController::renderScrollView(loka::app::ScrollViewNode *scrollView)
{
  const ViewportScrollBarBinding *binding = 0;
  for (std::size_t i = 0; i < this->scrollBarLedger_.viewportScrollBars_.size(); ++i)
  {
    if (this->scrollBarLedger_.viewportScrollBars_[i].scrollView == scrollView)
    {
      binding = &this->scrollBarLedger_.viewportScrollBars_[i];
      break;
    }
  }
  if (this->projectionParentScopes_.insideViewport() || this->projectionParentScopes_.insideUnit())
    return;
  if (!binding || !binding->usedThisFrame)
  {
    LOKA_PAINT_REFUSE(this->observation_, TOOLBOX_PAINT_REFUSAL_OBLIGATION_UNMET);
    return;
  }
  if (!this->scrollViewClipRgn_)
  {
    LOKA_PAINT_REFUSE(this->observation_, TOOLBOX_PAINT_REFUSAL_CLIP_ALLOCATION);
    return;
  }
  Rect viewportRect = binding->contentRect;
  // Match layout: a viewport inside a clip box inherits its final paint bound.
  if (this->projectionParentScopes_.currentIsClip())
    this->intersectWithProjectionClip(binding->contentRect, viewportRect);
  const loka::core::Frame viewportClip(viewportRect.left,
                                       viewportRect.top,
                                       viewportRect.right - viewportRect.left,
                                       viewportRect.bottom - viewportRect.top);
  loka::app::scene::ProjectionParentScope renderScope(
      this->projectionParentScopes_.current().nativeParent, 0, 0, viewportClip);

  {
    ToolboxPaintClip clip(viewportRect, ToolboxPaintClip::INTERSECT);
    if (!clip.isActive())
    {
      LOKA_PAINT_REFUSE(this->observation_, TOOLBOX_PAINT_REFUSAL_CLIP_ALLOCATION);
      return;
    }
    loka::app::scene::ProjectionViewportScopeGuard scopeGuard(this->projectionParentScopes_, renderScope);
    if (scopeGuard.isActive())
    {
      RenderChildren(scrollView, this);
    }
    else
      LOKA_PAINT_REFUSE(this->observation_, TOOLBOX_PAINT_REFUSAL_CLIP_ALLOCATION);
  }
  this->reachNativeBar(binding->barId[loka::app::SCROLL_BAR_VERTICAL]);
  this->drawScrollBarControl(binding->barId[loka::app::SCROLL_BAR_VERTICAL]);
  this->reachNativeBar(binding->barId[loka::app::SCROLL_BAR_HORIZONTAL]);
  this->drawScrollBarControl(binding->barId[loka::app::SCROLL_BAR_HORIZONTAL]);
}
