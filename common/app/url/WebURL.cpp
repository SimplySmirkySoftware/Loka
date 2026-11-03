#include "app/url/WebURL.hpp"

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

      const std::size_t schemeLength = internal::urlSchemeLength(bytes, length);
      if (schemeLength != 4 && schemeLength != 5 && schemeLength != 6)
        return false;
      if (schemeLength == length)
        return false;

      const char *scheme = schemeLength == 6 ? "mailto" : (schemeLength == 5 ? "https" : "http");
      for (std::size_t i = 0; i < schemeLength; ++i)
      {
        if (internal::asciiLower(bytes[i]) != scheme[i])
          return false;
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

      if (!internal::isLexicalURLText(bytes, length))
        return false;
      return internal::lowercasedSchemeText(text, bytes, length, schemeLength, out.text_);
    }
  } // namespace app
} // namespace loka
