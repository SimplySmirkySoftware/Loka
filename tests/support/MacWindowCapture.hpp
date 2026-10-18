#ifndef LOKA_TESTS_MAC_WINDOW_CAPTURE_HPP
#define LOKA_TESTS_MAC_WINDOW_CAPTURE_HPP
#include "MacObjCCompat.hpp"
#include <ApplicationServices/ApplicationServices.h>
#include <cmath>
#include <dlfcn.h>
namespace loka
{
  namespace testing
  {
#if MAC_OS_X_VERSION_MAX_ALLOWED >= 1050
    /** One server read per snapshot; O(window pixels), followed by O(content
        pixels) normalization. The caller owns the bitmap. dlsym adds no 10.5
        load-time symbol requirement when a newer SDK deploys to Tiger. */
    inline NSBitmapImageRep *ReadMacWindowServer(NSView *root, const char *&reason)
    {
      typedef CGImageRef (*Capture)(CGRect, CGWindowListOption, CGWindowID, CGWindowImageOption);
      Capture capture = reinterpret_cast<Capture>(dlsym(RTLD_DEFAULT, "CGWindowListCreateImage"));
      if (!capture)
      {
        reason = "window-server capture unavailable (requires 10.5)";
        return nil;
      }
      CGWindowImageOption options = kCGWindowImageBoundsIgnoreFraming;
#if MAC_OS_X_VERSION_MAX_ALLOWED >= 1080
      options |= kCGWindowImageBestResolution;
#endif
      NSWindow *window = [root window];
      CGImageRef image = capture(
          CGRectNull, kCGWindowListOptionIncludingWindow, static_cast<CGWindowID>([window windowNumber]), options);
      if (!image)
      {
        reason = "window-server capture returned nil (possibly permission refused)";
        return nil;
      }
      // BoundsIgnoreFraming removes the shadow, not the title bar. Derive backing
      // scale from server pixels / window points, avoiding newer AppKit APIs.
      // convertRect:toView:nil changes flipped root coordinates to window y-up;
      // CGImage cropping uses top-down pixels.
      const NSRect frame = [window frame];
      const NSRect content = [root convertRect:[root bounds] toView:nil];
      const CGFloat sx = CGImageGetWidth(image) / frame.size.width;
      const CGFloat sy = CGImageGetHeight(image) / frame.size.height;
      const CGRect crop = CGRectMake(std::floor(NSMinX(content) * sx),
                                     std::floor((frame.size.height - NSMaxY(content)) * sy),
                                     std::ceil(content.size.width * sx),
                                     std::ceil(content.size.height * sy));
      CGImageRef cropped = CGImageCreateWithImageInRect(image, crop);
      CGImageRelease(image);
      if (!cropped || CGImageGetWidth(cropped) == 0 || CGImageGetHeight(cropped) == 0)
      {
        if (cropped)
          CGImageRelease(cropped);
        reason = "window-server capture empty content crop";
        return nil;
      }
      const size_t width = CGImageGetWidth(cropped), height = CGImageGetHeight(cropped);
      NSBitmapImageRep *bitmap = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:NULL
                                                                         pixelsWide:width
                                                                         pixelsHigh:height
                                                                      bitsPerSample:8
                                                                    samplesPerPixel:4
                                                                           hasAlpha:YES
                                                                           isPlanar:NO
                                                                     colorSpaceName:NSDeviceRGBColorSpace
                                                                        bytesPerRow:width * 4
                                                                       bitsPerPixel:32];
      CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
      CGContextRef context =
          bitmap && space
              ? CGBitmapContextCreate(
                    [bitmap bitmapData], width, height, 8, [bitmap bytesPerRow], space, kCGImageAlphaPremultipliedLast)
              : 0;
      if (space)
        CGColorSpaceRelease(space);
      if (!context)
      {
        CGImageRelease(cropped);
        [bitmap release];
        reason = "window-server capture normalization allocation refused";
        return nil;
      }
      CGContextClearRect(context, CGRectMake(0, 0, width, height));
      CGContextSetBlendMode(context, kCGBlendModeCopy);
      CGContextDrawImage(context, CGRectMake(0, 0, width, height), cropped);
      CGContextRelease(context);
      CGImageRelease(cropped);
      bool visible = false;
      for (size_t y = 0; y < height && !visible; ++y)
        for (size_t x = 0; x < width; ++x)
          if ([bitmap bitmapData][y * [bitmap bytesPerRow] + x * 4 + 3])
          {
            visible = true;
            break;
          }
      if (!visible)
      {
        [bitmap release];
        reason = "window-server capture all-transparent (possibly permission refused)";
        return nil;
      }
      return bitmap;
    }
#else
    inline NSBitmapImageRep *ReadMacWindowServer(NSView *, const char *&reason)
    {
      reason = "window-server capture unavailable (requires 10.5)";
      return nil;
    }
#endif
  } // namespace testing
} // namespace loka
#endif
