#ifndef LOKA_WIN32_TEXT_INK_HPP
#define LOKA_WIN32_TEXT_INK_HPP
#include "Win32Ground.hpp"
#include "app/style/StyleVocab.hpp"
namespace loka
{
  namespace win32
  {
    /** Completed paint source: a live system index or an absolute sRGB ink. */
    class Win32TextInk
    {
    public:
      enum Kind
      {
        SYSTEM,
        FIXED
      };
      static Win32TextInk system(int index)
      {
        return Win32TextInk(SYSTEM, index);
      }
      static Win32TextInk fixed(unsigned long rgb)
      {
        return Win32TextInk(FIXED, rgb);
      }
      Kind kind() const
      {
        return this->kind_;
      }
      unsigned long value() const
      {
        return this->value_;
      }

    private:
      Win32TextInk(Kind kind, unsigned long value)
          : kind_(kind),
            value_(value)
      {
      }
      Kind kind_;
      unsigned long value_;
    };
    /** Light-column twin of ToolboxTextInk; system roles resolve only at paint. */
    inline Win32TextInk ResolveWin32TextInk(app::TextColor color, app::SurfaceGround ground)
    {
      // #1196, owner 2026-10-11: Windows 11 light values, also used on Windows 10.
      // RED/GREEN/YELLOW: microsoft-ui-xaml @8463f451,
      // Common_themeresources_any.xaml SystemFillColorCritical/Success/Caution.
      // BLUE/MAGENTA: fluentui @c78d2235 packages/tokens web light theme,
      // colorBrandForeground1 / colorPaletteBerryForeground1.
      // CYAN: that revision's cyan ramp shade20 (owner choice, no alias token).
      switch (color)
      {
      case app::TEXT_COLOR_UNSET:
      case app::TEXT_COLOR_PRIMARY:
        return Win32TextInk::system(Win32TextRoleColor(ground));
      case app::TEXT_COLOR_SECONDARY:
        return Win32TextInk::system(WIN32_SYSTEM_COLOR_GRAYTEXT);
      case app::TEXT_COLOR_BLACK:
        return Win32TextInk::fixed(0x000000);
      case app::TEXT_COLOR_WHITE:
        return Win32TextInk::fixed(0xFFFFFF);
      case app::TEXT_COLOR_RED:
        return Win32TextInk::fixed(0xC42B1C);
      case app::TEXT_COLOR_GREEN:
        return Win32TextInk::fixed(0x0F7B0F);
      case app::TEXT_COLOR_BLUE:
        return Win32TextInk::fixed(0x0F6CBD);
      case app::TEXT_COLOR_CYAN:
        return Win32TextInk::fixed(0x00748F);
      case app::TEXT_COLOR_MAGENTA:
        return Win32TextInk::fixed(0xAF33A1);
      case app::TEXT_COLOR_YELLOW:
        return Win32TextInk::fixed(0x9D5D00);
      }
      assert(false && "unknown text color");
      return Win32TextInk::system(Win32TextRoleColor(ground));
    }
  } // namespace win32
} // namespace loka
#endif
