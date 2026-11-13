/** Instantiate only where Hit's concrete context inheritance is complete. */
template <class Hit, PressRow::Kind KIND>
void ToolboxPressRows<Hit, KIND>::retire(const loka::app::scene::NodeContext *context,
                                         ToolboxPressLedger &ledger,
                                         ToolboxReleasedStates &released)
{
  for (std::size_t i = 0; i < this->rows_.size();)
  {
    if (this->rows_[i].context == context)
    {
      Hit::release(this->rows_[i], released);
      ledger.forget(KIND, i);
      this->rows_.erase(this->rows_.begin() + i);
    }
    else
      ++i;
  }
}
