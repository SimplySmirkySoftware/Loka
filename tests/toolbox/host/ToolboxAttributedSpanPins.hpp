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

namespace
{
  class SpanWalkString : public loka::platform::String
  {
  public:
    SpanWalkString(const std::string &first,
                   const std::string &second,
                   unsigned &calls,
                   bool firstReadable = true,
                   bool secondReadable = true)
        : first_(first),
          second_(second),
          calls_(calls),
          firstReadable_(firstReadable),
          secondReadable_(secondReadable)
    {
    }
    virtual bool appendUtf8(std::string &out) const
    {
      const bool first = this->calls_++ == 0;
      if (!(first ? this->firstReadable_ : this->secondReadable_))
        return false;
      out += first ? this->first_ : this->second_;
      return true;
    }

  private:
    const std::string first_, second_;
    unsigned &calls_;
    const bool firstReadable_, secondReadable_;
  };
  class SpanReadableString : public loka::platform::String
  {
  public:
    explicit SpanReadableString(const bool &readable)
        : readable_(readable)
    {
    }
    virtual bool appendUtf8(std::string &out) const
    {
      if (!this->readable_)
        return false;
      out += "target";
      return true;
    }

  private:
    const bool &readable_;
  };
  class SpanSwitchBody : public loka::platform::String
  {
  public:
    SpanSwitchBody(bool &readable, bool next)
        : readable_(readable),
          next_(next)
    {
    }
    virtual bool appendUtf8(std::string &out) const
    {
      this->readable_ = this->next_;
      out += "x";
      return true;
    }

  private:
    bool &readable_;
    const bool next_;
  };
  loka::core::String SpanString(loka::platform::String *value)
  {
    return loka::core::String::FromPlatform(loka::core::Managed<loka::platform::String>::Wrap(value));
  }

