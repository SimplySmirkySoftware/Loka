#include "platform/ToolboxHfsName.hpp"

#include <string>

#include "platform/StringUTF8.hpp"
#include "platform/ToolboxMacRoman.hpp"

namespace loka
{
  namespace toolbox
  {
    bool CopyApplicationRelativeToHfsPath(const loka::core::String &value, Str63 out)
    {
      out[0] = 0;
      std::string relative;
      if (!loka::platform::CollectUtf8(value, relative))
        return false;
      std::size_t start = 0;
      do
      {
        const std::size_t separator = relative.find(':', start);
        const std::size_t end = separator == std::string::npos ? relative.size() : separator;
        Str63 component;
        if (!CopyStringToHfsName(loka::core::String::Utf8(relative.data() + start, end - start), component)
            || out[0] + 1u + component[0] > 63u)
        {
          out[0] = 0;
          return false;
        }
        out[++out[0]] = ':';
        for (unsigned int i = 1; i <= component[0]; ++i)
          out[++out[0]] = component[i];
        if (separator == std::string::npos)
          return true;
        start = separator + 1;
      } while (start <= relative.size());
      return false;
    }

    bool CopyStringToHfsName(const loka::core::String &value, Str63 out)
    {
      out[0] = 0;
      std::string utf8;
      if (!loka::platform::CollectUtf8(value, utf8))
      {
        return false;
      }

      std::size_t input = 0;
      unsigned char outputLength = 0;
      while (input < utf8.size())
      {
        const unsigned char first = static_cast<unsigned char>(utf8[input]);
        unsigned long codepoint = 0;
        std::size_t continuationCount = 0;
        unsigned long minimum = 0;
        if (first < 0x80)
        {
          codepoint = first;
        }
        else if ((first & 0xE0) == 0xC0)
        {
          codepoint = first & 0x1F;
          continuationCount = 1;
          minimum = 0x80;
        }
        else if ((first & 0xF0) == 0xE0)
        {
          codepoint = first & 0x0F;
          continuationCount = 2;
          minimum = 0x800;
        }
        else if ((first & 0xF8) == 0xF0)
        {
          codepoint = first & 0x07;
          continuationCount = 3;
          minimum = 0x10000;
        }
        else
        {
          return false;
        }

        if (input + continuationCount >= utf8.size())
        {
          return false;
        }
        for (std::size_t i = 0; i < continuationCount; ++i)
        {
          const unsigned char next = static_cast<unsigned char>(utf8[input + i + 1]);
          if ((next & 0xC0) != 0x80)
          {
            return false;
          }
          codepoint = (codepoint << 6) | (next & 0x3F);
        }
        if ((continuationCount > 0 && codepoint < minimum) || codepoint > 0x10FFFF
            || (codepoint >= 0xD800 && codepoint <= 0xDFFF))
        {
          return false;
        }
        input += continuationCount + 1;

        unsigned char macRomanByte = 0;
        const bool representable = ToolboxMacRomanEncode(codepoint, macRomanByte);
        if (!representable || outputLength >= 31)
        {
          return false;
        }
        out[outputLength + 1] = static_cast<unsigned char>(macRomanByte);
        ++outputLength;
      }

      out[0] = outputLength;
      return outputLength > 0;
    }
  } // namespace toolbox
} // namespace loka
