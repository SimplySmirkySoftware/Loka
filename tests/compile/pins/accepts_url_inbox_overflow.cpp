#include "app/url/URLInbox.hpp"

void capacityPin()
{
  loka::app::URLInbox<255> owner("myapp");
  (void)owner;
}
