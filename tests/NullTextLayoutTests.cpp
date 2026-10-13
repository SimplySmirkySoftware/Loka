#include "NullTextLayoutTests.hpp"
#include "support/TestVerify.hpp"
#include "support/RailTextLayoutFixture.hpp"

#include <cassert>

#include "app/nodes/Text.hpp"
#include "platform/null/NullScenePlatformController.hpp"
#include "platform/null/context/NullTextContext.hpp"

void testNullTextLayoutWordAndCharacterWrapProduceDifferentGeometry()
{
  loka::app::TextProps noneProps("aa bbbb cc");
  noneProps.blockStyle_ = loka::app::BlockStyle().wrap(loka::app::TEXT_WRAP_NONE);
  loka::app::TextNode noneText(noneProps);
  loka::app::TextProps wordProps("aa bbbb cc");
  wordProps.blockStyle_ = loka::app::BlockStyle().wrap(loka::app::TEXT_WRAP_WORD);
  loka::app::TextNode wordText(wordProps);
  loka::app::TextProps characterProps("aa bbbb cc");
  characterProps.blockStyle_ = loka::app::BlockStyle().wrap(loka::app::TEXT_WRAP_CHAR);
  loka::app::TextNode characterText(characterProps);
  loka::app::scene::LayoutState state;
  state.y = 5;
  state.width = 24;
  state.lineHeight = 10;
  NullScenePlatformController platform;

  const int noneResultY = platform.projectLayoutForTesting(&noneText, state);
  const int wordResultY = platform.projectLayoutForTesting(&wordText, state);
  const int characterResultY = platform.projectLayoutForTesting(&characterText, state);

  (void)noneResultY;
  (void)wordResultY;
  assert(noneResultY != wordResultY);
  (void)characterResultY;
  assert(noneResultY != characterResultY);
  assert(wordResultY != characterResultY);
  assert(noneResultY == 17);
  assert(wordResultY == 41);
  assert(characterResultY == 29);
}

namespace
{
  const NullTextMeasurement &measurementFor(loka::app::TextNode &text)
  {
    NullTextContext *context = static_cast<NullTextContext *>(text.getContext());
    assert(context);
    return context->measurement();
  }
} // namespace

void testNullTextLayoutTruncationModesProduceDifferentWidths()
{
  (void)&measurementFor;
  loka::app::TextProps noneProps("abcdefghij");
  noneProps.blockStyle_ = loka::app::BlockStyle().truncation(loka::app::TEXT_TRUNCATION_NONE);
  loka::app::TextNode noneText(noneProps);
  loka::app::TextProps clipProps("abcdefghij");
  clipProps.blockStyle_ = loka::app::BlockStyle().truncation(loka::app::TEXT_TRUNCATION_CLIP);
  loka::app::TextNode clipText(clipProps);
  loka::app::TextProps ellipsisProps("abcdefghij");
  ellipsisProps.blockStyle_ = loka::app::BlockStyle().truncation(loka::app::TEXT_TRUNCATION_ELLIPSIS);
  loka::app::TextNode ellipsisText(ellipsisProps);
  loka::app::scene::LayoutState state;
  state.width = 22;
  state.lineHeight = 10;
  NullScenePlatformController platform;

  platform.projectLayoutForTesting(&noneText, state);
  platform.projectLayoutForTesting(&clipText, state);
  platform.projectLayoutForTesting(&ellipsisText, state);

  assert(measurementFor(noneText).width() == 40);
  assert(measurementFor(clipText).width() == 22);
  assert(measurementFor(ellipsisText).width() == 20);
  // Characterize the existing ellipsis snap independently of role resolution.
  const loka::app::TextLineRecord rawLine = {loka::app::TextLineMetrics(16), 100, 0, 0};
  LOKA_VERIFY(loka::app::SyntheticTextLineWidth(
                  rawLine, loka::app::BlockStyle().truncation(loka::app::TEXT_TRUNCATION_ELLIPSIS), 13)
              == 10);
}

void testNullTextLayoutHonorsExplicitBreaksAndForceBreaksLongWords()
{
  // Null has no spaceScale input. It can nevertheless discriminate the
  // shared Box/Column contract: extra native-equivalent lines inside a fixed
  // page cannot move the later caption/button rows.
  for (int lines = 1; lines <= 2; ++lines)
  {
    NullScenePlatformController controller;
    RailTextLayoutFixture fixture(true, lines == 1 ? "First" : "First\nSecond");
    loka::app::scene::LayoutState frame;
    frame.x = 20;
    frame.y = 20;
    frame.width = 300;
    frame.height = 210;
    frame.lineHeight = 20;
    frame.spacing = 12;
    const int bottom = controller.projectLayoutForTesting(&fixture.column, frame);
    const NullTextContext *text = static_cast<const NullTextContext *>(fixture.wrapped->getContext());
    LOKA_VERIFY(text != 0);
    LOKA_VERIFY(text->measurement().lineCount() == lines);
    std::printf("  Null fixed text column: lines=%d resultY=%d\n", lines, bottom);
    std::fflush(stdout);
    // Null's deterministic default font is 12 lu, unlike native Text's 20 lu
    // minimum: 20 + page 170 + caption 12 + gap 12 + button 32 + trailing gap 12.
    LOKA_VERIFY(bottom == 258);
  }

  loka::app::TextProps breakProps("a\nb");
  breakProps.blockStyle_ = loka::app::BlockStyle().wrap(loka::app::TEXT_WRAP_WORD);
  loka::app::TextNode breakText(breakProps);
  loka::app::TextProps longWordProps("abcdefgh");
  longWordProps.blockStyle_ = loka::app::BlockStyle().wrap(loka::app::TEXT_WRAP_WORD);
  loka::app::TextNode longWordText(longWordProps);
  loka::app::scene::LayoutState state;
  state.width = 16;
  state.lineHeight = 10;
  NullScenePlatformController platform;

  const int breakResultY = platform.projectLayoutForTesting(&breakText, state);
  const int longWordResultY = platform.projectLayoutForTesting(&longWordText, state);

  (void)breakResultY;
  assert(breakResultY == 24);
  assert(measurementFor(breakText).width() == 4);
  assert(measurementFor(breakText).lineCount() == 2);
  (void)longWordResultY;
  assert(longWordResultY == 24);
  assert(measurementFor(longWordText).width() == 16);
  assert(measurementFor(longWordText).lineCount() == 2);
}

