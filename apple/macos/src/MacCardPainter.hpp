#ifndef LOKA_MAC_CARD_PAINTER_HPP
#define LOKA_MAC_CARD_PAINTER_HPP

#include <ApplicationServices/ApplicationServices.h>
#include "platform/MacProjection.hpp"
#include "app/nodes/controls/Card.hpp"

namespace loka
{
  namespace macos
  {
    /** Draw a band's ink in its local point coordinates. Phase is the local
        origin relative to the window content's top-left, in points. The current
        graphics transform supplies backing pixels, independently of spaceScale. */
    void PaintCardBandInk(CGContextRef graphics,
                          const core::Frame &band,
                          const app::CardBandProps &props,
                          const MacProjection &projection,
                          NSPoint phase);
    /** Whole-body offscreen rendering with absolute layout edges and an explicit
        projected surface origin. Shares ink mechanics, not band reconstruction. */
    void PaintCardInk(CGContextRef graphics,
                      const core::Frame &body,
                      const app::CardStyle &style,
                      const MacProjection &projection,
                      NSPoint origin,
                      NSPoint phase);
  } // namespace macos
} // namespace loka
#endif
