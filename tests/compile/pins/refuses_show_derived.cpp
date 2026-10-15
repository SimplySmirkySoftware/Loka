#include "app/nodes/nestable/Show.hpp"
#include "app/nodes/Text.hpp"

// A State of another type is not a boolean condition, even as an lvalue.
int main()
{
  loka::core::DerivedState<int> visible(0, 0);
  const loka::core::DerivedState<int> &constant = visible;
  loka::app::Show(visible);
  (void)constant;
  return 0;
}
