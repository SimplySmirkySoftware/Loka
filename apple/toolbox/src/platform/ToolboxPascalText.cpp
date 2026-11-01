#include "platform/ToolboxPascalText.hpp"
#include "platform/ToolboxMacRoman.hpp"
#include "platform/StringUTF8.hpp"
#include "platform/ToolboxUtf8Bytes.hpp"
#include <Script.h>
#include <cstring>

namespace
{
  void AppendDisplayScalar(std::string &utf8, unsigned long scalar)
  {
    // The Roman table contains only BMP scalars; ASCII uses the first branch.
    if (scalar < 0x80)
      utf8 += static_cast<char>(scalar);
    else if (scalar < 0x800)
    {
      utf8 += static_cast<char>(0xC0 | (scalar >> 6));
      utf8 += static_cast<char>(0x80 | (scalar & 0x3F));
    }
    else
    {
      utf8 += static_cast<char>(0xE0 | (scalar >> 12));
      utf8 += static_cast<char>(0x80 | ((scalar >> 6) & 0x3F));
      utf8 += static_cast<char>(0x80 | (scalar & 0x3F));
    }
  }

  // Validation mirrors strict HFS decoding in ToolboxHfsName.cpp; recovery is
  // deliberately different: the caller consumes only one byte on refusal.
  bool DecodeScalar(const char *utf8, std::size_t remaining,
                    unsigned long &scalar, std::size_t &length)
  {
    const unsigned char first = static_cast<unsigned char>(utf8[0]);
    unsigned long minimum = 0;
    // ToolboxNextTextUnit's inline ASCII pre-check mirrors this branch.
    if (first < 0x80) { scalar = first; length = 1; }
    else if ((first & 0xE0) == 0xC0) { scalar = first & 0x1F; length = 2; minimum = 0x80; }
    else if ((first & 0xF0) == 0xE0) { scalar = first & 0x0F; length = 3; minimum = 0x800; }
    else if ((first & 0xF8) == 0xF0) { scalar = first & 0x07; length = 4; minimum = 0x10000; }
    else return false;
    if (length > remaining)
      return false;
    for (std::size_t i = 1; i < length; ++i)
    {
      const unsigned char next = static_cast<unsigned char>(utf8[i]);
      if ((next & 0xC0) != 0x80)
        return false;
      scalar = (scalar << 6) | (next & 0x3F);
    }
    return scalar >= minimum && scalar <= 0x10FFFF
        && !(scalar >= 0xD800 && scalar <= 0xDFFF);
  }
}

ToolboxTextUnit loka::toolbox::detail::DecodeToolboxTextUnit(const char *input, std::size_t remaining, bool roman)
{
  assert(input && remaining);
  ToolboxTextUnit unit = { '?', 1, '?' };
  unsigned long scalar = 0;
  std::size_t length = 1;
  if (DecodeScalar(input, remaining, scalar, length))
  {
    unit.scalar = scalar;
    unit.consumed = length;
    if (scalar < 0x80)
      unit.native = static_cast<unsigned char>(scalar);
    else if (roman)
      ToolboxMacRomanEncode(scalar, unit.native);
  }
  return unit;
}

void ToolboxNativeText::clear()
{
  this->bytes_.clear();
  this->source_ = loka::core::String();
}

bool ToolboxNativeText::build(const loka::core::String &value)
{
  this->clear();
  const bool roman = GetScriptManagerVariable(smSysScript) == smRoman;
  std::string utf8;
  if (!loka::platform::CollectUtf8(value, utf8))
    return false;
  std::size_t count = 0;
  for (std::size_t input = 0; input < utf8.size(); ++count)
    input += ToolboxNextTextUnit(utf8.data() + input, utf8.size() - input, roman).consumed;
  if (!this->bytes_.allocate(count))
    return false;
  count = 0;
  for (std::size_t input = 0; input < utf8.size();)
  {
    const ToolboxTextUnit unit = ToolboxNextTextUnit(utf8.data() + input, utf8.size() - input, roman);
    this->bytes_[count++] = unit.native;
    input += unit.consumed;
  }
  this->source_ = value;
  return true;
}

std::size_t ToolboxNativeText::next(std::size_t offset) const
{
  assert(this->valid() && offset < this->size());
  // T1 emits one byte per strict unit. T2 supplies variable boundaries here.
  return offset + 1;
}

std::size_t ToolboxNativeText::previous(std::size_t end) const
{
  assert(this->valid() && end > 0 && end <= this->size());
  return end - 1;
}

std::size_t ToolboxNativeText::cappedEnd(std::size_t budget) const
{
  std::size_t end = 0;
  while (end < this->size())
  {
    const std::size_t next = this->next(end);
    if (next > budget)
      break;
    end = next;
  }
  return end;
}

void ToolboxNativeText::copyPascal(std::size_t start, std::size_t end, Str255 out) const
{
  assert(this->valid() && start <= end && end <= this->size() && end - start <= 255);
  out[0] = static_cast<unsigned char>(end - start);
  if (end != start)
    std::memcpy(out + 1, this->data() + start, end - start);
}

bool ToolboxEncodePascal(const loka::core::String &value, Str255 out)
{
  out[0] = 0;
  const bool roman = GetScriptManagerVariable(smSysScript) == smRoman;
  const ToolboxUtf8Bytes utf8(value);
  if (!utf8.valid())
    return false;
  // Stream straight into the capped output: building a full projection here
  // would add an allocation refusal that no short label had before (T1a).
  std::size_t count = 0;
  for (std::size_t input = 0; input < utf8.length() && count < 255;)
  {
    const ToolboxTextUnit unit = ToolboxNextTextUnit(utf8.bytes() + input, utf8.length() - input, roman);
    out[++count] = unit.native;
    input += unit.consumed;
  }
  out[0] = static_cast<unsigned char>(count);
  return true;
}

bool ToolboxDecodeNative(const unsigned char *bytes, std::size_t length, loka::core::String &out)
{
  const bool roman = GetScriptManagerVariable(smSysScript) == smRoman;
  if (!bytes && length)
    return false;
  std::string utf8;
  for (std::size_t i = 0; i < length; ++i)
  {
    unsigned long scalar = bytes[i];
    if (roman)
      ToolboxMacRomanDecode(bytes[i], scalar);
    else if (bytes[i] >= 0x80)
      return false;
    AppendDisplayScalar(utf8, scalar);
  }
  out = loka::core::String::Utf8(utf8.data(), utf8.size());
  return true;
}

loka::core::String ToolboxChosenFileDisplayName(const unsigned char *bytes, std::size_t length)
{
  assert(bytes || !length);
  const bool roman = GetScriptManagerVariable(smSysScript) == smRoman;
  std::string display;
  for (std::size_t i = 0; i < length; ++i)
  {
    unsigned long scalar = bytes[i];
    if (roman)
      ToolboxMacRomanDecode(bytes[i], scalar);
    else if (bytes[i] >= 0x80)
      scalar = '?';
    AppendDisplayScalar(display, scalar);
  }
  return loka::core::String::Utf8(display.data(), display.size());
}
