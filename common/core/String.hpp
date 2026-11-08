#ifndef LOKA_CORE_STRING_HPP
#define LOKA_CORE_STRING_HPP

#include <cstddef>
#include <string>

#include "core/Managed.hpp"
#include "core/StringBuffer.hpp"

namespace loka
{
  namespace platform
  {
    class String;
    class GraphemeString;
  } // namespace platform

  namespace core
  {
    enum StringCompareResult
    {
      StringCompareEqual = 0,
      StringCompareNotEqual,
      StringCompareBufferRequired
    };

    class String
    {
    public:
      String();
      explicit String(const char *literal);
      explicit String(const std::string &utf8);
      explicit String(const loka::core::Managed<platform::String> &handle);
      String(const String &other);
      String &operator=(const String &other);
      ~String();

      static String Literal(const char *literal);
      static String Utf8(const char *bytes, std::size_t length);
      static String FromPlatform(const loka::core::Managed<platform::String> &platformValue);
      static String FromInt(int value);

      /** Equal representation implies equal value, not the reverse. Never allocates. */
      bool sharesRepresentation(const String &other) const
      {
        return this->handle_ == other.handle_;
      }

      bool empty() const;
      StringCompareResult compare(const String &other, bool allowAllocateBuffer) const;
      int compare(const String &other) const;
      bool equals(const String &other) const;
      StringBuffer buffer() const;
      StringBuffer bufferWithEncoding(StringEncoding encoding) const;
      bool assignToBuffer(StringBuffer &buffer) const;
      bool requiredUnits(StringEncoding encoding, std::size_t &units) const;

      String operator+(const String &rhs) const;
      String operator+(const char *rhs) const;
      String operator+(const std::string &rhs) const;
      String &operator+=(const String &rhs);
      String &operator+=(const char *rhs);
      String &operator+=(const std::string &rhs);

      static String Concat(const String &lhs, const String &rhs);

      template <typename Fmt> static String Format(const Fmt &fmt);
      template <typename Fmt, typename T1> static String Format(const Fmt &fmt, const T1 &a1);
      template <typename Fmt, typename T1, typename T2>
      static String Format(const Fmt &fmt, const T1 &a1, const T2 &a2);
      template <typename Fmt, typename T1, typename T2, typename T3>
      static String Format(const Fmt &fmt, const T1 &a1, const T2 &a2, const T3 &a3);
      template <typename Fmt, typename T1, typename T2, typename T3, typename T4>
      static String Format(const Fmt &fmt, const T1 &a1, const T2 &a2, const T3 &a3, const T4 &a4);

    private:
      static String FormatArray(const String &format, const String *args, std::size_t count);
      static String ToStringSegment(const String &value);
      static String ToStringSegment(const char *literal);
      static String ToStringSegment(const std::string &utf8);
      static String ToStringSegment(const loka::core::Managed<platform::String> &platformValue);

      loka::core::Managed<platform::String> handle_;
      friend class StringAccess;
    };

  } // namespace core
} // namespace loka

inline loka::core::String loka::core::String::ToStringSegment(const loka::core::String &value)
{
  return value;
}

inline loka::core::String loka::core::String::ToStringSegment(const char *literal)
{
  return literal ? loka::core::String::Literal(literal) : loka::core::String();
}

inline loka::core::String loka::core::String::ToStringSegment(const std::string &utf8)
{
  return loka::core::String(utf8);
}

inline loka::core::String
loka::core::String::ToStringSegment(const loka::core::Managed<loka::platform::String> &platformValue)
{
  return loka::core::String::FromPlatform(platformValue);
}

inline loka::core::String operator+(const char *lhs, const loka::core::String &rhs)
{
  return loka::core::String::Concat(loka::core::String::Literal(lhs), rhs);
}

inline loka::core::String operator+(const std::string &lhs, const loka::core::String &rhs)
{
  return loka::core::String::Concat(loka::core::String(lhs), rhs);
}

template <typename Fmt> inline loka::core::String loka::core::String::Format(const Fmt &fmt)
{
  return ToStringSegment(fmt);
}

template <typename Fmt, typename T1> inline loka::core::String loka::core::String::Format(const Fmt &fmt, const T1 &a1)
{
  loka::core::String args[1];
  args[0] = ToStringSegment(a1);
  return FormatArray(ToStringSegment(fmt), args, 1);
}

template <typename Fmt, typename T1, typename T2>
inline loka::core::String loka::core::String::Format(const Fmt &fmt, const T1 &a1, const T2 &a2)
{
  loka::core::String args[2];
  args[0] = ToStringSegment(a1);
  args[1] = ToStringSegment(a2);
  return FormatArray(ToStringSegment(fmt), args, 2);
}

template <typename Fmt, typename T1, typename T2, typename T3>
inline loka::core::String loka::core::String::Format(const Fmt &fmt, const T1 &a1, const T2 &a2, const T3 &a3)
{
  loka::core::String args[3];
  args[0] = ToStringSegment(a1);
  args[1] = ToStringSegment(a2);
  args[2] = ToStringSegment(a3);
  return FormatArray(ToStringSegment(fmt), args, 3);
}

template <typename Fmt, typename T1, typename T2, typename T3, typename T4>
inline loka::core::String
loka::core::String::Format(const Fmt &fmt, const T1 &a1, const T2 &a2, const T3 &a3, const T4 &a4)
{
  loka::core::String args[4];
  args[0] = ToStringSegment(a1);
  args[1] = ToStringSegment(a2);
  args[2] = ToStringSegment(a3);
  args[3] = ToStringSegment(a4);
  return FormatArray(ToStringSegment(fmt), args, 4);
}

#endif // LOKA_CORE_STRING_HPP
