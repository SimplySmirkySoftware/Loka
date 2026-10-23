#ifndef LOKA_APP_UTF8_SCAN_HPP
#define LOKA_APP_UTF8_SCAN_HPP
#include "core/Utf8Decode.hpp"
#include <cassert>
namespace loka
{
  namespace app
  {
    /** Strict UTF-8, including NUL. On refusal, count contains complete scalars before failure. */
    inline bool Utf8Scan(const char *bytes, std::size_t length, std::size_t &codePoints)
    {
      codePoints = 0;
      if (!bytes && length)
        return false;
      std::size_t index = 0;
      unsigned int scalar = 0;
      while (index < length)
      {
        if (!core::detail::DecodeUtf8Codepoint(bytes, length, index, scalar))
          return false;
        ++codePoints;
      }
      return true;
    }
    /** Requires well-formed UTF-8. Endpoints and scalar starts are boundaries. */
    inline bool Utf8ColumnOnBoundary(const char *bytes, std::size_t length, std::size_t column)
    {
      return column <= length
             && (column == 0 || column == length || (static_cast<unsigned char>(bytes[column]) & 0xC0) != 0x80);
    }
    /** Requires well-formed UTF-8 and an in-range column; retreats at most three bytes. */
    inline std::size_t Utf8RetreatToBoundary(const char *bytes, std::size_t length, std::size_t column)
    {
      assert(column <= length);
      while (!Utf8ColumnOnBoundary(bytes, length, column))
        --column;
      return column;
    }
  } // namespace app
} // namespace loka
#endif
