// Included by the controller and the Toolbox host fixture.
#include "ToolboxNativeDoorScope.hpp"
void ToolboxScenePlatformController::drawControlsInRect(const Rect &rect)
{
  for (size_t i = 0; i < buttonControls_.size(); ++i)
  {
    ButtonControlBinding &binding = buttonControls_[i];
    if (!binding.control || !binding.usedThisFrame)
    {
      continue;
    }
    Rect visible;
    if (!binding.placement.queryVisible(visible) || rect.right < visible.left || rect.left > visible.right
        || rect.bottom < visible.top || rect.top > visible.bottom)
    {
      continue;
    }
    assert(binding.context && "button rows belong to their creating context");
    if (binding.context)
      binding.context->repaint(binding.control, binding.label);
  }
  for (size_t i = 0; i < scrollBarLedger_.scrollBarControls_.size(); ++i)
  {
    ScrollBarControlBinding &binding = scrollBarLedger_.scrollBarControls_[i];
    if (!binding.control || !binding.usedThisFrame)
    {
      continue;
    }
    Rect visible;
    if (!binding.placement.queryVisible(visible) || rect.right < visible.left || rect.left > visible.right
        || rect.bottom < visible.top || rect.top > visible.bottom)
    {
      continue;
    }
    if (this->window_ && this->window_->window())
    {
      ToolboxNativeDoorScope door(this->window_->window(), visible);
      if (!door.clipAllocated())
        LOKA_PAINT_REFUSE(this->observation_, TOOLBOX_PAINT_REFUSAL_CLIP_ALLOCATION);
      this->drawControl(binding.control);
    }
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
    if (!this->window_ || !this->window_->window())
      return;
    GrafPtr port = this->window_->window();
    ToolboxPortScope owner(port);
    Rect visible;
    if (!row.placement.queryVisible(visible) || !RectInRgn(&visible, port->clipRgn))
      return;
    ToolboxNativeDoorScope door(port, visible);
    if (!door.clipAllocated())
      LOKA_PAINT_REFUSE(this->observation_, TOOLBOX_PAINT_REFUSAL_CLIP_ALLOCATION);
    this->drawControl(row.control);
    return;
  }
}

/** Submit and count one native draw at its owning paint-path reach. */
void ToolboxScenePlatformController::drawControl(ControlRef control)
{
  Draw1Control(control);
  this->noteControlDraw();
}

void ToolboxScenePlatformController::noteControlDraw()
{
  ++this->debugStats_.controlDrawCount;
  ++this->debugStats_.totalControlDrawCount;
}
