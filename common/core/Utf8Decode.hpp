#ifndef LOKA_CORE_UTF8_DECODE_HPP
#define LOKA_CORE_UTF8_DECODE_HPP
#include <cstddef>
namespace loka
{
  namespace core
  {
    namespace detail
    {
      /** Decode one strict UTF-8 scalar; failure leaves index unchanged. */
      inline bool
      DecodeUtf8Codepoint(const char *bytes, std::size_t length, std::size_t &index, unsigned int &codepoint)
      {
        if (!bytes || index >= length)
          return false;
        unsigned char lead = static_cast<unsigned char>(bytes[index]);
        if (lead < 0x80)
        {
          codepoint = lead;
          ++index;
          return true;
        }
        if ((lead & 0xE0) == 0xC0)
        {
          if (length - index < 2)
            return false;
          unsigned char c1 = static_cast<unsigned char>(bytes[index + 1]);
          if ((c1 & 0xC0) != 0x80)
            return false;
          codepoint = ((lead & 0x1F) << 6) | (c1 & 0x3F);
          if (codepoint < 0x80)
            return false;
          index += 2;
          return true;
        }
        if ((lead & 0xF0) == 0xE0)
        {
          if (length - index < 3)
            return false;
          unsigned char c1 = static_cast<unsigned char>(bytes[index + 1]);
          unsigned char c2 = static_cast<unsigned char>(bytes[index + 2]);
          if ((c1 & 0xC0) != 0x80 || (c2 & 0xC0) != 0x80)
            return false;
          codepoint = ((lead & 0x0F) << 12) | ((c1 & 0x3F) << 6) | (c2 & 0x3F);
          if (codepoint < 0x800)
            return false;
          if (codepoint >= 0xD800 && codepoint <= 0xDFFF)
            return false;
          index += 3;
          return true;
        }
        if ((lead & 0xF8) == 0xF0)
        {
          if (length - index < 4)
            return false;
          unsigned char c1 = static_cast<unsigned char>(bytes[index + 1]);
          unsigned char c2 = static_cast<unsigned char>(bytes[index + 2]);
          unsigned char c3 = static_cast<unsigned char>(bytes[index + 3]);
          if ((c1 & 0xC0) != 0x80 || (c2 & 0xC0) != 0x80 || (c3 & 0xC0) != 0x80)
            return false;
          codepoint = ((lead & 0x07) << 18) | ((c1 & 0x3F) << 12) | ((c2 & 0x3F) << 6) | (c3 & 0x3F);
          if (codepoint < 0x10000 || codepoint > 0x10FFFF)
            return false;
          index += 4;
          return true;
        }
        return false;
      }

    } // namespace detail
  } // namespace core
} // namespace loka
#endif
