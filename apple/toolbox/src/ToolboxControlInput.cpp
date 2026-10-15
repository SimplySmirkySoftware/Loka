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

void ToolboxScenePlatformController::commitViewportScrollBarValue(
    ViewportScrollBarBinding &binding,
    ScrollBarControlBinding &native)
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
  scrollView->publishOrigin(scrollView->requestedOrigin().x, settled);
  endBatchUpdate();
}

bool ToolboxScenePlatformController::handleControlClick(const Point &point)
{
  if (!window_ || !window_->window())
  {
    return false;
  }
  ControlRef control = 0;
  ControlPartCode part = FindControl(point, window_->window(), &control);
  if (part == 0 || !control)
  {
    return false;
  }
  for (size_t i = 0; i < buttonControls_.size(); ++i)
  {
    ButtonControlBinding &binding = buttonControls_[i];
    if (binding.control == control && binding.emitter)
    {
      if (binding.enabled && !binding.enabled->get())
      {
        return true;
      }
      beginBatchUpdate();
      ControlPartCode tracked = TrackControl(control, point, 0);
      if (tracked != 0)
      {
        binding.emitter->emit();
      }
      endBatchUpdate();
      return true;
    }
  }
  for (size_t i = 0; i < scrollBarLedger_.scrollBarControls_.size(); ++i)
  {
    if (scrollBarLedger_.scrollBarControls_[i].control != control)
    {
      continue;
    }
    std::size_t viewportIndex = scrollBarLedger_.viewportScrollBars_.size();
    for (size_t j = 0; j < scrollBarLedger_.viewportScrollBars_.size(); ++j)
    {
      if (scrollBarLedger_.viewportScrollBars_[j].resourceId ==
          scrollBarLedger_.scrollBarControls_[i].resourceId)
      {
        viewportIndex = j;
        break;
      }
    }
    if (!scrollBarLedger_.scrollBarControls_[i].active)
    {
      // An inactive bar still owns its rect; swallowing the click keeps the
      // hit from falling through to whatever is drawn beneath it.
      return true;
    }
    gActiveScrollBarLineStep = scrollBarLedger_.scrollBarControls_[i].lineStep;
    gActiveScrollBarPageStep = scrollBarLedger_.scrollBarControls_[i].pageStep;
    // The thumb gets no action proc: the CDEF's own outline drag is the
    // Classic gesture. Arrows and page areas need one so a held press keeps
    // moving instead of stepping once.
    ControlActionUPP action = (part == kControlIndicatorPart) ? 0 : ScrollBarActionUPP();
    TrackControl(control, point, action);
    // Read after the loop has ended, never during it (ruling 1). The return
    // code is deliberately ignored: releasing off an arrow ends the scroll
    // but keeps what already scrolled, and commitScrollBarValueAt is the one
    // place that decides whether anything actually changed.
    if (viewportIndex != scrollBarLedger_.viewportScrollBars_.size())
    {
      commitViewportScrollBarValue(
          scrollBarLedger_.viewportScrollBars_[viewportIndex], scrollBarLedger_.scrollBarControls_[i]);
    }
    else
    {
      commitScrollBarValueAt(i);
    }
    return true;
  }
  return false;
}
