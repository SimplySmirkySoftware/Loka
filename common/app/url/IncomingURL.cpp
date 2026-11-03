#include "app/url/IncomingURL.hpp"

namespace loka
{
  namespace app
  {
    bool IncomingURL::parse(const core::String &text, const URLScheme &scheme, IncomingURL &out)
    {
      if (scheme.isNone())
        return false;
      core::StringBuffer buffer = text.bufferWithEncoding(core::StringEncodingUtf8);
      const std::size_t length = buffer.length();
      if (length == 0 || length > MAX_TEXT_BYTES)
        return false;
      char *bytes = static_cast<char *>(buffer.data());
      if (!bytes)
        return false;
      const std::size_t schemeLength = internal::urlSchemeLength(bytes, length);
      if (schemeLength == length || schemeLength == 0)
        return false;
      if (schemeLength + 1 == length)
        return false;
      if (!scheme.matchesIgnoringCase(bytes, schemeLength))
        return false;
      if (!internal::isLexicalURLText(bytes, length))
        return false;
      return internal::lowercasedSchemeText(text, bytes, length, schemeLength, out.text_);
    }

    core::String IncomingURL::rest() const
    {
      if (this->isNone())
        return core::String();
      core::StringBuffer buffer = this->text_.bufferWithEncoding(core::StringEncodingUtf8);
      const char *bytes = static_cast<const char *>(buffer.data());
      const std::size_t length = buffer.length();
      if (!bytes)
        return core::String();
      const std::size_t schemeLength = internal::urlSchemeLength(bytes, length);
      if (schemeLength == length)
        return core::String();
      return core::String::Utf8(bytes + schemeLength + 1, length - schemeLength - 1);
    }

    bool IncomingURL::restStartsWith(const char *prefix) const
    {
      if (!prefix || this->isNone())
        return false;
      core::StringBuffer buffer = this->text_.bufferWithEncoding(core::StringEncodingUtf8);
      const char *bytes = static_cast<const char *>(buffer.data());
      const std::size_t length = buffer.length();
      if (!bytes)
        return false;
      const std::size_t schemeLength = internal::urlSchemeLength(bytes, length);
      if (schemeLength == length)
        return false;
      const std::size_t rest = schemeLength + 1;
      for (std::size_t i = 0; prefix[i]; ++i)
      {
        if (i >= length - rest || bytes[rest + i] != prefix[i])
          return false;
      }
      return true;
    }
  } // namespace app
} // namespace loka
