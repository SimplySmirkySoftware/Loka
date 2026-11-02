#include "TextUnitsTests.hpp"
#include "app/nodes/controls/TextEditorDiff.hpp"
#include "support/TextEditorDiffCases.hpp"
#include "core/TextUnits.hpp"
#include "support/TestVerify.hpp"
#include <cstring>

using namespace loka::core;

void testTextUnitsRoundTrip()
{
  const char bytes[] = "a\303\251\346\227\245\360\237\230\200b";
  const unsigned short expected[] = {'a', 0xE9, 0x65E5, 0xD83D, 0xDE00, 'b'};
  unsigned short units[6];
  std::size_t written = 99;
  LOKA_VERIFY(Utf8ToUtf16(bytes, sizeof(bytes) - 1, units, 6, written));
  LOKA_VERIFY(written == 6 && std::memcmp(units, expected, sizeof(expected)) == 0);
  char back[12];
  LOKA_VERIFY(Utf16ToUtf8(units, 6, back, sizeof(back), written, 0, 0));
  LOKA_VERIFY(written == sizeof(bytes) - 1 && std::memcmp(back, bytes, written) == 0);
  const char delimiters[] = "\r\n";
  LOKA_VERIFY(Utf8ToUtf16(delimiters, 2, units, 6, written));
  LOKA_VERIFY(written == 2 && units[0] == '\r' && units[1] == '\n');
  const char nul[] = {0};
  LOKA_VERIFY(Utf8ToUtf16(nul, 1, units, 6, written));
  LOKA_VERIFY(written == 1 && units[0] == 0);
}

void testTextUnitsCapacity()
{
  const char bytes[] = "a\360\237\230\200";
  unsigned short units[] = {99, 99, 99, 99};
  std::size_t written = 99;
  LOKA_VERIFY(Utf8ToUtf16(bytes, 5, units, 3, written));
  LOKA_VERIFY(written == 3 && units[3] == 99);
  units[2] = 99;
  LOKA_VERIFY(!Utf8ToUtf16(bytes, 5, units, 2, written));
  LOKA_VERIFY(written == 0 && units[2] == 99 && units[3] == 99);
  LOKA_VERIFY(Utf8ToUtf16(0, 0, 0, 0, written) && written == 0);
  LOKA_VERIFY(!Utf8ToUtf16("a", 1, 0, 0, written) && written == 0);
  const unsigned short pair[] = {'a', 0xD83D, 0xDE00};
  char out[] = {99, 99, 99, 99, 99, 99};
  LOKA_VERIFY(Utf16ToUtf8(pair, 3, out, 5, written, 0, 0));
  LOKA_VERIFY(written == 5 && out[5] == 99);
  out[4] = 99;
  LOKA_VERIFY(!Utf16ToUtf8(pair, 3, out, 4, written, 0, 0));
  LOKA_VERIFY(written == 0 && out[4] == 99 && out[5] == 99);
  LOKA_VERIFY(Utf16ToUtf8(0, 0, 0, 0, written, 0, 0) && written == 0);
  LOKA_VERIFY(!Utf16ToUtf8(pair, 3, 0, 0, written, 0, 0) && written == 0);
  // Exercise the single-byte and three-byte capacity edges independently too.
  const unsigned short widths[] = {'a', 0xE9, 0x65E5};
  const char *encoded[] = {"a", "\303\251", "\346\227\245"};
  for (std::size_t i = 0; i < 3; ++i)
  {
    LOKA_VERIFY(Utf16ToUtf8(widths + i, 1, out, i + 1, written, 0, 0));
    LOKA_VERIFY(written == i + 1 && std::memcmp(out, encoded[i], written) == 0);
    LOKA_VERIFY(!Utf16ToUtf8(widths + i, 1, out, i, written, 0, 0) && written == 0);
    LOKA_VERIFY(Utf8ToUtf16(encoded[i], i + 1, units, 1, written) && written == 1);
    LOKA_VERIFY(!Utf8ToUtf16(encoded[i], i + 1, units, 0, written) && written == 0);
  }
}

void testTextUnitsMalformedUtf8()
{
  const char *bad[] = {"\342\202", "\300\257", "\200", "\355\240\200", "\364\220\200\200"};
  const std::size_t sizes[] = {2, 2, 1, 3, 4};
  unsigned short out[8];
  for (std::size_t i = 0; i < 5; ++i)
  {
    std::size_t written = 99;
    LOKA_VERIFY(!Utf8ToUtf16(bad[i], sizes[i], out, 8, written));
    LOKA_VERIFY(written == 0);
    LOKA_VERIFY(Utf16UnitsAt(bad[i], sizes[i], sizes[i]) == -1);
  }
  std::size_t written = 99;
  LOKA_VERIFY(!Utf8ToUtf16(0, 1, out, 8, written) && written == 0);
}

void testTextUnitsInvalidUtf16()
{
  const unsigned short bad[][2] = {{0xDC00, 0}, {0xD800, 0}, {0xD800, 'a'}, {0, 'a'}, {'a', 0}};
  const std::size_t sizes[] = {1, 1, 2, 2, 2};
  char out[8];
  for (std::size_t i = 0; i < 5; ++i)
  {
    std::size_t written = 99;
    LOKA_VERIFY(!Utf16ToUtf8(bad[i], sizes[i], out, sizeof(out), written, 0, 0));
    LOKA_VERIFY(written == 0);
  }
  std::size_t written = 99;
  LOKA_VERIFY(!Utf16ToUtf8(0, 1, out, sizeof(out), written, 0, 0) && written == 0);
}

