#ifndef LOKA_APP_STYLE_LINK_TARGET_HPP
#define LOKA_APP_STYLE_LINK_TARGET_HPP

#include "core/String.hpp"

namespace loka
{
  namespace app
  {
    /** Immutable, uninterpreted application link destination, sharing its String. */
    class LinkTarget
    {
    public:
      LinkTarget();
      explicit LinkTarget(const core::String &text);
      /** No handle or readable zero length means none; refusal does not. */
      bool isNone() const;
      const core::String &text() const;
      /** Unsigned UTF-8 prefix match. Null or unreadable returns false;
          an empty prefix matches every readable target, including none. */
      bool startsWith(const char *prefix) const;
      /** Same handle or equal readable bytes (including all empty forms);
          distinct refusals are unequal. Conversion refusal is evaluated per call,
          so equality can change if a target's readability changes. */
      bool operator==(const LinkTarget &other) const;
      bool operator!=(const LinkTarget &other) const;

    private:
      core::String text_;
    };
  } // namespace app
} // namespace loka

#endif