void testNullTextLayoutUsesFixedAdvancePerCodePoint()
{
  const char utf8[] = "\xC3\xA9\xC3\xA9\xC3\xA9";
  loka::app::TextProps props(loka::core::String::Utf8(utf8, 6));
  loka::app::TextNode text(props);
  loka::app::scene::LayoutState state;
  state.width = 100;
  NullScenePlatformController platform;

  platform.projectLayoutForTesting(&text, state);

  assert(measurementFor(text).width() == 12);
  assert(measurementFor(text).lineCount() == 1);
}

void testNullTextLayoutPreservesNegativeStartY()
{
  loka::app::TextNode text((loka::app::TextProps("a")));
  loka::app::scene::LayoutState state;
  state.y = -20;
  state.width = 100;
  state.lineHeight = 10;
  NullScenePlatformController platform;

  const int resultY = platform.projectLayoutForTesting(&text, state);

  (void)resultY;
  assert(resultY == -8);
  assert(measurementFor(text).height() == 12);
}

void testNullTextLayoutWrapsAtPositiveSubGlyphWidth()
{
  loka::app::TextProps wordProps("ab");
  wordProps.blockStyle_ = loka::app::BlockStyle().wrap(loka::app::TEXT_WRAP_WORD);
  loka::app::TextNode wordText(wordProps);
  loka::app::TextProps characterProps("ab");
  characterProps.blockStyle_ = loka::app::BlockStyle().wrap(loka::app::TEXT_WRAP_CHAR);
  loka::app::TextNode characterText(characterProps);
  loka::app::scene::LayoutState state;
  state.width = 2;
  state.lineHeight = 10;
  NullScenePlatformController platform;

  const int wordResultY = platform.projectLayoutForTesting(&wordText, state);
  const int characterResultY = platform.projectLayoutForTesting(&characterText, state);

  (void)wordResultY;
  assert(wordResultY == 24);
  (void)characterResultY;
  assert(characterResultY == 24);
  assert(measurementFor(wordText).width() == 4);
  assert(measurementFor(characterText).width() == 4);
  assert(measurementFor(wordText).lineCount() == 2);
  assert(measurementFor(characterText).lineCount() == 2);
}

void testNullTextLayoutWordWrapMeasuresStandaloneSpaces()
{
  loka::app::TextProps props("    ");
  props.blockStyle_ = loka::app::BlockStyle().wrap(loka::app::TEXT_WRAP_WORD);
  loka::app::TextNode text(props);
  loka::app::scene::LayoutState state;
  state.width = 8;
  state.lineHeight = 10;
  NullScenePlatformController platform;

  const int resultY = platform.projectLayoutForTesting(&text, state);

  (void)resultY;
  assert(resultY == 24);
  assert(measurementFor(text).width() == 8);
  assert(measurementFor(text).lineCount() == 2);
}

void testNullTextSmallLabelMetrics()
{
  using namespace loka::app;
  NullScenePlatformController platform;
  TextNode small((Text("MMMM") + SmallLabel).props);
  TextNode plain(Text("MMMM").props);
  scene::LayoutState state;
  state.lineHeight = 0;
  const int smallY = platform.projectLayoutForTesting(&small, state);
  const int plainY = platform.projectLayoutForTesting(&plain, state);
  LOKA_VERIFY(smallY == 9 && plainY == 12);
  const NullTextMeasurement &smallMeasurement = measurementFor(small);
  const NullTextMeasurement &plainMeasurement = measurementFor(plain);
  LOKA_VERIFY(smallMeasurement.height() == 9);
  LOKA_VERIFY(smallMeasurement.width() < plainMeasurement.width());
  LOKA_VERIFY(NullTextPaintStyle(small.props).fontSize == 9);
  NullTextMeasurement refused;
  testing::NullTextMeasurementAccess::failMeasure(1);
  const bool measured = MeasureNullText(SmallLabel, BlockStyle(), 0, state, refused);
  LOKA_VERIFY(!measured && refused.height() == 9);
}
