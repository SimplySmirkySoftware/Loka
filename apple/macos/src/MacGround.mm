#include "MacGround.hpp"
#include <AvailabilityMacros.h>
#include <cassert>

namespace loka
{
  namespace macos
  {
    bool QueryMacGroundColor(app::SurfaceGround ground, NSColor *&out)
    {
      switch (ground)
      {
      case app::SURFACE_GROUND_TRANSPARENT:
      case app::SURFACE_GROUND_NATIVE:
        return false;
      case app::SURFACE_GROUND_WINDOW:
        out = [NSColor windowBackgroundColor];
        return true;
      case app::SURFACE_GROUND_DOCUMENT:
        out = [NSColor textBackgroundColor];
        return true;
      case app::SURFACE_GROUND_CONTROL:
        out = [NSColor controlBackgroundColor];
        return true;
      }
      return false;
    }

    NSColor *MacTextRoleColor()
    {
#if defined(MAC_OS_X_VERSION_MAX_ALLOWED) && MAC_OS_X_VERSION_MAX_ALLOWED >= 101000
      if ([NSColor respondsToSelector:@selector(labelColor)])
        return [NSColor labelColor];
#endif
      return [NSColor textColor];
    }

    namespace
    {
      // A newer dynamic system color is looked up by selector at run time, so
      // this file builds against SDKs that predate it and falls back on
      // systems that lack it (the MacDisplayAppearance shape).
      NSColor *SystemColorOr(SEL selector, NSColor *fallback)
      {
        if ([NSColor respondsToSelector:selector])
          return (NSColor *)[NSColor performSelector:selector];
        return fallback;
      }
    } // namespace

    bool QueryMacTextColor(app::TextColor color, NSColor *&out)
    {
      switch (color)
      {
      case app::TEXT_COLOR_UNSET:
        return false;
      case app::TEXT_COLOR_PRIMARY:
        out = MacTextRoleColor();
        return true;
      case app::TEXT_COLOR_SECONDARY:
        out = SystemColorOr(@selector(secondaryLabelColor), [NSColor disabledControlTextColor]);
        return true;
      case app::TEXT_COLOR_BLACK:
        out = [NSColor blackColor];
        return true;
      case app::TEXT_COLOR_WHITE:
        out = [NSColor whiteColor];
        return true;
      case app::TEXT_COLOR_RED:
        out = SystemColorOr(@selector(systemRedColor), [NSColor redColor]);
        return true;
      case app::TEXT_COLOR_GREEN:
        out = SystemColorOr(@selector(systemGreenColor), [NSColor greenColor]);
        return true;
      case app::TEXT_COLOR_BLUE:
        out = SystemColorOr(@selector(systemBlueColor), [NSColor blueColor]);
        return true;
      case app::TEXT_COLOR_YELLOW:
        out = SystemColorOr(@selector(systemYellowColor), [NSColor yellowColor]);
        return true;
      case app::TEXT_COLOR_CYAN:
        // systemCyanColor arrived in macOS 12; systemTealColor in 10.12.
        out = SystemColorOr(@selector(systemCyanColor), SystemColorOr(@selector(systemTealColor), [NSColor cyanColor]));
        return true;
      case app::TEXT_COLOR_MAGENTA:
        out = SystemColorOr(@selector(systemPurpleColor), [NSColor magentaColor]);
        return true;
      }
      return false;
    }

    void MacPaintGround(app::SurfaceGround ground, NSRect rect)
    {
      NSColor *color = nil;
      const bool answered = QueryMacGroundColor(ground, color);
      assert(answered && "only Loka-owned grounds may be painted");
      if (!answered)
        return;
      [color setFill];
      NSRectFill(rect);
    }
  }
}
