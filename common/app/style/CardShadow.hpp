#ifndef LOKA_APP_STYLE_CARD_SHADOW_HPP
#define LOKA_APP_STYLE_CARD_SHADOW_HPP
#include "app/style/CardStyle.hpp"
#include "core/Frame.hpp"
namespace loka
{
  namespace app
  {
    /** Density on the layout-unit lattice for an admitted rounded body.
        Pure O(1) per sample: one tile/profile lookup, no allocation or rows walked. */
    unsigned char CardShadowDensity(const CardStyle &style, const core::Frame &body, int x, int y);
    /** Window/device lattice threshold; pure O(1) per sample, including negative coordinates. */
    bool CardShadowStencilInk(unsigned char density, int windowX, int windowY);
  } // namespace app
} // namespace loka
#endif
