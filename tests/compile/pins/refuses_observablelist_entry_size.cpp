#include "core/ObservableList.hpp"
#include <climits>
#include <cstddef>
struct PinValue
{
  // One Entry larger than std::size_t / USHRT_MAX: USHRT_MAX of them would
  // overflow the allocation size, so the buffer refuses the type.
  char bytes[static_cast<std::size_t>(-1) / USHRT_MAX + 1];
};
// sizeof instantiates the class, which evaluates its size wall.
std::size_t entryBufferSize()
{
  return sizeof(loka::core::list_detail::EntryBuffer<PinValue>);
}
