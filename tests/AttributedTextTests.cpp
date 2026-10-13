#include <cstdio>
#include <climits>
#include "app/layout/TextLineBreaker.hpp"
#include "platform/String.hpp"
#include "AttributedTextTests.hpp"
#include "app/nodes/AttributedText.hpp"
#include "app/nodes/Text.hpp"
#include "app/nodes/boundary/StdComposition.hpp"
#include "app/scene/Scene.hpp"
#include "core/util/StateTrackerGuard.hpp"
#include "platform/null/NullScenePlatformController.hpp"
#include "platform/null/context/NullAttributedTextContext.hpp"
#include "support/LokaAllocFailure.hpp"
#include "support/TestVerify.hpp"
#include "testing/scene/SceneTestFlow.hpp"

namespace
{
  using namespace loka::app;
  using namespace loka::app::scene;
  using namespace loka::core;
  using loka::dsl::testing::SceneTestAccess;

  NullTextMeasurement measured(const AttributedString &value, short width = 200, const BlockStyle &block = BlockStyle())
  {
    NullScenePlatformController controller;
    AttributedTextNode node((AttributedText(value) + block).props);
    LayoutState state;
    state.width = width;
    controller.projectLayoutForTesting(&node, state);
    NullAttributedTextContext *context = static_cast<NullAttributedTextContext *>(node.getContext());
    LOKA_VERIFY(context != 0);
    return context->measurement();
  }

  class Registrar : public DirtySourceRegistrar
  {
  public:
    Registrar()
        : calls(0),
          source(0),
          flags(NODE_DIRTY_NONE)
    {
    }
    virtual void markDirtyOnChange(StateBase *state, NodeDirtyFlags dirty)
    {
      ++this->calls;
      this->source = state;
      this->flags = dirty;
    }
    int calls;
    StateBase *source;
    NodeDirtyFlags flags;
  };

  class Editor;
  struct EditorProps : NodePropsBase<EditorProps>
  {
    typedef Editor NodeType;
    typedef Editor TypeTag;
    explicit EditorProps(State<AttributedString> *source = 0)
        : value(source)
    {
    }
    bool operator<(const PropsBase &rhs) const
    {
      return this->value < static_cast<const EditorProps &>(rhs).value;
    }
    State<AttributedString> *value;
  };
  class Editor : public StdCompositionBoundaryNodeBase<EditorProps>
  {
  public:
    explicit Editor(const EditorProps &input)
        : StdCompositionBoundaryNodeBase<EditorProps>(input)
    {
    }
    virtual bool flushViewDirtyImmediately(NodeDirtyFlags) const
    {
      return false;
    }
    virtual void composeNode(NodeComposition &composition)
    {
      composition.declare(AttributedText(this->props.value));
    }
  };
  class Presenter : public NullScenePlatformController
  {
  public:
    Presenter()
        : refuseProjection(false),
          commits(0),
          flags(NODE_DIRTY_NONE)
    {
    }
    virtual void onPaintCommitted()
    {
      ++this->commits;
    }
    virtual void onChange(Node *root, NodeDirtyFlags dirtyFlags, bool)
    {
      this->flags = dirtyFlags;
      LayoutState state;
      state.width = 200;
      state.height = 200;
      this->projectLayoutForTesting(root, state);
    }
    virtual bool prepareProjectedLayout(Node *node, LayoutState &state)
    {
      if (this->refuseProjection && node->asAttributedTextNode())
        return false;
      return NullScenePlatformController::prepareProjectedLayout(node, state);
    }
    bool refuseProjection;
    unsigned commits;
    NodeDirtyFlags flags;
  };
  void settle(Scene &scene)
  {
    for (int i = 0; scene.hasPendingInvalidation() && i < 12; ++i)
      LOKA_VERIFY(scene.flushInvalidation());
    LOKA_VERIFY(!scene.hasPendingInvalidation());
  }
  AttributedTextNode *textIn(Node *node)
  {
    if (node->asAttributedTextNode())
      return node->asAttributedTextNode();
    INestable *nestable = node->asNestable();
    for (Node *child = nestable ? nestable->childrenHead() : 0; child; child = child->nextInComposition)
      if (AttributedTextNode *found = textIn(child))
        return found;
    return 0;
  }
  AttributedString invalidValue()
  {
    const String input("refused");
    loka::core::testing::failLokaAllocRaw("AttributedString", "Segments", 1);
    const AttributedString invalid = Styled(input, Bold);
    loka::core::testing::allowLokaAllocRaw();
    LOKA_VERIFY(!invalid.valid());
    return invalid;
  }
} // namespace

