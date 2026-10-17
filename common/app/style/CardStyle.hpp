#ifndef LOKA_APP_STYLE_CARD_STYLE_HPP
#define LOKA_APP_STYLE_CARD_STYLE_HPP

#include "app/style/CardShadowTables.hpp"
#include "platform/CpuClass.hpp"
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
      /** Snapshot of StandardCardStyle() at construction; later answers never
          change this value. Construct default styles inside main: PPC Gestalt
          before InitGraf is unverified; this requirement is not enforced.
          One query per default construction (declarations, CardProps/CardBandProps
          default arguments, clone shells and nestable definition copies).
          Constant on 68k/macOS/Win32/Linux; one or two Gestalt calls on PPC from
          #1425 PR 2. Walks no rows. Ordinary value copies do not query. */
      CardStyle();
      /** Explicit look, independent of the process standard; no query or row walk. */
      explicit CardStyle(CardShadowExtent extent, CardShadowInk ink = CARD_SHADOW_STENCIL)
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
    /** Concrete standard look for a capability class: 68K DROP/STENCIL,
        PPC 60x WIDE/STENCIL, G3 or faster WIDE/ALPHA. One constant-time table
        selection per call by StandardCardStyle or an explicit caller; no query
        or row walk. The returned value never follows later platform answers. */
    CardStyle StandardCardStyleFor(platform::CpuClass cpuClass);
    /** Snapshot of this process's standard look; a decline means CPU_CLASS_68K.
        Call inside main (PPC Gestalt before InitGraf is unverified, not enforced).
        Called once by each default CardStyle construction, or explicitly by apps;
        one query, constant on 68k/macOS/Win32/Linux, one or two Gestalt calls on
        PPC from #1425 PR 2, no rows walked. Later answers never change the value. */
    CardStyle StandardCardStyle();
  } // namespace app
} // namespace loka
#endif
