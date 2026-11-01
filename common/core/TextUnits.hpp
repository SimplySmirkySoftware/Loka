#ifndef LOKA_CORE_TEXT_UNITS_HPP
#define LOKA_CORE_TEXT_UNITS_HPP
#include "core/Utf8Decode.hpp"
#include <cassert>
#include <cstddef>

namespace loka
{
  namespace core
  {
    // Formula home for Win32 LogicalRow / TextEditorFromWide (Win32EditTextBridge.hpp)
    // and macOS NativeRow (MacTextEditorContext.mm); their delegation is a follow-up.

    /** Decode strict UTF-8 without delimiter normalization. Refuse malformed input
        or insufficient capacity, setting written to zero; a partial prefix may
        remain. Nothing is written past capacity. out may be null only at zero capacity. */
    inline bool
    Utf8ToUtf16(const char *bytes, std::size_t length, unsigned short *out, std::size_t capacity, std::size_t &written)
    {
      assert(out || capacity == 0);
      written = 0;
      std::size_t index = 0, used = 0;
      while (index < length)
      {
        unsigned int scalar = 0;
        if (!detail::DecodeUtf8Codepoint(bytes, length, index, scalar))
          return false;
        const std::size_t width = scalar > 0xFFFF ? 2 : 1;
        if (width > capacity - used)
          return false;
        if (width == 1)
          out[used++] = static_cast<unsigned short>(scalar);
        else
        {
          scalar -= 0x10000;
          out[used++] = static_cast<unsigned short>(0xD800 | (scalar >> 10));
          out[used++] = static_cast<unsigned short>(0xDC00 | (scalar & 0x3FF));
        }
      }
      written = used;
      return true;
    }

    /** Encode UTF-16, normalizing LF and CRLF to CR. Refuse NUL, lone surrogates,
        null nonempty input, or insufficient capacity; written is zero on refusal.
        Nothing is written past capacity; output and offsets may hold partial work
        on refusal. out may be null only at zero capacity. At most four native unit
        offsets (each <= count) are rewritten to normalized UNIT offsets, not bytes:
        subtract swallowed LFs strictly before each offset. offsets may be null when
        offsetCount is zero. The caller maps row units to bytes with Utf8BytesFloor. */
    inline bool Utf16ToUtf8(const unsigned short *units,
                            std::size_t count,
                            char *out,
                            std::size_t capacity,
                            std::size_t &written,
                            std::size_t *offsets,
                            std::size_t offsetCount)
    {
      assert(out || capacity == 0);
      assert(offsetCount <= 4);
      assert(offsets || offsetCount == 0);
      std::size_t nativeOffsets[4];
      for (std::size_t n = 0; n < offsetCount; ++n)
      {
        assert(offsets[n] <= count);
        nativeOffsets[n] = offsets[n];
      }
      written = 0;
      if (!units && count)
        return false;
      std::size_t used = 0;
      for (std::size_t i = 0; i < count; ++i)
      {
        unsigned int scalar = units[i];
        if (!scalar || (scalar >= 0xDC00 && scalar <= 0xDFFF))
          return false;
        if (scalar >= 0xD800 && scalar <= 0xDBFF)
        {
          if (i + 1 == count || units[i + 1] < 0xDC00 || units[i + 1] > 0xDFFF)
            return false;
          scalar = 0x10000 + ((scalar - 0xD800) << 10) + (units[++i] - 0xDC00);
        }
        else if (scalar == '\r' && i + 1 < count && units[i + 1] == '\n')
        {
          ++i;
          for (std::size_t n = 0; n < offsetCount; ++n)
            if (i < nativeOffsets[n])
              --offsets[n];
        }
        else if (scalar == '\n')
          scalar = '\r';
        const std::size_t width = scalar < 0x80 ? 1 : scalar < 0x800 ? 2 : scalar < 0x10000 ? 3 : 4;
        if (width > capacity - used)
          return false;
        if (width == 1)
          out[used++] = static_cast<char>(scalar);
        else
        {
          const unsigned int lead = width == 2 ? 0xC0 : width == 3 ? 0xE0 : 0xF0;
          out[used++] = static_cast<char>(lead | (scalar >> (6 * (width - 1))));
          for (std::size_t remaining = width - 1; remaining > 0; --remaining)
            out[used++] = static_cast<char>(0x80 | ((scalar >> (6 * (remaining - 1))) & 0x3F));
        }
      }
      written = used;
      return true;
    }

    /** Count UTF-16 units before a scalar-boundary byte column (asserted using
        the decoder's reached offset). Malformed UTF-8 refuses with -1. No buffer
        is written, so no capacity can be exceeded. The row's unit count must fit int. */
    inline int Utf16UnitsAt(const char *bytes, std::size_t length, std::size_t column)
    {
      assert(column <= length);
      std::size_t index = 0;
      int units = 0;
      while (index < column)
      {
        unsigned int scalar = 0;
        if (!detail::DecodeUtf8Codepoint(bytes, length, index, scalar))
          return -1;
        units += scalar > 0xFFFF ? 2 : 1;
      }
      assert(index == column);
      return units;
    }

    /** Return the byte boundary at or before a UTF-16 unit offset. A position
        inside a surrogate pair floors to its start; beyond the row returns length.
        Malformed UTF-8 stops at the last decoded boundary; nonpositive units return
        zero. No buffer is written, so no capacity can be exceeded. */
    inline std::size_t Utf8BytesFloor(const char *bytes, std::size_t length, int units)
    {
      std::size_t index = 0;
      while (index < length && units > 0)
      {
        const std::size_t start = index;
        unsigned int scalar = 0;
        if (!detail::DecodeUtf8Codepoint(bytes, length, index, scalar))
          return start;
        const int width = scalar > 0xFFFF ? 2 : 1;
        if (units < width)
          return start;
        units -= width;
      }
      return index;
    }
  } // namespace core
} // namespace loka
#endif
