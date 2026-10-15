#ifndef LOKA_APP_LAYOUT_LAYOUT_WIDTH_SUM_HPP
#define LOKA_APP_LAYOUT_LAYOUT_WIDTH_SUM_HPP

#include <climits>
#include <cassert>

namespace loka
{
  namespace app
  {
    namespace layout
    {
      /** Nonnegative width arithmetic, sticky above the layout short range.
          Checks precede arithmetic even when long is only 32 bits. */
      class LayoutWidthSum
      {
      public:
        LayoutWidthSum()
            : value_(0)
        {
        }
        void add(long value)
        {
          this->addProduct(1, value);
        }
        void addProduct(long count, long width)
        {
          assert(count >= 0 && width >= 0);
          if (this->value_ > SHRT_MAX)
            return;
          if (count != 0 && width > (SHRT_MAX - this->value_) / count)
            this->value_ = static_cast<long>(SHRT_MAX) + 1;
          else
            this->value_ += count * width;
        }
        long value() const
        {
          return this->value_;
        }

      private:
        long value_;
      };
    } // namespace layout
  } // namespace app
} // namespace loka
#endif
