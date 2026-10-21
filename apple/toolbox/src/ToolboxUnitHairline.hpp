#ifndef LOKA_TOOLBOX_UNIT_HAIRLINE_HPP
#define LOKA_TOOLBOX_UNIT_HAIRLINE_HPP
#include <Quickdraw.h>

/** Shared by the scratch milk adapter and the window picture adapter. */
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
/** Paint the picture's bottom edge under the caller's clip, preserving drawing state. */
inline void ToolboxPaintUnitHairline(const Rect &rect, const Rect &unit)
{
  Rect hairline;
  if (!ToolboxUnitHairlineRect(rect, unit, hairline))
    return;
  // Window adapter; the matching scratch adapter is in ToolboxFrostCache::paint.
  PenState pen;
  RGBColor foreground;
  GetPenState(&pen);
  GetForeColor(&foreground);
  PenNormal();
  const RGBColor grey = {TOOLBOX_UNIT_HAIRLINE_GREY, TOOLBOX_UNIT_HAIRLINE_GREY, TOOLBOX_UNIT_HAIRLINE_GREY};
  RGBForeColor(&grey);
  PaintRect(&hairline);
  RGBForeColor(&foreground);
  SetPenState(&pen);
}
#endif
