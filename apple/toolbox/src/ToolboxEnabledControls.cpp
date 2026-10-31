#include "ToolboxPortScope.hpp"
#include "ToolboxPaintSuppressScope.hpp"
#include "context/ToolboxLayoutUtil.hpp"

/** Enabled observers queue into the existing batch, or request one rect directly. */
void ToolboxScenePlatformController::requestControlRepaint(const Rect &rect)
{
  if (this->inBatchUpdate_)
    this->addPendingDirty(rect);
  else
    this->window_->requestInvalidateRect(rect);
}

/** Shared setter door: suppress native painting, then replay in composition order. */
void ToolboxScenePlatformController::refreshButtonEnabled(ButtonControlBinding &binding)
{
  if (binding.control)
  {
    ToolboxPortScope port(this->window_->window());
    if (!this->paintSuppressClipRgn_)
      LOKA_PAINT_REFUSE(this->observation_, TOOLBOX_PAINT_REFUSAL_CLIP_ALLOCATION);
    ToolboxPaintSuppressScope suppress(this->paintSuppressClipRgn_);
    Rect visible;
    if (binding.placement.queryVisible(visible))
      this->requestControlRepaint(visible);
    if (binding.enabled->get())
    {
      HiliteControl(binding.control, 0);
    }
    else
    {
      HiliteControl(binding.control, 255);
    }
  }
}

void ToolboxScenePlatformController::refreshScrollBarEnabled(ScrollBarControlBinding &binding)
{
  // A disabled bar and an unscrollable one share the inactive
  // presentation, so re-derive from both rather than from enabled alone.
  binding.active = binding.enabled->get() && loka::app::ScrollBarIsScrollable(binding.minimum, binding.maximum);
  if (binding.control)
  {
    ToolboxPortScope port(this->window_->window());
    if (!this->paintSuppressClipRgn_)
      LOKA_PAINT_REFUSE(this->observation_, TOOLBOX_PAINT_REFUSAL_CLIP_ALLOCATION);
    ToolboxPaintSuppressScope suppress(this->paintSuppressClipRgn_);
    Rect visible;
    if (binding.placement.queryVisible(visible))
      this->requestControlRepaint(visible);
    HiliteControl(binding.control, binding.active ? 0 : 255);
  }
}
