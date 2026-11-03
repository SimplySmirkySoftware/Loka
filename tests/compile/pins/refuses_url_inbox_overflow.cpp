#include "app/url/URLInbox.hpp"

void capacityPin()
{
  loka::app::URLInbox<256> owner("myapp");
  (void)owner;
}
