#include "StyleTests.hpp"
#include "TextFamilyTests.hpp"
#include "app/style/AttributedString.hpp"
#include "app/nodes/nestable/Box.hpp"
#include "support/PropsReconciliation.hpp"
#include "testing/scene/SceneTestFlow.hpp"

#include <climits>
#include "core/State.hpp"
#include "core/util/StateTrackerGuard.hpp"

#include "app/nodes/Text.hpp"
#include "app/nodes/nestable/Show.hpp"
#include "platform/null/NullScenePlatformController.hpp"
#include "platform/null/context/NullTextContext.hpp"
#include "support/TestVerify.hpp"

#define LOKA_STYLE_VOCAB_DEFINE_NAMES
#include "app/style/StyleVocab.hpp"
#undef LOKA_STYLE_VOCAB_DEFINE_NAMES

namespace
{
  // A namespace-scope style composed from the named styles, the shape an
  // application would write. It must be initialised after Bold and Italic,
  // which the header guarantees by defining them per translation unit.
  const loka::app::TextStyle kWarningStyle = loka::app::Bold + loka::app::Italic;
}

void testTextStyleWeightMerge()
{
  using namespace loka::app;
  const TextStyle normal = TextStyle().weight(TEXT_WEIGHT_NORMAL);
  LOKA_VERIFY(TextStyle() + Bold == Bold);
  LOKA_VERIFY(Bold + TextStyle() == Bold);
  LOKA_VERIFY(Bold + normal == normal);
  LOKA_VERIFY(normal + Bold == Bold);
}

void testTextStyleWeightEquality()
{
  using namespace loka::app;
  LOKA_VERIFY(TextStyle() != TextStyle().weight(TEXT_WEIGHT_NORMAL));
  LOKA_VERIFY(TextStyle().weight(TEXT_WEIGHT_BOLD) == Bold);
  LOKA_VERIFY(TextStyle().weight(TEXT_WEIGHT_NORMAL) != Bold);
}

void testTextStyleWeightOrdering()
{
  using namespace loka::app;
  const TextStyle unset;
  const TextStyle normal = TextStyle().weight(TEXT_WEIGHT_NORMAL);
  LOKA_VERIFY(unset < normal);
  LOKA_VERIFY(!(normal < unset));
  LOKA_VERIFY(normal < Bold);
  LOKA_VERIFY(unset < Bold);
  const TextStyle unsetItalic = TextStyle().italic();
  const TextStyle normalPlain = normal + TextStyle().italic(false);
  LOKA_VERIFY(normalPlain < unsetItalic);
  LOKA_VERIFY(!(unsetItalic < normalPlain));
}

void testTextStyleWeightDeclaresFont()
{
  using namespace loka::app;
  LOKA_VERIFY(!TextStyle().declaresFont());
  LOKA_VERIFY(!TextStyle().color(TEXT_COLOR_RED).declaresFont());
  LOKA_VERIFY(TextStyle().weight(TEXT_WEIGHT_NORMAL).declaresFont());
  LOKA_VERIFY(Bold.declaresFont());
}

void testTextStyleWeightReadingPresence()
{
  using namespace loka::app;
  const TextStyle styles[] = {
      TextStyle(), TextStyle().weight(TEXT_WEIGHT_NORMAL), Bold, Bold + TextStyle().weight(TEXT_WEIGHT_NORMAL)};
  for (int i = 0; i < 4; ++i)
  {
    const NullTextPaintStyle projected((Text("x") + styles[i]).props);
    LOKA_VERIFY(projected.weight == (i == 2 ? TEXT_WEIGHT_BOLD : TEXT_WEIGHT_NORMAL));
    LOKA_VERIFY(styles[i].declaresFont() == (i != 0));
  }
}

