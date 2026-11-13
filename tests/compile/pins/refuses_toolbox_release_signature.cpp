#include "toolbox_press_rows_fixture.hpp"
struct BadHit
{
  loka::app::scene::NodeContext *context;
  static int release(const BadHit &, ToolboxReleasedStates &)
  {
    return 0;
  }
};
void refuses()
{
  ToolboxPressStoreChain chain;
  ToolboxPressRows<BadHit, PressRow::BUTTON_HIT> rows(chain);
}
