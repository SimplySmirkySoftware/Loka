#ifndef LOKA_WIN32_PAINT_GROUND_HPP
#define LOKA_WIN32_PAINT_GROUND_HPP

#include "Win32Ground.hpp"
#include "Win32PaintTextInk.hpp"
#include "context/Win32TextColorProperty.hpp"
#include <windows.h>

namespace loka
{
  namespace win32
  {
    typedef char SystemColorWindowMustMatchWindows[(static_cast<int>(WIN32_SYSTEM_COLOR_WINDOW) == COLOR_WINDOW) ? 1 : -1];
    typedef char SystemColorWindowTextMustMatchWindows[(static_cast<int>(WIN32_SYSTEM_COLOR_WINDOWTEXT) == COLOR_WINDOWTEXT) ? 1 : -1];
    typedef char SystemColorButtonFaceMustMatchWindows[(static_cast<int>(WIN32_SYSTEM_COLOR_BTNFACE) == COLOR_BTNFACE) ? 1 : -1];
    typedef char SystemColorButtonTextMustMatchWindows[(static_cast<int>(WIN32_SYSTEM_COLOR_BTNTEXT) == COLOR_BTNTEXT) ? 1 : -1];

    typedef char
        SystemColorGrayTextMustMatchWindows[(static_cast<int>(WIN32_SYSTEM_COLOR_GRAYTEXT) == COLOR_GRAYTEXT) ? 1 : -1];

    // Deliberate platform-seam twin of ToolboxPaintGround/MacPaintGround.
    /** Paints with a system-owned brush (never deleted). Declining roles assert
        in debug and paint nothing in release. Returns paint success so a
        RectSurface certifies presentation only after every fill succeeds. */
    inline bool Win32PaintGround(HDC dc, app::SurfaceGround ground, const RECT &rect)
    {
      int index;
      const bool answered = QueryWin32GroundColor(ground, index);
      assert(answered && "only Loka-owned grounds may be painted");
      return answered && FillRect(dc, &rect, GetSysColorBrush(index)) != 0;
    }

    /** Shared parent WM_CTLCOLORSTATIC response. Static text stays transparent
        to show its own parent's ground, which that parent paints. Overlapping
        transparent siblings are unsupported; ZStack upper children live in
        opaque units and every context window clips its siblings. */
    inline HBRUSH Win32StaticTextColors(HDC dc, HWND child)
    {
      if (dc)
      {
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, Win32PaintTextInk(Win32TextColorProperty::read(child), app::SURFACE_GROUND_WINDOW));
      }
      return static_cast<HBRUSH>(GetStockObject(NULL_BRUSH));
    }
  }
}

#endif
