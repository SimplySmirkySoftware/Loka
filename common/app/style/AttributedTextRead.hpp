#ifndef LOKA_APP_STYLE_ATTRIBUTED_TEXT_READ_HPP
#define LOKA_APP_STYLE_ATTRIBUTED_TEXT_READ_HPP

#include "app/style/LinkTarget.hpp"
#include "platform/String.hpp"

namespace loka
{
  namespace app
  {
    namespace style_internal
    {
      enum TextReadKind
      {
        TEXT_EMPTY,
        TEXT_REFUSED,
        TEXT_READABLE
      };
      /** Initializes out on every call. Direct bytes borrow text; converted
          bytes borrow caller-owned scratch. Both owners must outlive every use
          of out and must not be reassigned meanwhile. No shared scratch. */
      TextReadKind ReadAttributedText(const core::String &text, core::StringBuffer &scratch, platform::Utf8View &out);
      /** Internal value order: EMPTY < REFUSED < READABLE; refused identities
          use Managed's control-block order, readable values use unsigned bytes. */
      int CompareLinkTargets(const LinkTarget &left, const LinkTarget &right);
    } // namespace style_internal
  } // namespace app
} // namespace loka

#endif