void testAttributedTextMetrics()
{
  const AttributedString mixed = Styled("ab", FontSize<12>()) + Styled("cd", FontSize<24>());
  const NullTextMeasurement line = measured(mixed);
  LOKA_VERIFY(line.width() == 24);
  LOKA_VERIFY(line.height() == 24);
  LOKA_VERIFY(line.lineCount() == 1);
  LOKA_VERIFY(measured(Styled("ab", FontSize<24>()) + Styled("cd", FontSize<12>())).height() == 24);
  LOKA_VERIFY(measured(Styled("wo", Bold) + Styled("rd", Italic)).width() == 16);
  LOKA_VERIFY(measured(AttributedString()).height() == 12);
  LOKA_VERIFY(measured(AttributedString()).width() == 0);
  LOKA_VERIFY(measured(Styled("a\n\nb", TextStyle())).height() == 36);
  LOKA_VERIFY(measured(Styled("a\r", TextStyle()) + Styled("\nb", Bold)).lineCount() == 2);
  LOKA_VERIFY(measured(Styled("a\n", FontSize<24>()) + Styled("b", FontSize<12>())).height() == 36);
  LOKA_VERIFY(measured(Styled("\n", FontSize<24>())).height() == 48);
  LOKA_VERIFY(measured(Styled("x", FontSize<9>())).height() == 9);
  LOKA_VERIFY(measured(Styled("x", FontSize<10>())).width() == 4);
  LOKA_VERIFY(measured(Styled("a\nb", FontSize<24>())).height() == 48);
  for (int mode = TEXT_WRAP_WORD; mode <= TEXT_WRAP_CHAR; ++mode)
  {
    const BlockStyle wrap = BlockStyle().wrap(static_cast<TextWrap>(mode));
    const NullTextMeasurement wrapped = measured(mixed, 20, wrap);
    LOKA_VERIFY(wrapped.width() == 16);
    LOKA_VERIFY(wrapped.height() == 48);
    LOKA_VERIFY(wrapped.lineCount() == 2);
    // A single segment participates in the shared word/character wrapping.
    LOKA_VERIFY(measured(Styled("ab cd", Bold), 8, wrap).width() == 8);
    LOKA_VERIFY(measured(Styled("ab cd", Bold), 8, wrap).lineCount() == 3);
  }
  LOKA_VERIFY(measured(mixed, 20).lineCount() == 1);
  LOKA_VERIFY(measured(mixed, 21, BlockStyle().truncation(TEXT_TRUNCATION_CLIP)).width() == 21);
  LOKA_VERIFY(measured(mixed, 21, BlockStyle().truncation(TEXT_TRUNCATION_ELLIPSIS)).width() == 16);
  LOKA_VERIFY(measured(mixed, 21, BlockStyle().truncation(TEXT_TRUNCATION_NONE)).width() == 24);
}

void testAttributedTextPropsIdentity()
{
  const AttributedString value = Styled("ab", Bold);
  const AttributedTextProps owned(value);
  const AttributedTextProps equivalent(Styled("a", Bold) + Styled("b", Bold));
  const AttributedTextProps different(Styled("ac", Bold));
  LOKA_VERIFY(!(owned < equivalent) && !(equivalent < owned));
  LOKA_VERIFY(owned < different);
  LOKA_VERIFY(!(different < owned));
  MutableState<AttributedString> live[2];
  const AttributedTextProps borrowed(&live[0]), other(&live[1]);
  LOKA_VERIFY(borrowed < other);
  LOKA_VERIFY(!(other < borrowed));
  LOKA_VERIFY(borrowed < owned);
  LOKA_VERIFY(!(owned < borrowed));
  LOKA_VERIFY(!(borrowed < AttributedTextProps(&live[0])));
  LOKA_VERIFY(!(owned < TextProps("ab")));
  AttributedTextProps styled(owned);
  styled.blockStyle_ = BlockStyle().wrap(TEXT_WRAP_WORD);
  LOKA_VERIFY((owned < styled) == (owned.blockStyle_ < styled.blockStyle_));
  LOKA_VERIFY((styled < owned) == (styled.blockStyle_ < owned.blockStyle_));
  LOKA_VERIFY((owned < styled) != (styled < owned));
  LOKA_VERIFY(styled.text_ == &styled.ownedText);
  AttributedTextProps assigned;
  assigned = styled;
  LOKA_VERIFY(assigned.text_ == &assigned.ownedText);
  assigned = borrowed;
  LOKA_VERIFY(assigned.text_ == &live[0] && !assigned.ownsText);
  assigned.text(value);
  LOKA_VERIFY(assigned.text_ == &assigned.ownedText && assigned.ownsText);
  assigned.text(&live[1]);
  LOKA_VERIFY(assigned.text_ == &live[1] && !assigned.ownsText);
  AttributedText definition(value);
  definition.testId("EditorLine");
  definition.setNodeTag(41);
  const AttributedTextDefinitionWithAttr result =
      definition + BlockStyle().wrap(TEXT_WRAP_WORD) + BlockStyle().truncation(TEXT_TRUNCATION_CLIP);
  LOKA_VERIFY(result.hasTestId() && result.testIdValue() == "EditorLine");
  LOKA_VERIFY(result.nodeTag() == 41);
  LOKA_VERIFY(result.props.blockStyle_.wrap_ == TEXT_WRAP_WORD);
  LOKA_VERIFY(result.props.blockStyle_.truncation_ == TEXT_TRUNCATION_CLIP);
  AttributedTextNode node(result.props);
  LOKA_VERIFY(node.asTextNode() == 0);
  LOKA_VERIFY(node.asAttributedTextNode() == &node);
  LOKA_VERIFY(node.kind() == NODE_KIND_ATTRIBUTED_TEXT);
  LOKA_VERIFY(node.nodeTypeKey() == NodeTypeToken<AttributedTextNode>());
  LOKA_VERIFY(node.asProjectedLayoutNode() == &node);
}

