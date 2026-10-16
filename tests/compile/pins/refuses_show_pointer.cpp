#include "app/nodes/nestable/Show.hpp"

int main()
{
  loka::core::State<bool> visible(true);
  (void)visible;
  loka::app::Show(&visible);
  return 0;
}
