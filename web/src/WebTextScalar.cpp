#include "WebTextScalar.hpp"
#include "core/Utf8Decode.hpp"
namespace
{
  /** WHATWG error consumption around the shared strict decoder. Cost: normalizer
      once per invalid scalar, consumes only the borrowed prefix (at least one byte). */
  void ConsumeError(const char *bytes, std::size_t length, std::size_t &index)
  {
    const unsigned char lead = static_cast<unsigned char>(bytes[index++]);
    unsigned need = lead >= 0xc2 && lead <= 0xdf   ? 1
                    : lead >= 0xe0 && lead <= 0xef ? 2
                    : lead >= 0xf0 && lead <= 0xf4 ? 3
                                                   : 0;
    for (unsigned n = 0; n < need && index < length; ++n)
    {
      const unsigned char c = static_cast<unsigned char>(bytes[index]);
      const unsigned low = n == 0 && lead == 0xe0 ? 0xa0 : n == 0 && lead == 0xf0 ? 0x90 : 0x80;
      const unsigned high = n == 0 && lead == 0xed ? 0x9f : n == 0 && lead == 0xf4 ? 0x8f : 0xbf;
      if (c < low || c > high)
        break;
      ++index;
    }
  }
} // namespace
bool WebTextScalar(const char *bytes, std::size_t length, std::size_t &index, unsigned int &value)
{
  if (loka::core::detail::DecodeUtf8Codepoint(bytes, length, index, value))
    return true;
  ConsumeError(bytes, length, index);
  value = 0xfffd;
  return false;
}
