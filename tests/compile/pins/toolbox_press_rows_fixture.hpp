#include "ToolboxHitLedger.hpp"
#include "ToolboxPressRows.inc"

struct PressRowsPinHit
{
  loka::app::scene::NodeContext *context;
  static void release(const PressRowsPinHit &, ToolboxReleasedStates &) {}
};
typedef ToolboxPressRows<PressRowsPinHit, PressRow::BUTTON_HIT> PressRowsPinStore;