void testTextStyleMergeAndSizeVocabulary()
{
  using namespace loka::app;
  LOKA_VERIFY(kWarningStyle.hasWeight_ && kWarningStyle.weight_ == TEXT_WEIGHT_BOLD);
  bool on = false;
  LOKA_VERIFY(kWarningStyle.queryItalic(on) && on);
  const TextSizeValue sizes[] = {TextSizeValue(),
                                 TextSizeValue::points(-1),
                                 TextSizeValue::points(0),
                                 TextSizeValue::points(11),
                                 TextSizeValue::points(24),
                                 TextSizeValue::role(TEXT_SIZE_ROLE_SMALL_LABEL)};
  int untouched = 71;
  LOKA_VERIFY(!sizes[0].queryPoints(untouched) && untouched == 71);
  for (int i = 0; i < 6; ++i)
  {
    const TextSizeValue copy(sizes[i]);
    LOKA_VERIFY(copy == sizes[i]);
    LOKA_VERIFY(copy.isSet() == (i != 0));
    for (int j = 0; j < 6; ++j)
    {
      LOKA_VERIFY((sizes[i] == sizes[j]) == (i == j));
      LOKA_VERIFY((sizes[i] == sizes[j]) == (!(sizes[i] < sizes[j]) && !(sizes[j] < sizes[i])));
      for (int k = 0; k < 6; ++k)
        LOKA_VERIFY(!(sizes[i] < sizes[j]) || !(sizes[j] < sizes[k]) || sizes[i] < sizes[k]);
    }
  }
  LOKA_VERIFY(sizeof(TextSizeValue) == 4);
  LOKA_VERIFY(sizeof(TextFamilyValue) == 1);
  LOKA_VERIFY(sizeof(TextStyle) == 16);
#if __cplusplus >= 201103L
  static_assert(sizeof(TextFamilyValue) == 1, "TextFamilyValue must remain one byte");
  static_assert(sizeof(TextStyle) == 16, "TextStyle must remain sixteen bytes");
#endif
  TextSizeRole role = TEXT_SIZE_ROLE_COUNT;
  LOKA_VERIFY(!sizes[0].queryRole(role) && role == TEXT_SIZE_ROLE_COUNT);
  LOKA_VERIFY(!sizes[3].queryRole(role) && role == TEXT_SIZE_ROLE_COUNT);
  LOKA_VERIFY(sizes[5].queryRole(role) && role == TEXT_SIZE_ROLE_SMALL_LABEL);
  LOKA_VERIFY(!sizes[5].queryPoints(untouched) && untouched == 71);
  LOKA_VERIFY(FontSize<12>() + SmallLabel == SmallLabel);
  LOKA_VERIFY(SmallLabel + FontSize<12>() == FontSize<12>());
  LOKA_VERIFY(SmallLabel + TextStyle() == SmallLabel);
  LOKA_VERIFY(TextStyle() + SmallLabel == SmallLabel);
  LOKA_VERIFY(SmallLabel != FontSize<9>() && SmallLabel != TextStyle());
  LOKA_VERIFY(TextStyle() < SmallLabel && SmallLabel < FontSize<9>());
  LOKA_VERIFY((Text("x") + SmallLabel).props.hasDeclaredLayoutStyle());
  const TextStyle boldLarge = Bold + FontSize<24>();
  const TextStyle largeBold = FontSize<24>() + Bold;
  LOKA_VERIFY(boldLarge == largeBold);
  int points = 0;
  LOKA_VERIFY(boldLarge.size_.queryPoints(points) && points == 24);
  LOKA_VERIFY(boldLarge.hasWeight_ && boldLarge.weight_ == TEXT_WEIGHT_BOLD);

  const TextStyle replaced = FontSize<12>() + FontSize<24>();
  LOKA_VERIFY(replaced.size_.queryPoints(points) && points == 24);
  LOKA_VERIFY(TextStyle() + FontSize<12>() == FontSize<12>());
  LOKA_VERIFY(FontSize<12>() + TextStyle() == FontSize<12>());
  LOKA_VERIFY(TextStyle() != FontSize<12>());
  LOKA_VERIFY(TextStyle() < FontSize<12>());
  LOKA_VERIFY(!(FontSize<12>() < TextStyle()));
  LOKA_VERIFY(FontSize<12>() < FontSize<24>());
  LOKA_VERIFY(!(FontSize<12>() < FontSize<12>()));
  LOKA_VERIFY((TextStyle() + Bold) < FontSize<12>());
  TextStyle rawZero;
  rawZero.size_ = TextSizeValue::points(0);
  TextStyle rawNegative;
  rawNegative.size_ = TextSizeValue::points(-1);
  LOKA_VERIFY(rawZero != TextStyle());
  LOKA_VERIFY(TextStyle() < rawZero);
  LOKA_VERIFY(rawNegative < TextStyle());
  LOKA_VERIFY(rawZero < Bold);
  LOKA_VERIFY(!(Bold < rawZero));
  LOKA_VERIFY(TextStyle() + Bold == Bold);
  LOKA_VERIFY(Bold + TextStyle() == Bold);
  LOKA_VERIFY(Italic.queryItalic(on) && on);
  LOKA_VERIFY(Body == FontSize<12>());
  LOKA_VERIFY(Caption == FontSize<9>());
  LOKA_VERIFY(Heading == FontSize<12>() + Bold);
  LOKA_VERIFY(Title == FontSize<18>() + Bold);

  const BlockStyle wrapped = BlockStyle().wrap(TEXT_WRAP_WORD);
  const BlockStyle clipped = wrapped + BlockStyle().truncation(TEXT_TRUNCATION_CLIP);
  LOKA_VERIFY(clipped.hasWrap_ && clipped.wrap_ == TEXT_WRAP_WORD);
  LOKA_VERIFY(clipped.hasTruncation_ && clipped.truncation_ == TEXT_TRUNCATION_CLIP);
  LOKA_VERIFY(clipped + BlockStyle() == clipped);
}

