#include "platform/ToolboxPascalText.hpp"
#include "platform/ToolboxHfsName.hpp"
#include "platform/String.hpp"
#include "support/TestVerify.hpp"
#include "support/LokaAllocFailure.hpp"
#include "platform/ToolboxMacRoman.hpp"
#include "platform/StringUTF8.hpp"
#include "ToolboxUtf8TestString.hpp"
#include "platform/ToolboxUtf8Bytes.hpp"
#include <Script.h>
#include <Sound.h>
#include <cstdio>
#include <cstring>
#include <string>

namespace
{
  using loka::core::String;

  // Frozen from 108c68de: keep independent of the production unit decoder.
  bool DecodeScalar(const char *utf8, std::size_t remaining, unsigned long &scalar, std::size_t &length)
  {
    const unsigned char first = static_cast<unsigned char>(utf8[0]);
    unsigned long minimum = 0;
    if (first < 0x80)
    {
      scalar = first;
      length = 1;
    }
    else if ((first & 0xE0) == 0xC0)
    {
      scalar = first & 0x1F;
      length = 2;
      minimum = 0x80;
    }
    else if ((first & 0xF0) == 0xE0)
    {
      scalar = first & 0x0F;
      length = 3;
      minimum = 0x800;
    }
    else if ((first & 0xF8) == 0xF0)
    {
      scalar = first & 0x07;
      length = 4;
      minimum = 0x10000;
    }
    else
      return false;
    if (length > remaining)
      return false;
    for (std::size_t i = 1; i < length; ++i)
    {
      const unsigned char next = static_cast<unsigned char>(utf8[i]);
      if ((next & 0xC0) != 0x80)
        return false;
      scalar = (scalar << 6) | (next & 0x3F);
    }
    return scalar >= minimum && scalar <= 0x10FFFF && !(scalar >= 0xD800 && scalar <= 0xDFFF);
  }
  ToolboxTextUnit ReferenceNextTextUnit(const char *input, std::size_t remaining, bool roman)
  {
    assert(input && remaining);
    ToolboxTextUnit unit = {'?', 1, '?'};
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

  bool ReferenceEncodePascal(const loka::core::String &value, Str255 out)
  {
    out[0] = 0;
    const bool roman = GetScriptManagerVariable(smSysScript) == smRoman;
    std::string utf8;
    if (!loka::platform::CollectUtf8(value, utf8))
      return false;
    // Stream straight into the capped output: building a full projection here
    // would add an allocation refusal that no short label had before (T1a).
    std::size_t count = 0;
    for (std::size_t input = 0; input < utf8.size() && count < 255;)
    {
      const ToolboxTextUnit unit = ReferenceNextTextUnit(utf8.data() + input, utf8.size() - input, roman);
      out[++count] = unit.native;
      input += unit.consumed;
    }
    out[0] = static_cast<unsigned char>(count);
    return true;
  }

  void expect(const std::string &source, const std::string &expected)
  {
    Str255 out;
    std::memset(out, 0xCC, sizeof(out));
    const unsigned reads = toolbox_host::scriptReads;
    LOKA_VERIFY(ToolboxEncodePascal(String(source), out));
    LOKA_VERIFY(toolbox_host::scriptReads == reads + 1);
    LOKA_VERIFY(out[0] == expected.size());
    LOKA_VERIFY(std::memcmp(out + 1, expected.data(), expected.size()) == 0);
  }

  std::string utf8(unsigned long scalar)
  {
    std::string out;
    if (scalar < 0x80)
      out += static_cast<char>(scalar);
    else if (scalar < 0x800)
    {
      out += static_cast<char>(0xC0 | (scalar >> 6));
      out += static_cast<char>(0x80 | (scalar & 63));
    }
    else
    {
      out += static_cast<char>(0xE0 | (scalar >> 12));
      out += static_cast<char>(0x80 | ((scalar >> 6) & 63));
      out += static_cast<char>(0x80 | (scalar & 63));
    }
    return out;
  }

  void expectDecode(const unsigned char *bytes, std::size_t length, const std::string &expected)
  {
    String out = String::Literal("old");
    const unsigned reads = toolbox_host::scriptReads;
    LOKA_VERIFY(ToolboxDecodeNative(bytes, length, out));
    LOKA_VERIFY(toolbox_host::scriptReads == reads + 1);
    std::string actual;
    LOKA_VERIFY(loka::platform::CollectUtf8(out, actual));
    LOKA_VERIFY(actual == expected);
  }

  void decode()
  {
    toolbox_host::systemScript = smRoman;
    unsigned char bytes[256];
    unsigned long scalars[256];
    for (unsigned i = 0; i < 256; ++i)
    {
      bytes[i] = static_cast<unsigned char>(i);
      LOKA_VERIFY(ToolboxMacRomanDecode(bytes[i], scalars[i]));
      unsigned char encoded = 0;
      LOKA_VERIFY(ToolboxMacRomanEncode(scalars[i], encoded));
      LOKA_VERIFY(encoded == bytes[i]);
      if (i < 128) LOKA_VERIFY(scalars[i] == i);
      for (unsigned j = 128; j < i; ++j)
        LOKA_VERIFY(scalars[i] != scalars[j]);
    }
    const unsigned char cafe[] = {'c', 'a', 'f', 0x8E};
    expectDecode(cafe, sizeof(cafe), "caf\xC3\xA9");
    expectDecode(bytes + 0xC9, 1, "\xE2\x80\xA6");
    expectDecode(bytes + 0xF0, 1, "\xEF\xA3\xBF");
    expectDecode(bytes + 0xDB, 1, "\xC2\xA4");
    const unsigned char nul[] = {'a', 0, 'b'};
    expectDecode(nul, sizeof(nul), std::string("a\0b", 3));
    expectDecode(0, 0, "");
    expectDecode(bytes, 0, "");
    String out;
    LOKA_VERIFY(ToolboxDecodeNative(bytes, sizeof(bytes), out));
    ToolboxNativeText native;
    LOKA_VERIFY(native.build(out));
    LOKA_VERIFY(native.size() == sizeof(bytes));
    LOKA_VERIFY(std::memcmp(native.data(), bytes, sizeof(bytes)) == 0);

    toolbox_host::systemScript = 1;
    expectDecode(bytes, 128, std::string(reinterpret_cast<const char *>(bytes), 128));
    out = String::Literal("unchanged");
    const unsigned reads = toolbox_host::scriptReads;
    LOKA_VERIFY(!ToolboxDecodeNative(cafe, sizeof(cafe), out));
    LOKA_VERIFY(toolbox_host::scriptReads == reads + 1);
    LOKA_VERIFY(out.equals(String::Literal("unchanged")));
    expectDecode(0, 0, "");
    toolbox_host::systemScript = smRoman;
    LOKA_VERIFY(!ToolboxDecodeNative(0, 1, out));
    LOKA_VERIFY(out.equals(String::Literal("unchanged")));
  }

  void hostStubs()
  {
    toolbox_host::systemScript = smRoman;
    toolbox_host::keyboardScript = 1;
    const unsigned reads = toolbox_host::scriptReads;
    LOKA_VERIFY(GetScriptManagerVariable(smKeyScript) == 1);
    LOKA_VERIFY(toolbox_host::scriptReads == reads);
    LOKA_VERIFY(GetScriptManagerVariable(smSysScript) == smRoman);
    LOKA_VERIFY(toolbox_host::scriptReads == reads + 1);
    const unsigned beeps = toolbox_host::beeps;
    SysBeep(1);
    LOKA_VERIFY(toolbox_host::beeps == beeps + 1);
    toolbox_host::reset();
    LOKA_VERIFY(toolbox_host::beeps == 0);
    toolbox_host::systemScript = smRoman;
    toolbox_host::keyboardScript = smRoman;
  }

  void codec()
  {
    expect("", "");
    expect("ASCII ;^!</(-\r", "ASCII ;^!</(-\r");
    std::string ascii;
    for (unsigned i = 0; i < 128; ++i)
      ascii += static_cast<char>(i);
    expect(ascii, ascii);
    expect("Open\xE2\x80\xA6", "Open\xC9");
    expect("\xC3\xA9", "\x8E");
    expect("\xC2\xA4", "\xDB");
    expect("\xE2\x82\xAC", "?");
    expect("\xE6\xBC\xA2", "?");
    expect("\xF0\x9F\x98\x80", "?");
    // U+100E9 must not narrow to the representable U+00E9.
    expect("\xF0\x90\x83\xA9", "?");
    expect("\xF4\x8F\xBF\xBF", "?");
  }

  void malformed()
  {
    expect("\xE2\x28\xA1", "?(?");
    expect("\xC0\xAF", "??");
    expect("\xE0\x80\xAF", "???");
    expect("\xF0\x80\x80\xAF", "????");
    expect("\xED\xA0\x80", "???");
    expect("\xED\xBF\xBF", "???");
    expect("\xF4\x90\x80\x80", "????");
    expect("\xF5\x80\x80\x80", "????");
    expect("\x80\xBF\xFE\xFF", "????");
    expect("\xC3", "?");
    expect("\xE2\x80", "??");
    expect("\xF0\x90\x80", "???");
    expect("\xE2\xC3\xA9", "?\x8E");
  }

  void truncation()
  {
    expect(std::string(254, 'a') + "\xC3\xA9", std::string(254, 'a') + "\x8E");
    std::string repeated;
    for (unsigned i = 0; i < 255; ++i)
      repeated += "\xC3\xA9";
    expect(repeated, std::string(255, static_cast<char>(0x8E)));
    expect(repeated + "X", std::string(255, static_cast<char>(0x8E)));
    expect(std::string(254, 'a') + "\xE6\xBC\xA2" + "X", std::string(254, 'a') + "?");
    expect(std::string(255, 'a') + "\xE2\x80\xA6", std::string(255, 'a'));
  }

  void script()
  {
    toolbox_host::systemScript = 1;
    expect("ASCII", "ASCII");
    expect("\xC3\xA9\xE2\x80\xA6\xC2\xA4", "???");
    toolbox_host::systemScript = smRoman;
    expect("\xC3\xA9\xE2\x80\xA6\xC2\xA4", "\x8E\xC9\xDB");
    toolbox_host::systemScript = 7;
    expect("\xC3\xA9", "?");
    toolbox_host::systemScript = smRoman;
  }

  class RefusingString : public loka::platform::String
  {
  public:
    virtual bool appendUtf8(std::string &out) const
    {
      out += "partial";
      return false;
    }
  };

  void failure()
  {
    const String source(loka::core::Managed<loka::platform::String>::Wrap(new RefusingString()));
    Str255 out;
    std::memset(out, 0xCC, sizeof(out));
    const unsigned reads = toolbox_host::scriptReads;
    LOKA_VERIFY(!ToolboxEncodePascal(source, out));
    LOKA_VERIFY(out[0] == 0);
    LOKA_VERIFY(toolbox_host::scriptReads == reads + 1);
    Str63 name;
    LOKA_VERIFY(!loka::toolbox::CopyStringToHfsName(source, name));
    LOKA_VERIFY(name[0] == 0);
    // The capped label door streams into Str255: a refused projection table
    // must not turn a long menu or popup label into an empty one.
    loka::core::testing::failLokaAllocRaw("TextLineBreaker", "Table", 1);
    LOKA_VERIFY(ToolboxEncodePascal(String(std::string(300, 'x')), out));
    LOKA_VERIFY(out[0] == 255 && out[1] == 'x' && out[255] == 'x');
    loka::core::testing::allowLokaAllocRaw();
  }

  class AppendOnlyString : public loka::platform::String
  {
  public:
    virtual bool appendUtf8(std::string &out) const
    {
      out += "fallback\xC3\xA9";
      return true;
    }
  };

  class EmptyViewString : public loka::platform::String
  {
  public:
    virtual bool queryUtf8(loka::platform::Utf8View &out) const
    {
      out.bytes = 0;
      out.length = 0;
      return true;
    }
    virtual bool appendUtf8(std::string &) const
    {
      return true;
    }
  };

  void reference()
  {
    const String inputs[] = {String(loka::core::Managed<loka::platform::String>::Wrap(new AppendOnlyString())),
                             String(),
                             String(loka::core::Managed<loka::platform::String>::Wrap(new EmptyViewString())),
                             String(std::string("A\0\xC0\r\xE2\n", 6)),
                             String("\xC3\xA9\xE6\xBC\xA2"),
                             String(std::string(256, 'x')),
                             String(std::string(254, 'x') + "\xC3\xA9\xE6\xBC\xA2"),
                             String(loka::core::Managed<loka::platform::String>::Wrap(new RefusingString()))};
    const short scripts[] = {smRoman, 1, 7};
    for (unsigned script = 0; script < 3; ++script)
    {
      toolbox_host::systemScript = scripts[script];
      for (unsigned i = 0; i < sizeof(inputs) / sizeof(inputs[0]); ++i)
      {
        Str255 expected, actual;
        const unsigned reads = toolbox_host::scriptReads;
        const bool success = ReferenceEncodePascal(inputs[i], expected);
        LOKA_VERIFY(toolbox_host::scriptReads == reads + 1);
        LOKA_VERIFY(ToolboxEncodePascal(inputs[i], actual) == success);
        LOKA_VERIFY(toolbox_host::scriptReads == reads + 2);
        LOKA_VERIFY(actual[0] == expected[0]);
        LOKA_VERIFY(std::memcmp(actual, expected, expected[0] + 1) == 0);
      }
    }
    toolbox_host::systemScript = smRoman;
  }

  void asciiFields()
  {
    for (unsigned roman = 0; roman < 2; ++roman)
      for (unsigned byte = 0; byte < 128; ++byte)
      {
        const char input = static_cast<char>(byte);
        const ToolboxTextUnit unit = ToolboxNextTextUnit(&input, 1, roman != 0);
        LOKA_VERIFY(unit.scalar == byte);
        LOKA_VERIFY(unit.consumed == 1);
        LOKA_VERIFY(unit.native == byte);
      }
  }

  void borrowing()
  {
    const short scripts[] = {smRoman, 1, 7};
    for (unsigned script = 0; script < 3; ++script)
      for (unsigned lends = 0; lends < 2; ++lends)
        for (unsigned refuses = 0; refuses < 2; ++refuses)
        {
          toolbox_host::systemScript = scripts[script];
          unsigned appends = 0;
          const String source(loka::core::Managed<loka::platform::String>::Wrap(
              new ToolboxUtf8TestString("caf\xC3\xA9", lends != 0, refuses != 0, appends)));
          Str255 out;
          std::memset(out, 0xCC, sizeof(out));
          const unsigned reads = toolbox_host::scriptReads;
          const bool succeeds = lends || !refuses;
          LOKA_VERIFY(ToolboxEncodePascal(source, out) == succeeds);
          LOKA_VERIFY(toolbox_host::scriptReads == reads + 1);
          LOKA_VERIFY(appends == (lends ? 0U : 1U));
          LOKA_VERIFY(out[0] == (succeeds ? 4 : 0));
          if (succeeds)
          {
            const unsigned char expected[] = {'c', 'a', 'f', static_cast<unsigned char>(script ? '?' : 0x8E)};
            LOKA_VERIFY(std::memcmp(out + 1, expected, sizeof(expected)) == 0);
          }
        }
    toolbox_host::systemScript = smRoman;
    const String empty;
    const ToolboxUtf8Bytes emptyBytes(empty);
    LOKA_VERIFY(emptyBytes.valid() && emptyBytes.bytes() && emptyBytes.length() == 0);
    const String emptyView(loka::core::Managed<loka::platform::String>::Wrap(new EmptyViewString()));
    const ToolboxUtf8Bytes borrowedEmpty(emptyView);
    LOKA_VERIFY(borrowedEmpty.valid() && borrowedEmpty.bytes() && borrowedEmpty.length() == 0);
    const String refused(loka::core::Managed<loka::platform::String>::Wrap(new RefusingString()));
    const ToolboxUtf8Bytes invalid(refused);
    LOKA_VERIFY(!invalid.valid() && !invalid.bytes() && invalid.length() == 0);
  }

  void counted()
  {
    const std::string input = std::string("\xC0\xAF" "A\n\x80" "B\0\r\n\t", 10) + "\xC3\xA9";
    const std::string expected = std::string("??A\n?B\0\r\n\t", 10) + "\x8E";
    std::size_t at = 0;
    for (std::size_t i = 0; i < expected.size(); ++i)
    {
      const ToolboxTextUnit unit = ToolboxNextTextUnit(input.data() + at, input.size() - at, true);
      LOKA_VERIFY(unit.native == static_cast<unsigned char>(expected[i]));
      LOKA_VERIFY(unit.scalar == (i == 10 ? 0xE9UL : static_cast<unsigned char>(expected[i])));
      LOKA_VERIFY(unit.consumed == (i == 10 ? 2U : 1U));
      at += unit.consumed;
    }
    LOKA_VERIFY(at == input.size());
    ToolboxNativeText native;
    LOKA_VERIFY(native.build(String(input)));
    LOKA_VERIFY(native.size() == expected.size());
    LOKA_VERIFY(std::memcmp(native.data(), expected.data(), expected.size()) == 0);
    for (std::size_t end = native.size(); end;)
    {
      const std::size_t previous = native.previous(end);
      LOKA_VERIFY(native.next(previous) == end);
      end = previous;
    }
    std::string longInput;
    for (unsigned i = 0; i < 33000; ++i) longInput += "\xC3\xA9";
    LOKA_VERIFY(native.build(String(longInput)));
    LOKA_VERIFY(native.size() == 33000);
    LOKA_VERIFY(native.data()[32999] == 0x8E);
    LOKA_VERIFY(native.cappedEnd(255) == 255);
    native.clear();
    LOKA_VERIFY(!native.valid());
    LOKA_VERIFY(native.build(String::Literal("")));
    LOKA_VERIFY(native.valid() && native.size() == 0);
  }

  void hfs()
  {
    // Independent expected System 7 MacRoman repertoire in byte order.
    const unsigned short expected[128] = {
      0x00C4, 0x00C5, 0x00C7, 0x00C9, 0x00D1, 0x00D6, 0x00DC, 0x00E1,
      0x00E0, 0x00E2, 0x00E4, 0x00E3, 0x00E5, 0x00E7, 0x00E9, 0x00E8,
      0x00EA, 0x00EB, 0x00ED, 0x00EC, 0x00EE, 0x00EF, 0x00F1, 0x00F3,
      0x00F2, 0x00F4, 0x00F6, 0x00F5, 0x00FA, 0x00F9, 0x00FB, 0x00FC,
      0x2020, 0x00B0, 0x00A2, 0x00A3, 0x00A7, 0x2022, 0x00B6, 0x00DF,
      0x00AE, 0x00A9, 0x2122, 0x00B4, 0x00A8, 0x2260, 0x00C6, 0x00D8,
      0x221E, 0x00B1, 0x2264, 0x2265, 0x00A5, 0x00B5, 0x2202, 0x2211,
      0x220F, 0x03C0, 0x222B, 0x00AA, 0x00BA, 0x03A9, 0x00E6, 0x00F8,
      0x00BF, 0x00A1, 0x00AC, 0x221A, 0x0192, 0x2248, 0x2206, 0x00AB,
      0x00BB, 0x2026, 0x00A0, 0x00C0, 0x00C3, 0x00D5, 0x0152, 0x0153,
      0x2013, 0x2014, 0x201C, 0x201D, 0x2018, 0x2019, 0x00F7, 0x25CA,
      0x00FF, 0x0178, 0x2044, 0x00A4, 0x2039, 0x203A, 0xFB01, 0xFB02,
      0x2021, 0x00B7, 0x201A, 0x201E, 0x2030, 0x00C2, 0x00CA, 0x00C1,
      0x00CB, 0x00C8, 0x00CD, 0x00CE, 0x00CF, 0x00CC, 0x00D3, 0x00D4,
      0xF8FF, 0x00D2, 0x00DA, 0x00DB, 0x00D9, 0x0131, 0x02C6, 0x02DC,
      0x00AF, 0x02D8, 0x02D9, 0x02DA, 0x00B8, 0x02DD, 0x02DB, 0x02C7
    };
    for (unsigned i = 0; i < 128; ++i)
      expect(utf8(expected[i]), std::string(1, static_cast<char>(i + 128)));
    Str63 out;
    for (unsigned scriptValue = 0; scriptValue < 2; ++scriptValue)
    {
      toolbox_host::systemScript = scriptValue;
      const unsigned reads = toolbox_host::scriptReads;
      for (unsigned i = 0; i < 128; ++i)
      {
        LOKA_VERIFY(loka::toolbox::CopyStringToHfsName(String(utf8(expected[i])), out));
        LOKA_VERIFY(out[0] == 1 && out[1] == i + 128);
      }
      LOKA_VERIFY(toolbox_host::scriptReads == reads);
    }
    toolbox_host::systemScript = smRoman;
    LOKA_VERIFY(loka::toolbox::CopyStringToHfsName(String(std::string(31, 'a')), out));
    LOKA_VERIFY(out[0] == 31);
    LOKA_VERIFY(!loka::toolbox::CopyStringToHfsName(String(std::string(32, 'a')), out));
    LOKA_VERIFY(out[0] == 0);
    LOKA_VERIFY(loka::toolbox::CopyApplicationRelativeToHfsPath(String("Pictures:X.PICT"), out));
    LOKA_VERIFY(out[0] == 16 && std::memcmp(out + 1, ":Pictures:X.PICT", 16) == 0);
    LOKA_VERIFY(loka::toolbox::CopyApplicationRelativeToHfsPath(String("X"), out));
    LOKA_VERIFY(out[0] == 2 && out[1] == ':' && out[2] == 'X');
    LOKA_VERIFY(loka::toolbox::CopyApplicationRelativeToHfsPath(
        String(std::string(31, 'a') + ":" + std::string(30, 'b')), out));
    LOKA_VERIFY(out[0] == 63);
    LOKA_VERIFY(!loka::toolbox::CopyApplicationRelativeToHfsPath(
        String(std::string(31, 'a') + ":" + std::string(31, 'b')), out));
    LOKA_VERIFY(out[0] == 0);
    LOKA_VERIFY(!loka::toolbox::CopyApplicationRelativeToHfsPath(String("Pictures:\xE6\xBC\xA2"), out));
    LOKA_VERIFY(out[0] == 0);
    std::string repeated;
    for (unsigned i = 0; i < 31; ++i) repeated += "\xC3\xA9";
    LOKA_VERIFY(loka::toolbox::CopyStringToHfsName(String(repeated), out));
    LOKA_VERIFY(out[0] == 31);
    for (unsigned i = 1; i <= 31; ++i) LOKA_VERIFY(out[i] == 0x8E);
    LOKA_VERIFY(!loka::toolbox::CopyStringToHfsName(String(repeated + "\xC3\xA9"), out));
    const char *refused[] = {"", "\xE2\x82\xAC", "\xE6\xBC\xA2", "\xE2\x28\xA1",
      "\xC0\xAF", "\xED\xA0\x80", "\xF4\x90\x80\x80", "\xF0\x90\x83\xA9", "\xC3"};
    for (unsigned i = 0; i < sizeof(refused) / sizeof(refused[0]); ++i)
    {
      LOKA_VERIFY(!loka::toolbox::CopyStringToHfsName(String::Literal(refused[i]), out));
      LOKA_VERIFY(out[0] == 0);
    }
  }
}

int main(int argc, char **argv)
{
  LOKA_VERIFY(argc == 2);
  if (std::strcmp(argv[1], "codec") == 0) codec();
  else if (std::strcmp(argv[1], "malformed") == 0) malformed();
  else if (std::strcmp(argv[1], "truncation") == 0) truncation();
  else if (std::strcmp(argv[1], "script") == 0) script();
  else if (std::strcmp(argv[1], "failure") == 0) failure();
  else if (std::strcmp(argv[1], "borrowing") == 0)
    borrowing();
  else if (std::strcmp(argv[1], "reference") == 0)
    reference();
  else if (std::strcmp(argv[1], "ascii-fields") == 0)
    asciiFields();
  else if (std::strcmp(argv[1], "counted") == 0) counted();
  else if (std::strcmp(argv[1], "decode") == 0) decode();
  else if (std::strcmp(argv[1], "host-stubs") == 0) hostStubs();
  else if (std::strcmp(argv[1], "hfs") == 0) hfs();
  else LOKA_VERIFY(false);
  std::puts("Pascal text pin passed");
  return 0;
}
