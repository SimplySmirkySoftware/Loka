#include "app/url/URLInbox.hpp"

void capacityPin()
{
  loka::app::URLInbox<0> owner("myapp");
  (void)owner;
}
