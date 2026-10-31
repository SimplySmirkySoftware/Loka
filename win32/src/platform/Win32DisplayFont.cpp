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

  /** One build-local DC; every candidate restores selection before deletion. */
  class CandidateDC
  {
  public:
    CandidateDC()
        : dc_(CreateCompatibleDC(NULL))
    {
    }
    ~CandidateDC()
    {
      if (this->dc_)
        DeleteDC(this->dc_);
    }
    HDC get() const
    {
      return this->dc_;
    }

  private:
    HDC dc_;
    CandidateDC(const CandidateDC &);
    CandidateDC &operator=(const CandidateDC &);
  };

  HFONT CreateMonospaceFont(LOGFONTW descriptor, HDC dc, const wchar_t *const *faces)
  {
    descriptor.lfPitchAndFamily = FIXED_PITCH | FF_MODERN;
    for (int candidate = 0; candidate < 3 && faces[candidate]; ++candidate)
    {
      lstrcpynW(descriptor.lfFaceName, faces[candidate], LF_FACESIZE);
      HFONT font = CreateFontIndirectW(&descriptor);
      if (!font)
        continue;
      HGDIOBJ previous = SelectObject(dc, font);
      wchar_t face[LF_FACESIZE];
      TEXTMETRICW metrics;
      int narrow = 0, wide = 0;
      const bool selected = previous && previous != HGDI_ERROR;
      const bool verified = selected && GetTextFaceW(dc, LF_FACESIZE, face) && lstrcmpiW(face, faces[candidate]) == 0
                            && GetTextMetricsW(dc, &metrics) && !(metrics.tmPitchAndFamily & TMPF_FIXED_PITCH)
                            && GetCharWidth32W(dc, L'i', L'i', &narrow) && GetCharWidth32W(dc, L'W', L'W', &wide)
                            && narrow > 0 && narrow == wide;
      if (selected)
        SelectObject(dc, previous);
      if (verified)
        return font;
      DeleteObject(font);
    }
    return 0;
  }

  // This rail's answer for each size role, in logical units of the message
  // face. SmallLabel is -10 px at 96 dpi: the smallest size whose kana and
  // kanji stayed legible beside the -12 px body in a window probe of Yu Gothic
  // UI, Segoe UI and Meiryo UI (2026-10-13). For the same role the Toolbox
  // answers Geneva 9 (ToolboxTextFontDescriptor::resolve), macOS the small
  // system font (MacScenePlatformController.mm) and headless 9
  // (SyntheticTextSize).
  int RoleLogicalUnits(loka::app::TextSizeRole role)
  {
    switch (role)
    {
    case loka::app::TEXT_SIZE_ROLE_SMALL_LABEL:
      return 10;
    case loka::app::TEXT_SIZE_ROLE_COUNT:
      break;
    }
    assert(!"TEXT_SIZE_ROLE_COUNT names no row");
    return 0;
  }
} // namespace

namespace loka
{
  namespace win32
  {
    Win32DisplayFont::Win32DisplayFont()
        : scale_()
    {
      this->initializeEmptyFamilies();
    }

    Win32DisplayFont::Win32DisplayFont(const Win32DisplayScale &scale)
        : scale_()
    {
      this->initializeEmptyFamilies();
      this->create(scale);
    }

    void Win32DisplayFont::initializeEmptyFamilies()
    {
      for (int row = 0; row < kFontRowCount; ++row)
        for (int bold = 0; bold < 2; ++bold)
          for (int italic = 0; italic < 2; ++italic)
            this->fonts_[row][bold][italic][1] = Win32FontResolution(0, loka::app::TEXT_FAMILY_MONOSPACE);
    }

    Win32DisplayFont::~Win32DisplayFont()
    {
      // Also releases every successful allocation in a refused partial build.
      for (int row = 0; row < kFontRowCount; ++row)
        for (int bold = 0; bold < 2; ++bold)
          for (int italic = 0; italic < 2; ++italic)
            for (int family = 0; family < 2; ++family)
              if (this->fonts_[row][bold][italic][family].font())
                DeleteObject(this->fonts_[row][bold][italic][family].font());
    }

