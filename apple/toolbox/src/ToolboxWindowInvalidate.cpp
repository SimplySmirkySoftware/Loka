/** Shared production invalidation procedure, also compiled by the host fixture. */
void ToolboxWindow::requestInvalidate()
{
  this->requestInvalidateWithReason(TOOLBOX_FULL_DRAW_UNKNOWN);
}

void ToolboxWindow::requestInvalidateWithReason(ToolboxFullDrawReason reason)
{
  // Keep every cause even when the first request already schedules the full draw.
  if (this->scenePlatformController_)
    this->scenePlatformController_->notePresentReason(reason);
  // Count a full request when next has none, including during a full draw.
  // The source is the last counted source since the stats reset.
  if (!this->next_.full() && this->scenePlatformController_)
    this->scenePlatformController_->noteWindowFullRequest(ToolboxFullDrawReasonName(reason));
  this->next_.addFull();
}

void ToolboxWindow::requestInvalidateRect(const Rect &rect)
{
  if (this->scenePlatformController_)
    this->scenePlatformController_->noteWindowRectRequest();
  this->next_.add(rect);
}

void ToolboxWindow::flushInvalidate()
{
  if (!this->window_)
    return;
  if (this->next_.empty())
    return;
  if (this->scenePlatformController_)
    assert(!this->scenePlatformController_->erasedPass().isOpen());
  {
    ToolboxPresentBatch sealed;
    this->next_.swap(sealed);
    const ToolboxPresentBatch &attempt = sealed;
    if (attempt.full())
    {
      if (this->scenePlatformController_)
        this->scenePlatformController_->noteWindowFlushFull();
      this->drawForFlush();
    }
    else
    {
      for (std::size_t i = 0; i < attempt.rects().size(); ++i)
      {
        if (this->scenePlatformController_)
          this->scenePlatformController_->noteWindowFlushDirty();
        this->drawDirty(attempt.rects()[i]);
      }
    }
  }
}

bool ToolboxWindow::hasPendingInvalidate() const
{
  return !this->next_.empty();
}
