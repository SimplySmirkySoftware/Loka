#include "toolbox_press_rows_fixture.hpp"
void acceptsToolboxPressRows()
{
  ToolboxPressStoreChain chain;
  PressRowsPinStore rows(chain);
  ToolboxHitLedger ledger;
  (void)rows;
  (void)ledger;
}