void testAttributedTextDirtySeatsAndEditorPresentation()
{
  MutableState<AttributedString> live(Styled("var ", Bold) + Styled("x", Italic) + Styled(" = 1;", TextStyle()));
  Presenter controller;
  AttributedTextNode borrowed((AttributedTextProps(&live)));
  Registrar registrar;
  borrowed.declareDirtySources(registrar);
  LOKA_VERIFY(registrar.calls == 1 && registrar.source == &live);
  LOKA_VERIFY(registrar.flags == (NODE_DIRTY_PROPS | NODE_DIRTY_LAYOUT));
  AttributedTextNode owned((AttributedTextProps(live.get())));
  Registrar constant;
  owned.declareDirtySources(constant);
  LOKA_VERIFY(constant.calls == 0);
  Scene scene((Boundary<Editor>(EditorProps(&live))));
  scene.mount(&controller);
  SceneTestAccess::updateAttached(scene, true);
  settle(scene);
  AttributedTextNode *node = textIn(SceneTestAccess::rootBoundary(scene));
  LOKA_VERIFY(node != 0);
  NullAttributedTextContext *context = static_cast<NullAttributedTextContext *>(node->getContext());
  LOKA_VERIFY(context != 0);
  LOKA_VERIFY(context->measurement().width() == 40 && context->measurement().height() == 12);
  LOKA_VERIFY(controller.commits > 0);
  const PaintQuery query = {controller.paintScope(), PLACEMENT_ELIGIBLE};
  LOKA_VERIFY(context->queryPaintDamage(query).kind == PAINT_ANSWER_EXACT);
  LOKA_VERIFY(context->queryPaintDamage(query).damage.width == 0);
  const unsigned beforeColor = controller.commits;
  {
    StateTrackerGuard guard(SceneTestAccess::rootBoundary(scene)->tracker());
    live.set(Styled("var ", Bold + Red) + Styled("x", Italic) + Styled(" = 1;", TextStyle()));
  }
  settle(scene);
  LOKA_VERIFY(controller.commits > beforeColor);
  LOKA_VERIFY(context->measurement().width() == 40 && context->measurement().height() == 12);
  LOKA_VERIFY(context->queryPaintDamage(query).kind == PAINT_ANSWER_EXACT);
  LOKA_VERIFY(context->queryPaintDamage(query).damage.width == 0);
  const unsigned before = controller.commits;
  {
    StateTrackerGuard guard(SceneTestAccess::rootBoundary(scene)->tracker());
    live.set(live.get() + FontSize<24>());
  }
  settle(scene);
  LOKA_VERIFY(controller.commits > before);
  LOKA_VERIFY((controller.flags & NODE_DIRTY_LAYOUT) != 0);
  LOKA_VERIFY(context->measurement().height() == 24 && context->measurement().width() == 80);
  LOKA_VERIFY(context->queryPaintDamage(query).kind == PAINT_ANSWER_EXACT);
  SceneTestAccess::unmount(scene);
}

void testAttributedTextInvalidProjectionRefusesAndRecovers()
{
  MutableState<AttributedString> live(Styled("before", Bold));
  Presenter controller;
  Scene scene((Boundary<Editor>(EditorProps(&live))));
  scene.mount(&controller);
  SceneTestAccess::updateAttached(scene, true);
  settle(scene);
  AttributedTextNode *node = textIn(SceneTestAccess::rootBoundary(scene));
  LOKA_VERIFY(node != 0);
  NullAttributedTextContext *context = static_cast<NullAttributedTextContext *>(node->getContext());
  LOKA_VERIFY(context != 0);
  const unsigned before = controller.commits;
  {
    StateTrackerGuard guard(SceneTestAccess::rootBoundary(scene)->tracker());
    live.set(invalidValue());
  }
  settle(scene);
  LOKA_VERIFY(controller.commits == before);
  LOKA_VERIFY(context->measurement().width() == 0);
  LOKA_VERIFY(context->measurement().height() == 12);
  const PaintQuery query = {controller.paintScope(), PLACEMENT_ELIGIBLE};
  PaintAnswer answer;
  LOKA_VERIFY(controller.queryPaintAnswer(node, context, query, answer));
  LOKA_VERIFY(answer.kind == PAINT_ANSWER_REFUSED && answer.reason == PAINT_REFUSED_PROPS_UNRECONCILED);
  LOKA_VERIFY(!context->commitPresented(live.get(), controller.paintScope()));
  {
    StateTrackerGuard guard(SceneTestAccess::rootBoundary(scene)->tracker());
    live.set(Styled("after", Italic));
  }
  settle(scene);
  LOKA_VERIFY(controller.commits > before);
  LOKA_VERIFY(context->queryPaintDamage(query).kind == PAINT_ANSWER_EXACT);
  context->onFactChanged(NODE_FACT_ATTACHED, NODE_FACT_DETACHED_RETAINED);
  LOKA_VERIFY(!context->commitPresented(live.get(), controller.paintScope()));
  context->onFactChanged(NODE_FACT_DETACHED_RETAINED, NODE_FACT_ATTACHED);
  LOKA_VERIFY(!context->commitPresented(live.get(), controller.paintScope()));
  LayoutState restored;
  restored.width = 200;
  restored.height = 200;
  controller.projectLayoutForTesting(SceneTestAccess::rootBoundary(scene), restored);
  LOKA_VERIFY(context->queryPaintDamage(query).kind == PAINT_ANSWER_EXACT);
  // A skipped placement cannot retain a drawable seat from an earlier pass.
  controller.refuseProjection = true;
  LayoutState state;
  state.width = 200;
  state.height = 200;
  controller.projectLayoutForTesting(SceneTestAccess::rootBoundary(scene), state);
  LOKA_VERIFY(!context->commitPresented(live.get(), controller.paintScope()));
  SceneTestAccess::unmount(scene);
}

void testAttributedTextHandlerCannotBeReplaced()
{
  NullScenePlatformController controller;
  RefusedNodeHandler foreign(NodeTypeToken<AttributedTextNode>());
  LOKA_VERIFY(!controller.registerNodeHandler(&foreign));
  AttributedTextNode node((AttributedTextProps(Styled("x", Bold))));
  LayoutState state;
  state.width = 100;
  LOKA_VERIFY(controller.prepareProjectedLayout(&node, state));
  LOKA_VERIFY(node.getContext() != 0);
  LOKA_VERIFY(foreign.refusalCount() == 0);
  RefusedNodeHandler nativeRefusal(NodeTypeToken<AttributedTextNode>());
  LOKA_VERIFY(nativeRefusal.ensureContext(&node, &controller, state) == 0);
  LOKA_VERIFY(nativeRefusal.refusalCount() == 1);
}

