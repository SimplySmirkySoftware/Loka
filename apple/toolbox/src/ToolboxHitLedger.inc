/** Rail and host include this after all concrete hit context definitions. */
#include "context/ToolboxAttributedTextContext.hpp"
#include "ToolboxPressRows.inc"

void ToolboxScenePlatformController::recordLinkHit(const Rect &rect, ToolboxAttributedTextContext *context)
{
  Rect clipped;
  if (!this->intersectWithProjectionClip(rect, clipped))
    return;
  LinkHit hit;
  hit.rect = clipped;
  hit.context = context;
  this->hitLedger_.linkHits_.record(hit, this->pressLedger_);
}

ToolboxHitLedger::ToolboxHitLedger()
    : buttonHits_(this->chain_),
      cellHits_(this->chain_),
      popupHits_(this->chain_),
      editHits_(this->chain_),
      linkHits_(this->chain_)
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
