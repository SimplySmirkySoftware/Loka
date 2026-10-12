#include "Win32DisplayFont.hpp"
#include <cassert>

namespace
{
  typedef BOOL(WINAPI *SystemParametersInfoForDpiFn)(UINT, UINT, PVOID, UINT, UINT);

  SystemParametersInfoForDpiFn ResolveSystemParametersInfoForDpi()
  {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    return user32
               ? reinterpret_cast<SystemParametersInfoForDpiFn>(
                     GetProcAddress(user32, "SystemParametersInfoForDpi"))
               : 0;
  }

  bool ReadMessageFont(const loka::win32::Win32DisplayScale &scale,
                       LOGFONTW &out)
  {
    NONCLIENTMETRICSW metrics;
    ZeroMemory(&metrics, sizeof(metrics));
    metrics.cbSize = sizeof(metrics);
    static SystemParametersInfoForDpiFn systemParametersInfoForDpi =
        ResolveSystemParametersInfoForDpi();
    if (systemParametersInfoForDpi
        && systemParametersInfoForDpi(
            SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0, scale.dpi()))
    {
      out = metrics.lfMessageFont;
      return true;
    }
    if (!SystemParametersInfoW(
            SPI_GETNONCLIENTMETRICS, metrics.cbSize, &metrics, 0))
    {
      // XP's NONCLIENTMETRICS predates iPaddedBorderWidth. A binary built
      // against a modern SDK must retry with the older structure size.
      metrics.cbSize = sizeof(metrics) - sizeof(int);
      if (!SystemParametersInfoW(
              SPI_GETNONCLIENTMETRICS, metrics.cbSize, &metrics, 0))
      {
        return false;
      }
    }
    const loka::win32::Win32DisplayScale systemScale =
        loka::win32::Win32DisplayScale::forSystem();
    out = metrics.lfMessageFont;
    out.lfHeight = scale.scaleLengthFrom(systemScale, out.lfHeight);
    out.lfWidth = scale.scaleLengthFrom(systemScale, out.lfWidth);
    return true;
  }
} // namespace

namespace loka
{
  namespace win32
  {
    Win32DisplayFont::Win32DisplayFont()
        : scale_()
    {
      ZeroMemory(this->fonts_, sizeof(this->fonts_));
    }

    Win32DisplayFont::~Win32DisplayFont()
    {
      // Also releases every successful allocation in a refused partial build.
      for (int row = 0; row < kFontRowCount; ++row)
        for (int bold = 0; bold < 2; ++bold)
          for (int italic = 0; italic < 2; ++italic)
            if (this->fonts_[row][bold][italic])
              DeleteObject(this->fonts_[row][bold][italic]);
    }

    bool Win32DisplayFont::create(const Win32DisplayScale &scale)
    {
      assert(!this->get() && "a display font must be created into an empty owner");
      if (this->get())
        return false;
      LOGFONTW base;
      ZeroMemory(&base, sizeof(base));
      if (!ReadMessageFont(scale, base))
        return false;
      Win32DisplayFont candidate;
      for (int row = 0; row < kFontRowCount; ++row)
        for (int bold = 0; bold < 2; ++bold)
          for (int italic = 0; italic < 2; ++italic)
          {
            LOGFONTW descriptor = base;
            // The shared native message-font row also serves fixed controls.
            if (row != kDefaultSizeRow)
            {
              const int height = scale.fontHeightToNative(loka::app::detail::StyleVocabularySizes[row]);
              if (height < 0)
                return false;
              descriptor.lfHeight = -height;
            }
            if (bold)
              descriptor.lfWeight = FW_BOLD;
            if (italic)
              descriptor.lfItalic = TRUE;
            candidate.fonts_[row][bold][italic] = CreateFontIndirectW(&descriptor);
            if (!candidate.fonts_[row][bold][italic])
              return false;
          }
      candidate.scale_ = scale;
      this->swap(candidate);
      return true;
    }

    HFONT Win32DisplayFont::find(const loka::app::TextStyle &style) const
    {
      const int bold = style.hasWeight_ && style.weight_ == loka::app::TEXT_WEIGHT_BOLD ? 1 : 0;
      const int italic = style.hasItalic_ && style.italic_ ? 1 : 0;
      int logicalUnits = 0;
      if (!style.size_.queryPoints(logicalUnits))
        return this->fonts_[kDefaultSizeRow][bold][italic];
      loka::app::SizeOf(logicalUnits).size_.queryPoints(logicalUnits);
      for (int row = 0; row < kSizeCount; ++row)
        if (loka::app::detail::StyleVocabularySizes[row] == logicalUnits)
          return this->fonts_[row][bold][italic];
      return 0;
    }

    HFONT Win32DisplayFont::replacementFor(HFONT font, const Win32DisplayFont &replacement) const
    {
      for (int row = 0; row < kFontRowCount; ++row)
        for (int bold = 0; bold < 2; ++bold)
          for (int italic = 0; italic < 2; ++italic)
            if (font && this->fonts_[row][bold][italic] == font)
              return replacement.fonts_[row][bold][italic];
      return replacement.get();
    }

    bool Win32DisplayFont::matches(const Win32DisplayScale &scale) const
    {
      return this->get() && this->scale_ == scale;
    }

    void Win32DisplayFont::swap(Win32DisplayFont &other)
    {
      for (int row = 0; row < kFontRowCount; ++row)
        for (int bold = 0; bold < 2; ++bold)
          for (int italic = 0; italic < 2; ++italic)
          {
            HFONT temporary = this->fonts_[row][bold][italic];
            this->fonts_[row][bold][italic] = other.fonts_[row][bold][italic];
            other.fonts_[row][bold][italic] = temporary;
          }
      Win32DisplayScale temporaryScale = this->scale_;
      this->scale_ = other.scale_;
      other.scale_ = temporaryScale;
    }
  } // namespace win32
} // namespace loka
