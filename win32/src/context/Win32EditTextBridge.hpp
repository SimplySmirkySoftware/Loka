#ifndef LOKA_WIN32_EDIT_TEXT_BRIDGE_HPP
#define LOKA_WIN32_EDIT_TEXT_BRIDGE_HPP

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <string>
#include <vector>

#include "core/String.hpp"
#include "app/text/Utf8Scan.hpp"
#include "platform/Win32String.hpp"
#include "platform/Win32DisplayScale.hpp"

namespace loka
{
  namespace win32
  {
#ifdef TEST_BUILD
    namespace testing
    {
      enum TextEditorSetFailure
      {
        TEXT_EDITOR_SET_REFUSED,
        TEXT_EDITOR_SET_TRUNCATED,
        TEXT_EDITOR_SET_FALSE_AFTER_DELIVERY
      };
      void failTextEditorSets(TextEditorSetFailure failure, unsigned count);
      bool setTextEditorWide(HWND hwnd, const wchar_t *text);
    } // namespace testing
#endif

    /** Projection/restore only: multiline WM_SETTEXT does not send EN_CHANGE.
        Input must use EDIT's input messages and parent WM_COMMAND route.
        Validate UTF-16 CRLF units, not the document's CR-inclusive byte count. */
    inline bool WriteTextEditorWide(HWND hwnd, const std::wstring &wide)
    {
#ifdef TEST_BUILD
      const bool submitted = testing::setTextEditorWide(hwnd, wide.c_str());
#else
      const bool submitted = SetWindowTextW(hwnd, wide.c_str()) != FALSE;
#endif
      return submitted && GetWindowTextLengthW(hwnd) == static_cast<int>(wide.size());
    }

    inline DWORD EditTextControlExStyle()
    {
      return WS_EX_CLIENTEDGE;
    }

    inline DWORD EditTextControlStyle()
    {
      return WS_TABSTOP | WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL;
    }

    /** The EditText native pair must stay UTF-16 end to end: an ANSI EDIT
        control stores IME input in the system codepage, and reading those
        bytes back as UTF-8 destroys any out-of-ASCII text (#160). Creation,
        write, and readback all live here so the whole contract is pinned by
        one committed LokaTestsWin32 test. */
    HWND CreateEditTextControl(HWND parent, const NativeRect &geometry);

    /** Legacy test caller coordinates are already device pixels. */
    inline HWND CreateEditTextControl(HWND parent, int x, int y, int width, int height)
    {
      const RECT pixels = {x, y, x + width, y + height};
      return CreateEditTextControl(parent, Win32DisplayScale::fromDevicePixels(pixels));
    }

    /** Borrowed validated UTF-8 row shorter than INT_MAX bytes; no native reads or mapping cache.
        Deliberate twin of NativeRow in apple/macos/src/context/MacTextEditorContext.mm. */
    // common/core/TextUnits.hpp is the formula home; delegation is a follow-up.
    class LogicalRow
    {
    public:
      LogicalRow(const char *bytes, std::size_t length)
          : bytes_(bytes),
            length_(length)
      {
      }
      int unitsAt(std::size_t bytes) const
      {
        assert(app::Utf8ColumnOnBoundary(this->bytes_, this->length_, bytes));
        std::size_t index = 0;
        int units = 0;
        while (index < bytes)
        {
          unsigned int scalar = 0;
          if (!core::detail::DecodeUtf8Codepoint(this->bytes_, this->length_, index, scalar))
            break;
          units += scalar > 0xffff ? 2 : 1;
        }
        return units;
      }
      std::size_t bytesFloor(int units) const
      {
        std::size_t index = 0;
        while (index < this->length_ && units > 0)
        {
          const std::size_t start = index;
          unsigned int scalar = 0;
          if (!core::detail::DecodeUtf8Codepoint(this->bytes_, this->length_, index, scalar))
            return start;
          const int width = scalar > 0xffff ? 2 : 1;
          if (units < width)
            return start;
          units -= width;
        }
        return index;
      }
      int unitLength() const
      {
        return this->unitsAt(this->length_);
      }

