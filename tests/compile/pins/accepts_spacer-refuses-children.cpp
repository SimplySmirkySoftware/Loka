#include "app/nodes/nestable/Box.hpp"
#include "app/nodes/nestable/RowColumn.hpp"
#include "app/nodes/nestable/Show.hpp"
#include "app/nodes/controls/Button.hpp"

void spacerDeclarationPin()
{
  using namespace loka::app;
  Box() << Button("x");
  HStack() << Spacer();
  VStack() << Spacer();
  Row() << Spacer();
  loka::core::MutableState<bool> shown(true);
  Show(shown) << Spacer();
}
