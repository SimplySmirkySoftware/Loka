#ifndef LOKA_WIN32_DISPLAY_FONT_HPP
#define LOKA_WIN32_DISPLAY_FONT_HPP

#include <windows.h>
#include "Win32DisplayScale.hpp"
#include "app/style/Style.hpp"

namespace loka
{
  namespace win32
  {
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
      HFONT get() const
      {
        return this->fonts_[kDefaultSizeRow][0][0];
      }

      HFONT find(const loka::app::TextStyle &style) const;
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
        kFontRowCount = kDefaultSizeRow + 1
      };
      HFONT fonts_[kFontRowCount][2][2];
      Win32DisplayScale scale_;

      Win32DisplayFont(const Win32DisplayFont &);
      Win32DisplayFont &operator=(const Win32DisplayFont &);
    };
  } // namespace win32
} // namespace loka

#endif // LOKA_WIN32_DISPLAY_FONT_HPP
