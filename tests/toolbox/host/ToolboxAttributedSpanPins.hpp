#ifndef LOKA_TOOLBOX_ATTRIBUTED_SPAN_PINS_HPP
#define LOKA_TOOLBOX_ATTRIBUTED_SPAN_PINS_HPP
namespace loka
{
  namespace testing
  {
    class ToolboxAttributedTextTableAccess
    {
    public:
      static TextFragment &fragment(const ToolboxAttributedTextTable &table, std::size_t i)
      {
        return const_cast<TextFragment &>(table.lines().fragment(i));
      }
      static std::size_t spans(const ToolboxAttributedTextTable &table)
      {
        return table.spans_.size();
      }
      static const TextStyleSpan &span(const ToolboxAttributedTextTable &table, std::size_t i)
      {
        return table.spans_[i];
      }
      static const TextBreakCharacter &character(const ToolboxAttributedTextTable &table, std::size_t i)
      {
        return table.characters_[i];
      }
      static int advance(const ToolboxAttributedTextTable &table, std::size_t i)
      {
        return table.advances_[i];
      }
      static std::size_t fonts(const ToolboxAttributedTextTable &table)
      {
        return table.fonts_.size();
      }
      static std::size_t bytes(const ToolboxAttributedTextTable &table)
      {
        return table.bytes_.size();
      }
      static std::size_t limit(const ToolboxAttributedTextTable &table, std::size_t span)
      {
        return SHRT_MAX / table.metrics_[span].maxAdvance;
      }
    };
  } // namespace testing
} // namespace loka
namespace
{
  typedef loka::testing::ToolboxAttributedTextTableAccess SpanAccess;
  void VerifySpanDraw(std::size_t i, const char *bytes, int x, int y, int size, Style face, long ink, short family = 3)
  {
    LOKA_VERIFY(i < toolbox_host::draws.size());
    const toolbox_host::Draw &d = toolbox_host::draws[i];
    LOKA_VERIFY(d.bytes == bytes && d.length == static_cast<int>(std::strlen(bytes)));
    LOKA_VERIFY(d.x == x && d.y == y && d.size == size && d.face == face && d.ink == ink);
    LOKA_VERIFY(d.family == family);
  }
  void SpanCharacterization()
  {
    ToolboxWindow window, other;
    HostPortScope port(other.window());
    ToolboxScenePlatformController controller(&window);
    ToolboxAttributedTextTable table;
    const AttributedString value = Styled("abcd", Bold) + Styled("efgh", FontSize<18>() + Italic);
    const TextAlign aligns[] = {TEXT_ALIGN_LEFT, TEXT_ALIGN_CENTER, TEXT_ALIGN_RIGHT};
    // Terminal dots cost 18, first-fragment units cost 5; both inside and exact cuts.
    const short seats[] = {17, 18, 23, 29, 38, 80};
    for (unsigned a = 0; a < 3; ++a)
      for (unsigned w = 0; w < 6; ++w)
      {
        Pin("ToolboxSpanCharacterization");
        const BlockStyle block = BlockStyle().truncation(TEXT_TRUNCATION_ELLIPSIS).align(aligns[a]);
        LOKA_VERIFY(table.build(value, block, seats[w], controller));
        toolbox_host::reset();
        LOKA_VERIFY(table.draw(7, 11, controller, block, seats[w]));
        const int painted = w == 0 ? 0 : w == 5 ? 44 : w == 3 ? 28 : seats[w];
        const int offset = a == 0 ? 0 : a == 1 ? (seats[w] - painted) / 2 : seats[w] - painted;
        const int x = 7 + offset;
        LOKA_VERIFY(toolbox_host::draws.size() == (w == 0 ? 0u : w == 1 ? 1u : 2u));
        if (w == 1)
          VerifySpanDraw(0, "...", x, 29, 18, italic, blackColor);
        if (w >= 2 && w <= 4)
        {
          VerifySpanDraw(0, w == 2 ? "a" : w == 3 ? "ab" : "abcd", x, 29, 12, bold, blackColor);
          VerifySpanDraw(1, "...", x + painted - 18, 29, 18, italic, blackColor);
        }
        if (w == 5)
        {
          VerifySpanDraw(0, "abcd", x, 29, 12, bold, blackColor);
          VerifySpanDraw(1, "efgh", x + 20, 29, 18, italic, blackColor);
          LOKA_VERIFY(toolbox_host::textTrace == "FSTIMDIFSTIMDIFST");
        }
        if (w == 3)
          LOKA_VERIFY(toolbox_host::textTrace == "FSTWFSTIMDIFSTMIDIFST");
        if (w == 4)
          LOKA_VERIFY(toolbox_host::textTrace == "FSTWFSTIMDIFSTIIFSTMIDIFST");
        GrafPtr selected = 0;
        GetPort(&selected);
        LOKA_VERIFY(selected == other.window());
        LOKA_VERIFY(window.port.txFont == 3 && window.port.txSize == 12 && window.port.txFace == 0);
      }
    const BlockStyle dots = BlockStyle().truncation(TEXT_TRUNCATION_ELLIPSIS);
    LOKA_VERIFY(table.build(Styled("abcdefgh\n", FontSize<18>()) + Styled("z", Bold), dots, 5, controller));
    toolbox_host::reset();
    LOKA_VERIFY(table.draw(7, 11, controller, dots, 5));
    LOKA_VERIFY(toolbox_host::draws.size() == 1);
    VerifySpanDraw(0, "z", 7, 46, 12, bold, blackColor);
    // Role-selected terminal family differs from the inherited first family.
    window.port.txFont = 4;
    const AttributedString families = Styled("abcd", Bold) + Styled("efgh", SmallLabel + Italic + White);
    LOKA_VERIFY(table.build(families, dots, 16, controller));
    toolbox_host::reset();
    LOKA_VERIFY(table.draw(7, 11, controller, dots, 16));
    LOKA_VERIFY(toolbox_host::draws.size() == 2);
    VerifySpanDraw(0, "a", 7, 23, 12, bold, blackColor, 4);
    VerifySpanDraw(1, "...", 12, 23, 9, italic, whiteColor);
    LOKA_VERIFY(window.port.txFont == 4 && window.port.fgColor == blackColor);
  }

