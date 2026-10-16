#include "app/nodes/nestable/Show.hpp"

struct Convertible
{
  operator loka::core::State<bool>() const;
};

int main()
{
  loka::core::State<bool> visible(true);
  Convertible convertible;
  (void)convertible;
  (void)visible;
  loka::app::Show(visible);
  return 0;
}