  void SpanPair(const LinkTarget &a, const LinkTarget &b, std::size_t expected)
  {
    ToolboxWindow window;
    HostPortScope port(window.window());
    ToolboxScenePlatformController controller(&window);
    ToolboxAttributedTextTable table;
    LOKA_VERIFY(table.build(Linked("a", Bold, a) + Linked("b", Bold, b), BlockStyle(), 100, controller));
    LOKA_VERIFY(SpanAccess::spans(table) == expected && SpanAccess::fonts(table) == expected);
    LOKA_VERIFY(table.lines().line(0).fragmentCount == expected);
    LOKA_VERIFY(SpanAccess::span(table, 0).segment == 0 && SpanAccess::span(table, 0).start == 0);
    LOKA_VERIFY(SpanAccess::span(table, 0).end == (expected == 1 ? 2u : 1u));
    LOKA_VERIFY(SpanAccess::character(table, 1).span == expected - 1);
    if (expected == 2)
      LOKA_VERIFY(SpanAccess::span(table, 1).segment == 1 && SpanAccess::span(table, 1).start == 1
                  && SpanAccess::span(table, 1).end == 2);
    toolbox_host::reset();
    LOKA_VERIFY(table.draw(7, 11, controller, BlockStyle(), 100));
    LOKA_VERIFY(toolbox_host::draws.size() == expected);
    VerifySpanDraw(0, expected == 1 ? "ab" : "a", 7, 23, 12, bold, blackColor);
    if (expected == 2)
      VerifySpanDraw(1, "b", 12, 23, 12, bold, blackColor);
  }
  void SpanLinks()
  {
    Pin("ToolboxSpanLinks");
    const LinkTarget x(loka::core::String("x")), y(loka::core::String("y"));
    SpanPair(x, y, 2);
    SpanPair(x, LinkTarget(loka::core::String("x")), 1);
    SpanPair(x, LinkTarget(), 2);
    SpanPair(LinkTarget(), x, 2);
    bool readable = false;
    const LinkTarget a(SpanString(new SpanReadableString(readable)));
    const LinkTarget b(SpanString(new SpanReadableString(readable)));
    SpanPair(a, a, 1);
    SpanPair(a, b, 2);
    ToolboxWindow window;
    HostPortScope port(window.window());
    ToolboxScenePlatformController controller(&window);
    ToolboxAttributedTextTable table;
    const AttributedString value = Linked("", Italic, y) + Linked("", Red, y) + Linked("a", Bold, x)
                                   + Linked("", Italic, y) + Linked("", Red, y) + Linked("b", Bold, x)
                                   + Linked("c", Bold, y) + Linked("", Italic, x) + Linked("", Red, x);
    LOKA_VERIFY(table.build(value, BlockStyle(), 100, controller));
    LOKA_VERIFY(SpanAccess::spans(table) == 2 && SpanAccess::bytes(table) == 3);
    LOKA_VERIFY(SpanAccess::span(table, 0).segment == 2 && SpanAccess::span(table, 0).end == 2);
    LOKA_VERIFY(SpanAccess::span(table, 1).segment == 6 && SpanAccess::span(table, 1).start == 2);
    LOKA_VERIFY(table.build(Linked("", Bold, x) + Linked("", Italic, y), BlockStyle(), 100, controller));
    LOKA_VERIFY(SpanAccess::spans(table) == 0 && SpanAccess::bytes(table) == 0);
    const LinkTarget target(loka::core::String("target")), other(loka::core::String("target"));
    const loka::core::String first = SpanString(new SpanSwitchBody(readable, true));
    const loka::core::String third = SpanString(new SpanSwitchBody(readable, false));
    Pin("ToolboxSpanPreviousNonempty");
    LOKA_VERIFY(table.build(Linked(first, Bold, a) + Linked("x", Bold, target) + Linked(third, Bold, other),
                            BlockStyle(),
                            100,
                            controller));
    LOKA_VERIFY(SpanAccess::spans(table) == 1 && SpanAccess::span(table, 0).segment == 0);
    LOKA_VERIFY(SpanAccess::span(table, 0).end == 3);
  }
  void SpanWall(const std::string &kind)
  {
    Pin(kind.c_str());
    ToolboxWindow window;
    HostPortScope port(window.window());
    ToolboxScenePlatformController controller(&window);
    ToolboxAttributedTextTable table;
    unsigned calls = 0;
    AttributedString value;
    if (kind == "span-char-growth" || kind == "span-char-shrink")
    {
      const bool growth = kind == "span-char-growth";
      value = Styled(
          SpanString(new SpanWalkString(std::string(growth ? 33 : 2, 'x'), std::string(growth ? 34 : 1, 'x'), calls)),
          Bold);
    }
    else
    {
      const unsigned count = kind == "span-heap-growth" ? 34 : 33;
      const bool growth = kind != "span-underfill";
      const LinkTarget last(SpanString(new SpanWalkString("last", "last", calls, growth, !growth)));
      AttributedString::Builder builder(count);
      for (unsigned i = 0; i < count; ++i)
      {
        char target[32];
        std::sprintf(target, "target-%u", i);
        const LinkTarget link =
            i + 1 == count ? last : LinkTarget(loka::core::String(i + 2 == count ? "last" : target));
        LOKA_VERIFY(builder.appendLink(loka::core::String("x"), Bold, link));
      }
      value = builder.build();
    }
    // Reuse previously populated inline storage so a missing completion check
    // cannot pass merely because uninitialized trailing rows happen to refuse.
    if (kind == "span-char-shrink")
    {
      LOKA_VERIFY(table.build(Styled("xx", Bold), BlockStyle(), 100, controller));
      table.clear();
    }
    calls = 0;
    LOKA_VERIFY(!table.build(value, BlockStyle(), 100, controller));
    LOKA_VERIFY(!table.valid() && table.width() == 0 && table.height() == 0);
  }
  void SpanCompensation()
  {
    Pin("ToolboxSpanCompensatingLengths");
    unsigned a = 0, b = 0;
    const AttributedString value = Styled(SpanString(new SpanWalkString("a", "ab", a)), Bold)
                                   + Styled(SpanString(new SpanWalkString("bc", "c", b)), Italic);
    ToolboxWindow window;
    HostPortScope port(window.window());
    ToolboxScenePlatformController controller(&window);
    ToolboxAttributedTextTable table;
    LOKA_VERIFY(table.build(value, BlockStyle(), 100, controller));
    LOKA_VERIFY(SpanAccess::bytes(table) == 3 && SpanAccess::spans(table) == 2);
    LOKA_VERIFY(SpanAccess::span(table, 0).end == 2 && SpanAccess::span(table, 1).start == 2);
    LOKA_VERIFY(SpanAccess::character(table, 1).value == 'b' && SpanAccess::character(table, 1).span == 0);
    toolbox_host::reset();
    LOKA_VERIFY(table.draw(7, 11, controller, BlockStyle(), 100));
    VerifySpanDraw(0, "ab", 7, 23, 12, bold, blackColor);
    VerifySpanDraw(1, "c", 17, 23, 12, italic, blackColor);
  }
  void SpanCache()
  {
    Pin("ToolboxSpanContextTargetOnly");
    ToolboxWindow window;
    HostPortScope port(window.window());
    ToolboxScenePlatformController controller(&window);
    const LinkTarget x(loka::core::String("x")), y(loka::core::String("y"));
    const AttributedString joined = Linked("a", Bold, x) + Linked("b", Bold, x);
    const AttributedString split = Linked("a", Bold, x) + Linked("b", Bold, y);
    AttributedTextNode node(AttributedText(joined).props);
    ToolboxAttributedTextContext context(&node, &controller);
    LayoutState seat = Seat(100);
    context.layout(&controller, seat);
    context.render(&controller);
    LOKA_VERIFY(ToolboxAttributedTextContextAccess::known(context));
    const ToolboxAttributedTextTable &table = ToolboxAttributedTextContextAccess::table(context);
    LOKA_VERIFY(SpanAccess::spans(table) == 1);
    const PaintQuery query = {ToolboxPaintScope(), PLACEMENT_ELIGIBLE};
    node.props = AttributedText(split).props;
    LOKA_VERIFY(context.queryPaintDamage(query).kind == PAINT_ANSWER_REFUSED);
    LOKA_VERIFY(context.queryPaintDamage(query).reason == PAINT_REFUSED_PROPS_UNRECONCILED);
    seat = Seat(100);
    seat.inputs = NODE_DIRTY_NONE;
    context.layout(&controller, seat);
    LOKA_VERIFY(SpanAccess::spans(table) == 2 && table.matches(split));
    const PaintAnswer changed = context.queryPaintDamage(query);
    LOKA_VERIFY(changed.kind == PAINT_ANSWER_EXACT && changed.damage.width > 0 && changed.damage.height > 0);
    context.render(&controller);
    node.props = AttributedText(joined).props;
    toolbox_host::reset();
    context.render(&controller);
    LOKA_VERIFY(SpanAccess::spans(table) == 1 && table.matches(joined));
    LOKA_VERIFY(toolbox_host::draws.size() == 1 && toolbox_host::draws[0].bytes == "ab");
    node.props = AttributedText(split).props;
    toolbox_host::reset();
    context.render(&controller);
    LOKA_VERIFY(SpanAccess::spans(table) == 2 && toolbox_host::draws.size() == 2);
    LOKA_VERIFY(context.queryPaintDamage(query).kind == PAINT_ANSWER_EXACT);
    LOKA_VERIFY(context.queryPaintDamage(query).damage.width == 0);
    context.onFactChanged(NODE_FACT_ATTACHED, NODE_FACT_RETIRED);
  }
} // namespace
namespace
{
  void SpanGeometry()
  {
    Pin("ToolboxSpanLinkedGeometry");
    ToolboxWindow window;
    HostPortScope port(window.window());
    ToolboxScenePlatformController controller(&window);
    const LinkTarget x(loka::core::String("x")), y(loka::core::String("y"));
    const std::size_t limit = SHRT_MAX / 5;
    for (int delta = -1; delta <= 1; ++delta)
    {
      const std::size_t boundary = limit + delta;
      const AttributedString value =
          Linked(loka::core::String(std::string(boundary, 'a')), Bold, x) + Linked("bbb", Bold, y);
      ToolboxAttributedTextTable table;
      toolbox_host::reset();
      LOKA_VERIFY(table.build(value, BlockStyle(), 100, controller));
      LOKA_VERIFY(SpanAccess::span(table, 0).end == boundary && SpanAccess::span(table, 1).start == boundary);
      LOKA_VERIFY(SpanAccess::character(table, boundary - 1).end == boundary);
      LOKA_VERIFY(SpanAccess::character(table, boundary).offset == boundary);
      LOKA_VERIFY(SpanAccess::advance(table, boundary) == static_cast<int>(boundary * 5));
      LOKA_VERIFY(SpanAccess::advance(table, boundary + 3) == static_cast<int>((boundary + 3) * 5));
      LOKA_VERIFY(toolbox_host::measurePayloads.size() == (delta == 1 ? 3u : 2u));
      LOKA_VERIFY(toolbox_host::measurePayloads[0] == std::string(boundary > limit ? limit : boundary, 'a'));
      LOKA_VERIFY(toolbox_host::measurePayloads.back() == "bbb");
      toolbox_host::reset();
      LOKA_VERIFY(table.draw(0, 0, controller, BlockStyle(), 100));
      LOKA_VERIFY(toolbox_host::draws.size() == (delta == 1 ? 3u : 2u));
      LOKA_VERIFY(toolbox_host::draws[0].bytes == std::string(boundary > limit ? limit : boundary, 'a'));
      LOKA_VERIFY(toolbox_host::draws.back().bytes == "bbb");
      LOKA_VERIFY(toolbox_host::draws.back().x
                  == static_cast<short>(boundary * 5 > SHRT_MAX ? SHRT_MAX : boundary * 5));
      LOKA_VERIFY(table.build(value, BlockStyle().wrap(TEXT_WRAP_CHAR), 20, controller));
      LOKA_VERIFY(table.lines().lineCount() == (boundary + 6) / 4);
      LOKA_VERIFY(table.lines().fragment(0).start == 0 && table.lines().fragment(0).end == 4);
    }
    const AttributedString value = Linked("abc", Bold, x) + Linked("defgh", Bold, y);
    for (int a = 0; a < 2; ++a)
      for (int n = 2; n <= 4; ++n)
      {
        const BlockStyle block =
            BlockStyle().truncation(TEXT_TRUNCATION_ELLIPSIS).align(a == 0 ? TEXT_ALIGN_CENTER : TEXT_ALIGN_RIGHT);
        const short seat = static_cast<short>(15 + n * 5 + 4);
        ToolboxAttributedTextTable table;
        LOKA_VERIFY(table.build(value, block, seat, controller));
        toolbox_host::reset();
        LOKA_VERIFY(table.draw(7, 11, controller, block, seat));
        const int start = a == 0 ? 9 : 11;
        LOKA_VERIFY(toolbox_host::draws.size() == (n == 4 ? 3u : 2u));
        VerifySpanDraw(0, n == 2 ? "ab" : "abc", start, 23, 12, bold, blackColor);
        if (n == 4)
          VerifySpanDraw(1, "d", start + 15, 23, 12, bold, blackColor);
        VerifySpanDraw(n == 4 ? 2 : 1, "...", start + n * 5, 23, 12, bold, blackColor);
      }
    // Cumulative floor(4.5*n): restarting at a split loses the half-pixel carry.
    toolbox_host::positionMode = 7;
    ToolboxAttributedTextTable joined, split;
    LOKA_VERIFY(joined.build(Styled("abcd", Bold), BlockStyle(), 100, controller));
    LOKA_VERIFY(split.build(Linked("a", Bold, x) + Linked("bcd", Bold, y), BlockStyle(), 100, controller));
    LOKA_VERIFY(joined.width() == 18 && split.width() == 17);
    const int prefix[] = {0, 4, 8, 13, 17};
    for (std::size_t i = 0; i < 5; ++i)
      LOKA_VERIFY(SpanAccess::advance(split, i) == prefix[i]);
    toolbox_host::reset();
    LOKA_VERIFY(split.draw(7, 11, controller, BlockStyle(), 100));
    VerifySpanDraw(0, "a", 7, 23, 12, bold, blackColor);
    VerifySpanDraw(1, "bcd", 11, 23, 12, bold, blackColor);
    LOKA_VERIFY(joined.build(Styled("abcd", Bold), BlockStyle().wrap(TEXT_WRAP_CHAR), 8, controller));
    LOKA_VERIFY(
        split.build(Linked("a", Bold, x) + Linked("bcd", Bold, y), BlockStyle().wrap(TEXT_WRAP_CHAR), 8, controller));
    LOKA_VERIFY(joined.lines().lineCount() == 4 && split.lines().lineCount() == 3);
    LOKA_VERIFY(split.lines().fragment(1).end == 2);
    toolbox_host::positionMode = 0;
    ToolboxAttributedTextTable malformed;
    LOKA_VERIFY(malformed.build(Linked("\xc3", Bold, x) + Linked("\xa9", Bold, y), BlockStyle(), 100, controller));
    LOKA_VERIFY(SpanAccess::bytes(malformed) == 2 && SpanAccess::spans(malformed) == 2);
    toolbox_host::reset();
    LOKA_VERIFY(malformed.draw(7, 11, controller, BlockStyle(), 100));
    VerifySpanDraw(0, "?", 7, 23, 12, bold, blackColor);
    VerifySpanDraw(1, "?", 12, 23, 12, bold, blackColor);
  }
} // namespace

#endif
