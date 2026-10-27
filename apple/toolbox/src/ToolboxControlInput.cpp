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
  const std::size_t index = static_cast<std::size_t>(&native - &this->scrollBarLedger_.scrollBarControls_[0]);
  const ToolboxViewportRows rows(0,
                                 axis == loka::app::SCROLL_BAR_HORIZONTAL ? index : ToolboxViewportRows::absent(),
                                 axis == loka::app::SCROLL_BAR_VERTICAL ? index : ToolboxViewportRows::absent());
  const Rect rect = binding.rect;
  this->commitViewportScrollBarValue(*binding.scrollView, rows, &rect);
}

void ToolboxScenePlatformController::commitViewportScrollBarValue(loka::app::ScrollViewNode &node,
                                                                  const ToolboxViewportRows &rows,
                                                                  const Rect *dirty)
{
  loka::core::Frame origin = node.requestedOrigin();
  bool changed = false;
  const loka::app::ScrollViewVisit::Axis axes[] = {loka::app::ScrollViewVisit::X, loka::app::ScrollViewVisit::Y};
  for (unsigned i = 0; i != 2; ++i)
  {
    std::size_t index;
    if (!rows.queryRow(axes[i], index))
      continue;
    ScrollBarControlBinding &native = this->scrollBarLedger_.scrollBarControls_[index];
    const int settled = static_cast<int>(GetControlValue(native.control));
    if (settled == native.appliedValue)
      continue;
    native.appliedValue = settled;
    if (axes[i] == loka::app::ScrollViewVisit::X)
      origin.x = settled;
    else
      origin.y = settled;
    changed = true;
  }
  if (!changed)
    return;
  // No row reference crosses publication, which can re-enter projection.
  if (dirty)
  {
    this->beginBatchUpdate();
    this->addPendingDirty(*dirty);
  }
  node.publishOrigin(origin.x, origin.y);
  if (dirty)
    this->endBatchUpdate();
}

#include "ToolboxPress.cpp"