void testAttributedTextSegmentationIndependentLayout()
{
  const AttributedString whole = Styled("abcd", Bold);
  const AttributedString split = Styled("ab", Bold) + Styled("cd", Bold);
  LOKA_VERIFY(whole == split);
  for (int mode = TEXT_WRAP_WORD; mode <= TEXT_WRAP_CHAR; ++mode)
  {
    const BlockStyle wrap = BlockStyle().wrap(static_cast<TextWrap>(mode));
    const NullTextMeasurement joined = measured(whole, 12, wrap);
    const NullTextMeasurement divided = measured(split, 12, wrap);
    LOKA_VERIFY(joined.width() == divided.width());
    LOKA_VERIFY(joined.height() == divided.height());
    LOKA_VERIFY(joined.lineCount() == divided.lineCount());
    LOKA_VERIFY(joined.width() == 12 && joined.height() == 24 && joined.lineCount() == 2);
  }
  for (int mode = TEXT_TRUNCATION_CLIP; mode <= TEXT_TRUNCATION_ELLIPSIS; ++mode)
  {
    const BlockStyle truncate = BlockStyle().truncation(static_cast<TextTruncation>(mode));
    const NullTextMeasurement joined = measured(whole, 11, truncate);
    const NullTextMeasurement divided = measured(split, 11, truncate);
    LOKA_VERIFY(joined.width() == divided.width());
    LOKA_VERIFY(joined.height() == divided.height());
    LOKA_VERIFY(joined.lineCount() == divided.lineCount());
    LOKA_VERIFY(joined.width() == (mode == TEXT_TRUNCATION_CLIP ? 11 : 8));
  }
}

void testAttributedTextWrapUsesJoinedWordsAndRunMetrics()
{
  // "abcd" fits a complete line. WORD moves it intact, even though its
  // character metrics change in the middle; CHAR fills the previous line.
  const AttributedString words = Styled("a ab", FontSize<12>()) + Styled("cd", FontSize<24>());
  const NullTextMeasurement word = measured(words, 24, BlockStyle().wrap(TEXT_WRAP_WORD));
  LOKA_VERIFY(word.width() == 24 && word.height() == 36 && word.lineCount() == 2);
  const NullTextMeasurement character = measured(words, 24, BlockStyle().wrap(TEXT_WRAP_CHAR));
  LOKA_VERIFY(character.width() == 24 && character.height() == 48 && character.lineCount() == 2);
  const AttributedString sameStyle = Styled("a ab", Bold) + Styled("cd", Bold);
  const NullTextMeasurement intact = measured(sameStyle, 16, BlockStyle().wrap(TEXT_WRAP_WORD));
  LOKA_VERIFY(intact.width() == 16 && intact.height() == 24 && intact.lineCount() == 2);
  // A size change halfway through a forced word break contributes to both lines.
  const AttributedString longWord = Styled("ab", FontSize<12>()) + Styled("cd", FontSize<24>());
  const NullTextMeasurement forced = measured(longWord, 20, BlockStyle().wrap(TEXT_WRAP_WORD));
  LOKA_VERIFY(forced.width() == 16 && forced.height() == 48 && forced.lineCount() == 2);
}