  void SpanRefusal(int fixture)
  {
    const int kind = fixture > 2 ? fixture - 2 : fixture;
    const bool earlierLine = fixture > 2;
    Pin(kind == 0   ? "ToolboxSpanMidEmissionRefusal"
        : kind == 1 ? "ToolboxSpanEndpointRefusal"
                    : "ToolboxSpanWidthRefusal");
    ToolboxWindow window, other;
    HostPortScope port(other.window());
    ToolboxScenePlatformController controller(&window);
    const AttributedString value =
        Styled(earlierLine ? "z\na" : "a", Bold) + Styled("bc", Italic) + Styled("defghijkl", Bold);
    window.port.fgColor = whiteColor;
    const BlockStyle block = kind == 0 ? BlockStyle() : BlockStyle().truncation(TEXT_TRUNCATION_ELLIPSIS);
    AttributedTextNode node((AttributedText(value) + block).props);
    ToolboxAttributedTextContext context(&node, &controller);
    LayoutState seat = Seat(kind == 0 ? 100 : 24);
    context.layout(&controller, seat);
    context.render(&controller);
    LOKA_VERIFY(ToolboxAttributedTextContextAccess::known(context));
    const ToolboxAttributedTextTable &table = ToolboxAttributedTextContextAccess::table(context);
    TextFragment &fragment = SpanAccess::fragment(table, (kind == 0 ? 2 : 1) + (earlierLine ? 1 : 0));
    if (kind == 0)
    {
      fragment.start = SpanAccess::bytes(table);
      fragment.end = fragment.start + SpanAccess::limit(table, fragment.span) + 1;
    }
    else
    {
      fragment.width = 100;
      if (kind == 1)
      {
        fragment.start = SpanAccess::bytes(table) + 1;
        fragment.end = fragment.start + 1;
      }
      else
        fragment.span = SpanAccess::spans(table);
    }
    toolbox_host::reset();
    LOKA_VERIFY(!table.draw(10, 20, controller, block, kind == 0 ? 100 : 24));
    LOKA_VERIFY(toolbox_host::draws.size() == (kind == 0 ? 2u : earlierLine ? 1u : 0u));
    toolbox_host::reset();
    controller.observation_.reset();
    context.render(&controller);
    LOKA_VERIFY(toolbox_host::draws.size() == (kind == 0 ? 2u : earlierLine ? 1u : 0u));
    if (earlierLine)
      VerifySpanDraw(0, "z", 10, 32, 12, bold, blackColor);
    if (kind == 0)
    {
      VerifySpanDraw(0, "a", 10, 32, 12, bold, blackColor);
      VerifySpanDraw(1, "bc", 15, 32, 12, italic, blackColor);
    }
    LOKA_VERIFY(!ToolboxAttributedTextContextAccess::known(context));
    LOKA_VERIFY(controller.observation_.outsideRefusal().reason == TOOLBOX_PAINT_REFUSAL_PARTIAL_DRAW);
    GrafPtr selected = 0;
    GetPort(&selected);
    LOKA_VERIFY(selected == other.window());
    LOKA_VERIFY(window.port.txFont == 3 && window.port.txSize == 12 && window.port.txFace == 0);
    LOKA_VERIFY(window.port.fgColor == whiteColor);
    context.onFactChanged(NODE_FACT_ATTACHED, NODE_FACT_RETIRED);
  }
} // namespace
namespace
{
  void SpanUnlinkedProjection()
  {
    ToolboxWindow window;
    HostPortScope port(window.window());
    ToolboxScenePlatformController controller(&window);
    ToolboxAttributedTextTable table;
    const AttributedString value = Styled("", Italic) + Styled("", Red) + Styled("ab", Bold) + Styled("", Italic)
                                   + Styled("", Red) + Styled("cd", Bold) + Styled("e", Italic) + Styled("f", Bold)
                                   + Styled("", Italic) + Styled("", Red);
    LOKA_VERIFY(table.build(value, BlockStyle().wrap(TEXT_WRAP_CHAR), 10, controller));
    LOKA_VERIFY(SpanAccess::spans(table) == 3 && SpanAccess::fonts(table) == 3 && SpanAccess::bytes(table) == 6);
    const std::size_t segments[] = {2, 6, 7}, starts[] = {0, 4, 5}, ends[] = {4, 5, 6};
    const int advances[] = {0, 5, 10, 15, 20, 24, 29};
    for (std::size_t i = 0; i < 3; ++i)
    {
      const TextStyleSpan &span = SpanAccess::span(table, i);
      LOKA_VERIFY(span.segment == segments[i] && span.start == starts[i] && span.end == ends[i]);
      LOKA_VERIFY(span.style == (i == 1 ? Italic : Bold));
    }
    for (std::size_t i = 0; i < 6; ++i)
    {
      const TextBreakCharacter &row = SpanAccess::character(table, i);
      LOKA_VERIFY(row.value == 'a' + i && row.offset == i && row.end == i + 1);
      LOKA_VERIFY(row.span == (i < 4 ? 0 : i - 3));
      LOKA_VERIFY(SpanAccess::advance(table, i) == advances[i]);
    }
    LOKA_VERIFY(SpanAccess::advance(table, 6) == 29);
    LOKA_VERIFY(table.lines().lineCount() == 3 && table.width() == 10 && table.height() == 51);
    const std::size_t spans[] = {0, 0, 1, 2}, from[] = {0, 2, 4, 5}, to[] = {2, 4, 5, 6};
    const int widths[] = {10, 10, 4, 5};
    for (std::size_t i = 0; i < 4; ++i)
    {
      const TextFragment &f = table.lines().fragment(i);
      LOKA_VERIFY(f.span == spans[i] && f.start == from[i] && f.end == to[i] && f.width == widths[i]);
    }
  }
  void SpanClipCharacterization()
  {
    for (int mode = 0; mode < 3; ++mode)
    {
      ToolboxWindow window;
      HostPortScope port(window.window());
      ToolboxScenePlatformController controller(&window);
      const AttributedString value = Styled("abcd", Bold) + Styled("efgh", FontSize<18>() + Italic);
      AttributedTextNode node(
          (AttributedText(value) + BlockStyle().truncation(TEXT_TRUNCATION_ELLIPSIS).align(TEXT_ALIGN_RIGHT)).props);
      ToolboxAttributedTextContext context(&node, &controller);
      LayoutState seat = Seat(29);
      context.layout(&controller, seat);
      const Rect paint = ToolboxAttributedTextContextAccess::paintRect(context);
      const Rect narrow = {
          paint.top, static_cast<short>(paint.left + 3), paint.bottom, static_cast<short>(paint.right - 3)};
      ToolboxPaintClip clip(mode == 1 ? narrow : paint);
      toolbox_host::reset();
      toolbox_host::failRegions = mode == 2 ? 2 : 0;
      context.render(&controller);
      LOKA_VERIFY(toolbox_host::draws.size() == 2);
      VerifySpanDraw(0, "ab", 11, 38, 12, bold, blackColor);
      VerifySpanDraw(1, "...", 21, 38, 18, italic, blackColor);
      const Rect expected = mode == 1 ? narrow : paint;
      for (std::size_t i = 0; i < 2; ++i)
        LOKA_VERIFY(EqualRect(&toolbox_host::draws[i].clip, &expected));
      context.onFactChanged(NODE_FACT_ATTACHED, NODE_FACT_RETIRED);
    }
  }
} // namespace

#endif
