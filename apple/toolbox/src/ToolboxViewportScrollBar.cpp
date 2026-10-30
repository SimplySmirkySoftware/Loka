// Included by the scrollbar ledger and the Toolbox host fixture.
#include "ToolboxPaintSuppressScope.hpp"
namespace
{
  /** One arrow click moves 16 pixels, the Win32 rail's line (kLineStepPixels).
      A multiple of 8 keeps CardBand's window-anchored 8x8 dither phase, so a
      viewport presented by shifting pixels matches a repaint (#1043, Q-E). */
  const int kViewportLineStepPixels = 16;
  /** One page is the viewport extent less one line of overlap, rounded down to
      a multiple of 8 for the same phase reason; never less than one line. */
  int ViewportPageStep(int extent)
  {
    const int page = (extent - 1) & ~7;
    return page > 0 ? page : kViewportLineStepPixels;
  }
  void ApplyViewportScrollBarValue(ToolboxScrollBarLedger::ScrollBarControlBinding *native,
                                   const ToolboxScrollViewMetrics &metrics)
  {
    if (native)
    {
      native->value = 0;
      native->onChange = 0;
      native->enabled = 0;
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
  // One suppression covers every native setter of these bars: move, size,
  // minimum and maximum in the ensures, value, hilite and show, and the
  // departing bar's HideControl. The scratch region is exclusive, so a second
  // scope inside would save the empty clip and leave it behind. A departing
  // bar's update stays queued. renderScrollView's Draw1Control, after the
  // outer clip is restored, still draws the complete bar after content erasure.
  ToolboxPaintSuppressScope suppress(this->paintSuppressClipRgn_);
  ScrollBarControlBinding *native = 0;
  Rect movedFrom;
  std::size_t x = ToolboxViewportRows::absent(), y = ToolboxViewportRows::absent();
  if (this->ensureScrollBarBinding(binding->barId[loka::app::SCROLL_BAR_VERTICAL],
                                   ToolboxScrollViewVerticalBarRect(
                                       viewportRect, horizontalBar, sizeBox, edges, scrollView->props.contentInsets_),
                                   0,
                                   metrics.maximum,
                                   kViewportLineStepPixels,
                                   ViewportPageStep(viewportHeight),
                                   scrollView->nativeLifetimeHint(),
                                   native,
                                   &movedFrom))
  {
    ApplyViewportScrollBarValue(native, metrics);
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
                                     kViewportLineStepPixels,
                                     ViewportPageStep(childWidth),
                                     scrollView->nativeLifetimeHint(),
                                     native,
                                     &movedFrom))
    {
      ApplyViewportScrollBarValue(native, horizontal);
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
