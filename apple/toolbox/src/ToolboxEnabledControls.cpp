#include "ToolboxPortScope.hpp"
#include "ToolboxPaintSuppressScope.hpp"
#include "context/ToolboxLayoutUtil.hpp"

/** Shared setter door: suppress native painting, then replay in composition order. */
void ToolboxScenePlatformController::refreshButtonEnabled(ButtonControlBinding &binding)
{
  if (binding.control)
  {
    ToolboxPortScope port(this->window_->window());
    ToolboxPaintSuppressScope suppress(this->paintSuppressClipRgn_);
    this->window_->requestInvalidateRect(binding.rect);
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
    ToolboxPaintSuppressScope suppress(this->paintSuppressClipRgn_);
    this->window_->requestInvalidateRect(binding.rect);
    HiliteControl(binding.control, binding.active ? 0 : 255);
  }
}
