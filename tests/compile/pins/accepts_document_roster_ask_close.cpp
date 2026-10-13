#include "app/core/DocumentRoster.hpp"

void ask(loka::app::DocumentRosterBase &roster, loka::core::ItemId)
{
  (void)roster.closeAsked();
}
