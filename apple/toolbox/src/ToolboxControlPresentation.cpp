// Included by the controller and the Toolbox host fixture.
void ToolboxScenePlatformController::drawControlsInRect(const Rect &rect)
{
  for (size_t i = 0; i < buttonControls_.size(); ++i)
  {
    ButtonControlBinding &binding = buttonControls_[i];
    if (!binding.control || !binding.usedThisFrame)
    {
      continue;
    }
    if (rect.right < binding.rect.left || rect.left > binding.rect.right || rect.bottom < binding.rect.top
        || rect.top > binding.rect.bottom)
    {
      continue;
    }
    if (binding.context)
      binding.context->repaint(binding.control, binding.label);
    else
    {
      Draw1Control(binding.control);
      ++debugStats_.controlDrawCount;
      ++debugStats_.totalControlDrawCount;
    }
  }
  for (size_t i = 0; i < scrollBarLedger_.scrollBarControls_.size(); ++i)
  {
    ScrollBarControlBinding &binding = scrollBarLedger_.scrollBarControls_[i];
    if (!binding.control || !binding.usedThisFrame)
    {
      continue;
    }
    if (rect.right < binding.rect.left || rect.left > binding.rect.right || rect.bottom < binding.rect.top
        || rect.top > binding.rect.bottom)
    {
      continue;
    }
    Draw1Control(binding.control);
    ++debugStats_.controlDrawCount;
    ++debugStats_.totalControlDrawCount;
  }
}

/** One render reach draws one owned scrollbar row under the caller's clip. */
void ToolboxScenePlatformController::drawScrollBarControl(short id)
{
  for (size_t i = 0; i < this->scrollBarLedger_.scrollBarControls_.size(); ++i)
  {
    ScrollBarControlBinding &row = this->scrollBarLedger_.scrollBarControls_[i];
    if (row.resourceId != id)
      continue;
    if (!row.control || !row.usedThisFrame || !(**row.control).contrlVis)
      return;
    if (this->paintSuppressClipRgn_)
    {
      GetClip(this->paintSuppressClipRgn_);
      if (!RectInRgn(&row.rect, this->paintSuppressClipRgn_))
        return;
    }
    Draw1Control(row.control);
    ++this->debugStats_.controlDrawCount;
    ++this->debugStats_.totalControlDrawCount;
    return;
  }
}
