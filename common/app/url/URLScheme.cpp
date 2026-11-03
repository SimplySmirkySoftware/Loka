#include "app/url/URLScheme.hpp"
#include "app/url/URLLexical.hpp"

namespace
{
  bool isAlpha(char byte)
  {
    return (byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z');
  }
} // namespace

namespace loka
{
  namespace app
  {
    bool URLScheme::parse(const char *name, URLScheme &out)
    {
      if (!name || !isAlpha(name[0]))
        return false;
      std::size_t length = 0;
      while (name[length])
      {
        const char byte = name[length];
        if (!isAlpha(byte) && !(byte >= '0' && byte <= '9') && byte != '+' && byte != '-' && byte != '.')
          return false;
        ++length;
      }
      const core::String text = core::String::Utf8(name, length);
      core::StringBuffer buffer = text.bufferWithEncoding(core::StringEncodingUtf8);
      char *bytes = static_cast<char *>(buffer.data());
      if (!bytes || buffer.length() != length)
        return false;
      return internal::lowercasedSchemeText(text, bytes, length, length, out.name_);
    }

    bool URLScheme::matchesIgnoringCase(const char *bytes, std::size_t length) const
    {
      if (this->isNone())
        return false;
      core::StringBuffer buffer = this->name_.bufferWithEncoding(core::StringEncodingUtf8);
      const char *name = static_cast<const char *>(buffer.data());
      if (!name || buffer.length() != length)
        return false;
      for (std::size_t i = 0; i < length; ++i)
      {
        if (internal::asciiLower(bytes[i]) != name[i])
          return false;
      }
      return true;
    }
  } // namespace app
} // namespace loka
