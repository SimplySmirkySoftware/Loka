// Included only by MacTextEditorContext.mm in TEST_BUILD: private helpers stay private.
#include "TestVerify.hpp"
#include "LokaAllocFailure.hpp"

void testMacTextEditorConversionHelpers()
{
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  const TextCapacity capacity = TextCapacity::of(16, 128);
  RowBuffers rows;
  LOKA_VERIFY(rows.admit(capacity.lines));
  NSString *texts[] = {@"", @"a\r", @"a\r\nb\nc\rd\r\n", @"a\302\205\u2028\u2029b"};
  const unsigned short counts[] = {1, 2, 5, 1};
  for (unsigned i = 0; i < 4; ++i)
  {
    const NativeLines lines(texts[i], capacity, rows.before());
    LOKA_VERIFY(lines.result == EDITOR_OK && lines.count == counts[i]);
    LOKA_VERIFY(NSMaxRange(lines.ranges[lines.count - 1]) == [texts[i] length]);
  }
  const NativeLines crlf(@"a\r\nb", TextCapacity::of(2, 3), rows.before());
  LOKA_VERIFY(crlf.result == EDITOR_OK && crlf.count == 2);
  LOKA_VERIFY(crlf.lineAt(1) == 0 && crlf.lineAt(2) == 0 && crlf.lineAt(3) == 1);
  const NativeRow first(@"a\r\nb", crlf.ranges[0]);
  LOKA_VERIFY(first.bytesFloor(2) == 1 && first.bytesCeil(2) == 1);
  LOKA_VERIFY(NativeLines(@"a\r\nb", TextCapacity::of(1, 3), rows.before()).result == EDITOR_CAPACITY);
  // The row bound alone, with bytes to spare: refused before the second row is written.
  LOKA_VERIFY(NativeLines(@"a\rb", TextCapacity::of(1, 100), rows.before()).result == EDITOR_CAPACITY);
  const unichar malformed[][2] = {{0, 'a'}, {0xD800, 'a'}, {0xDC00, 'a'}, {0xD800, 0xD801}};
  for (unsigned i = 0; i < 4; ++i)
  {
    NSString *text = [NSString stringWithCharacters:malformed[i] length:2];
    LOKA_VERIFY(NativeLines(text, capacity, rows.before()).result == EDITOR_NON_ASCII);
  }
  const unichar lone = 0xD800;
  LOKA_VERIFY(NativeLines([NSString stringWithCharacters:&lone length:1], capacity, rows.before()).result
              == EDITOR_NON_ASCII);

  NSString *mixed = @"x\n\u00e9\U0001f600z";
  const NativeLines lines(mixed, capacity, rows.before());
  const NativeRow row(mixed, lines.ranges[1]);
  LOKA_VERIFY(row.byteLength() == 7);
  LOKA_VERIFY(row.unitsAt(0) == 0 && row.unitsAt(2) == 1 && row.unitsAt(6) == 3 && row.unitsAt(7) == 4);
  LOKA_VERIFY(row.bytesFloor(1) == 2 && row.bytesFloor(2) == 2 && row.bytesFloor(3) == 6);
  LOKA_VERIFY(row.bytesCeil(2) == 6 && row.bytesCeil(99) == 7 && row.bytesFloor(99) == 7);
  std::string encoded;
  LOKA_VERIFY(row.appendUtf8(encoded));
  LOKA_VERIFY(encoded == "\303\251\360\237\230\200z");
  const NativeRow accent(@"\u00e9", NSMakeRange(0, 1));
  LOKA_VERIFY(accent.unitsAt(2) == 1 && accent.byteLength() == 2 && accent.bytesFloor(1) == 2);

  // Shared low surrogate leaves the generic span end inside the new pair.
  NSString *before = @"x\n\U00010000", *after = @"x\n\U00010400";
  const loka::app::detail::TextChangeSpan span(NativeCharacters(before, 2), 2, NativeCharacters(after, 2), 2);
  const NativeLines afterLines(after, capacity, rows.before());
  const NSUInteger absoluteEnd = 2 + span.afterEnd();
  LOKA_VERIFY(absoluteEnd == 3 && afterLines.lineAt(absoluteEnd) == 1);
  const NativeRow pair(after, afterLines.ranges[1]);
  LOKA_VERIFY(NativeCaretBytes(pair, absoluteEnd - 2, MacTextEditorContext::STORAGE_EDIT) == 4);
  LOKA_VERIFY(pair.unitsAt(pair.bytesCeil(1)) == 2);
  LOKA_VERIFY(NativeCaretBytes(pair, 1, MacTextEditorContext::VIEW_CHANGE) == 0);
  LOKA_VERIFY(pair.byteLength() == 4 && pair.unitsAt(4) == 2);

  const std::string model("a\rb");
  LOKA_VERIFY(SameAsModel(@"a\nb", model));
  LOKA_VERIFY(!SameAsModel(@"a\nc", model));
  LOKA_VERIFY(!SameAsModel(@"a\n", model) && !SameAsModel(@"a\nbc", model));
  LOKA_VERIFY(!SameAsModel(@"a\rb", model));
  LOKA_VERIFY(SameAsModel(@"", std::string()) && !SameAsModel(nil, std::string()));
  LOKA_VERIFY(SameAsModel(@"\u00e9\n\U0001f600", "\303\251\r\360\237\230\200"));
  LOKA_VERIFY(model == "a\rb");
  loka::core::testing::failLokaAllocRaw("MacTextEditor", "NativeTextBytes", 1);
  LOKA_VERIFY(SameAsModel(@"a\nb", model));
  LOKA_VERIFY(loka::core::testing::lokaAllocRawAttempts() == 0);
  {
    const NativeTextBytes bytes(model);
    LOKA_VERIFY(bytes.copyString() == nil);
  }
  LOKA_VERIFY(loka::core::testing::lokaAllocRawAttempts() == 1);
  loka::core::testing::allowLokaAllocRaw();
  {
    const NativeTextBytes empty((std::string()));
    NSString *text = empty.copyString();
    LOKA_VERIFY(text && [text length] == 0);
    [text release];
    const NativeTextBytes bytes(model);
    text = bytes.copyString();
    LOKA_VERIFY([text isEqualToString:@"a\nb"] && model == "a\rb");
    [text release];
    // Invalid UTF-8 separately exercises NSString's nil construction result.
    const NativeTextBytes invalid(std::string("\200"));
    LOKA_VERIFY(invalid.copyString() == nil);
  }
  [pool drain];
}