void testTextUnitsDelimiters()
{
  const char *native[] = {"a\rb", "a\nb", "a\r\nb", "\r\n\r\n", "\n\n", "\r\r", "a\r", "\r"};
  const char *expected[] = {"a\rb", "a\rb", "a\rb", "\r\r", "\r\r", "\r\r", "a\r", "\r"};
  for (std::size_t i = 0; i < 8; ++i)
  {
    unsigned short units[8];
    std::size_t written = 99;
    const std::size_t count = std::strlen(native[i]);
    LOKA_VERIFY(Utf8ToUtf16(native[i], count, units, 8, written));
    char out[8];
    LOKA_VERIFY(Utf16ToUtf8(units, count, out, sizeof(out), written, 0, 0));
    LOKA_VERIFY(written == std::strlen(expected[i]) && std::memcmp(out, expected[i], written) == 0);
  }
}

void testTextUnitsOffsets()
{
  const unsigned short units[] = {'a', '\r', '\n', 'b', 'c'};
  const std::size_t expected[] = {0, 1, 2, 2, 3, 4};
  char out[8];
  std::size_t written = 99;
  for (std::size_t i = 0; i <= 5; ++i)
  {
    std::size_t offset = i;
    LOKA_VERIFY(Utf16ToUtf8(units, 5, out, 8, written, &offset, 1));
    LOKA_VERIFY(offset == expected[i] && written == 4);
  }
  const unsigned short twice[] = {'\r', '\n', 'a', '\r', '\n', 'b'};
  std::size_t offsets[] = {1, 2, 4, 6};
  LOKA_VERIFY(Utf16ToUtf8(twice, 6, out, 8, written, offsets, 4));
  LOKA_VERIFY(offsets[0] == 1 && offsets[1] == 1 && offsets[2] == 3 && offsets[3] == 4);
  const unsigned short lone[] = {'a', '\n', 'b'};
  std::size_t offset = 3;
  LOKA_VERIFY(Utf16ToUtf8(lone, 3, out, 8, written, &offset, 1));
  LOKA_VERIFY(offset == 3 && written == 3);
  LOKA_VERIFY(Utf16ToUtf8(units, 5, out, 8, written, 0, 0));
}

void testTextUnitsBoundaries()
{
  const char row[] = "a\303\251\346\227\245\360\237\230\200b";
  const std::size_t boundaries[] = {0, 1, 3, 6, 10, 11};
  const int units[] = {0, 1, 2, 3, 5, 6};
  for (std::size_t i = 0; i < 6; ++i)
  {
    LOKA_VERIFY(Utf16UnitsAt(row, 11, boundaries[i]) == units[i]);
    LOKA_VERIFY(Utf8BytesFloor(row, 11, units[i]) == boundaries[i]);
  }
  LOKA_VERIFY(Utf8BytesFloor(row, 11, 4) == 6);
  LOKA_VERIFY(Utf8BytesFloor(row, 11, 99) == 11);
  LOKA_VERIFY(Utf8BytesFloor(row, 11, -1) == 0);
  LOKA_VERIFY(Utf16UnitsAt(0, 0, 0) == 0 && Utf8BytesFloor(0, 0, 1) == 0);
}

void testTextUnitsDiffOverloads()
{
  using namespace text_editor_diff_cases;
  for (std::size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
  {
    const std::string before(cases[i].before), after(cases[i].after);
    const loka::app::TextEditorLineDiff owned = loka::app::DiffTextEditorLines(before, after);
    const loka::app::TextEditorLineDiff borrowed =
        loka::app::DiffTextEditorLines(before.data(), before.size(), after.data(), after.size());
    LOKA_VERIFY(borrowed.first() == owned.first() && borrowed.before() == owned.before()
                && borrowed.after() == owned.after());
    LOKA_VERIFY(borrowed.first() == cases[i].first && borrowed.before() == cases[i].oldCount
                && borrowed.after() == cases[i].newCount);
  }
  const char before[] = {'a', 0, 'b', '\r', 'c'};
  const char after[] = {'a', 0, 'B', '\r', 'c'};
  const loka::app::TextEditorLineDiff raw = loka::app::DiffTextEditorLines(before, 5, after, 5);
  LOKA_VERIFY(raw.first() == 0 && raw.before() == 1 && raw.after() == 1);
  const loka::app::TextEditorLineDiff empty =
      loka::app::DiffTextEditorLines(static_cast<const char *>(0), 0, static_cast<const char *>(0), 0);
  LOKA_VERIFY(empty.first() == 1 && empty.before() == 0 && empty.after() == 0);
  const char *old[] = {"a\ra", "a\ra", "\r\r", "abcd", "abcd\r"};
  const char *now[] = {"a\r\ra", "aa", "\r", "abcd\r", "abcd"};
  const int lines[] = {1, 1, 1, 0, 1};
  const int columns[] = {0, 0, 0, 4, 0};
  for (std::size_t i = 0; i < 5; ++i)
  {
    const std::string a(old[i]), b(now[i]);
    const loka::app::TextEditorLineDiff owned = loka::app::DiffTextEditorLines(a, b, lines[i], columns[i]);
    const loka::app::TextEditorLineDiff borrowed =
        loka::app::DiffTextEditorLines(a.data(), a.size(), b.data(), b.size(), lines[i], columns[i]);
    LOKA_VERIFY(borrowed.first() == owned.first() && borrowed.before() == owned.before()
                && borrowed.after() == owned.after());
  }
}
