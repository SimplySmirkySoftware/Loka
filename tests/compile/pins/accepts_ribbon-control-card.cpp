#include "app/nodes/controls/Ribbon.hpp"
#include "app/nodes/controls/Card.hpp"
void cardPin()
{
  loka::app::Card() << (loka::app::RibbonControl() << loka::app::RibbonItem("x"));
}
