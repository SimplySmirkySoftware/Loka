#include "ToolboxDialogConstruction.hpp"

// Out of line on purpose: these read the QuickDraw and low-memory globals
// (`qd`, the menu bar height), which the Multiversal interfaces declare with
// unknown bounds; inlined into a caller they trip -Warray-bounds. The
// out-of-memory dialog and the confirm dialog share this one definition.
namespace loka { namespace toolbox {
/** Alert placement: centered horizontally, one third down the work area. */
Rect CenterDialogBounds(short width, short height)
{
  const Rect &screen = qd.screenBits.bounds;
  const short workTop = static_cast<short>(screen.top + GetMBarHeight());
  const short left = static_cast<short>((screen.left + screen.right - width) / 2);
  const short top = static_cast<short>(workTop + (screen.bottom - workTop - height) / 3);
  const Rect bounds = {top, left, static_cast<short>(top + height), static_cast<short>(left + width)};
  return bounds;
}
/** Preserve the caller's port and pen while drawing the default button ring. */
void DrawDialogDefaultOutline(DialogPtr dialog, const Rect &button)
{
  GrafPtr savedPort;
  GetPort(&savedPort);
  SetPort(reinterpret_cast<GrafPtr>(dialog));
  PenState savedPen;
  GetPenState(&savedPen);
  PenNormal();
  PenSize(3, 3);
  Rect bounds = button;
  InsetRect(&bounds, -4, -4);
  FrameRoundRect(&bounds, 16, 16);
  SetPenState(&savedPen);
  SetPort(savedPort);
}
} }
