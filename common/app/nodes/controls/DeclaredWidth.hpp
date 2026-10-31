#ifndef LOKA_APP_NODES_CONTROLS_DECLARED_WIDTH_HPP
#define LOKA_APP_NODES_CONTROLS_DECLARED_WIDTH_HPP

namespace loka
{
  namespace app
  {
    namespace detail
    {
      /** Optional fixed seat; the numeric payload is read only in FIXED mode. */
      class DeclaredWidth
      {
      public:
        DeclaredWidth()
            : kind_(NATURAL),
              value_(0)
        {
        }
        explicit DeclaredWidth(short value)
            : kind_(FIXED),
              value_(value)
        {
        }
        bool query(short &value) const
        {
          if (this->kind_ == NATURAL)
            return false;
          value = this->value_;
          return true;
        }

      private:
        enum Kind
        {
          NATURAL,
          FIXED
        };
        Kind kind_;
        short value_;
      };
    } // namespace detail
  } // namespace app
} // namespace loka

#endif // LOKA_APP_NODES_CONTROLS_DECLARED_WIDTH_HPP
