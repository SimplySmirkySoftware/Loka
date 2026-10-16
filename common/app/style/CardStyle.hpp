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
      // User-provided, so `const CardStyle look;` compiles under C++98 rules
      // (GCC 4.2 and Xcode 6.2 clang refuse it otherwise; #1380).
      CardStyle() {}
      enum
      {
        kCornerRadius = 6,
        kBorderBand = 3,
        kShadowLeft = 0,
        kShadowTop = 0,
        kShadowRight = 3,
        kShadowBottom = 3,
        kShadowClearance = 2
      };
      /** The displaced shadow's far corner ends at least two layout units
          before the opposite band. At supported scales >= 1 this exceeds the
          worst absolute-edge projection rounding (< 1.75 device pixels). */
      static int minimumBodyExtent(int firstShadow, int lastShadow)
      {
        const int shadow = firstShadow > lastShadow ? firstShadow : lastShadow;
        const int cornerMinimum = 2 * kCornerRadius;
        const int borderMinimum = 2 * kBorderBand;
        const int shapeMinimum = cornerMinimum > borderMinimum ? cornerMinimum : borderMinimum;
        const int displacedMinimum = kCornerRadius + shadow + kBorderBand + kShadowClearance;
        return shapeMinimum > displacedMinimum ? shapeMinimum : displacedMinimum;
      }
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
