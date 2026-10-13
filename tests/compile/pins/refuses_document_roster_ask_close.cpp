#include "app/core/DocumentRoster.hpp"

void ask(loka::app::DocumentRosterBase &roster, loka::core::ItemId id)
{
  roster.askClose(id);
}
