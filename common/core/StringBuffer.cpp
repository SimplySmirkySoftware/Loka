#include "core/StringBuffer.hpp"
#include "core/TextUnits.hpp"

#include <cstring>
#include <climits>

#include "platform/String.hpp"

namespace loka
{
  namespace core
  {
    namespace
    {
      bool countUtf8Units(const std::string &utf8, StringEncoding encoding, std::size_t &units)
      {
        units = 0;
        if (encoding == StringEncodingUtf8)
        {
          units = utf8.size();
          return true;
        }
        if (encoding == StringEncodingUtf16)
        {
          const int count = Utf16UnitsAt(utf8.data(), utf8.size(), utf8.size());
          if (count < 0)
          {
            // Preserve the completed-prefix count on malformed input too.
            const std::size_t prefix = Utf8BytesFloor(utf8.data(), utf8.size(), INT_MAX);
            units = static_cast<std::size_t>(Utf16UnitsAt(utf8.data(), utf8.size(), prefix));
            return false;
          }
          units = static_cast<std::size_t>(count);
          return true;
        }
        std::size_t index = 0;
        while (index < utf8.size())
        {
          unsigned int codepoint = 0;
          if (!detail::DecodeUtf8Codepoint(utf8.data(), utf8.size(), index, codepoint))
            return false;
          units += 1;
        }
        return true;
      }

      bool fillUtf16FromUtf8(const std::string &utf8, std::vector<StringBuffer::Utf16Unit> &out)
      {
        std::size_t written = 0;
        return Utf8ToUtf16(utf8.data(), utf8.size(), out.empty() ? 0 : &out[0], out.size(), written);
      }

      bool fillUtf32FromUtf8(const std::string &utf8, std::vector<StringBuffer::Utf32Unit> &out)
      {
        std::size_t index = 0;
        std::size_t outIndex = 0;
        while (index < utf8.size())
        {
          unsigned int codepoint = 0;
          if (!detail::DecodeUtf8Codepoint(utf8.data(), utf8.size(), index, codepoint))
            return false;
          out[outIndex++] = static_cast<StringBuffer::Utf32Unit>(codepoint);
        }
        return true;
      }

    } // namespace

    StringBuffer::StringBuffer()
        : encoding_(StringEncodingUtf16),
          platformHandle_(),
          utf8_(),
          utf16_(),
          utf32_()
    {
    }

    StringBuffer::StringBuffer(StringEncoding encoding)
        : encoding_(encoding),
          platformHandle_(),
          utf8_(),
          utf16_(),
          utf32_()
    {
    }

    StringEncoding StringBuffer::encoding() const
    {
      return encoding_;
    }

    unsigned char StringBuffer::characterSize() const
    {
      switch (encoding_)
      {
      case StringEncodingUtf8:
        return 1;
      case StringEncodingUtf16:
        return 2;
      case StringEncodingUtf32:
        return 4;
      }
      return 1;
    }

    std::size_t StringBuffer::length() const
    {
      switch (encoding_)
      {
      case StringEncodingUtf8:
        return utf8_.size();
      case StringEncodingUtf16:
        return utf16_.size();
      case StringEncodingUtf32:
        return utf32_.size();
      }
      return 0;
    }

    unsigned int StringBuffer::characterAt(std::size_t index) const
    {
      switch (encoding_)
      {
      case StringEncodingUtf8:
        return index < utf8_.size() ? utf8_[index] : 0;
      case StringEncodingUtf16:
        return index < utf16_.size() ? utf16_[index] : 0;
      case StringEncodingUtf32:
        return index < utf32_.size() ? utf32_[index] : 0;
      }
      return 0;
    }

    const void *StringBuffer::data() const
    {
      switch (encoding_)
      {
      case StringEncodingUtf8:
        return utf8_.empty() ? 0 : &utf8_[0];
      case StringEncodingUtf16:
        return utf16_.empty() ? 0 : &utf16_[0];
      case StringEncodingUtf32:
        return utf32_.empty() ? 0 : &utf32_[0];
      }
      return 0;
    }

    void *StringBuffer::data()
    {
      switch (encoding_)
      {
      case StringEncodingUtf8:
        return utf8_.empty() ? 0 : &utf8_[0];
      case StringEncodingUtf16:
        return utf16_.empty() ? 0 : &utf16_[0];
      case StringEncodingUtf32:
        return utf32_.empty() ? 0 : &utf32_[0];
      }
      return 0;
    }

    void StringBuffer::clear()
    {
      utf8_.clear();
      utf16_.clear();
      utf32_.clear();
    }

    void StringBuffer::reserve(std::size_t units)
    {
      switch (encoding_)
      {
      case StringEncodingUtf8:
        utf8_.reserve(units);
        break;
      case StringEncodingUtf16:
        utf16_.reserve(units);
        break;
      case StringEncodingUtf32:
        utf32_.reserve(units);
        break;
      }
    }

    void StringBuffer::resize(std::size_t units)
    {
      switch (encoding_)
      {
      case StringEncodingUtf8:
        utf8_.resize(units);
        break;
      case StringEncodingUtf16:
        utf16_.resize(units);
        break;
      case StringEncodingUtf32:
        utf32_.resize(units);
        break;
      }
    }

    bool StringBuffer::assignFromUtf8(const std::string &utf8)
    {
      return assignFromUtf8(utf8, true);
    }

    bool StringBuffer::assignFromUtf8(const std::string &utf8, bool allowResize)
    {
      std::size_t units = 0;
      if (!countUtf8Units(utf8, encoding_, units))
        return false;
      if (!allowResize)
      {
        switch (encoding_)
        {
        case StringEncodingUtf8:
          if (utf8_.capacity() < units)
            return false;
          break;
        case StringEncodingUtf16:
          if (utf16_.capacity() < units)
            return false;
          break;
        case StringEncodingUtf32:
          if (utf32_.capacity() < units)
            return false;
          break;
        }
      }

      utf8_.clear();
      utf16_.clear();
      utf32_.clear();

      switch (encoding_)
      {
      case StringEncodingUtf8:
        utf8_.resize(units);
        if (units > 0)
          std::memcpy(&utf8_[0], utf8.data(), units);
        return true;
      case StringEncodingUtf16:
        utf16_.resize(units);
        if (!fillUtf16FromUtf8(utf8, utf16_))
        {
          utf16_.clear();
          return false;
        }
        return true;
      case StringEncodingUtf32:
        utf32_.resize(units);
        if (!fillUtf32FromUtf8(utf8, utf32_))
        {
          utf32_.clear();
          return false;
        }
        return true;
      }
      return false;
    }

    bool StringBuffer::CountUnitsFromUtf8(const std::string &utf8, StringEncoding encoding, std::size_t &units)
    {
      return countUtf8Units(utf8, encoding, units);
    }

    const loka::core::Managed<platform::String> &StringBuffer::platformHandle() const
    {
      return platformHandle_;
    }

    loka::core::Managed<platform::GraphemeString> StringBuffer::graphemeHandle() const
    {
      if (!platformHandle_.isValid())
        return loka::core::Managed<platform::GraphemeString>();
      return platformHandle_->createGraphemeString();
    }

    void StringBuffer::setPlatformHandle(const loka::core::Managed<platform::String> &handle)
    {
      platformHandle_ = handle;
    }

  } // namespace core
} // namespace loka
