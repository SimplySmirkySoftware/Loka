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
  if (!binding || !binding->usedThisFrame || !this->scrollViewClipRgn_
      || this->projectionParentScopes_.activeDepth() != 0)
  {
    return;
  }
  const Rect viewportRect = binding->contentRect;
  const loka::core::Frame viewportClip(viewportRect.left,
                                       viewportRect.top,
                                       viewportRect.right - viewportRect.left,
                                       viewportRect.bottom - viewportRect.top);
  loka::app::scene::ProjectionParentScope renderScope(
      this->projectionParentScopes_.current().nativeParent, 0, 0, viewportClip);

  {
    ToolboxPaintClip clip(viewportRect, ToolboxPaintClip::INTERSECT);
    if (!clip.isActive())
      return;
    loka::app::scene::ProjectionViewportScopeGuard scopeGuard(this->projectionParentScopes_, renderScope);
    if (scopeGuard.isActive())
    {
      RenderChildren(scrollView, this);
    }
  }
  this->drawScrollBarControl(binding->barId[loka::app::SCROLL_BAR_VERTICAL]);
  this->drawScrollBarControl(binding->barId[loka::app::SCROLL_BAR_HORIZONTAL]);
}
