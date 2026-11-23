#ifndef LOKA_APP_DOC_DETAIL_HTML_IMAGE_HPP
#define LOKA_APP_DOC_DETAIL_HTML_IMAGE_HPP

#include "app/doc/Image.hpp"
#include <climits>
#include <cstdio>
#include <string>

namespace loka
{
  namespace app
  {
    namespace doc
    {
      namespace detail
      {
        /** Cost: the inline and block writers escape one original byte per call. */
        inline void AppendHtmlByte(std::string &out, char c, bool attribute)
        {
          switch (c)
          {
          case '&':
            out += "&amp;";
            break;
          case '<':
            out += "&lt;";
            break;
          case '>':
            out += "&gt;";
            break;
          case '"':
            if (attribute)
              out += "&quot;";
            else
              out += c;
            break;
          default:
            out += c;
          }
        }

        /** Cost: once per HTML image tag, escape the two collected lines and format any dimensions. */
        inline bool
        AppendHtmlImage(std::string &out, const std::string &source, const std::string &alt, const ImageSize *size)
        {
          if (size && (!size->width() || !size->height()))
            return false;
          out += "<img src=\"";
          for (size_t i = 0; i < source.size(); ++i)
            AppendHtmlByte(out, source[i], true);
          out += "\" alt=\"";
          for (size_t i = 0; i < alt.size(); ++i)
            AppendHtmlByte(out, alt[i], true);
          if (size)
          {
            char attributes[sizeof("\" width=\"\" height=\"") + 2 * sizeof(unsigned) * CHAR_BIT];
            ::snprintf(attributes, sizeof(attributes), "\" width=\"%u\" height=\"%u", size->width(), size->height());
            out += attributes;
          }
          out += "\">";
          return true;
        }
      } // namespace detail
    } // namespace doc
  } // namespace app
} // namespace loka
#endif
