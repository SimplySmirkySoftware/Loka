#include "app/core/WindowSeat.hpp"

struct Document
{
};
WindowProps factory(loka::core::ItemId, const Document &, void *)
{
  return WindowProps();
}
void declare(loka::app::DocumentRoster<Document, 4> &roster)
{
  (void)loka::app::DocumentWindows(roster, &factory, 0);
}
