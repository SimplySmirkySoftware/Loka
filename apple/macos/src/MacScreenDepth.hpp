#ifndef LOKA_MACOS_SCREEN_DEPTH_HPP
#define LOKA_MACOS_SCREEN_DEPTH_HPP

#import <AppKit/AppKit.h>

#include "MacObjCCompat.hpp" // NSInteger on the 10.4u SDK

namespace loka
{
  namespace macos
  {
    /** Reads a screen's bits per pixel; declines on a null screen or a zero
        reading and leaves out untouched. Choosing the screen belongs to the
        caller: MacWindow::queryDisplayDepth (the screen a window overlaps most)
        and MacPlatformContext::queryMainDisplayDepth (the main screen). */
    inline bool queryScreenDepth(NSScreen *screen, int &out)
    {
      if (!screen)
      {
        return false;
      }
      const NSInteger bitsPerPixel = NSBitsPerPixelFromDepth([screen depth]);
      if (bitsPerPixel <= 0)
      {
        return false;
      }
      out = (int)bitsPerPixel;
      return true;
    }
  } // namespace macos
} // namespace loka

#endif // LOKA_MACOS_SCREEN_DEPTH_HPP
