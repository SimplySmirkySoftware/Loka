/** Shared deferred native queue, pool intake, bulk control teardown and the
    live-binding checks retirement asks, included by the controller and host fixture. */
template <typename HandleT>
void ToolboxScenePlatformController::queueRetiredNativeHandle(std::vector<RetiredNativeEntry<HandleT> > &retired,
                                                              HandleT handle,
                                                              loka::app::scene::NativeLifetimeHint lifetimeHint)
{
  if (!handle)
  {
    return;
  }
  for (size_t i = 0; i < retired.size(); ++i)
  {
    if (retired[i].handle == handle)
    {
      return;
    }
  }
  RetiredNativeEntry<HandleT> entry;
  entry.handle = handle;
  entry.lifetimeHint = lifetimeHint;
  retired.push_back(entry);
}

void ToolboxScenePlatformController::queueRetiredTextEdit(TEHandle te,
                                                          loka::app::scene::NativeLifetimeHint lifetimeHint)
{
  queueRetiredNativeHandle(retiredTextEdits_, te, lifetimeHint);
}

bool ToolboxScenePlatformController::hasLiveBinding(TEHandle te) const
{
  for (size_t i = 0; i < editControls_.size(); ++i)
  {
    if (editControls_[i].te == te)
    {
      return true;
    }
  }
  return false;
}

void ToolboxScenePlatformController::disposeNativeHandle(TEHandle te)
{
  if (te)
  {
    TEDispose(te);
  }
}

template <typename HandleT>
void ToolboxScenePlatformController::flushRetiredEntriesInto(
    std::vector<RetiredNativeEntry<HandleT> > &retired,
    loka::app::scene::ExactMatchHandleBucket<HandleT> &bucket)
{
  for (size_t i = 0; i < retired.size(); ++i)
  {
    HandleT handle = retired[i].handle;
    if (!handle)
    {
      continue;
    }
    if (retired[i].lifetimeHint == loka::app::scene::NATIVE_HINT_EAGER_RELEASE)
    {
      disposeNativeHandle(handle);
      continue;
    }
    // Bag entries must hold zero pointers into Loka; a live binding still
    // referencing the handle means the retire ritual did not complete.
    // Leaking the handle (counted) is the safe arm — disposing it would
    // hand the live binding a dead handle, and pooling it would pay the
    // same handle out twice.
    if (hasLiveBinding(handle))
    {
      ++poolIntakeAuditFailCount_;
      continue;
    }
    if (!bucket.offer(handle))
    {
      disposeNativeHandle(handle);
    }
  }
  retired.clear();
}

void ToolboxScenePlatformController::clearControls()
{
  for (size_t i = 0; i < buttonControls_.size(); ++i)
  {
    if (buttonControls_[i].context)
      buttonControls_[i].context->forgetPresentedControl();
    if (buttonControls_[i].control)
    {
      HideControl(buttonControls_[i].control);
      queueRetiredControl(buttonControls_[i].control, buttonControls_[i].lifetimeHint);
      buttonControls_[i].control = 0;
    }
  }
  buttonControls_.clear();
  for (size_t i = 0; i < scrollBarLedger_.scrollBarControls_.size(); ++i)
  {
    this->retireScrollBarBinding(this->scrollBarLedger_.scrollBarControls_[i],
                                 this->scrollBarLedger_.scrollBarControls_[i].lifetimeHint);
  }
  scrollBarLedger_.scrollBarControls_.clear();
  // Bulk teardown also returns ids whose axis never materialized a control.
  for (size_t i = 0; i < scrollBarLedger_.viewportScrollBars_.size(); ++i)
  {
    this->controlIds_.release(scrollBarLedger_.viewportScrollBars_[i].barId[loka::app::SCROLL_BAR_VERTICAL]);
    this->controlIds_.release(scrollBarLedger_.viewportScrollBars_[i].barId[loka::app::SCROLL_BAR_HORIZONTAL]);
  }
  scrollBarLedger_.viewportScrollBars_.clear();
  for (size_t i = 0; i < editControls_.size(); ++i)
  {
    this->retireEditTextBinding(this->editControls_[i], this->editControls_[i].lifetimeHint);
  }
  editControls_.clear();
}

void ToolboxScenePlatformController::queueRetiredControl(ControlRef control,
                                                         loka::app::scene::NativeLifetimeHint lifetimeHint)
{
  queueRetiredNativeHandle(retiredControls_, control, lifetimeHint);
}

void ToolboxScenePlatformController::queueRetiredScrollBarControl(ControlRef control,
                                                                   loka::app::scene::NativeLifetimeHint lifetimeHint)
{
  queueRetiredNativeHandle(retiredScrollBarControls_, control, lifetimeHint);
}

bool ToolboxScenePlatformController::hasLiveBinding(ControlRef control) const
{
  for (size_t i = 0; i < buttonControls_.size(); ++i)
  {
    if (buttonControls_[i].control == control)
    {
      return true;
    }
  }
  for (size_t i = 0; i < scrollBarLedger_.scrollBarControls_.size(); ++i)
  {
    if (scrollBarLedger_.scrollBarControls_[i].control == control)
    {
      return true;
    }
  }
  return false;
}

bool ToolboxScenePlatformController::hasLiveBinding(loka::core::State<bool> *enabled) const
{
  for (size_t i = 0; i < this->buttonControls_.size(); ++i)
  {
    if (this->buttonControls_[i].enabled == enabled)
      return true;
  }
  for (size_t i = 0; i < hitLedger_.buttonHits_.size(); ++i)
  {
    if (hitLedger_.buttonHits_[i].enabled == enabled)
    {
      return true;
    }
  }
  for (size_t i = 0; i < hitLedger_.popupHits_.size(); ++i)
  {
    if (hitLedger_.popupHits_[i].enabled == enabled)
    {
      return true;
    }
  }
  for (size_t i = 0; i < scrollBarLedger_.scrollBarControls_.size(); ++i)
  {
    if (scrollBarLedger_.scrollBarControls_[i].enabled == enabled)
    {
      return true;
    }
  }
  return false;
}

void ToolboxScenePlatformController::disposeNativeHandle(ControlRef control)
{
  if (control)
  {
    DisposeControl(control);
  }
}

void ToolboxScenePlatformController::retireScrollBarBinding(ScrollBarControlBinding &row,
                                                            loka::app::scene::NativeLifetimeHint hint)
{
  row.editor.revoke();
  if (row.control)
  {
    HideControl(row.control);
    this->queueRetiredScrollBarControl(row.control, hint);
    row.control = 0;
  }
}
