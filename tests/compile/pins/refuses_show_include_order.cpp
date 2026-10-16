#include "app/nodes/nestable/Show.hpp"
#include "app/scene/state/RequestSettlement.hpp"
#include "app/nodes/Text.hpp"

// Before #1359 an unsigned left operand converted to State<bool> and reached
// Show's operator<<; that conversion also made `1u << FollowUp` ambiguous.
int main()
{
  using namespace loka::app::scene;
  unsigned bits = 1u;
  (void)(bits << loka::app::Text("x"));
  return 0;
}
