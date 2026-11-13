/** Shared production liveness query; include after concrete context definitions. */
bool ToolboxScenePlatformController::hasLiveBinding(loka::core::State<loka::core::String> *text) const
{
  for (size_t i = 0; i < editControls_.size(); ++i)
  {
    if (editControls_[i].text == text)
    {
      return true;
    }
  }
  for (size_t i = 0; i < hitLedger_.cellHits_.size(); ++i)
  {
    if (hitLedger_.cellHits_[i].context->liveTextState() == text)
    {
      return true;
    }
  }
  for (size_t i = 0; i < hitLedger_.editHits_.size(); ++i)
  {
    if (hitLedger_.editHits_[i].text == text)
    {
      return true;
    }
  }
  for (size_t i = 0; i < hitLedger_.textHits_.size(); ++i)
  {
    if (hitLedger_.textHits_[i].context->liveTextState() == text)
    {
      return true;
    }
  }
  return false;
}
