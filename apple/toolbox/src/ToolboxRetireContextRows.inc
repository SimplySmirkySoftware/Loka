/** Shared synchronous row removal inside retireNodeContext on rail and host. */
if (context)
{
  ToolboxReleasedStates released;

  this->retireEditTextControl(context, lifetimeHint);
  assert(!editControls_.contains(context)
         && "detach must strip the context's native edit binding before context reclaim");

  this->hitLedger_.retirePressRows(context, this->pressLedger_, released);
  for (size_t i = 0; i < hitLedger_.textHits_.size();)
  {
    if (hitLedger_.textHits_[i].context == context)
    {
      released.texts.push_back(hitLedger_.textHits_[i].text);
      hitLedger_.textHits_.erase(hitLedger_.textHits_.begin() + i);
    }
    else
      ++i;
  }

  for (size_t i = 0; i < released.texts.size(); ++i)
  {
    loka::core::State<loka::core::String> *text = released.texts[i];
    if (text && !this->hasLiveBinding(text))
    {
      this->unbindTextState(text);
    }
  }
  for (size_t i = 0; i < released.enableds.size(); ++i)
  {
    loka::core::State<bool> *enabled = released.enableds[i];
    if (enabled && !this->hasLiveBinding(enabled))
    {
      this->unbindEnabledState(enabled);
    }
  }
}
