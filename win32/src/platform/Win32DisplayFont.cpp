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

  HFONT CreateDisplayFont(const LOGFONTW &descriptor)
  {
#ifdef TEST_BUILD
    if (loka::win32::testing::refuseDisplayFontCreation())
      return 0;
#endif
    return CreateFontIndirectW(&descriptor);
  }

  HFONT CreateMonospaceFont(LOGFONTW &descriptor, HDC dc, const wchar_t *const *faces)
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
      for (int slot = 0; slot < kSlotCount; ++slot)
        for (int underline = 0; underline < 2; ++underline)
          this->fonts_[slot].members[underline] =
              Win32FontResolution(0, slot % 2 ? loka::app::TEXT_FAMILY_MONOSPACE : loka::app::TEXT_FAMILY_SYSTEM);
    }

    Win32DisplayFont::~Win32DisplayFont()
    {
      // Also releases every successful allocation in a refused partial build.
      for (int slot = 0; slot < kSlotCount; ++slot)
        for (int underline = 0; underline < 2; ++underline)
          if (this->fonts_[slot].members[underline].font())
            DeleteObject(this->fonts_[slot].members[underline].font());
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
      for (int slot = 0; slot < kSlotCount; ++slot)
      {
        // Inverse of slotIndex: one address convention for all table walks.
        const int row = slot / 8;
        const int bold = (slot / 4) % 2;
        const int italic = (slot / 2) % 2;
        const loka::app::TextFamily family =
            slot % 2 ? loka::app::TEXT_FAMILY_MONOSPACE : loka::app::TEXT_FAMILY_SYSTEM;
        LOGFONTW descriptor = base;
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
        descriptor.lfUnderline = FALSE;
        if (family == loka::app::TEXT_FAMILY_SYSTEM)
        {
          candidate.fonts_[slot].members[0] = Win32FontResolution(CreateDisplayFont(descriptor), family);
          if (!candidate.fonts_[slot].members[0].font())
            return false;
        }
        else
        {
          candidate.fonts_[slot].members[0] =
              Win32FontResolution(CreateMonospaceFont(descriptor, dc.get(), faces), family);
          if (!candidate.fonts_[slot].members[0].font())
            continue;
        }
        // Monospace returns the descriptor of its verified candidate. The twin
        // uses that exact face without repeating candidate selection.
        descriptor.lfUnderline = TRUE;
        candidate.fonts_[slot].members[1] = Win32FontResolution(CreateDisplayFont(descriptor), family);
        if (!candidate.fonts_[slot].members[1].font())
          return false;
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
      const bool underline = style.queryUnderline(on) && on;
      loka::app::TextSizeRole role;
      if (style.size_.queryRole(role))
        return this->fonts_[slotIndex(kRoleRowBase + role, bold, italic, family)].member(underline);
      int logicalUnits = 0;
      if (!style.size_.queryPoints(logicalUnits))
        return this->fonts_[slotIndex(kDefaultSizeRow, bold, italic, family)].member(underline);
      loka::app::SizeOf(logicalUnits).size_.queryPoints(logicalUnits);
      for (int row = 0; row < kSizeCount; ++row)
        if (loka::app::detail::StyleVocabularySizes[row] == logicalUnits)
          return this->fonts_[slotIndex(row, bold, italic, family)].member(underline);
      return this->fonts_[slotIndex(kDefaultSizeRow, bold, italic, family)].member(underline);
    }

    HFONT Win32DisplayFont::replacementFor(HFONT font, const Win32DisplayFont &replacement) const
    {
      for (int slot = 0; slot < kSlotCount; ++slot)
        for (int underline = 0; underline < 2; ++underline)
          if (font && this->fonts_[slot].members[underline].font() == font)
            return replacement.fonts_[slot].members[underline].font();
      return replacement.get();
    }

    bool Win32DisplayFont::matches(const Win32DisplayScale &scale) const
    {
      return this->get() && this->scale_ == scale;
    }

    void Win32DisplayFont::swap(Win32DisplayFont &other)
    {
      for (int slot = 0; slot < kSlotCount; ++slot)
      {
        FontPair temporary = this->fonts_[slot];
        this->fonts_[slot] = other.fonts_[slot];
        other.fonts_[slot] = temporary;
      }
      Win32DisplayScale temporaryScale = this->scale_;
      this->scale_ = other.scale_;
      other.scale_ = temporaryScale;
    }
  } // namespace win32
} // namespace loka