void testBlockStyleNamedAlignmentsEqualTheirDeclarations()
{
  using namespace loka::app;
  LOKA_VERIFY(block::Centered == BlockStyle().align(TEXT_ALIGN_CENTER));
  LOKA_VERIFY(block::RightAligned == BlockStyle().align(TEXT_ALIGN_RIGHT));
  // The names carry only the alignment field, so they merge over wrap/truncation.
  const BlockStyle merged = BlockStyle().wrap(TEXT_WRAP_WORD) + block::Centered;
  LOKA_VERIFY(merged.hasWrap_ && merged.wrap_ == TEXT_WRAP_WORD);
  LOKA_VERIFY(merged.hasAlign_ && merged.align_ == TEXT_ALIGN_CENTER);
  LOKA_VERIFY(!merged.hasTruncation_);
}

void testSizeOfSnapsToNearestVocabularySizeWithTiesDown()
{
  using namespace loka::app;
  const int inputs[] = {INT_MIN, 0, 9, 10, 11, 12, 13, 16, 18, 21, 24, 99, INT_MAX};
  const int expected[] = {9, 9, 9, 10, 10, 12, 12, 14, 18, 18, 24, 24, 24};
  const int count = static_cast<int>(sizeof(inputs) / sizeof(inputs[0]));
  for (int i = 0; i < count; ++i)
  {
    const TextStyle style = SizeOf(inputs[i]);
    int points = 0;
    LOKA_VERIFY(style.size_.queryPoints(points));
    LOKA_VERIFY(points == expected[i]);
  }
}

void testTextPropsItalicParticipatesInDefinitionEquivalence()
{
  using namespace loka::app;
  const TextDefinition plain = Text("same");
  const TextDefinitionWithAttr italic = Text("same") + Italic;
  LOKA_VERIFY((plain.props < italic.props) || (italic.props < plain.props));
}

namespace
{
  short layoutHeight(const loka::app::TextDefinitionWithAttr &definition)
  {
    loka::app::TextNode node(definition.props);
    loka::app::scene::LayoutState state;
    state.width = 100;
    NullScenePlatformController platform;
    (void)platform.projectLayoutForTesting(&node, state);
    NullTextContext *context = static_cast<NullTextContext *>(node.getContext());
    LOKA_VERIFY(context != 0);
    return context->measurement().height();
  }
}

void testNullTextLayoutUsesResolvedFontSize()
{
  const short small = layoutHeight(loka::app::Text("x") + loka::app::FontSize<12>());
  const short large = layoutHeight(loka::app::Text("x") + loka::app::FontSize<24>());
  loka::core::MutableState<loka::app::TextStyle> live((loka::app::FontSize<24>()));
  const short liveWins = layoutHeight((loka::app::Text("x") + loka::app::FontSize<12>()) + &live);
  LOKA_VERIFY(small == 12);
  LOKA_VERIFY(large == 24);
  LOKA_VERIFY(liveWins == 24);
  LOKA_VERIFY(small != large);
}

void testTextStyleDslPreservesDefinitionPolicies()
{
  using namespace loka::app;
  TextDefinition source("x");
  source.testId("StyledText");
  source.setNodeTag(41);
  source.setCompositionSeatSlot(7);
  source.setNativeLifetimeHint(scene::NATIVE_HINT_DESIRE_STAY);
  const TextDefinitionWithAttr result = source + Bold;
  LOKA_VERIFY(result.hasTestId());
  LOKA_VERIFY(result.testIdValue() == "StyledText");
  LOKA_VERIFY(result.nodeTag() == 41);
  LOKA_VERIFY(result.compositionSeatSlot() == 7);
  LOKA_VERIFY(result.nativeLifetimeHint() == scene::NATIVE_HINT_DESIRE_STAY);
}