    private:
      const char *bytes_;
      std::size_t length_;
    };

    /** Locate a borrowed row without constructing a prefix or row copy. */
    inline bool TextEditorRowRange(const std::string &logical, int row, std::size_t &start, std::size_t &length)
    {
      if (row < 0)
        return false;
      start = 0;
      while (row-- > 0)
      {
        const std::size_t end = logical.find('\r', start);
        if (end == std::string::npos)
          return false;
        start = end + 1;
      }
      const std::size_t end = logical.find('\r', start);
      length = (end == std::string::npos ? logical.size() : end) - start;
      return true;
    }

    /** Multiline conversion; alphabet admission belongs to the model.
        Outbound LF/NUL refuse; inbound CRLF and lone CR/LF normalize to CR. */
    inline bool TextEditorToWide(const std::string &logical, std::wstring &wide)
    {
      wide.clear();
      std::size_t start = 0;
      for (std::size_t i = 0; i <= logical.size(); ++i)
      {
        if (i < logical.size() && (!logical[i] || logical[i] == '\n'))
        {
          wide.clear();
          return false;
        }
        if (i == logical.size() || logical[i] == '\r')
        {
          std::size_t scalars = 0;
          if (!app::Utf8Scan(logical.data() + start, i - start, scalars)
              || !detail::AppendUtf8ToWide(logical.data() + start, i - start, wide, MB_ERR_INVALID_CHARS))
          {
            wide.clear();
            return false;
          }
          if (i != logical.size())
            wide += L"\r\n";
          start = i + 1;
        }
      }
      return true;
    }

    // common/core/TextUnits.hpp is the normalization formula home; delegation is a follow-up.
    inline bool TextEditorFromWide(const wchar_t *wide, std::size_t length, std::string &logical)
    {
      logical.clear();
      if (!wide && length)
        return false;
      std::wstring normalized;
      for (std::size_t i = 0; i < length; ++i)
      {
        const wchar_t value = wide[i];
        // XP cannot ask WideCharToMultiByte to reject unpaired surrogates.
        if (!value || (value >= 0xdc00 && value <= 0xdfff))
          return false;
        if (value >= 0xd800 && value <= 0xdbff)
        {
          if (i + 1 == length || wide[i + 1] < 0xdc00 || wide[i + 1] > 0xdfff)
            return false;
          normalized += value;
          normalized += wide[++i];
          continue;
        }
        normalized += value == L'\n' ? L'\r' : value;
        if (value == L'\r' && i + 1 < length && wide[i + 1] == L'\n')
          ++i;
      }
      return detail::AppendWideToUtf8(normalized.data(), normalized.size(), logical);
    }

    inline void ReadEditTextWide(HWND hwnd, std::wstring &out)
    {
      out.clear();
      if (!hwnd)
      {
        return;
      }
      int length = GetWindowTextLengthW(hwnd);
      if (length <= 0)
      {
        return;
      }
      std::vector<wchar_t> buffer(length + 1, L'\0');
      GetWindowTextW(hwnd, &buffer[0], length + 1);
      out.assign(&buffer[0]);
    }

    inline loka::core::String ReadEditTextString(HWND hwnd)
    {
      std::wstring wide;
      ReadEditTextWide(hwnd, wide);
      return loka::core::String(CreateWin32StringFromUtf16(wide.c_str(), wide.size()));
    }

    inline void WriteEditTextString(HWND hwnd, const loka::core::String &value)
    {
      std::wstring wide;
      if (!MaterializeWideString(value, wide))
      {
        wide.clear();
      }
      SetWindowTextW(hwnd, wide.c_str());
    }

  } // namespace win32
} // namespace loka

#endif // LOKA_WIN32_EDIT_TEXT_BRIDGE_HPP
