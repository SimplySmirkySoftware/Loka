#ifndef LOKA_WIN32_DISPLAY_DEPTH_HPP
#define LOKA_WIN32_DISPLAY_DEPTH_HPP

#include <windows.h>

namespace loka
{
  namespace win32
  {
    /** Reads a device context's bits per pixel; declines on a null context or
        a zero reading and leaves out untouched. Choosing the context (a
        window's or the screen's) belongs to the caller: Win32Window::queryDisplayDepth
        and Win32PlatformContext::queryMainDisplayDepth are the two. */
    inline bool queryDeviceContextDepth(HDC dc, int &out)
    {
      if (!dc)
      {
        return false;
      }
      const int bitsPerPixel = GetDeviceCaps(dc, BITSPIXEL) * GetDeviceCaps(dc, PLANES);
      if (bitsPerPixel <= 0)
      {
        return false;
      }
      out = bitsPerPixel;
      return true;
    }
  } // namespace win32
} // namespace loka

#endif // LOKA_WIN32_DISPLAY_DEPTH_HPP
