#include "app/nodes/nestable/Show.hpp"
#include "app/nodes/Text.hpp"

int main()
{
  loka::core::DerivedState<bool> visible(0, 0);
  const loka::core::DerivedState<bool> &constant = visible;
  loka::app::Show(visible);
  loka::app::Show(constant);
  visible << loka::app::Text("x");
  constant << loka::app::Text("x");
  return 0;
}
