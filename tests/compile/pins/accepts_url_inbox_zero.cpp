#include "app/url/URLInbox.hpp"

void capacityPin()
{
  loka::app::URLInbox<1> owner("myapp");
  (void)owner;
}
