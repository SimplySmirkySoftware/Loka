#ifndef LOKA_WIN32_PAINT_TEXT_INK_HPP
#define LOKA_WIN32_PAINT_TEXT_INK_HPP
#include "Win32TextInk.hpp"
#include <windows.h>
namespace loka
{
  namespace win32
  {
    /** Resolve theme roles at each paint; fixed values use Windows byte order. */
    inline COLORREF Win32PaintTextInk(app::TextColor color, app::SurfaceGround ground)
    {
      const Win32TextInk ink = ResolveWin32TextInk(color, ground);
      if (ink.kind() == Win32TextInk::SYSTEM)
        return GetSysColor(static_cast<int>(ink.value()));
      const unsigned long rgb = ink.value();
      return RGB((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255);
    }
  } // namespace win32
} // namespace loka
#endif