void testTextPropsDeclaredStyleGuardsNativeLabelConfiguration()
{
  using namespace loka::app;
  // The rails configure their native label (single-line mode, clipping,
  // SS_LEFTNOWORDWRAP) only when a layout style field was declared; an undeclared
  // Text keeps the platform defaults (#814).
  LOKA_VERIFY(!TextProps("plain").hasDeclaredLayoutStyle());
  LOKA_VERIFY((Text("x") + Bold).props.hasDeclaredLayoutStyle());
  LOKA_VERIFY((Text("x") + Italic).props.hasDeclaredLayoutStyle());
  LOKA_VERIFY((Text("x") + FontSize<12>()).props.hasDeclaredLayoutStyle());
  LOKA_VERIFY((Text("x") + BlockStyle().wrap(TEXT_WRAP_WORD)).props.hasDeclaredLayoutStyle());
  LOKA_VERIFY((Text("x") + BlockStyle().truncation(TEXT_TRUNCATION_ELLIPSIS)).props.hasDeclaredLayoutStyle());
  loka::core::MutableState<TextStyle> live;
  LOKA_VERIFY((Text("x") + &live).props.hasDeclaredLayoutStyle());
  // An unset field on a declared style does not undeclare it, and merging an
  // empty style over nothing declares nothing.
  LOKA_VERIFY(!(Text("x") + TextStyle()).props.hasDeclaredLayoutStyle());
}

void testTextColorVocabularyAndMerge()
{
  using namespace loka::app;
  LOKA_VERIFY(TEXT_COLOR_UNSET == 0);
  LOKA_VERIFY(TextStyle().color_ == TEXT_COLOR_UNSET);
  const TextStyle colors[] = {Primary, Secondary, Black, White, Red, Green, Blue, Cyan, Magenta, Yellow};
  LOKA_VERIFY(sizeof(colors) / sizeof(colors[0])
              == sizeof(detail::StyleVocabularyColors) / sizeof(detail::StyleVocabularyColors[0]));
  for (unsigned i = 0; i < sizeof(detail::StyleVocabularyColors) / sizeof(detail::StyleVocabularyColors[0]); ++i)
  {
    // Enum rows and named styles are independent generated sources.
    LOKA_VERIFY(colors[i].color_ == detail::StyleVocabularyColors[i].value);
    LOKA_VERIFY(detail::StyleVocabularyColors[i].value == static_cast<TextColor>(i + 1));
  }
  LOKA_VERIFY(Red + Blue == Blue);
  LOKA_VERIFY(Red + TextStyle() == Red);
  const TextStyle warning = Bold + Red;
  LOKA_VERIFY((warning + Primary).color_ == TEXT_COLOR_PRIMARY);
  LOKA_VERIFY((warning + Primary).weight_ == TEXT_WEIGHT_BOLD);
  LOKA_VERIFY(Red != Blue);
  LOKA_VERIFY(Red < Blue);
  LOKA_VERIFY(!(Blue < Red));
  LOKA_VERIFY(!(Red < Red));
  LOKA_VERIFY(!((Text("x") + Red).props.hasDeclaredLayoutStyle()));
  LOKA_VERIFY((Text("x") + Red).props.resolvedTextStyle().color_ == TEXT_COLOR_RED);
  loka::core::PushStateTracker tracker;
  loka::core::MutableState<TextStyle> live(Red);
  tracker.addState(&live);
  const TextDefinitionWithAttr text = Text("x") + &live;
  LOKA_VERIFY(text.props.resolvedTextStyle().color_ == TEXT_COLOR_RED);
  {
    loka::core::StateTrackerGuard guard(&tracker);
    live.set(TextStyle());
  }
  LOKA_VERIFY(text.props.resolvedTextStyle().color_ == TEXT_COLOR_UNSET);
  tracker.removeState(&live);
}

