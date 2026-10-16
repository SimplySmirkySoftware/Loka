#ifndef LOKA_APP_LAYOUT_LAYOUT_GAP_HPP
#define LOKA_APP_LAYOUT_LAYOUT_GAP_HPP

#include <cassert>
#include <climits>

namespace loka
{
  namespace app
  {
    namespace layout
    {
      /** A named spacing value: unspecified resolves to the property default,
          of(0) makes the boxes touch, and of(n) is checked before narrowing (a
          negative n asserts and becomes 0 in release; above SHRT_MAX it asserts and
          becomes SHRT_MAX). Both unspecified and standard-window-margin sentinels are private;
          identity, order and hash distinguish them from every explicit value. */
      class LayoutGap
      {
      public:
        LayoutGap()
            : value_(-1)
        {
        }
        static LayoutGap unspecified()
        {
          return LayoutGap();
        }
        static LayoutGap of(int n)
        {
          assert(n >= 0 && n <= SHRT_MAX);
          return LayoutGap(static_cast<short>(n < 0 ? 0 : n > SHRT_MAX ? SHRT_MAX : n));
        }
        static LayoutGap standardWindowMargin()
        {
          return LayoutGap(-2);
        }
        int resolve(int propertyDefault, int standardWindowMargin) const
        {
          return this->value_ == -1 ? propertyDefault : this->value_ == -2 ? standardWindowMargin : this->value_;
        }
        bool operator==(const LayoutGap &other) const
        {
          return this->value_ == other.value_;
        }
        bool operator!=(const LayoutGap &other) const
        {
          return !(*this == other);
        }
        bool operator<(const LayoutGap &other) const
        {
          return this->value_ < other.value_;
        }
        int hash() const
        {
          return this->value_;
        }

      private:
        explicit LayoutGap(short value)
            : value_(value)
        {
        }
        short value_;
      };
    } // namespace layout
    /** Names the rail standard outer margin, independently of a Window override. */
    inline layout::LayoutGap StandardWindowMargin()
    {
      return layout::LayoutGap::standardWindowMargin();
    }
  } // namespace app
} // namespace loka
#endif
