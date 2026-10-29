/** Shared production invalidation procedure, also compiled by the host fixture. */
void ToolboxWindow::requestInvalidate()
{
  requestInvalidateWithReason(TOOLBOX_FULL_DRAW_UNKNOWN);
}

void ToolboxWindow::requestInvalidateWithReason(ToolboxFullDrawReason reason)
{
  // Keep every cause even when the first request already schedules the full draw.
  if (scenePlatformController_)
    scenePlatformController_->notePresentReason(reason);
  if (needsInvalidate_)
  {
    return;
  }
  if (scenePlatformController_)
  {
    scenePlatformController_->noteWindowFullRequest(ToolboxFullDrawReasonName(reason));
  }
  needsInvalidate_ = true;
  pendingInvalidateRects_.clear();
}

void ToolboxWindow::requestInvalidateRect(const Rect &rect)
{
  if (scenePlatformController_)
  {
    scenePlatformController_->noteWindowRectRequest();
  }
  if (needsInvalidate_)
  {
    return;
  }
  for (std::size_t i = 0; i < pendingInvalidateRects_.size(); ++i)
  {
    Rect &pending = pendingInvalidateRects_[i];
    if (rect.right < pending.left || rect.left > pending.right || rect.bottom < pending.top
        || rect.top > pending.bottom)
    {
      continue;
    }
    if (rect.left < pending.left)
    {
      pending.left = rect.left;
    }
    if (rect.top < pending.top)
    {
      pending.top = rect.top;
    }
    if (rect.right > pending.right)
    {
      pending.right = rect.right;
    }
    if (rect.bottom > pending.bottom)
    {
      pending.bottom = rect.bottom;
    }
    return;
  }
  pendingInvalidateRects_.push_back(rect);
}

void ToolboxWindow::flushInvalidate()
{
  if (!window_)
  {
    return;
  }
  if (!needsInvalidate_ && pendingInvalidateRects_.empty())
  {
    return;
  }
  if (needsInvalidate_)
  {
    if (scenePlatformController_)
    {
      scenePlatformController_->noteWindowFlushFull();
    }
    // Projected contexts are materialized during the tree walk and request a
    // structure present themselves. This draw already includes them, so
    // consume that request only after the walk completes.
    this->drawForFlush();
    needsInvalidate_ = false;
    pendingInvalidateRects_.clear();
    return;
  }
  needsInvalidate_ = false;
  std::vector<Rect> rects = pendingInvalidateRects_;
  pendingInvalidateRects_.clear();
  for (std::size_t i = 0; i < rects.size(); ++i)
  {
    if (scenePlatformController_)
    {
      scenePlatformController_->noteWindowFlushDirty();
    }
    this->drawDirty(rects[i]);
  }
}

bool ToolboxWindow::hasPendingInvalidate() const
{
  return needsInvalidate_ || !pendingInvalidateRects_.empty();
}
