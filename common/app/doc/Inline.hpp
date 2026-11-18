#ifndef LOKA_APP_DOC_INLINE_HPP
#define LOKA_APP_DOC_INLINE_HPP
#include "app/style/AttributedString.hpp"
namespace loka
{
  namespace app
  {
    namespace doc
    {
      /** Linked inline content. Cost: one segment allocation per author call; owns no rows. */
      inline AttributedString Link(const core::String &label, const char *url)
      {
        return Linked(label, ::loka::app::Link, LinkTarget(core::String(url)));
      }
      /** Literal linked content. Cost: one String and segment per author call. */
      inline AttributedString Link(const char *label, const char *url)
      {
        return Linked(label, ::loka::app::Link, LinkTarget(core::String(url)));
      }
      /** Inline code is a monospace style, not a separate semantic value.
          Cost: one segment allocation per author call. */
      inline AttributedString Code(const core::String &text)
      {
        return Styled(text, Monospace);
      }
      /** Literal inline code. Cost: one String and segment per author call. */
      inline AttributedString Code(const char *text)
      {
        return Styled(text, Monospace);
      }
    } // namespace doc
  } // namespace app
} // namespace loka
#endif
