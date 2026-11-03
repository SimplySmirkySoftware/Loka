#include "app/url/URLLexical.hpp"

#include <cstring>

namespace
{
  bool isHex(char byte)
  {
    return (byte >= '0' && byte <= '9') || (byte >= 'A' && byte <= 'F') || (byte >= 'a' && byte <= 'f');
  }

  bool isURLByte(char byte)
  {
    return (byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z') || (byte >= '0' && byte <= '9')
           || (byte != '\0' && std::strchr("-._~:/?#[]@!$&'()*+,;=%", byte) != 0);
  }
} // namespace

namespace loka
{
  namespace app
  {
    namespace internal
    {
      bool isLexicalURLText(const char *bytes, std::size_t length)
      {
        if (length == 0 || length > URL_MAX_TEXT_BYTES)
          return false;
        for (std::size_t i = 0; i < length; ++i)
        {
          if (!isURLByte(bytes[i]))
            return false;
          if (bytes[i] == '%' && (length - i < 3 || !isHex(bytes[i + 1]) || !isHex(bytes[i + 2])))
            return false;
        }
        return true;
      }

      std::size_t urlSchemeLength(const char *bytes, std::size_t length)
      {
        std::size_t schemeLength = 0;
        while (schemeLength < length && bytes[schemeLength] != ':')
          ++schemeLength;
        return schemeLength;
      }

      bool lowercasedSchemeText(
          const core::String &text, char *bytes, std::size_t length, std::size_t schemeLength, core::String &out)
      {
        bool lowercase = true;
        for (std::size_t i = 0; i < schemeLength; ++i)
        {
          const char lower = asciiLower(bytes[i]);
          if (lower != bytes[i])
          {
            bytes[i] = lower;
            lowercase = false;
          }
        }
        if (lowercase)
          out = text;
        else
        {
          const core::String normalized = core::String::Utf8(bytes, length);
          if (normalized.empty())
            return false;
          out = normalized;
        }
        return true;
      }
    } // namespace internal
  } // namespace app
} // namespace loka
