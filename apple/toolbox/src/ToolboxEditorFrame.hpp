#ifndef LOKA_TOOLBOX_EDITOR_FRAME_HPP
#define LOKA_TOOLBOX_EDITOR_FRAME_HPP
#include <Quickdraw.h>
#include <climits>
/** Port-local editor edges. Unrepresentable edges produce an empty seat. */
inline Rect ToolboxEditorFrame(short x, short y, int width, int height)
{
  Rect rect;
  // Check differences first: Frame dimensions can span all of a 32-bit int,
  // and Classic's long cannot hold an overflowing origin + dimension sum.
  if (width >= static_cast<long>(SHRT_MIN) - x && width <= static_cast<long>(SHRT_MAX) - x
      && height >= static_cast<long>(SHRT_MIN) - y && height <= static_cast<long>(SHRT_MAX) - y)
    SetRect(&rect,
            x,
            y,
            static_cast<short>(static_cast<long>(x) + width),
            static_cast<short>(static_cast<long>(y) + height));
  else
    SetRect(&rect, x, y, x, y);
  return rect;
}
#endif
