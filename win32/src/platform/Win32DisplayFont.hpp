#ifndef LOKA_WIN32_DISPLAY_FONT_HPP
#define LOKA_WIN32_DISPLAY_FONT_HPP

#include <windows.h>
#include "Win32DisplayScale.hpp"
#include "app/style/Style.hpp"

namespace loka
{
  namespace win32
  {
#ifdef TEST_BUILD
    namespace testing
    {
      /** Borrow a null-terminated list (at most three faces) for the next build. */
      void useMonospaceCandidatesForNextBuild(const wchar_t *const *faces);
      const wchar_t *const *takeMonospaceCandidates();
      /** Count system-plain and twin creation gates; monospace candidate probes
          do not consume the count. Refusal is one-shot. */
      void failDisplayFontCreation(unsigned successfulFontsBeforeFailure);
      bool refuseDisplayFontCreation();
    } // namespace testing
#endif

    /** Table-owned result. A null font is failure for the named requested family.
        Borrow only until the table swaps or dies; success has no separate flag. */
    class Win32FontResolution
    {
    public:
      Win32FontResolution(HFONT font = 0, loka::app::TextFamily family = loka::app::TEXT_FAMILY_SYSTEM)
          : font_(font),
            family_(family)
      {
      }
      HFONT font() const
      {
        return this->font_;
      }
      loka::app::TextFamily family() const
      {
        return this->family_;
      }

    private:
      HFONT font_;
      loka::app::TextFamily family_;
    };

    /** Owns the finite descriptor table for one Win32 display scale. */
    class Win32DisplayFont
    {
    public:
      Win32DisplayFont();
      explicit Win32DisplayFont(const Win32DisplayScale &scale);
      ~Win32DisplayFont();

      bool create(const Win32DisplayScale &scale);
      bool matches(const Win32DisplayScale &scale) const;
      void swap(Win32DisplayFont &other);
      HFONT get(bool underline = false) const
      {
        return this->fonts_[slotIndex(kDefaultSizeRow, 0, 0, 0)].member(underline).font();
      }

      const Win32FontResolution &find(const loka::app::TextStyle &style) const;
      /** Translate a borrowed old handle before this table is released. */
      HFONT replacementFor(HFONT font, const Win32DisplayFont &replacement) const;

    private:
      // Rows: one per vocabulary size, one per size role, then the native
      // message-font row that fixed controls share.
      enum
      {
        kSizeCount = loka::app::detail::kStyleVocabularySizeCount,
        kRoleRowBase = kSizeCount,
        kDefaultSizeRow = kRoleRowBase + loka::app::TEXT_SIZE_ROLE_COUNT,
        kFontRowCount = kDefaultSizeRow + 1,
        kSlotCount = kFontRowCount * 8
      };
      /** Both members belong to the table, including a refused partial build. */
      struct FontPair
      {
        Win32FontResolution members[2];
        const Win32FontResolution &member(bool underline) const
        {
          return this->members[underline ? 1 : 0];
        }
      };
      /** Family is the fastest axis; every table walk uses this ordering. */
      static int slotIndex(int row, int bold, int italic, int family)
      {
        return ((row * 2 + bold) * 2 + italic) * 2 + family;
      }
      FontPair fonts_[kSlotCount];
      Win32DisplayScale scale_;

      void initializeEmptyFamilies();
      Win32DisplayFont(const Win32DisplayFont &);
      Win32DisplayFont &operator=(const Win32DisplayFont &);
    };
  } // namespace win32
} // namespace loka

#endif // LOKA_WIN32_DISPLAY_FONT_HPP
