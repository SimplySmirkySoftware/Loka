#ifndef LOKA_APP_STYLE_CARD_SHADOW_HPP
#define LOKA_APP_STYLE_CARD_SHADOW_HPP
#include "app/style/CardStyle.hpp"
#include "core/Frame.hpp"
namespace loka
{
  namespace app
  {
    /** Immutable window-lattice lines bounding the nine half-open shadow cells.
        Indices 0/3 are outer allowance edges; 1/2 delimit the straight profile.
        Overlapping corner cuts share one line, leaving an empty middle cell. */
    struct CardShadowGridLines
    {
      const int x[4];
      const int y[4];
    };
    /** O(1), allocation-free partition for an admitted body; shared by samplers
        and native transfers. No pixels or table rows are walked. */
    CardShadowGridLines CardShadowGrid(const CardStyle &style, const core::Frame &body);

    /** Density on the layout-unit lattice for an admitted rounded body.
        Pure O(1) per sample: one tile/profile lookup, no allocation or rows walked. */
    unsigned char CardShadowDensity(const CardStyle &style, const core::Frame &body, int x, int y);
    /** Window/device lattice threshold; pure O(1) per sample, including negative coordinates. */
    bool CardShadowStencilInk(unsigned char density, int windowX, int windowY);
  } // namespace app
} // namespace loka
#endif
