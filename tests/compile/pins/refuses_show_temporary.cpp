#include "app/nodes/nestable/Show.hpp"

int main()
{
  const loka::core::State<bool> visible(true);
  (void)visible;
  loka::app::Show(loka::core::State<bool>(true));
  return 0;
}
