#ifndef LOKA_APP_STYLE_CARD_STYLE_HPP
#define LOKA_APP_STYLE_CARD_STYLE_HPP

#include "app/style/CardShadowTables.hpp"
#include <cassert>

namespace loka
{
  namespace app
  {
    enum CardShadowExtent
    {
      CARD_SHADOW_NONE,
      CARD_SHADOW_DROP,
      CARD_SHADOW_WIDE,
      CARD_SHADOW_EXTENT_COUNT
    };
    enum CardShadowInk
    {
      CARD_SHADOW_STENCIL,
      CARD_SHADOW_ALPHA,
      CARD_SHADOW_INK_COUNT
    };
    /** Closed card look. Whole-value replacement; no mutable platform state. */
    class CardStyle
    {
    public:
      // User-provided, so `const CardStyle look;` compiles under C++98 rules
      // (GCC 4.2 and Xcode 6.2 clang refuse it otherwise; #1380).
      explicit CardStyle(CardShadowExtent extent = CARD_SHADOW_DROP, CardShadowInk ink = CARD_SHADOW_STENCIL)
          : extent_(extent),
            ink_(ink)
      {
        assert(extent >= CARD_SHADOW_NONE && extent < CARD_SHADOW_EXTENT_COUNT);
        assert(ink >= CARD_SHADOW_STENCIL && ink < CARD_SHADOW_INK_COUNT);
      }
      CardShadowExtent extent() const
      {
        return this->extent_;
      }
      CardShadowInk ink() const
      {
        return this->ink_;
      }
      /** O(1) immutable extent facts, shared by layout and shadow composition. */
      const detail::CardShadowTable &geometry() const
      {
        return detail::CardShadowTables[this->extent_];
      }
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
        return this->extent_ * CARD_SHADOW_INK_COUNT + this->ink_;
      }
      bool operator==(const CardStyle &other) const
      {
        return this->extent_ == other.extent_ && this->ink_ == other.ink_;
      }
      bool operator<(const CardStyle &other) const
      {
        return this->extent_ < other.extent_ || (this->extent_ == other.extent_ && this->ink_ < other.ink_);
      }

    private:
      CardShadowExtent extent_;
      CardShadowInk ink_;
    };
  } // namespace app
} // namespace loka
#endif
