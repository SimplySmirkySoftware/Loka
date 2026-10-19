/** Shared synchronous row removal inside retireNodeContext on rail and host. */
if (context)
{
  std::vector<loka::core::State<loka::core::String> *> retiredTextStates;
  std::vector<loka::core::State<bool> *> retiredEnabledStates;

  this->retireEditTextControl(context, lifetimeHint);
  assert(!editControls_.contains(context)
         && "detach must strip the context's native edit binding before context reclaim");

  for (size_t i = 0; i < hitLedger_.buttonHits_.size();)
  {
    if (static_cast<loka::app::scene::NodeContext *>(hitLedger_.buttonHits_[i].context) == context)
    {
      retiredEnabledStates.push_back(hitLedger_.buttonHits_[i].enabled);
      this->pressLedger_.forget(PressRow::BUTTON_HIT, i);
      hitLedger_.buttonHits_.erase(hitLedger_.buttonHits_.begin() + i);
    }
    else
    {
      ++i;
    }
  }
  for (size_t i = 0; i < hitLedger_.cellHits_.size();)
  {
    if (static_cast<loka::app::scene::NodeContext *>(hitLedger_.cellHits_[i].context) == context)
    {
      retiredTextStates.push_back(hitLedger_.cellHits_[i].text);
      this->pressLedger_.forget(PressRow::CELL_HIT, i);
      hitLedger_.cellHits_.erase(hitLedger_.cellHits_.begin() + i);
    }
    else
    {
      ++i;
    }
  }
  for (size_t i = 0; i < hitLedger_.popupHits_.size();)
  {
    if (static_cast<loka::app::scene::NodeContext *>(hitLedger_.popupHits_[i].context) == context)
    {
      retiredEnabledStates.push_back(hitLedger_.popupHits_[i].enabled);
      this->pressLedger_.forget(PressRow::POPUP_HIT, i);
      hitLedger_.popupHits_.erase(hitLedger_.popupHits_.begin() + i);
    }
    else
    {
      ++i;
    }
  }

  for (size_t i = 0; i < hitLedger_.editHits_.size();)
  {
    if (hitLedger_.editHits_[i].context == context)
    {
      loka::core::State<loka::core::String> *text = hitLedger_.editHits_[i].text;
      retiredTextStates.push_back(text);
      this->pressLedger_.forget(PressRow::EDIT_HIT, i);
      hitLedger_.editHits_.erase(hitLedger_.editHits_.begin() + i);
    }
    else
      ++i;
  }
  for (size_t i = 0; i < hitLedger_.textHits_.size();)
  {
    if (hitLedger_.textHits_[i].context == context)
    {
      retiredTextStates.push_back(hitLedger_.textHits_[i].text);
      hitLedger_.textHits_.erase(hitLedger_.textHits_.begin() + i);
    }
    else
      ++i;
  }

  for (size_t i = 0; i < retiredTextStates.size(); ++i)
  {
    loka::core::State<loka::core::String> *text = retiredTextStates[i];
    if (text && !this->hasLiveBinding(text))
    {
      this->unbindTextState(text);
    }
  }
  for (size_t i = 0; i < retiredEnabledStates.size(); ++i)
  {
    loka::core::State<bool> *enabled = retiredEnabledStates[i];
    if (enabled && !this->hasLiveBinding(enabled))
    {
      this->unbindEnabledState(enabled);
    }
  }
}