void testTextPropsOwnedAndBorrowedStorage()
{
  using namespace loka::app;
  using namespace loka::core;
  PushStateTracker tracker;
  MutableState<String> external(String::Literal("external"));
  TextProps original("owned");
  TextProps copied(original);
  LOKA_VERIFY(copied.text_ == &copied.ownedText);
  tracker.addState(&original.ownedText);
  {
    StateTrackerGuard guard(&tracker);
    original.ownedText.set(String::Literal("changed"));
  }
  LOKA_VERIFY(copied.text_->get().compare(String::Literal("owned")) == 0);
  tracker.removeState(&original.ownedText);

  TextProps borrowed(&external);
  TextProps borrowedCopy(borrowed);
  LOKA_VERIFY(borrowedCopy.text_ == &external);

  TextProps assigned(&external);
  assigned = original;
  LOKA_VERIFY(assigned.text_ == &assigned.ownedText);
  LOKA_VERIFY(assigned.text_->get().compare(String::Literal("changed")) == 0);
  assigned = borrowed;
  LOKA_VERIFY(assigned.text_ == &external);
  assigned.text("x");
  LOKA_VERIFY(assigned.text_ == &assigned.ownedText);
  LOKA_VERIFY(assigned.text_->get().compare(String::Literal("x")) == 0);
  assigned.text(&external);
  LOKA_VERIFY(assigned.text_ == &external);
  assigned.text("again");
  LOKA_VERIFY(assigned.text_ == &assigned.ownedText);
  LOKA_VERIFY(assigned.text_->get().compare(String::Literal("again")) == 0);

  LOKA_VERIFY(borrowed < original);
  LOKA_VERIFY(!(original < borrowed));
  LOKA_VERIFY(!(borrowed < borrowedCopy));
  LOKA_VERIFY(!(copied < TextProps("owned")));
}

void testTextFamilyRejectsBeforeNarrowing()
{
  using namespace loka::app;
  const int invalid[] = {TEXT_FAMILY_COUNT, -1, 255, 256, 257, 258, INT_MIN, INT_MAX};
  for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i)
  {
    const TextFamilyValue refused(invalid[i]);
    LOKA_VERIFY(!refused.isSet());
    LOKA_VERIFY(refused.family() == TEXT_FAMILY_UNSET);
  }
  LOKA_VERIFY(TextStyle().family(TEXT_FAMILY_COUNT).family() == TEXT_FAMILY_UNSET);
}

void testTextFamilyMergeAndComparisons()
{
  using namespace loka::app;
  const TextStyle system = TextStyle().family(TEXT_FAMILY_SYSTEM);
  LOKA_VERIFY(TEXT_FAMILY_UNSET == 0);
  LOKA_VERIFY(!TextStyle().declaresFont());
  LOKA_VERIFY(!Red.declaresFont());
  LOKA_VERIFY(Monospace.declaresFont());
  LOKA_VERIFY(system.declaresFont());
  LOKA_VERIFY(Bold.declaresFont() && Italic.declaresFont() && Body.declaresFont());
  LOKA_VERIFY((Text("x") + Monospace).props.hasDeclaredLayoutStyle());
  LOKA_VERIFY((Monospace + FontSize<12>() + Bold).family() == TEXT_FAMILY_MONOSPACE);
  LOKA_VERIFY(Monospace + FontSize<12>() == Body + Monospace);
  int points = 0;
  LOKA_VERIFY((Monospace + FontSize<12>()).size_.queryPoints(points) && points == 12);
  LOKA_VERIFY(Monospace + system == system);
  LOKA_VERIFY(system + Monospace == Monospace);
  LOKA_VERIFY(Monospace + TextStyle() == Monospace);
  LOKA_VERIFY(TextStyle() + Monospace == Monospace);
  const TextStyle values[] = {TextStyle(), system, Monospace};
  for (unsigned i = 0; i < 3; ++i)
    for (unsigned j = 0; j < 3; ++j)
    {
      LOKA_VERIFY((values[i] == values[j]) == (i == j));
      LOKA_VERIFY((values[i] < values[j]) == (i < j));
      LOKA_VERIFY((Text("same") + values[i]).props.operator<((Text("same") + values[j]).props) == (i < j));
      LOKA_VERIFY((Styled("same", values[i]) == Styled("same", values[j])) == (i == j));
      LOKA_VERIFY((Styled("same", values[i]).compare(Styled("same", values[j])) < 0) == (i < j));
    }
  LOKA_VERIFY(Styled("ab", Monospace) == Styled("a", Monospace) + Styled("b", Monospace));
  LOKA_VERIFY(Styled("ab", Monospace) != Styled("a", Monospace) + Styled("b", system));
}

