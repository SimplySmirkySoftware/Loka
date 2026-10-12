#include "app/core/WindowSeat.hpp"

struct Document
{
};
// Complete the seat type without constructing it or instantiating its virtuals.
unsigned long seatSize()
{
  return sizeof(loka::app::DocumentWindowSeat<Document, 4>);
}
