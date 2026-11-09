#ifndef LOKA_APP_LAYOUT_TEXT_SPAN_BREAKS_HPP
#define LOKA_APP_LAYOUT_TEXT_SPAN_BREAKS_HPP

#include "app/style/AttributedString.hpp"

namespace loka
{
  namespace app
  {
    /** Whether segment starts a span. Callers skip decoded empty bodies and
        supply the previous non-empty segment, or null for the first one. */
    inline bool TextSpanBreaks(const AttributedString::Segment *previous, const AttributedString::Segment &segment)
    {
      return !previous || previous->style != segment.style || previous->link != segment.link;
    }
  } // namespace app
} // namespace loka

#endif // LOKA_APP_LAYOUT_TEXT_SPAN_BREAKS_HPP