void testRetainedTextFamilyRecording()
{
  using namespace loka::app;
  using namespace loka::app::scene;
  using loka::app::testing::NullTextStyleAccess;
  using loka::dsl::testing::SceneTestAccess;
  BoxDefinition box;
  box << Text("aa").testId("family");
  NullScenePlatformController platform;
  Scene scene((Boundary<PropsReconciliationSupport::Tree<BoxDefinition> >(
      PropsReconciliationSupport::Props<BoxDefinition>(&box))));
  scene.mount(&platform);
  SceneTestAccess::updateAttached(scene, true);
  TextNode *node = 0;
  loka::dsl::FlowError error;
  loka::dsl::testing::LookupNodeById(&scene, "family", node, error);
  LOKA_VERIFY(node != 0);
  LayoutState state;
  state.width = 100;
  platform.projectLayoutForTesting(SceneTestAccess::rootBoundary(scene), state);
  NullTextContext *context = static_cast<NullTextContext *>(node->getContext());
  LOKA_VERIFY(context != 0);
  const short width = context->measurement().width();
  const short height = context->measurement().height();
  LOKA_VERIFY(NullTextStyleAccess::family(*context) == TEXT_FAMILY_UNSET);
  LOKA_VERIFY((Text("aa") + Monospace).applyPropsToNode(node));
  const PaintQuery query = {platform.paintScope(), PLACEMENT_ELIGIBLE};
  LOKA_VERIFY(context->queryPaintDamage(query).reason == PAINT_REFUSED_PLACEMENT_UNSETTLED);
  PropsReconciliationSupport::root(scene)->markViewDirty(NODE_DIRTY_LAYOUT);
  LOKA_VERIFY(scene.flushInvalidation());
  LOKA_VERIFY(node->getContext() == context);
  LOKA_VERIFY(NullTextStyleAccess::family(*context) == TEXT_FAMILY_MONOSPACE);
  LOKA_VERIFY(context->measurement().width() == width && context->measurement().height() == height);
  loka::core::MutableState<TextStyle> live;
  LOKA_VERIFY((Text("aa") + &live).applyPropsToNode(node));
  PropsReconciliationSupport::root(scene)->markViewDirty(NODE_DIRTY_LAYOUT);
  LOKA_VERIFY(scene.flushInvalidation());
  LOKA_VERIFY(NullTextStyleAccess::family(*context) == TEXT_FAMILY_UNSET);
  live.set(Monospace);
  LOKA_VERIFY(scene.flushInvalidation());
  LOKA_VERIFY(NullTextStyleAccess::family(*context) == TEXT_FAMILY_MONOSPACE);
  live.set(TextStyle());
  LOKA_VERIFY(scene.flushInvalidation());
  LOKA_VERIFY(NullTextStyleAccess::family(*context) == TEXT_FAMILY_UNSET);
  SceneTestAccess::unmount(scene);
}

namespace
{
  loka::app::TextStyle italicCase(int index)
  {
    using namespace loka::app;
    TextStyle style;
    if (index & 1)
      style.family(TEXT_FAMILY_MONOSPACE);
    if (index & 2)
      style.weight(TEXT_WEIGHT_NORMAL);
    if (index & 4)
      style.size_ = TextSizeValue::points(0);
    if (index / 8)
      style.italic(index / 8 == 2);
    return style;
  }
} // namespace

void testItalicOrderAndMergeTable()
{
  // Frozen before the fold: index = italic * 8 + zeroSize * 4 + normalWeight * 2 + family.
  // Rank records every pair's exact old order, including presence between off and on.
  const int rank[] = {0, 1, 4, 5, 8, 9, 12, 13, 2, 3, 6, 7, 10, 11, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23};
  for (int i = 0; i < 24; ++i)
    for (int j = 0; j < 24; ++j)
    {
      const loka::app::TextStyle left = italicCase(i), right = italicCase(j);
      LOKA_VERIFY((left == right) == (i == j));
      LOKA_VERIFY((left != right) == (i != j));
      LOKA_VERIFY((left < right) == (rank[i] < rank[j]));
      const int mergedItalic = j / 8 ? j / 8 : i / 8;
      LOKA_VERIFY(left + right == italicCase(mergedItalic * 8 + ((i | j) & 7)));
      for (int k = 0; k < 24; ++k)
        LOKA_VERIFY(!(left < right) || !(right < italicCase(k)) || left < italicCase(k));
    }
}

void testNullItalicPaintRecording()
{
  using namespace loka::app;
  const TextStyle styles[] = {TextStyle(), TextStyle().italic(false), TextStyle().italic(true)};
  for (int i = 0; i < 3; ++i)
  {
    NullScenePlatformController platform;
    TextNode node((Text("italic") + styles[i]).props);
    loka::app::scene::LayoutState state;
    state.width = 100;
    platform.projectLayoutForTesting(&node, state);
    const NullTextContext *context = static_cast<NullTextContext *>(node.getContext());
    LOKA_VERIFY(context != 0);
    LOKA_VERIFY(loka::app::testing::NullTextStyleAccess::italic(*context) == (i == 2));
    LOKA_VERIFY(styles[i].declaresFont() == (i != 0));
  }
}

