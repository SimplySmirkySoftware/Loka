#include "core/ObservableList.hpp"
#include <climits>
#include <cstddef>
struct PinValue
{
  // Same shape as the refusing twin, with a size that fits.
  char bytes[4];
};
// sizeof instantiates the class, which evaluates its size wall.
std::size_t entryBufferSize()
{
  return sizeof(loka::core::list_detail::EntryBuffer<PinValue>);
}
