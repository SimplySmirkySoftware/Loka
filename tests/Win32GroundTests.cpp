#include "Win32GroundTests.hpp"
#include "../win32/src/Win32Ground.hpp"
#include "support/ContrastRatio.hpp"
#include "support/TestVerify.hpp"
#include <cstdio>

namespace
{
  using namespace loka::app;
  using namespace loka::win32;
  const SurfaceGround grounds[] = {
      SURFACE_GROUND_WINDOW, SURFACE_GROUND_DOCUMENT, SURFACE_GROUND_CONTROL};

  struct Theme
  {
    const char *name;
    unsigned long buttonFace, buttonText, window, windowText;
  };

  double themeLuminance(const Theme &theme, int index)
  {
    unsigned long rgb = 0;
    switch (index)
    {
    case WIN32_SYSTEM_COLOR_BTNFACE: rgb = theme.buttonFace; break;
    case WIN32_SYSTEM_COLOR_BTNTEXT: rgb = theme.buttonText; break;
    case WIN32_SYSTEM_COLOR_WINDOW: rgb = theme.window; break;
    case WIN32_SYSTEM_COLOR_WINDOWTEXT: rgb = theme.windowText; break;
    default: LOKA_VERIFY(false && "unexpected system color index");
    }
    return loka_test::RelativeLuminance(
        ((rgb >> 16) & 255) / 255.0, ((rgb >> 8) & 255) / 255.0, (rgb & 255) / 255.0);
  }
}

void testWin32GroundRoles()
{
  int index = -123;
  LOKA_VERIFY(!QueryWin32GroundColor(SURFACE_GROUND_TRANSPARENT, index));
  LOKA_VERIFY(index == -123);
  LOKA_VERIFY(!QueryWin32GroundColor(SURFACE_GROUND_NATIVE, index));
  LOKA_VERIFY(index == -123);
  LOKA_VERIFY(QueryWin32GroundColor(SURFACE_GROUND_WINDOW, index));
  LOKA_VERIFY(index == WIN32_SYSTEM_COLOR_BTNFACE);
  LOKA_VERIFY(QueryWin32GroundColor(SURFACE_GROUND_DOCUMENT, index));
  LOKA_VERIFY(index == WIN32_SYSTEM_COLOR_WINDOW);
  LOKA_VERIFY(QueryWin32GroundColor(SURFACE_GROUND_CONTROL, index));
  LOKA_VERIFY(index == WIN32_SYSTEM_COLOR_BTNFACE);
  LOKA_VERIFY(Win32TextRoleColor(SURFACE_GROUND_WINDOW) == WIN32_SYSTEM_COLOR_BTNTEXT);
  LOKA_VERIFY(Win32TextRoleColor(SURFACE_GROUND_CONTROL) == WIN32_SYSTEM_COLOR_BTNTEXT);
  LOKA_VERIFY(Win32TextRoleColor(SURFACE_GROUND_DOCUMENT) == WIN32_SYSTEM_COLOR_WINDOWTEXT);
}

void testWin32GroundLegibility()
{
  // Fixed shipped light-theme values from #1199 page 2's frozen 2026-10-10
  // ruling (BTNFACE / BTNTEXT / WINDOW / WINDOWTEXT, sRGB). Luna blue and olive
  // share a row. High contrast and user-composed colors are page 3, out of scope.
  const Theme themes[] = {
      {"Vista+", 0xF0F0F0, 0, 0xFFFFFF, 0},
      {"XP Luna blue/olive", 0xECE9D8, 0, 0xFFFFFF, 0},
      {"XP Luna silver", 0xE0DFE3, 0, 0xFFFFFF, 0},
      {"Windows Classic 2000/XP", 0xD4D0C8, 0, 0xFFFFFF, 0}};
  for (unsigned t = 0; t < sizeof(themes) / sizeof(themes[0]); ++t)
    for (unsigned g = 0; g < sizeof(grounds) / sizeof(grounds[0]); ++g)
      for (unsigned r = 0; r < sizeof(grounds) / sizeof(grounds[0]); ++r)
      {
        int index = -1;
        LOKA_VERIFY(QueryWin32GroundColor(grounds[g], index));
        const int text = Win32TextRoleColor(grounds[r]);
        const double ratio = loka_test::ContrastRatio(
            themeLuminance(themes[t], text), themeLuminance(themes[t], index));
        std::printf("%s text=%d ground=%d contrast=%.3f\n", themes[t].name, text, index, ratio);
        LOKA_VERIFY(ratio >= 4.5);
      }
}