void testTextToggleValueContract()
{
  using namespace loka::app;
  const int invalid[] = {-1, TEXT_TOGGLE_COUNT, 255, 256, 257, 258, INT_MIN, INT_MAX};
  for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i)
  {
    const TextToggleValue refused(invalid[i]);
    bool on = true;
    LOKA_VERIFY(!refused.isSet());
    LOKA_VERIFY(!refused.query(on) && on);
    LOKA_VERIFY(refused == TextToggleValue());
  }
  const TextToggleValue values[] = {
      TextToggleValue(), TextToggleValue(TEXT_TOGGLE_OFF), TextToggleValue(TEXT_TOGGLE_ON)};
  for (int i = 0; i < 3; ++i)
  {
    bool on = true;
    LOKA_VERIFY(values[i].query(on) == (i != 0));
    LOKA_VERIFY(on == (i != 1));
    on = false;
    LOKA_VERIFY(values[i].query(on) == (i != 0));
    LOKA_VERIFY(on == (i == 2));
    LOKA_VERIFY(values[i].isOn() == (i == 2));
    for (int j = 0; j < 3; ++j)
    {
      LOKA_VERIFY((values[i] == values[j]) == (i == j));
      LOKA_VERIFY((values[i] < values[j]) == (i < j));
      LOKA_VERIFY(values[i] + values[j] == values[j ? j : i]);
    }
  }
  bool on = true;
  LOKA_VERIFY(!TextStyle().queryItalic(on) && on);
  LOKA_VERIFY(TextStyle().italic(false).queryItalic(on) && !on);
  LOKA_VERIFY(TextStyle().italic(true).queryItalic(on) && on);
  LOKA_VERIFY(sizeof(TextToggleValue) == 1);
}

void testUnderlineStyleContract()
{
  using namespace loka::app;
  const TextStyle values[] = {TextStyle(), TextStyle().underline(false), Underline};
  bool on = true;
  LOKA_VERIFY(!values[0].queryUnderline(on) && on);
  LOKA_VERIFY(values[1].queryUnderline(on) && !on);
  LOKA_VERIFY(values[2].queryUnderline(on) && on);
  LOKA_VERIFY((Underline + Bold).queryUnderline(on) && on);
  LOKA_VERIFY((Underline + Bold).hasWeight_ && (Underline + Bold).weight_ == TEXT_WEIGHT_BOLD);
  LOKA_VERIFY(!Underline.declaresFont());
  LOKA_VERIFY(!(Text("same") + Underline).props.hasDeclaredLayoutStyle());
  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 3; ++j)
    {
      LOKA_VERIFY((values[i] == values[j]) == (i == j));
      LOKA_VERIFY(values[i] + values[j] == values[j ? j : i]);
      LOKA_VERIFY((values[i] < values[j]) == (i < j));
      LOKA_VERIFY((Styled("same", values[i]) == Styled("same", values[j])) == (i == j));
      for (int k = 0; k < 3; ++k)
        LOKA_VERIFY(!(values[i] < values[j]) || !(values[j] < values[k]) || values[i] < values[k]);
    }
  LOKA_VERIFY(Underline < Monospace);
  LOKA_VERIFY(!(Monospace < Underline));
  const AttributedString split = Styled("a", TextStyle()) + Styled("b", Underline);
  LOKA_VERIFY(split.valid() && split.segmentCount() == 2);
  LOKA_VERIFY(split.segment(0).style == TextStyle() && split.segment(1).style == Underline);
  LOKA_VERIFY(split != Styled("ab", TextStyle()));
}

