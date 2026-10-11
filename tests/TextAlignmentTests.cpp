#include "TextAlignmentTests.hpp"
#include "support/TestVerify.hpp"
#include "support/PropsReconciliation.hpp"
#include "app/layout/AlignedLineOffset.hpp"
#include "app/nodes/AttributedText.hpp"
#include "app/nodes/Text.hpp"
#include "app/nodes/nestable/Box.hpp"
#include "platform/null/NullScenePlatformController.hpp"
#include "platform/null/context/NullAttributedTextContext.hpp"
#include "platform/null/context/NullTextContext.hpp"
#include "testing/scene/SceneTestFlow.hpp"
#include <climits>

namespace
{
  using namespace loka::app;
  using namespace loka::app::scene;
  using loka::app::testing::NullTextMeasurementAccess;
  using loka::dsl::testing::SceneTestAccess;

  NullTextMeasurement project(bool attributed, const char *text, short width, const BlockStyle &block)
  {
    NullScenePlatformController platform;
    LayoutState state;
    state.width = width;
    if (attributed)
    {
      AttributedTextNode node((AttributedText(Styled(text, TextStyle())) + block).props);
      platform.projectLayoutForTesting(&node, state);
      LOKA_VERIFY(node.getContext() != 0);
      return static_cast<NullAttributedTextContext *>(node.getContext())->measurement();
    }
    TextNode node((Text(text) + block).props);
    platform.projectLayoutForTesting(&node, state);
    LOKA_VERIFY(node.getContext() != 0);
    return static_cast<NullTextContext *>(node.getContext())->measurement();
  }

  void verifyLines(bool attributed,
                   const char *text,
                   short width,
                   const BlockStyle &block,
                   const int *painted,
                   const int *offsets,
                   std::size_t count)
  {
    const NullTextMeasurement result = project(attributed, text, width, block);
    LOKA_VERIFY(result.lineCount() == static_cast<short>(count));
    for (std::size_t i = 0; i < count; ++i)
    {
      const loka::core::Frame &line = NullTextMeasurementAccess::line(result, i);
      LOKA_VERIFY(line.width == painted[i]);
      LOKA_VERIFY(line.x == offsets[i]);
      LOKA_VERIFY(line.y == static_cast<int>(i) * 12);
    }
  }

  void projectionPins(bool attributed)
  {
    const int paragraphs[] = {4, 12, 8};
    const int center[] = {8, 4, 6};
    const int right[] = {16, 8, 12};
    const int left[] = {0, 0, 0};
    verifyLines(attributed, "a\naaa\naa", 20, BlockStyle(), paragraphs, left, 3);
    verifyLines(attributed, "a\naaa\naa", 20, BlockStyle().align(TEXT_ALIGN_CENTER), paragraphs, center, 3);
    verifyLines(attributed, "a\naaa\naa", 20, BlockStyle().align(TEXT_ALIGN_RIGHT), paragraphs, right, 3);
    const int wrapped[] = {12, 12, 8};
    const int wrapCenter[] = {1, 1, 3};
    const int wrapRight[] = {2, 2, 6};
    verifyLines(
        attributed, "aaa aa a", 14, BlockStyle().wrap(TEXT_WRAP_WORD).align(TEXT_ALIGN_CENTER), wrapped, wrapCenter, 3);
    verifyLines(
        attributed, "aaa aa a", 14, BlockStyle().wrap(TEXT_WRAP_WORD).align(TEXT_ALIGN_RIGHT), wrapped, wrapRight, 3);
    const int chars[] = {12, 8};
    const int charCenter[] = {1, 3};
    const int charRight[] = {2, 6};
    verifyLines(
        attributed, "aaaaa", 14, BlockStyle().wrap(TEXT_WRAP_CHAR).align(TEXT_ALIGN_CENTER), chars, charCenter, 2);
    verifyLines(
        attributed, "aaaaa", 14, BlockStyle().wrap(TEXT_WRAP_CHAR).align(TEXT_ALIGN_RIGHT), chars, charRight, 2);
    const int zero[] = {0};
    const int twelve[] = {12};
    const int fourteen[] = {14};
    const int one[] = {1};
    const int two[] = {2};
    for (int a = TEXT_ALIGN_CENTER; a <= TEXT_ALIGN_RIGHT; ++a)
    {
      const TextAlign align = static_cast<TextAlign>(a);
      verifyLines(
          attributed, "abcdefgh", 12, BlockStyle().truncation(TEXT_TRUNCATION_CLIP).align(align), twelve, zero, 1);
      verifyLines(
          attributed, "abcdefgh", 12, BlockStyle().truncation(TEXT_TRUNCATION_ELLIPSIS).align(align), twelve, zero, 1);
      verifyLines(
          attributed, "abcdefgh", 14, BlockStyle().truncation(TEXT_TRUNCATION_CLIP).align(align), fourteen, zero, 1);
      // Fitted ellipsis leaves two pixels: raw width (32) would incorrectly yield zero.
      verifyLines(attributed,
                  "abcdefgh",
                  14,
                  BlockStyle().truncation(TEXT_TRUNCATION_ELLIPSIS).align(align),
                  twelve,
                  align == TEXT_ALIGN_CENTER ? one : two,
                  1);
    }
  }
} // namespace