    bool Win32DisplayFont::create(const Win32DisplayScale &scale)
    {
      assert(!this->get() && "a display font must be created into an empty owner");
      if (this->get())
        return false;
      static const wchar_t *const defaultFaces[] = {L"Consolas", L"Lucida Console", L"Courier New", 0};
      const wchar_t *const *faces = defaultFaces;
#ifdef TEST_BUILD
      const wchar_t *const *overrideFaces = testing::takeMonospaceCandidates();
      if (overrideFaces)
        faces = overrideFaces;
#endif
      CandidateDC dc;
      if (!dc.get())
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
              const int logicalUnits = row < kRoleRowBase
                                           ? loka::app::detail::StyleVocabularySizes[row]
                                           : RoleLogicalUnits(static_cast<loka::app::TextSizeRole>(row - kRoleRowBase));
              const int height = scale.fontHeightToNative(logicalUnits);
              if (height < 0)
                return false;
              descriptor.lfHeight = -height;
            }
            if (bold)
              descriptor.lfWeight = FW_BOLD;
            if (italic)
              descriptor.lfItalic = TRUE;
#ifdef TEST_BUILD
            if (testing::refuseDisplayFontCreation())
              return false;
#endif
            candidate.fonts_[row][bold][italic][0] = Win32FontResolution(CreateFontIndirectW(&descriptor));
            if (!candidate.fonts_[row][bold][italic][0].font())
              return false;
            candidate.fonts_[row][bold][italic][1] =
                Win32FontResolution(CreateMonospaceFont(descriptor, dc.get(), faces), loka::app::TEXT_FAMILY_MONOSPACE);
          }
      candidate.scale_ = scale;
      this->swap(candidate);
      return true;
    }

    const Win32FontResolution &Win32DisplayFont::find(const loka::app::TextStyle &style) const
    {
      const int family = style.family() == loka::app::TEXT_FAMILY_MONOSPACE ? 1 : 0;
      const int bold = style.hasWeight_ && style.weight_ == loka::app::TEXT_WEIGHT_BOLD ? 1 : 0;
      bool on = false;
      const int italic = style.queryItalic(on) && on ? 1 : 0;
      loka::app::TextSizeRole role;
      if (style.size_.queryRole(role))
        return this->fonts_[kRoleRowBase + role][bold][italic][family];
      int logicalUnits = 0;
      if (!style.size_.queryPoints(logicalUnits))
        return this->fonts_[kDefaultSizeRow][bold][italic][family];
      loka::app::SizeOf(logicalUnits).size_.queryPoints(logicalUnits);
      for (int row = 0; row < kSizeCount; ++row)
        if (loka::app::detail::StyleVocabularySizes[row] == logicalUnits)
          return this->fonts_[row][bold][italic][family];
      return this->fonts_[kDefaultSizeRow][bold][italic][family];
    }

    HFONT Win32DisplayFont::replacementFor(HFONT font, const Win32DisplayFont &replacement) const
    {
      for (int row = 0; row < kFontRowCount; ++row)
        for (int bold = 0; bold < 2; ++bold)
          for (int italic = 0; italic < 2; ++italic)
            for (int family = 0; family < 2; ++family)
              if (font && this->fonts_[row][bold][italic][family].font() == font)
                return replacement.fonts_[row][bold][italic][family].font();
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
            for (int family = 0; family < 2; ++family)
            {
              Win32FontResolution temporary = this->fonts_[row][bold][italic][family];
              this->fonts_[row][bold][italic][family] = other.fonts_[row][bold][italic][family];
              other.fonts_[row][bold][italic][family] = temporary;
            }
      Win32DisplayScale temporaryScale = this->scale_;
      this->scale_ = other.scale_;
      other.scale_ = temporaryScale;
    }
  } // namespace win32
} // namespace loka
