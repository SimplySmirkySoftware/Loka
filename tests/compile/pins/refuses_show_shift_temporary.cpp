#include "app/nodes/nestable/Show.hpp"
#include "app/nodes/Text.hpp"

int main()
{
  const loka::core::State<bool> visible(true);
  (void)visible;
  loka::core::State<bool>(true) << loka::app::Text("x");
  return 0;
}
