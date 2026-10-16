#include "app/nodes/nestable/Show.hpp"
#include "app/nodes/Text.hpp"

int main()
{
  const loka::core::State<bool> visible(true);
  (void)visible;
  visible << loka::app::Text("x");
  return 0;
}
