#ifndef LOKA_APP_TEXT_CAPACITY_HPP
#define LOKA_APP_TEXT_CAPACITY_HPP
#include <cstddef>
namespace loka
{
  namespace app
  {
    /** Uniform admitted rows and UTF-8 bytes, including normalized separators. */
    struct TextCapacity
    {
      unsigned short lines;
      std::size_t bytes;
      static TextCapacity of(unsigned short lines, std::size_t bytes)
      {
        TextCapacity value = {lines, bytes};
        return value;
      }
      TextCapacity clampedTo(const TextCapacity &maximum) const
      {
        return of(this->lines < maximum.lines ? this->lines : maximum.lines,
                  this->bytes < maximum.bytes ? this->bytes : maximum.bytes);
      }
      bool fitsIn(const TextCapacity &maximum) const
      {
        return this->lines <= maximum.lines && this->bytes <= maximum.bytes;
      }
    };
  } // namespace app
} // namespace loka
#endif
