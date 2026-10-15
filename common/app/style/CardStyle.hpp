#ifndef LOKA_APP_STYLE_CARD_STYLE_HPP
#define LOKA_APP_STYLE_CARD_STYLE_HPP

namespace loka
{
  namespace app
  {
    /** The single v1 card look. A value, with no mutable or platform-owned state. */
    class CardStyle
    {
    public:
      enum
      {
        kCornerRadius = 6,
        kBorderBand = 3,
        kShadowLeft = 0,
        kShadowTop = 0,
        kShadowRight = 3,
        kShadowBottom = 3
      };
      int hash() const
      {
        return 0;
      }
      bool operator==(const CardStyle &) const
      {
        return true;
      }
      bool operator<(const CardStyle &) const
      {
        return false;
      }
    };
  } // namespace app
} // namespace loka
#endif
