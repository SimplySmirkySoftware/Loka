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
  if (!binding || !binding->usedThisFrame || !this->scrollViewClipRgn_ || !this->paintSuppressClipRgn_
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

  GetClip(this->scrollViewClipRgn_);
  // Preserve the dirty caller clip while walking viewport children. The
  // scratch region is free here; setters borrow it only after SetClip copies it.
  RectRgn(this->paintSuppressClipRgn_, &viewportRect);
  SectRgn(this->scrollViewClipRgn_, this->paintSuppressClipRgn_, this->paintSuppressClipRgn_);
  SetClip(this->paintSuppressClipRgn_);
  {
    loka::app::scene::ProjectionViewportScopeGuard scopeGuard(this->projectionParentScopes_, renderScope);
    if (scopeGuard.isActive())
    {
      RenderChildren(scrollView, this);
    }
  }
  SetClip(this->scrollViewClipRgn_);
  this->drawScrollBarControl(binding->barId[loka::app::SCROLL_BAR_VERTICAL]);
  this->drawScrollBarControl(binding->barId[loka::app::SCROLL_BAR_HORIZONTAL]);
}