void testNullUnderlinePaintRecording()
{
  using namespace loka::app;
  using namespace loka::app::scene;
  using loka::app::testing::NullTextStyleAccess;
  using loka::dsl::testing::SceneTestAccess;
  const TextStyle styles[] = {TextStyle(), Underline, TextStyle().underline(false), TextStyle()};
  LOKA_VERIFY(NullTextPaintStyle().underline == TextToggleValue(TEXT_TOGGLE_OFF));
  loka::core::MutableState<TextStyle> live;
  BoxDefinition box;
  box << Text("same").testId("underline");
  NullScenePlatformController platform;
  Scene scene((Boundary<PropsReconciliationSupport::Tree<BoxDefinition> >(
      PropsReconciliationSupport::Props<BoxDefinition>(&box))));
  scene.mount(&platform);
  SceneTestAccess::updateAttached(scene, true);
  TextNode *node = 0;
  loka::dsl::FlowError error;
  loka::dsl::testing::LookupNodeById(&scene, "underline", node, error);
  LOKA_VERIFY(node != 0);
  for (int mode = 0; mode < 2; ++mode)
  {
    if (mode)
    {
      LOKA_VERIFY((Text("same") + &live).applyPropsToNode(node));
      PropsReconciliationSupport::root(scene)->markViewDirty(NODE_DIRTY_LAYOUT);
      PropsReconciliationSupport::settle(scene);
    }
    for (int i = 0; i < 4; ++i)
    {
      if (mode)
        live.set(styles[i]);
      else
        LOKA_VERIFY((Text("same") + styles[i]).applyPropsToNode(node));
      if (!mode)
        PropsReconciliationSupport::root(scene)->markViewDirty(NODE_DIRTY_LAYOUT);
      PropsReconciliationSupport::settle(scene);
      const NullTextContext *context = static_cast<NullTextContext *>(node->getContext());
      LOKA_VERIFY(context != 0);
      LOKA_VERIFY(NullTextStyleAccess::underline(*context)
                  == TextToggleValue(i == 1 ? TEXT_TOGGLE_ON : TEXT_TOGGLE_OFF));
      LOKA_VERIFY(node->props.text_->get().equals(loka::core::String::Literal("same")));
    }
  }
  const NullTextPaintStyle plain(Text("same").props), underlined((Text("same") + Underline).props);
  LOKA_VERIFY(!(plain == underlined));
  SceneTestAccess::unmount(scene);
}

namespace
{
  class UnderlineParkedRoot : public loka::app::scene::BoundaryNodeFor<UnderlineParkedRoot>
  {
  public:
    loka::app::scene::NodeState<bool> shown;
    loka::app::scene::NodeState<loka::app::TextStyle> style;
    explicit UnderlineParkedRoot(const loka::app::scene::BoundaryPropsFor<UnderlineParkedRoot> &props)
        : loka::app::scene::BoundaryNodeFor<UnderlineParkedRoot>(props)
    {
      this->state(this->shown, true);
      this->state(this->style, loka::app::TextStyle());
    }
    virtual bool flushViewDirtyImmediately(loka::app::scene::NodeDirtyFlags) const
    {
      return false;
    }
    virtual void composeNode(loka::app::scene::NodeComposition &composition)
    {
      composition.declare(loka::app::Show(*this->shown.state())
                          << (loka::app::Text("same") + this->style.state()).testId("parked"));
    }
  };
} // namespace

void testNullUnderlineParkedReattach()
{
  using namespace loka::app;
  using namespace loka::app::scene;
  using loka::app::testing::NullTextStyleAccess;
  using loka::dsl::testing::SceneTestAccess;
  NullScenePlatformController platform;
  Scene scene((Boundary<UnderlineParkedRoot>()));
  scene.mount(&platform);
  SceneTestAccess::updateAttached(scene, true);
  UnderlineParkedRoot *root = static_cast<UnderlineParkedRoot *>(SceneTestAccess::rootBoundary(scene));
  TextNode *node = 0;
  loka::dsl::FlowError error;
  loka::dsl::testing::LookupNodeById(&scene, "parked", node, error);
  LOKA_VERIFY(node != 0);
  const NullTextContext *context = static_cast<NullTextContext *>(node->getContext());
  LOKA_VERIFY(context != 0);
  const TextStyle styles[] = {TextStyle(), Underline, TextStyle().underline(false), TextStyle()};
  for (int i = 0; i < 4; ++i)
  {
    {
      loka::core::StateTrackerGuard guard(root->tracker());
      root->shown.set(false);
    }
    PropsReconciliationSupport::settle(scene);
    LOKA_VERIFY(node->lifecycleFact() == NODE_FACT_DETACHED_RETAINED);
    {
      loka::core::StateTrackerGuard guard(root->tracker());
      root->style.set(styles[i]);
    }
    PropsReconciliationSupport::settle(scene);
    {
      loka::core::StateTrackerGuard guard(root->tracker());
      root->shown.set(true);
    }
    PropsReconciliationSupport::settle(scene);
    LOKA_VERIFY(node->getContext() == context);
    LOKA_VERIFY(NullTextStyleAccess::underline(*context) == TextToggleValue(i == 1 ? TEXT_TOGGLE_ON : TEXT_TOGGLE_OFF));
    LOKA_VERIFY(node->props.text_->get().equals(loka::core::String::Literal("same")));
  }
  SceneTestAccess::unmount(scene);
}
