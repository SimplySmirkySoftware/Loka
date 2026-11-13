#include "toolbox_press_rows_fixture.hpp"
struct BadHit
{
  loka::app::scene::NodeContext *context;
  void release(const BadHit &, ToolboxReleasedStates &) {}
};
void refuses()
{
  ToolboxPressStoreChain chain;
  ToolboxPressRows<BadHit, PressRow::BUTTON_HIT> rows(chain);
}
