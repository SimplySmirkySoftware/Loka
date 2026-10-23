#include "platform/Win32String.hpp"

#include "core/StringAccess.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstring>
#include <climits>

namespace loka
{
  namespace win32
  {
    namespace detail
    {
#ifdef TEST_BUILD
      namespace
      {
        bool failWrite = false;
        bool refuseWrite()
        {
          const bool refuse = failWrite;
          failWrite = false;
          return refuse;
        }
      } // namespace
      namespace testing
      {
        void failNextConversionWrite()
        {
          failWrite = true;
        }
      } // namespace testing
#endif
      bool AppendUtf8ToWide(const char *bytes, std::size_t length, std::wstring &out, unsigned long flags)
      {
        if (length == 0)
          return true;
        if (!bytes || length >= static_cast<std::size_t>(INT_MAX))
          return false;
        int required = MultiByteToWideChar(CP_UTF8, flags, bytes, static_cast<int>(length), NULL, 0);
        if (required <= 0)
          return false;
        const std::size_t start = out.size();
        if (static_cast<std::size_t>(required) > out.max_size() - start)
          return false;
        out.resize(start + static_cast<std::size_t>(required));
        int written = MultiByteToWideChar(CP_UTF8, flags, bytes, static_cast<int>(length), &out[start], required);
#ifdef TEST_BUILD
        if (refuseWrite())
          written = 0;
#endif
        if (written != required)
        {
          out.resize(start);
          return false;
        }
        return true;
      }

      bool AppendWideToUtf8(const wchar_t *chars, std::size_t length, std::string &out)
      {
        if (length == 0)
          return true;
        if (!chars || length >= static_cast<std::size_t>(INT_MAX))
          return false;
        int required = WideCharToMultiByte(CP_UTF8, 0, chars, static_cast<int>(length), NULL, 0, NULL, NULL);
        if (required <= 0)
          return false;
        const std::size_t start = out.size();
        if (static_cast<std::size_t>(required) > out.max_size() - start)
          return false;
        out.resize(start + static_cast<std::size_t>(required));
        int written =
            WideCharToMultiByte(CP_UTF8, 0, chars, static_cast<int>(length), &out[start], required, NULL, NULL);
#ifdef TEST_BUILD
        if (refuseWrite())
          written = 0;
#endif
        if (written != required)
        {
          out.resize(start);
          return false;
        }
        return true;
      }

    } // namespace detail

    Win32String::Win32String()
        : storage_()
    {
    }

    Win32String::Win32String(const std::wstring &value)
        : storage_(value)
    {
    }

    const wchar_t *Win32String::c_str() const
    {
      return this->storage_.empty() ? L"" : this->storage_.c_str();
    }

    std::size_t Win32String::length() const
    {
      return this->storage_.size();
    }

    bool Win32String::empty() const
    {
      return this->storage_.empty();
    }

    bool Win32String::appendUtf8(std::string &out) const
    {
      return detail::AppendWideToUtf8(this->storage_.c_str(), this->storage_.size(), out);
    }

    loka::core::Managed<loka::platform::GraphemeString> Win32String::createGraphemeString() const
    {
      return loka::core::Managed<loka::platform::GraphemeString>();
    }

    bool MaterializeWideString(const loka::core::String &logical, std::wstring &out)
    {
      out.clear();
      const loka::core::Managed<loka::platform::String> &handle = loka::core::StringAccess::handle(logical);
      if (!handle.isValid())
        return true;
      Win32String *existing = dynamic_cast<Win32String *>(handle.get());
      if (existing)
      {
        out.assign(existing->c_str(), existing->c_str() + existing->length());
        return true;
      }
      std::string utf8;
      if (!handle->appendUtf8(utf8))
        return false;
      return detail::AppendUtf8ToWide(utf8.c_str(), utf8.length(), out, 0);
    }

    loka::core::Managed<loka::platform::String> CreatePlatformStringFromUtf8(const char *bytes, std::size_t length)
    {
      std::wstring wide;
      if (!detail::AppendUtf8ToWide(bytes, bytes ? length : 0, wide, 0))
        return loka::core::Managed<loka::platform::String>();
      return loka::core::Managed<loka::platform::String>::Wrap(new Win32String(wide));
    }

    loka::core::Managed<loka::platform::String> CreatePlatformStringFromLiteral(const char *literal)
    {
      if (!literal)
        return CreatePlatformStringFromUtf8("", 0);
      return CreatePlatformStringFromUtf8(literal, std::strlen(literal));
    }

    loka::core::Managed<loka::platform::String> CreateWin32String(const loka::core::String &logical)
    {
      const loka::core::Managed<loka::platform::String> &handle = loka::core::StringAccess::handle(logical);
      Win32String *existing = handle.isValid() ? dynamic_cast<Win32String *>(handle.get()) : 0;
      if (existing)
        return handle;
      std::wstring wide;
      if (!MaterializeWideString(logical, wide))
        return loka::core::Managed<loka::platform::String>();
      return loka::core::Managed<loka::platform::String>::Wrap(new Win32String(wide));
    }

    loka::core::Managed<loka::platform::String> CreateWin32StringFromUtf16(const wchar_t *chars, std::size_t length)
    {
      if (!chars || length == 0)
        return loka::core::Managed<loka::platform::String>::Wrap(new Win32String());
      std::wstring wide(chars, chars + length);
      return loka::core::Managed<loka::platform::String>::Wrap(new Win32String(wide));
    }

  } // namespace win32
} // namespace loka

// Provide the platform-level factory functions expected by loka::core on Windows.
namespace loka
{
  namespace platform
  {
    loka::core::Managed<loka::platform::String> CreatePlatformStringFromUtf8(const char *bytes, std::size_t length)
    {
      return loka::win32::CreatePlatformStringFromUtf8(bytes, length);
    }

    loka::core::Managed<loka::platform::String> CreatePlatformStringFromLiteral(const char *literal)
    {
      return loka::win32::CreatePlatformStringFromLiteral(literal);
    }
  } // namespace platform
} // namespace loka
