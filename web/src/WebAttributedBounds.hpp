#ifndef LOKA_WEB_ATTRIBUTED_BOUNDS_HPP
#define LOKA_WEB_ATTRIBUTED_BOUNDS_HPP
#include <cstddef>
#include <stdint.h>
#include <limits>
/** Internal producer arithmetic, also used for allocation-free boundary pins. */
namespace web_attributed
{
  typedef char WireWordMustBe32Bits[sizeof(int) == 4 ? 1 : -1];
  /** Cost: builder per append; validates its aggregate before addition, O(1). */
  inline bool add(std::size_t left, std::size_t right, std::size_t &out)
  {
    if (left > INT32_MAX || right > INT32_MAX - left)
      return false;
    out = left + right;
    return true;
  }
  /** Cost: builder per table allocation; validates its own allocation bytes, O(1). */
  inline bool allocation(std::size_t count, std::size_t size)
  {
    return count <= INT32_MAX && size && count <= (std::numeric_limits<std::size_t>::max)() / size;
  }
  /** Cost: serialization once per build; checks own header/table counts before multiplication. */
  inline bool wordCount(std::size_t fragments, std::size_t descriptors, std::size_t &out)
  {
    if (fragments > (INT32_MAX - 3) / 7)
      return false;
    const std::size_t prefix = 3 + 7 * fragments;
    if (descriptors > (INT32_MAX - prefix) / 9)
      return false;
    out = prefix + 9 * descriptors;
    return true;
  }
} // namespace web_attributed
#endif
