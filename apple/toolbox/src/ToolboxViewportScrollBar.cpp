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
      // (the whole control). Suppress setter painting; renderScrollView's
      // Draw1Control after restoring the outer clip still restores the complete bar after content erasure.
      ToolboxPaintSuppressScope suppress(paintSuppressClipRgn);
      SetControlValue(native->control, static_cast<short>(metrics.clampedOffset));
      native->appliedValue = metrics.clampedOffset;
      native->active = metrics.maximum > 0;
      HiliteControl(native->control, native->active ? 0 : 255);
      ShowControl(native->control);
    }
  }
} // namespace

ToolboxViewportRows
ToolboxScenePlatformController::ensureViewportScrollBarControl(const Rect &viewportRect,
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

  Rect port = Rect();
  const bool hasPort = this->window_ && this->window_->queryPortRect(port);
  const ToolboxScrollViewEdges edges = ToolboxScrollViewFrameEdges(viewportRect, port, hasPort);
  Rect sizeBox = Rect();
  if (this->window_)
    this->window_->querySizeBoxRect(sizeBox);
  ScrollBarControlBinding *native = 0;
  Rect movedFrom;
  std::size_t x = ToolboxViewportRows::absent(), y = ToolboxViewportRows::absent();
  if (this->ensureScrollBarBinding(binding->barId[loka::app::SCROLL_BAR_VERTICAL],
                                   ToolboxScrollViewVerticalBarRect(
                                       viewportRect, horizontalBar, sizeBox, edges, scrollView->props.contentInsets_),
                                   0,
                                   metrics.maximum,
                                   1,
                                   viewportHeight > 1 ? viewportHeight - 1 : 1,
                                   scrollView->nativeLifetimeHint(),
                                   native,
                                   &movedFrom))
  {
    ApplyViewportScrollBarValue(native, metrics, this->paintSuppressClipRgn_);
    if (native && !EmptyRect(&movedFrom))
      this->validateOwnControlMove(movedFrom, native->placement.full());
  }
  if (native)
    y = static_cast<std::size_t>(native - &this->scrollBarLedger_.scrollBarControls_[0]);
  // A refused or empty vertical binding must not skip horizontal handling.
  const bool verticalPresent = native != 0;
  if (horizontalBar)
  {
    const int childWidth = ToolboxScrollViewChildWidth(viewportRect, edges);
    if (this->ensureScrollBarBinding(binding->barId[loka::app::SCROLL_BAR_HORIZONTAL],
                                     ToolboxScrollViewHorizontalBarRect(viewportRect, sizeBox, edges),
                                     0,
                                     horizontal.maximum,
                                     1,
                                     childWidth > 1 ? childWidth - 1 : 1,
                                     scrollView->nativeLifetimeHint(),
                                     native,
                                     &movedFrom))
    {
      ApplyViewportScrollBarValue(native, horizontal, this->paintSuppressClipRgn_);
      if (native && !EmptyRect(&movedFrom))
        this->validateOwnControlMove(movedFrom, native->placement.full());
    }
    if (native)
      x = static_cast<std::size_t>(native - &this->scrollBarLedger_.scrollBarControls_[0]);
  }
  if (!verticalPresent || !horizontalBar)
  {
    // A departing bar is hidden here, before the children paint into its
    // row. The post-render sweep would HideControl after them, and the
    // Control Manager fills a hidden control's rect with the window
    // background, erasing that strip until the next update event.
    // HideControl on an already hidden control does nothing.
    for (std::size_t i = 0; i < this->scrollBarLedger_.scrollBarControls_.size(); ++i)
    {
      ScrollBarControlBinding &row = this->scrollBarLedger_.scrollBarControls_[i];
      if (row.control
          && ((!verticalPresent && row.resourceId == binding->barId[loka::app::SCROLL_BAR_VERTICAL])
              || (!horizontalBar && row.resourceId == binding->barId[loka::app::SCROLL_BAR_HORIZONTAL])))
      {
        HideControl(row.control);
      }
    }
  }
  return ToolboxViewportRows(metrics.clampedOffset, x, y);
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
