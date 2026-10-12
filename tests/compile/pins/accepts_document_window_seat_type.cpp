#include "app/core/WindowSeat.hpp"

struct Document : loka::app::DocumentFacts
{
};
// Complete the seat type without constructing it or instantiating its virtuals.
unsigned long seatSize()
{
  return sizeof(loka::app::DocumentWindowSeat<Document, 4>);
}
