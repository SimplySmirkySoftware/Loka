#ifndef LOKA_TOOLBOX_UNIT_HAIRLINE_HPP
#define LOKA_TOOLBOX_UNIT_HAIRLINE_HPP
#include <Quickdraw.h>

/** Grey used by the single scratch adapter for milk and picture hairlines. */
const unsigned short TOOLBOX_UNIT_HAIRLINE_GREY = 0xBBBB;

/** The unit's own bottom edge, restricted to its visible portion. */
inline bool ToolboxUnitHairlineRect(const Rect &rect, const Rect &unit, Rect &out)
{
  if (rect.left >= rect.right || rect.top >= rect.bottom || unit.bottom != rect.bottom)
    return false;
  out = rect;
  out.top = static_cast<short>(rect.bottom - 1);
  return true;
}
#endif
