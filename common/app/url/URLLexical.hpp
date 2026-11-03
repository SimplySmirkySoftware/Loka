#ifndef LOKA_APP_URL_URL_LEXICAL_HPP
#define LOKA_APP_URL_URL_LEXICAL_HPP

#include "core/String.hpp"
#include <cstddef>

namespace loka
{
  namespace app
  {
    namespace internal
    {
      enum
      {
        URL_MAX_TEXT_BYTES = 2048
      };

      /** ASCII-lowercases one byte; every other byte is returned unchanged. */
      inline char asciiLower(char byte)
      {
        return byte >= 'A' && byte <= 'Z' ? static_cast<char>(byte + ('a' - 'A')) : byte;
      }
      /** Checks the bounded RFC 3986 byte set and percent triplets; bytes covers length bytes. */
      bool isLexicalURLText(const char *bytes, std::size_t length);
      /** Returns the first colon's index, or length when absent. */
      std::size_t urlSchemeLength(const char *bytes, std::size_t length);
      /** Lowercases bytes before schemeLength; allocation refusal leaves out untouched. */
      bool lowercasedSchemeText(
          const core::String &text, char *bytes, std::size_t length, std::size_t schemeLength, core::String &out);
    } // namespace internal
  } // namespace app
} // namespace loka

#endif // LOKA_APP_URL_URL_LEXICAL_HPP