void testTextBreakerCharacterization()
{
  struct Case
  {
    const char *text;
    TextWrap wrap;
    short width, height, lines;
  };
  const Case cases[] = {{"ab cd", TEXT_WRAP_WORD, 12, 24, 2},
                        {"ab cd", TEXT_WRAP_CHAR, 12, 24, 2},
                        {"ab cd", TEXT_WRAP_NONE, 20, 12, 1},
                        {"a\r\nb", TEXT_WRAP_WORD, 4, 24, 2},
                        {"abcdefg", TEXT_WRAP_WORD, 12, 36, 3},
                        {"a\n\nb", TEXT_WRAP_CHAR, 4, 36, 3},
                        {"", TEXT_WRAP_NONE, 0, 12, 1}};
  for (std::size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
  {
    const BlockStyle block = BlockStyle().wrap(cases[i].wrap);
    const String text = String::Literal(cases[i].text);
    LayoutState state;
    state.width = 12;
    bool valid = false;
    NullTextMeasurement plain;
    valid = MeasureNullText(TextStyle(), block, &text, state, plain);
    const NullTextMeasurement attributed = measured(Styled(text, TextStyle()), 12, block);
    LOKA_VERIFY(valid);
    LOKA_VERIFY(plain.width() == cases[i].width && plain.height() == cases[i].height
                && plain.lineCount() == cases[i].lines);
    LOKA_VERIFY(attributed.width() == cases[i].width && attributed.height() == cases[i].height
                && attributed.lineCount() == cases[i].lines);
    std::printf("characterization %lu: %d/%d/%d (Text and AttributedText)\n",
                static_cast<unsigned long>(i),
                plain.width(),
                plain.height(),
                plain.lineCount());
  }
  const NullTextMeasurement mixed =
      measured(Styled("ab", SizeOf(12)) + Styled("cd", SizeOf(24)), 20, BlockStyle().wrap(TEXT_WRAP_WORD));
  LOKA_VERIFY(mixed.width() == 16 && mixed.height() == 48 && mixed.lineCount() == 2);
  const NullTextMeasurement word =
      measured(Styled("a wo", Bold) + Styled("rd", Italic), 20, BlockStyle().wrap(TEXT_WRAP_WORD));
  LOKA_VERIFY(word.width() == 16 && word.height() == 24 && word.lineCount() == 2);
  std::printf("characterization mixed: 16/48/2; word across runs: 16/24/2\n");
  const String maximum(std::string(8192, 'x'));
  for (int mode = TEXT_WRAP_WORD; mode <= TEXT_WRAP_CHAR; ++mode)
  {
    LayoutState state;
    state.width = SHRT_MAX;
    bool valid = false;
    const BlockStyle wrap = BlockStyle().wrap(static_cast<TextWrap>(mode));
    NullTextMeasurement plain;
    valid = MeasureNullText(TextStyle(), wrap, &maximum, state, plain);
    const NullTextMeasurement attributed = measured(Styled(maximum, TextStyle()), SHRT_MAX, wrap);
    LOKA_VERIFY(valid && plain.width() == 32764 && plain.height() == 24 && plain.lineCount() == 2);
    LOKA_VERIFY(attributed.width() == 32764 && attributed.height() == 24 && attributed.lineCount() == 2);
  }
  std::printf("characterization SHRT_MAX WORD/CHAR: 32764/24/2 (Text and AttributedText)\n");
}

namespace
{
  /** Range-sensitive fake native widths: an equal-style span pays one fixed
      cost, so splitting it into segment or character widths is observable. */
  class RangeSource : public SyntheticTextWidthSource
  {
  public:
    explicit RangeSource(const AttributedString &value)
        : SyntheticTextWidthSource(value)
    {
    }
    virtual bool width(std::size_t start, std::size_t end, std::size_t, int &out) const
    {
      out = static_cast<int>(end - start) * 2 + 3;
      return true;
    }
    virtual TextLineMetrics metrics(const TextStyle &style) const
    {
      int points = 0;
      return style.size_.queryPoints(points) && points == 24 ? TextLineMetrics(14, 7, 3) : TextLineMetrics(8, 2, 1);
    }
  };
  /** A UTF-16-shaped source: the middle code point occupies two native units. */
  class NativeRangeSource : public RangeSource
  {
  public:
    NativeRangeSource()
        : RangeSource(Styled("a\xF0\x9F\x98\x80"
                             "b",
                             TextStyle()))
    {
      for (std::size_t i = 0; i < 3; ++i)
        this->rows_[i] = SyntheticTextWidthSource::character(i);
      this->rows_[1].end = 3;
      this->rows_[2].offset = 3;
      this->rows_[2].end = 4;
    }
    virtual const TextBreakCharacter &character(std::size_t index) const
    {
      return this->rows_[index];
    }

  private:
    TextBreakCharacter rows_[3];
  };
  class RefusingRangeSource : public RangeSource
  {
  public:
    explicit RefusingRangeSource(const AttributedString &value, unsigned refuseAt = 2)
        : RangeSource(value),
          calls_(0),
          refuseAt_(refuseAt)
    {
    }
    virtual bool width(std::size_t start, std::size_t end, std::size_t span, int &out) const
    {
      if (++this->calls_ == this->refuseAt_)
        return false;
      return RangeSource::width(start, end, span, out);
    }

  private:
    mutable unsigned calls_;
    const unsigned refuseAt_;
  };
  /** Refuse the fill pass after a preceding segment has already been decoded. */
  class RefusedSpanString : public loka::platform::String
  {
  public:
    RefusedSpanString()
        : calls_(0)
    {
    }
    virtual bool appendUtf8(std::string &out) const
    {
      if (++this->calls_ == 2)
        return false;
      out += "x";
      return true;
    }

  private:
    mutable unsigned calls_;
  };
  /** Deliberately violates repeatability after the counting pass to exercise
      the always-on capacity refusal independently of debug table assertions. */
  class ChangingRangeSource : public SyntheticTextWidthSource
  {
  public:
    ChangingRangeSource(const AttributedString &value, bool insertBreaks)
        : SyntheticTextWidthSource(value),
          calls_(0),
          insertBreaks_(insertBreaks),
          newline_()
    {
      this->newline_ = SyntheticTextWidthSource::character(0);
      this->newline_.value = '\n';
    }
    virtual const TextBreakCharacter &character(std::size_t index) const
    {
      return this->insertBreaks_ && this->calls_ >= 41 ? this->newline_ : SyntheticTextWidthSource::character(index);
    }
    virtual bool width(std::size_t start, std::size_t end, std::size_t, int &out) const
    {
      out = static_cast<int>(end - start) * (++this->calls_ > 41 ? 4 : 1);
      return true;
    }

  private:
    mutable unsigned calls_;
    const bool insertBreaks_;
    TextBreakCharacter newline_;
  };
  class ShapingController : public NullScenePlatformController
  {
  public:
    ShapingController(TextShaping shaping, const TextWidthSource &source)
        : NullScenePlatformController(8, shaping),
          source_(source),
          perRun(0),
          wholeLine(0)
    {
    }
    virtual const TextWidthSource &textWidthSource(TextShaping shaping, const TextWidthSource &synthetic) const
    {
      switch (shaping)
      {
      case PER_RUN:
        ++this->perRun;
        return synthetic;
      case WHOLE_LINE:
        ++this->wholeLine;
        return this->source_;
      }
      return synthetic;
    }
    const TextWidthSource &source_;
    mutable unsigned perRun, wholeLine;
  };
} // namespace

void testTextBreakerRangesAndRefusal()
{
  const AttributedString split = Styled("ab", Bold) + Styled("cd", Bold);
  const RangeSource source(split);
  const TextLineBreaker wrapped(source, BlockStyle().wrap(TEXT_WRAP_CHAR), 7);
  LOKA_VERIFY(wrapped.valid() && wrapped.lineCount() == 2);
  LOKA_VERIFY(source.span(wrapped.fragment(wrapped.line(1).firstFragment).span).segment == 0);
  const TextLineBreaker result(source, BlockStyle().wrap(TEXT_WRAP_WORD), 11);
  LOKA_VERIFY(result.valid());
  LOKA_VERIFY(result.width() == 11 && result.height() == 10 && result.lineCount() == 1);
  LOKA_VERIFY(result.line(0).fragmentCount == 1);
  const TextFragment &fragment = result.fragment(0);
  LOKA_VERIFY(fragment.span == 0 && fragment.start == 0 && fragment.end == 4 && fragment.width == 11);
  const RangeSource mixed(Styled("ab", SizeOf(12)) + Styled("cd", SizeOf(24)));
  const TextLineBreaker lines(mixed, BlockStyle(), 100);
  LOKA_VERIFY(lines.valid() && lines.width() == 14 && lines.height() == 21);
  LOKA_VERIFY(lines.line(0).metrics.ascent == 14 && lines.line(0).metrics.descent == 7
              && lines.line(0).metrics.leading == 3);
  LOKA_VERIFY(lines.line(0).fragmentCount == 2 && mixed.span(lines.fragment(1).span).segment == 1);
  const SyntheticTextWidthSource unicode(Styled("a\xF0\x9F\x98\x80"
                                                "b",
                                                TextStyle()));
  const TextLineBreaker codePoints(unicode, BlockStyle().wrap(TEXT_WRAP_CHAR), 4);
  LOKA_VERIFY(codePoints.valid() && codePoints.lineCount() == 3 && codePoints.height() == 36);
  const NativeRangeSource native;
  const TextLineBreaker nativeLines(native, BlockStyle().wrap(TEXT_WRAP_CHAR), 7);
  LOKA_VERIFY(nativeLines.valid() && nativeLines.lineCount() == 3);
  const TextFragment &surrogate = nativeLines.fragment(nativeLines.line(1).firstFragment);
  LOKA_VERIFY(surrogate.start == 1 && surrogate.end == 3 && surrogate.width == 7);
  const RefusingRangeSource refusedWidth(split);
  const TextLineBreaker failedWidth(refusedWidth, BlockStyle(), 100);
  LOKA_VERIFY(!failedWidth.valid() && failedWidth.lineCount() == 0);
  // Four growing ranges plus one completed fragment in the counting pass.
  const RefusingRangeSource refusesFill(split, 6);
  const TextLineBreaker failedFill(refusesFill, BlockStyle(), 100);
  LOKA_VERIFY(!failedFill.valid() && failedFill.lineCount() == 0 && failedFill.width() == 0);
  const AttributedString longValue = Styled("abcdefghijklmnopqrstuvwxyz012345abcdefghijklmnopqrstuvwxyz012345", Bold);
  const RangeSource longSource(longValue);
  for (int allocation = 1; allocation <= 2; ++allocation)
  {
    loka::core::testing::failLokaAllocRaw("TextLineBreaker", "Table", allocation);
    const TextLineBreaker refused(longSource, BlockStyle().wrap(TEXT_WRAP_CHAR), 1);
    LOKA_VERIFY(!refused.valid() && refused.lineCount() == 0 && refused.width() == 0);
    loka::core::testing::allowLokaAllocRaw();
  }
  loka::core::testing::failLokaAllocRaw("TextLineBreaker", "Table", 1);
  const SyntheticTextWidthSource refusedSource(longValue);
  loka::core::testing::allowLokaAllocRaw();
  LOKA_VERIFY(!refusedSource.valid());
  const TextLineBreaker refusedInput(refusedSource, BlockStyle(), 100);
  LOKA_VERIFY(!refusedInput.valid());
}

void testNullTextShapingDispatch()
{
  const AttributedString value = Styled("ab", Bold) + Styled("cd", Bold);
  const RangeSource injected(value);
  for (int kind = 0; kind < 2; ++kind)
  {
    const TextShaping shaping = kind == 0 ? PER_RUN : WHOLE_LINE;
    ShapingController controller(shaping, injected);
    AttributedTextNode node((AttributedText(value)).props);
    LayoutState state;
    state.width = 100;
    controller.projectLayoutForTesting(&node, state);
    NullAttributedTextContext *context = static_cast<NullAttributedTextContext *>(node.getContext());
    LOKA_VERIFY(context != 0);
    LOKA_VERIFY(controller.textShaping() == shaping);
    LOKA_VERIFY(context->measurement().width() == (kind == 0 ? 16 : 11));
    LOKA_VERIFY(context->measurement().height() == (kind == 0 ? 12 : 10));
    LOKA_VERIFY(context->measurement().lineCount() == 1);
    LOKA_VERIFY(kind == 0 ? controller.perRun > 0 && controller.wholeLine == 0
                          : controller.wholeLine > 0 && controller.perRun == 0);
    const unsigned calls = kind == 0 ? controller.perRun : controller.wholeLine;
    LOKA_VERIFY(context->commitPresented(value, controller.paintScope()));
    LOKA_VERIFY((kind == 0 ? controller.perRun : controller.wholeLine) > calls);
  }
  NullScenePlatformController controller(8, WHOLE_LINE);
  AttributedTextNode node((AttributedText(value)).props);
  LayoutState state;
  state.width = 100;
  controller.projectLayoutForTesting(&node, state);
  const NullAttributedTextContext *context = static_cast<const NullAttributedTextContext *>(node.getContext());
  LOKA_VERIFY(context != 0 && context->measurement().width() == 16 && context->measurement().height() == 12);
}

void testTextSpanTable()
{
  const SyntheticTextWidthSource colors(Styled("a", Red) + Styled("b", Red) + Styled("c", Blue));
  LOKA_VERIFY(colors.valid() && colors.spanCount() == 2);
  LOKA_VERIFY(colors.spanStyle(0) == Red && colors.spanStyle(1) == Blue);
  const TextLineBreaker colorLines(colors, BlockStyle(), 100);
  LOKA_VERIFY(colorLines.valid() && colorLines.lineCount() == 1);
  LOKA_VERIFY(colorLines.line(0).fragmentCount == 2);
  LOKA_VERIFY(colorLines.fragment(0).start == 0 && colorLines.fragment(0).end == 2);
  LOKA_VERIFY(colorLines.fragment(1).start == 2 && colorLines.fragment(1).end == 3);

  const AttributedString value = Styled("", Italic) + Styled("ab", Bold) + Styled("", Italic) + Styled("cd", Bold)
                                 + Styled("e", Italic) + Styled("f", Bold);
  const SyntheticTextWidthSource source(value);
  LOKA_VERIFY(source.valid() && source.length() == 6 && source.spanCount() == 3);
  LOKA_VERIFY(source.span(0).segment == 1 && source.span(0).start == 0 && source.span(0).end == 4);
  LOKA_VERIFY(source.span(1).segment == 4 && source.span(1).start == 4 && source.span(1).end == 5);
  LOKA_VERIFY(source.span(2).segment == 5 && source.span(2).start == 5 && source.span(2).end == 6);
  LOKA_VERIFY(source.spanStyle(0) == Bold && source.spanStyle(1) == Italic && source.spanStyle(2) == Bold);
  LOKA_VERIFY(&source.spanStyle(0) == &source.span(0).style);
  for (std::size_t i = 0; i < source.length(); ++i)
    LOKA_VERIFY(source.character(i).span == (i < 4 ? 0 : i - 3));
  const TextLineBreaker lines(source, BlockStyle().wrap(TEXT_WRAP_CHAR), 8);
  LOKA_VERIFY(lines.valid() && lines.lineCount() == 3 && lines.width() == 8 && lines.height() == 36);
  const TextFragment &continued = lines.fragment(lines.line(1).firstFragment);
  LOKA_VERIFY(continued.span == 0 && source.span(continued.span).segment == 1);
  LOKA_VERIFY(source.spanStyle(continued.span) == Bold && continued.start == 2 && continued.end == 4);

  const SyntheticTextWidthSource empty(Styled("", Bold) + Styled("", Italic));
  const SyntheticTextWidthSource plainEmpty(String(), Bold);
  LOKA_VERIFY(empty.valid() && empty.length() == 0 && empty.spanCount() == 0);
  LOKA_VERIFY(plainEmpty.valid() && plainEmpty.length() == 0 && plainEmpty.spanCount() == 0);

  const String refusesFill = String::FromPlatform(Managed<loka::platform::String>::Wrap(new RefusedSpanString()));
  const SyntheticTextWidthSource partial(Styled("a", Bold) + Styled(refusesFill, Italic));
  LOKA_VERIFY(!partial.valid() && partial.length() == 0 && partial.spanCount() == 0);
  const TextLineBreaker refusedPartial(partial, BlockStyle(), 100);
  LOKA_VERIFY(!refusedPartial.valid() && refusedPartial.lineCount() == 0);

  AttributedString alternating;
  AttributedString equal;
  for (int i = 0; i < 40; ++i)
  {
    alternating = alternating + Styled("x", i % 2 == 0 ? Bold : Italic);
    equal = equal + Styled("x", Bold);
  }
  const SyntheticTextWidthSource many(alternating);
  const SyntheticTextWidthSource coalesced(equal);
  LOKA_VERIFY(many.valid() && many.spanCount() == 40);
  LOKA_VERIFY(coalesced.valid() && coalesced.spanCount() == 1 && coalesced.span(0).end == 40);
  for (std::size_t i = 0; i < many.spanCount(); ++i)
  {
    LOKA_VERIFY(many.span(i).segment == i && many.span(i).start == i && many.span(i).end == i + 1);
    LOKA_VERIFY(many.character(i).span == i);
  }
  // Both character and span tables exceed inline capacity. Failure of the
  // second allocation must release the first through the existing table owner.
  for (int allocation = 1; allocation <= 2; ++allocation)
  {
    loka::core::testing::failLokaAllocRaw("TextLineBreaker", "Table", allocation);
    {
      const SyntheticTextWidthSource refused(alternating);
      LOKA_VERIFY(!refused.valid() && refused.length() == 0 && refused.spanCount() == 0);
      const TextLineBreaker result(refused, BlockStyle(), 100);
      LOKA_VERIFY(!result.valid() && result.lineCount() == 0);
    }
    LOKA_VERIFY(loka::core::testing::lokaAllocRawLive() == 0);
    loka::core::testing::allowLokaAllocRaw();
  }
}

namespace
{
  void census(const char *name,
              const AttributedString &value,
              short width,
              const BlockStyle &block,
              int expectedAllocations = 0)
  {
    using namespace loka::core::testing;
    failLokaAllocRaw("TextLineBreaker", "Table", 0);
    int sourceAllocations = 0, resultAllocations = 0;
    {
      const SyntheticTextWidthSource source(value);
      sourceAllocations = lokaAllocRawAttempts();
      const TextLineBreaker result(source, block, width);
      resultAllocations = lokaAllocRawAttempts() - sourceAllocations;
      LOKA_VERIFY(result.valid());
      std::printf("census %s: source=%d breaker=%d total=%d extent=%d/%d lines=%lu sizeof=%lu\n",
                  name,
                  sourceAllocations,
                  resultAllocations,
                  lokaAllocRawAttempts(),
                  result.width(),
                  result.height(),
                  static_cast<unsigned long>(result.lineCount()),
                  static_cast<unsigned long>(sizeof(result)));
    }
    LOKA_VERIFY(lokaAllocRawLive() == 0);
    allowLokaAllocRaw();
    LOKA_VERIFY(resultAllocations == expectedAllocations);
  }
} // namespace

void testTextBreakerAllocationCensus()
{
  std::printf("sizeof(BlockStyle)=%lu\n", static_cast<unsigned long>(sizeof(BlockStyle)));
  const BlockStyle word = BlockStyle().wrap(TEXT_WRAP_WORD);
  census("WORD", Styled("ab cd", TextStyle()), 12, word);
  census("CHAR", Styled("ab cd", TextStyle()), 12, BlockStyle().wrap(TEXT_WRAP_CHAR));
  census("NONE", Styled("ab cd", TextStyle()), 12, BlockStyle().wrap(TEXT_WRAP_NONE));
  census("CRLF", Styled("a\r", TextStyle()) + Styled("\nb", Bold), 12, word);
  census("empty-line", Styled("a\n\nb", TextStyle()), 12, word);
  census("empty", AttributedString(), 12, word);
  census("long-word", Styled("abcdefg", TextStyle()), 12, word);
  census("mixed", Styled("ab", SizeOf(12)) + Styled("cd", SizeOf(24)), 20, word);
  census("across-runs", Styled("a wo", Bold) + Styled("rd", Italic), 20, word);
  census("lines32", Styled(String(std::string(31, '\n')), Bold), 200, word);
  census("lines33", Styled(String(std::string(32, '\n')), Bold), 200, word, 1);
  census("forced40", Styled(String(std::string(40, 'x')), Bold), 4, word, 2);
  AttributedString spans;
  for (int i = 0; i < 33; ++i)
    spans = spans + Styled("x", i % 2 ? Bold : Italic);
  census("fragments33", spans, 200, word, 1);
  census("editor40", Styled("01234567890123456789", Bold) + Styled("01234567890123456789", Italic), 200, word);
}

namespace
{
  void
  verifyEstimate(const AttributedString &value, short available, const BlockStyle &block, short width, short height)
  {
    const NullTextMeasurement projection = measured(value, available, block);
    Frame estimate(7, 8, 9, 10);
    const bool valid = value.estimateExtent(available, block, estimate);
    LOKA_VERIFY(valid);
    LOKA_VERIFY(estimate.x == 0 && estimate.y == 0);
    LOKA_VERIFY(estimate.width == projection.width() && estimate.height == projection.height());
    LOKA_VERIFY(estimate.width == width && estimate.height == height);
  }
} // namespace

void testAttributedStringEstimateExtent()
{
  const AttributedString mixed = Styled("ab", SizeOf(12)) + Styled("cd", SizeOf(24));
  for (int mode = TEXT_WRAP_WORD; mode <= TEXT_WRAP_CHAR; ++mode)
  {
    const BlockStyle block = BlockStyle().wrap(static_cast<TextWrap>(mode));
    verifyEstimate(mixed, 20, block, 16, 48);
    verifyEstimate(Styled("ab cd", TextStyle()), 12, block, 12, 24);
    verifyEstimate(Styled("abcdefg", TextStyle()), 12, block, 12, 36);
    verifyEstimate(Styled("a\r", TextStyle()) + Styled("\nb", Bold), 12, block, 4, 24);
    verifyEstimate(Styled("a\n\nb", TextStyle()), 12, block, 4, 36);
    verifyEstimate(AttributedString(), 12, block, 0, 12);
  }
  const BlockStyle word = BlockStyle().wrap(TEXT_WRAP_WORD);
  verifyEstimate(Styled("a wo", Bold) + Styled("rd", Italic), 20, word, 16, 24);
  const AttributedString across = Styled("a ab", SizeOf(12)) + Styled("cd", SizeOf(24));
  verifyEstimate(across, 24, word, 24, 36);
  verifyEstimate(across, 24, BlockStyle().wrap(TEXT_WRAP_CHAR), 24, 48);
  verifyEstimate(mixed, 20, BlockStyle().wrap(TEXT_WRAP_NONE), 24, 24);
  verifyEstimate(Styled("ab cd", TextStyle()), 12, BlockStyle(), 20, 12);
  verifyEstimate(mixed, 0, word, 24, 24);
  verifyEstimate(mixed, -1, word, 24, 24);
  verifyEstimate(Styled("x", SizeOf(10)), 12, BlockStyle(), 4, 10);
  verifyEstimate(Styled("\n", SizeOf(24)), 12, word, 0, 48);
  verifyEstimate(mixed, 21, BlockStyle().truncation(TEXT_TRUNCATION_CLIP), 21, 24);
  verifyEstimate(mixed, 21, BlockStyle().truncation(TEXT_TRUNCATION_ELLIPSIS), 16, 24);
  verifyEstimate(mixed, 21, BlockStyle().truncation(TEXT_TRUNCATION_NONE), 24, 24);
}

void testAttributedStringEstimateRefusal()
{
  const Frame sentinel(7, 8, 9, 10);
  Frame out = sentinel;
  const bool invalid = invalidValue().estimateExtent(100, BlockStyle(), out);
  LOKA_VERIFY(!invalid && out == sentinel);
  const AttributedString value = Styled(String(std::string(40, 'x')), Bold);
  // Character, line and fragment tables each need heap storage at width 4.
  for (int allocation = 1; allocation <= 3; ++allocation)
  {
    loka::core::testing::failLokaAllocRaw("TextLineBreaker", "Table", allocation);
    const bool accepted = value.estimateExtent(4, BlockStyle().wrap(TEXT_WRAP_CHAR), out);
    LOKA_VERIFY(!accepted && out == sentinel);
    LOKA_VERIFY(loka::core::testing::lokaAllocRawLive() == 0);
    loka::core::testing::allowLokaAllocRaw();
  }
  verifyEstimate(value, 4, BlockStyle().wrap(TEXT_WRAP_CHAR), 4, 480);
}

void testTextBreakerCountingCapacityRefusal()
{
  const AttributedString value = Styled(String(std::string(40, 'x')), Bold);
  for (int mode = 0; mode < 2; ++mode)
  {
    const ChangingRangeSource source(value, mode != 0);
    const TextLineBreaker result(source, BlockStyle().wrap(TEXT_WRAP_CHAR), 40);
    LOKA_VERIFY(!result.valid() && result.lineCount() == 0 && result.width() == 0 && result.height() == 0);
  }
}
