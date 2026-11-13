/** Rail and host include this after all concrete hit context definitions. */
#include "ToolboxPressRows.inc"

ToolboxHitLedger::ToolboxHitLedger()
    : buttonHits_(this->chain_),
      cellHits_(this->chain_),
      popupHits_(this->chain_),
      editHits_(this->chain_)
{
}
ToolboxHitLedger::~ToolboxHitLedger() {}
void ToolboxHitLedger::retirePressRows(const loka::app::scene::NodeContext *context,
                                       ToolboxPressLedger &ledger,
                                       ToolboxReleasedStates &released)
{
  this->chain_.retire(context, ledger, released);
}
void ToolboxHitLedger::clearPressRows()
{
  this->chain_.clear();
}
