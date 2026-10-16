#include "app/nodes/nestable/Show.hpp"
#include "app/nodes/Text.hpp"

int main()
{
  loka::core::MutableState<bool> visible(true);
  (void)visible;
  visible << loka::app::Text("x");
  return 0;
}