void testAlignedLineOffsetTable()
{
  struct Case
  {
    int available, painted, center, right;
  };
  const Case cases[] = {{20, 4, 8, 16},
                        {20, 24, 0, 0},
                        {20, 15, 2, 5},
                        {20, 20, 0, 0},
                        {0, 0, 0, 0},
                        {0, 4, 0, 0},
                        {INT_MAX, 0, INT_MAX / 2, INT_MAX},
                        {INT_MIN, INT_MAX, 0, 0},
                        {INT_MAX, -1, 0, 0}};
  for (std::size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
  {
    LOKA_VERIFY(AlignedLineOffset(cases[i].available, cases[i].painted, TEXT_ALIGN_LEFT) == 0);
    LOKA_VERIFY(AlignedLineOffset(cases[i].available, cases[i].painted, TEXT_ALIGN_CENTER) == cases[i].center);
    LOKA_VERIFY(AlignedLineOffset(cases[i].available, cases[i].painted, TEXT_ALIGN_RIGHT) == cases[i].right);
  }
}

void testBlockStyleAlignmentValue()
{
  const BlockStyle none;
  const BlockStyle left = BlockStyle().align(TEXT_ALIGN_LEFT);
  const BlockStyle center = BlockStyle().align(TEXT_ALIGN_CENTER);
  const BlockStyle right = BlockStyle().align(TEXT_ALIGN_RIGHT);
  LOKA_VERIFY(!none.hasAlign_);
  LOKA_VERIFY(left.hasAlign_ && left.align_ == TEXT_ALIGN_LEFT);
  LOKA_VERIFY(center + right == right);
  LOKA_VERIFY(right + left == left);
  LOKA_VERIFY(right + none == right && none + right == right);
  LOKA_VERIFY(none != left && none < left && !(left < none));
  LOKA_VERIFY(left != center && left < center && !(center < left));
  LOKA_VERIFY(center != right && center < right && !(right < center));
  LOKA_VERIFY((Text("x") + center).props.hasDeclaredLayoutStyle());
  LOKA_VERIFY(!(Text("x") + none).props.hasDeclaredLayoutStyle());
  LOKA_VERIFY((center + BlockStyle().wrap(TEXT_WRAP_WORD)).align_ == TEXT_ALIGN_CENTER);
}

void testNullTextAlignmentProjection()
{
  projectionPins(false);
}
void testNullAttributedTextAlignmentProjection()
{
  projectionPins(true);
}

void testRetainedTextAlignmentRelayout()
{
  BoxDefinition box;
  box << Text("aa").testId("plain");
  box << AttributedText(Styled("aa", TextStyle())).testId("rich");
  NullScenePlatformController platform;
  Scene scene((Boundary<PropsReconciliationSupport::Tree<BoxDefinition> >(
      PropsReconciliationSupport::Props<BoxDefinition>(&box))));
  scene.mount(&platform);
  SceneTestAccess::updateAttached(scene, true);
  TextNode *plain = 0;
  Node *rich = 0;
  loka::dsl::FlowError error;
  loka::dsl::testing::LookupNodeById(&scene, "plain", plain, error);
  loka::dsl::testing::LookupNodeById(&scene, "rich", rich, error);
  LOKA_VERIFY(plain && rich);
  NodeContext *plainContext = plain->getContext();
  NodeContext *richContext = rich->getContext();
  LayoutState state;
  state.width = 20;
  platform.projectLayoutForTesting(SceneTestAccess::rootBoundary(scene), state);
  LOKA_VERIFY(NullTextMeasurementAccess::line(static_cast<NullTextContext *>(plainContext)->measurement(), 0).x == 0);
  LOKA_VERIFY(NullTextMeasurementAccess::line(static_cast<NullAttributedTextContext *>(richContext)->measurement(), 0).x
              == 0);
  LOKA_VERIFY((Text("aa") + BlockStyle().align(TEXT_ALIGN_RIGHT)).applyPropsToNode(plain));
  LOKA_VERIFY(
      (AttributedText(Styled("aa", TextStyle())) + BlockStyle().align(TEXT_ALIGN_CENTER)).applyPropsToNode(rich));
  // Retained apply does not classify style diffs. Its caller requests the existing
  // conservative layout invalidation; no alignment-specific dirty class exists.
  const PaintQuery query = {platform.paintScope(), PLACEMENT_ELIGIBLE};
  LOKA_VERIFY(static_cast<NullTextContext *>(plainContext)->queryPaintDamage(query).reason
              == PAINT_REFUSED_PLACEMENT_UNSETTLED);
  LOKA_VERIFY(static_cast<NullAttributedTextContext *>(richContext)->queryPaintDamage(query).reason
              == PAINT_REFUSED_PLACEMENT_UNSETTLED);
  PropsReconciliationSupport::root(scene)->markViewDirty(NODE_DIRTY_LAYOUT);
  LOKA_VERIFY(scene.flushInvalidation());
  LOKA_VERIFY(SceneTestAccess::lastApplyPlan(scene).hasLayoutWork());
  LOKA_VERIFY(plain->getContext() == plainContext && rich->getContext() == richContext);
  LOKA_VERIFY(NullTextMeasurementAccess::line(static_cast<NullTextContext *>(plainContext)->measurement(), 0).x == 12);
  LOKA_VERIFY(NullTextMeasurementAccess::line(static_cast<NullAttributedTextContext *>(richContext)->measurement(), 0).x
              == 6);
  SceneTestAccess::unmount(scene);
}
