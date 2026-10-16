#include "app/nodes/nestable/Show.hpp"
#include "app/scene/state/RequestSettlement.hpp"
#include "app/nodes/Text.hpp"

int main()
{
  using namespace loka::app::scene;
  unsigned bits = 1u;
  (void)bits;
  return FollowUps().including(REPAINT).contains(REPAINT) ? 0 : 1;
}
