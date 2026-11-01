#include "app/url/WebURL.hpp"

#include <cstring>

namespace
{
  char asciiLower(char byte)
  {
    return byte >= 'A' && byte <= 'Z' ? static_cast<char>(byte + ('a' - 'A')) : byte;
  }

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
    bool WebURL::parse(const core::String &text, WebURL &out)
    {
      core::StringBuffer buffer = text.bufferWithEncoding(core::StringEncodingUtf8);
      const std::size_t length = buffer.length();
      if (length == 0 || length > MAX_TEXT_BYTES)
        return false;
      char *bytes = static_cast<char *>(buffer.data());
      if (!bytes)
        return false;

      std::size_t schemeLength = 0;
      while (schemeLength < length && bytes[schemeLength] != ':')
        ++schemeLength;
      if (schemeLength != 4 && schemeLength != 5 && schemeLength != 6)
        return false;
      if (schemeLength == length)
        return false;

      const char *scheme = schemeLength == 6 ? "mailto" : (schemeLength == 5 ? "https" : "http");
      bool lowercase = true;
      for (std::size_t i = 0; i < schemeLength; ++i)
      {
        if (asciiLower(bytes[i]) != scheme[i])
          return false;
        if (bytes[i] != scheme[i])
          lowercase = false;
      }

      const std::size_t rest = schemeLength + 1;
      if (rest == length)
        return false;
      if (schemeLength != 6)
      {
        if (length - rest < 3 || bytes[rest] != '/' || bytes[rest + 1] != '/')
          return false;
        const char authorityFirst = bytes[rest + 2];
        if (authorityFirst == '/' || authorityFirst == '?' || authorityFirst == '#')
          return false;
      }

      for (std::size_t i = 0; i < length; ++i)
      {
        if (!isURLByte(bytes[i]))
          return false;
        if (bytes[i] == '%' && (length - i < 3 || !isHex(bytes[i + 1]) || !isHex(bytes[i + 2])))
          return false;
      }

      if (lowercase)
        out.text_ = text;
      else
      {
        for (std::size_t i = 0; i < schemeLength; ++i)
          bytes[i] = scheme[i];
        const core::String normalized = core::String::Utf8(bytes, length);
        if (normalized.empty())
          return false;
        out.text_ = normalized;
      }
      return true;
    }
  } // namespace app
} // namespace loka
