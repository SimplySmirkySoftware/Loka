/** Tracking and its commit are one operation; included by the rail and host. */
void ToolboxScenePlatformController::commitScrollBarValueAt(std::size_t index)
{
  if (index >= scrollBarLedger_.scrollBarControls_.size())
  {
    return;
  }
  ScrollBarControlBinding &binding = scrollBarLedger_.scrollBarControls_[index];
  if (!binding.control || !binding.value)
  {
    return;
  }
  const int settled = static_cast<int>(GetControlValue(binding.control));
  if (settled == binding.appliedValue)
  {
    // A cancelled thumb drag lands back on the value the CDEF started from.
    // Publishing it anyway would fire onChange for a gesture the user
    // deliberately abandoned.
    return;
  }
  // Copy out before the batch: publishing re-enters projection, which can
  // reallocate scrollBarControls_ underneath this reference.
  const Rect rect = binding.rect;
  loka::core::EmitterState *onChange = binding.onChange;
  binding.appliedValue = settled;

  beginBatchUpdate();
  addPendingDirty(rect);
  // The write enters the window tracker's transaction the way handleTextKey
  // already does for typing: a settled value is scene input, and dependent
  // state resolves in the same transaction rather than at whatever tick
  // happens next.
  {
    loka::core::StateTrackerGuard _(window_ ? window_->getTracker() : 0);
    // Order is the contract (ruling 1): the binding holds the settled value
    // before any handler runs.
    binding.valueSeat.set(settled, true);
    if (onChange)
    {
      onChange->emit();
    }
  }
  endBatchUpdate();
}

void ToolboxScenePlatformController::commitViewportScrollBarValue(ViewportScrollBarBinding &binding,
                                                                  ScrollBarControlBinding &native,
                                                                  loka::app::ScrollBarOrientation axis)
{
  const int settled = static_cast<int>(GetControlValue(native.control));
  if (settled == native.appliedValue)
  {
    // Cancelled thumb drags and range-edge presses publish no fact.
    return;
  }
  loka::app::ScrollViewNode *scrollView = binding.scrollView;
  const Rect rect = binding.rect;
  native.appliedValue = settled;

  beginBatchUpdate();
  addPendingDirty(rect);
  // Unlike the DSL ScrollBar commit above, this fact belongs to the
  // ScrollView owner. The complete NodeState door selects that tracker.
  // This input moves one axis; the preceding layout has already clamped the other.
  switch (axis)
  {
  case loka::app::SCROLL_BAR_VERTICAL:
    scrollView->publishOrigin(scrollView->requestedOrigin().x, settled);
    break;
  case loka::app::SCROLL_BAR_HORIZONTAL:
    scrollView->publishOrigin(settled, scrollView->requestedOrigin().y);
    break;
  }
  endBatchUpdate();
}

#include "ToolboxPress.cpp"
