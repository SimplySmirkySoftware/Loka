#ifndef LOKA_APP_TEXT_SEPARATORS_HPP
#define LOKA_APP_TEXT_SEPARATORS_HPP
#include <cstddef>
namespace loka
{
  namespace app
  {
    /** The logical body folds CR, LF and CRLF into one LF; other scalars remain text. */
    struct TextSeparators
    {
      /** After decoding a scalar, consume its optional LF tail and classify it. */
      static bool Fold(unsigned int scalar, const char *bytes, std::size_t length, std::size_t &next)
      {
        if (scalar == '\r' && next < length && bytes[next] == '\n')
          ++next;
        return scalar == '\r' || scalar == '\n';
      }
    };
  } // namespace app
} // namespace loka
#endif
