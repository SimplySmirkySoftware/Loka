// Included by the scrollbar ledger and the Toolbox host fixture.
#include "ToolboxPaintSuppressScope.hpp"
namespace
{
  void ApplyViewportScrollBarValue(ToolboxScrollBarLedger::ScrollBarControlBinding *native,
                                   const ToolboxScrollViewMetrics &metrics,
                                   RgnHandle paintSuppressClipRgn)
  {
    if (native)
    {
      native->value = 0;
      native->onChange = 0;
      native->enabled = 0;
      // SetControlValue requests drawCntl 129 (indicator movement), not 0
      // (the whole control). Suppress setter painting; the post-render
      // Draw1Control still restores the complete bar after content erasure.
      ToolboxPaintSuppressScope suppress(paintSuppressClipRgn);
      SetControlValue(native->control, static_cast<short>(metrics.clampedOffset));
      native->appliedValue = metrics.clampedOffset;
      native->active = metrics.maximum > 0;
      HiliteControl(native->control, native->active ? 0 : 255);
      ShowControl(native->control);
    }
  }
} // namespace

int ToolboxScenePlatformController::ensureViewportScrollBarControl(const Rect &viewportRect,
                                                                   loka::app::ScrollViewNode *scrollView,
                                                                   int contentHeight,
                                                                   int viewportHeight,
                                                                   int requestedOffset,
                                                                   const ToolboxScrollViewMetrics &horizontal,
                                                                   bool horizontalBar)
{
  const ToolboxScrollViewMetrics metrics = ToolboxScrollViewResolveMetrics(
      contentHeight, viewportHeight, requestedOffset);
  ViewportScrollBarBinding *binding = 0;
  for (std::size_t i = 0; i < this->scrollBarLedger_.viewportScrollBars_.size(); ++i)
  {
    if (this->scrollBarLedger_.viewportScrollBars_[i].scrollView == scrollView)
    {
      binding = &this->scrollBarLedger_.viewportScrollBars_[i];
      break;
    }
  }
  if (!binding)
  {
    ViewportScrollBarBinding entry = ViewportScrollBarBinding();
    entry.barId[loka::app::SCROLL_BAR_VERTICAL] = this->allocateControlId();
    entry.barId[loka::app::SCROLL_BAR_HORIZONTAL] = this->allocateControlId();
    entry.scrollView = scrollView;
    entry.usedThisFrame = true;
    entry.rect = viewportRect;
    scrollBarLedger_.viewportScrollBars_.push_back(entry);
    binding = &scrollBarLedger_.viewportScrollBars_.back();
  }
  binding->usedThisFrame = true;
  binding->rect = viewportRect;

  ScrollBarControlBinding *native = 0;
  if (!this->ensureScrollBarBinding(binding->barId[loka::app::SCROLL_BAR_VERTICAL],
                                    ToolboxScrollViewVerticalBarRect(viewportRect, horizontalBar),
                                    0,
                                    metrics.maximum,
                                    1,
                                    viewportHeight > 1 ? viewportHeight - 1 : 1,
                                    scrollView->nativeLifetimeHint(),
                                    native))
  {
    return metrics.clampedOffset;
  }
  ApplyViewportScrollBarValue(native, metrics, this->paintSuppressClipRgn_);
  if (horizontalBar)
  {
    const int childWidth = ToolboxScrollViewChildWidth(viewportRect.right - viewportRect.left);
    if (this->ensureScrollBarBinding(binding->barId[loka::app::SCROLL_BAR_HORIZONTAL],
                                     ToolboxScrollViewHorizontalBarRect(viewportRect),
                                     0,
                                     horizontal.maximum,
                                     1,
                                     childWidth > 1 ? childWidth - 1 : 1,
                                     scrollView->nativeLifetimeHint(),
                                     native))
      ApplyViewportScrollBarValue(native, horizontal, this->paintSuppressClipRgn_);
  }
  return metrics.clampedOffset;
}

void ToolboxScenePlatformController::destroyViewportScrollBarControl(
    loka::app::ScrollViewNode *scrollView,
    loka::app::scene::NativeLifetimeHint lifetimeHint)
{
  for (std::size_t index = 0; index < this->scrollBarLedger_.viewportScrollBars_.size(); ++index)
  {
    ViewportScrollBarBinding &binding = scrollBarLedger_.viewportScrollBars_[index];
    if (binding.scrollView != scrollView)
    {
      continue;
    }
    const short verticalId = binding.barId[loka::app::SCROLL_BAR_VERTICAL];
    const short horizontalId = binding.barId[loka::app::SCROLL_BAR_HORIZONTAL];
    scrollBarLedger_.viewportScrollBars_.erase(scrollBarLedger_.viewportScrollBars_.begin() + index);
    this->destroyScrollBarControl(verticalId, lifetimeHint);
    this->destroyScrollBarControl(horizontalId, lifetimeHint);
    return;
  }
}
