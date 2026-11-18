#include "DocumentMarkdownTests.hpp"
#include "app/doc/Inline.hpp"
#include "app/doc/Markdown.hpp"
#include "app/nodes/nestable/Match.hpp"
#include "app/nodes/nestable/For.hpp"
#include "app/nodes/nestable/Keyed.hpp"
#include "app/nodes/nestable/LazyView.hpp"
#include "app/nodes/nestable/Show.hpp"
#include "support/DocumentFixture.hpp"
#include "support/LokaAllocFailure.hpp"

namespace
{
  using namespace loka::app;
  using namespace loka::app::scene;
  using namespace loka::core;
  using namespace PropsReconciliationSupport;
  using loka::dsl::testing::SceneTestAccess;

  void expect(Node *node, const char *expected)
  {
    std::string out = "prefix";
    bool ok = doc::WriteMarkdown(node, out);
    if (expected && out != std::string("prefix") + expected)
      std::fprintf(stderr, "expected [%s] got [%s]\n", expected, out.c_str());
    LOKA_VERIFY(ok == (expected != 0));
    LOKA_VERIFY(out == std::string("prefix") + (expected ? expected : ""));
  }
  template <class D> void golden(const D &d, const char *expected)
  {
    Fixture<D> f(d, false);
    expect(f.node(), expected);
  }
  AttributedString plain(const char *s)
  {
    return Styled(s, TextStyle());
  }

  class PartialString : public loka::platform::String
  {
    std::string bytes_;
    bool readable_;

  public:
    PartialString(const std::string &bytes, bool readable)
        : bytes_(bytes),
          readable_(readable)
    {
    }
    virtual bool appendUtf8(std::string &out) const
    {
      out += this->bytes_;
      return this->readable_;
    }
  };
  String raw(const std::string &bytes, bool readable = true)
  {
    return String::FromPlatform(Managed<loka::platform::String>::Wrap(new PartialString(bytes, readable)));
  }
  void late(const AttributedString &p)
  {
    ColumnDefinition column;
    column << doc::Heading(1, "successful") << doc::Paragraph(p);
    golden(column, 0);
  }
  class NestedBlock : public doc::BlockNode<doc::ParagraphProps>
  {
  public:
    explicit NestedBlock(const doc::ParagraphProps &p)
        : BlockNode<doc::ParagraphProps>(p)
    {
    }
    virtual void composeChildren(NodeComposition &c)
    {
      c.declare(doc::Paragraph("hidden"));
    }
  };
  class InlineBoundary : public BoundaryNodeFor<InlineBoundary>
  {
  public:
    explicit InlineBoundary(const BoundaryPropsFor<InlineBoundary> &p)
        : BoundaryNodeFor<InlineBoundary>(p)
    {
    }
    virtual void composeNode(NodeComposition &c)
    {
      c.declare(doc::Paragraph("nested"));
    }
  };
  class Transparent : public ComponentNodeWithProps<doc::ParagraphProps>
  {
  public:
    explicit Transparent(const doc::ParagraphProps &p)
        : ComponentNodeWithProps<doc::ParagraphProps>(p)
    {
    }
    virtual void composeChildren(NodeComposition &c)
    {
      c.declare(Boundary<InlineBoundary>());
    }
  };
  class Probe : public BoundaryNodeFor<Probe>
  {
  public:
    unsigned consultations;
    explicit Probe(const BoundaryPropsFor<Probe> &p)
        : BoundaryNodeFor<Probe>(p),
          consultations(0)
    {
    }
    virtual Node *retainedLifecycleBranch(unsigned i)
    {
      ++this->consultations;
      return BoundaryNodeFor<Probe>::retainedLifecycleBranch(i);
    }
    virtual void composeNode(NodeComposition &c)
    {
      c.declare(
          Card() << (Column() << Text("ordinary") << AttributedText(plain("direct"))
                              << NodeDefinition<doc::ParagraphProps, NestedBlock>(doc::ParagraphProps("outer"))
                              << doc::Heading(2, "sibling") << Button("button")
                              << NodeDefinition<doc::ParagraphProps, Transparent>(doc::ParagraphProps("omitted"))));
    }
  };
} // namespace
void testDocumentMarkdownBlocks()
{
  for (int i = 1; i <= 6; ++i)
    golden(doc::Heading(i, "h"), (std::string(i, '#') + " h\n").c_str());
  golden(doc::Heading(2, ""), "##\n");
  golden(doc::Heading(1, "a\r\nb\rc"), "# a b c\n");
  golden(doc::Paragraph("\n\r"), "");
  golden(doc::Paragraph("\nabc\r"), "abc\n");
  golden(doc::CodeBlock(""), "```\n```\n");
  golden(doc::CodeBlock("\n"), "```\n\n```\n");
  golden(doc::CodeBlock("x\r\n\r"), "```\nx\n\n```\n");
  golden(doc::CodeBlock("x````y"), "`````\nx````y\n`````\n");
  golden(doc::Paragraph(doc::Code("\n")), "` `\n");
}
void testDocumentMarkdownInline()
{
  golden(doc::Paragraph(Styled("a", Bold) + Styled("b", Bold + Italic) + Styled("c", Italic)),
         "**a**<strong><em>b</em></strong><em>c</em>\n");
  golden(doc::Paragraph(plain("a") + Styled("€", Italic) + plain("b")), "a<em>€</em>b\n");
  golden(doc::Paragraph(plain("日") + Styled("「x」", Bold) + plain("語")), "日<strong>「x」</strong>語\n");
  golden(doc::Paragraph(Linked("a*b", Bold + Italic + Monospace, LinkTarget(String("u")))), "[***`a*b`***](<u>)\n");
  golden(doc::Paragraph(doc::Code("`x`")), "`` `x` ``\n");
  golden(doc::Paragraph(doc::Code(" x ")), "`  x  `\n");
  golden(doc::Paragraph(Styled(" a ", Italic)), "&#32;*a*&#32;\n");
  golden(doc::Paragraph(Styled(" ", Bold)), "&#32;\n");
  golden(doc::Paragraph(Linked(" a ", Bold + Monospace, LinkTarget(String("u")))),
         "[` `<strong>`a`</strong>` `](<u>)\n");
  golden(doc::Paragraph(Styled("a", Link)
                        + Styled("b", FontSize<18>() + TextStyle().italic(false).weight(TEXT_WEIGHT_NORMAL))),
         "ab\n");
  golden(doc::Paragraph(Styled("a", Bold) + Styled("b", Bold + Underline)), "**ab**\n");
  golden(doc::Paragraph(doc::Link("a", "u") + doc::Link("", "v") + doc::Link("b", "u")
                        + Linked("c", Bold, LinkTarget(String("u"))) + doc::Link("d", "v")),
         "[ab<strong>c</strong>](<u>)[d](<v>)\n");
  golden(doc::Paragraph(doc::Link("x", "a b") + doc::Link("y", "a%20b")), "[x](<a b>)[y](<a%20b>)\n");
  golden(doc::Paragraph(doc::Link("x", "<&>\\")), "[x](<\\<\\&\\>\\\\>)\n");
  golden(doc::Paragraph(Styled("日\n</strong>", Bold)), "<strong>日\\\n\\</strong></strong>\n");
  golden(doc::Paragraph(Styled("</strong>", Bold)), "**\\</strong>**\n");
}
void testDocumentMarkdownPhysicalLines()
{
  golden(doc::Paragraph("1. a\n2) b\n> c\n+ d\n- e\n= f\n# g"),
         "1\\. a\\\n2\\) b\\\n\\> c\\\n\\+ d\\\n\\- e\\\n\\= f\\\n\\# g\n");
  golden(doc::Paragraph(doc::Link(">x", "u")), "[>x](<u>)\n");
  golden(doc::Paragraph(plain("1") + Styled(". x", Underline)), "1\\. x\n");
  golden(doc::Paragraph(plain("!") + doc::Link("x", "y")), "\\![x](<y>)\n");
  golden(doc::Paragraph("\xEF\xBB\xBFx\xEF\xBB\xBFy"), "&#xFEFF;x\xEF\xBB\xBFy\n");
  golden(doc::Paragraph("x\\\n\ny"), "x\\\\\\\n\\\ny\n");
  golden(doc::Paragraph(Styled("A\r", Underline) + doc::Code("\nB")), "A` B`\n");
  golden(doc::Paragraph("  x　\n\ty　"), "&#xA0;&#32;x&#x3000;\\\n&#9;y&#x3000;\n");
  golden(doc::Paragraph(doc::Link("x", "u") + doc::Link("\r", "v") + doc::Link("\ny", "u")), "[x\\\ny](<u>)\n");
  golden(doc::Heading(1, "　x "), "# &#x3000;x&#xA0;\n");
  golden(doc::Paragraph("[x](y) `*_~<&"), "\\[x\\](y) \\`\\*\\_\\~\\<\\&\n");
}
void testDocumentMarkdownFailure()
{
  expect(0, 0);
  golden(doc::Paragraph(Linked(raw("read"), TextStyle(), LinkTarget(raw("ok")))), "[read](<ok>)\n");
  late(plain("\vbody"));
  late(plain("body\xe2\x80\xa9"));
  late(doc::Code(" \t "));
  late(doc::Code("   "));
  late(Styled(raw("partial", false), Bold));
  late(Linked("x", Bold, LinkTarget(raw("partial", false))));
  late(Styled(raw(std::string("a\0b", 3)), Bold));
  late(Linked("x", Bold, LinkTarget(raw(std::string("a\0b", 3)))));
  late(Styled(raw("\xc0\x80"), Bold));
  late(Linked("x", Bold, LinkTarget(raw("\xff"))));
  late(doc::Link("", "\r"));
  late(doc::Link("x", "\n"));
  golden(doc::CodeBlock(raw(std::string("x\0", 2))), 0);
  AttributedString::Builder consumed(1);
  consumed.build();
  late(consumed.build());
}
void testDocumentMarkdownWalk()
{
  NullScenePlatformController controller;
  Scene scene((Boundary<Probe>()));
  scene.mount(&controller);
  SceneTestAccess::updateAttached(scene, true);
  Probe *owner = static_cast<Probe *>(root(scene));
  owner->consultations = 0;
  expect(owner, "outer\n\n## sibling\n\nnested\n");
  LOKA_VERIFY(owner->consultations == 0);
  Node *column = static_cast<CardNode *>(owner->childrenHead())->contentHead();
  Node *outer = column->asNestable()->childrenHead()->nextInComposition->nextInComposition;
  LOKA_VERIFY(outer->nextInComposition);
  expect(outer, "outer\n");
}
namespace
{
  class KeyRoot : public BoundaryNodeFor<KeyRoot>
  {
  public:
    NodeState<int> generation;
    explicit KeyRoot(const BoundaryPropsFor<KeyRoot> &p)
        : BoundaryNodeFor<KeyRoot>(p)
    {
      this->state(this->generation, 0);
    }
    void declareArm(NodeComposition &c)
    {
      c.declare(doc::Paragraph(this->generation.get() ? "second" : "first"));
    }
    virtual void composeNode(NodeComposition &c)
    {
      typedef reservation::Nodes<doc::ParagraphNode, 1, reservation::Nodes<AttributedTextNode, 1> > Nodes;
      c.declare(Keyed(*this->generation.state(), this, &KeyRoot::declareArm, reservation::SeatNodes<Nodes>()));
    }
  };
  class EmptyHeading : public doc::HeadingNode
  {
  public:
    explicit EmptyHeading(const doc::HeadingProps &p)
        : doc::HeadingNode(p)
    {
    }
    virtual void composeChildren(NodeComposition &) {}
  };
} // namespace
void testDocumentMarkdownSeats()
{
  MutableState<bool> shown(true);
  ShowDefinition show = Show(shown);
  show << doc::Paragraph("yes");
  static_cast<FragmentDefinition *>(show.armDefinition(0))->addOwnedChild(doc::Paragraph("no").clone());
  Fixture<ShowDefinition> s(show);
  expect(root(s.scene), "yes\n");
  Node *parked = s.node();
  shown.set(false);
  settle(s.scene);
  expect(root(s.scene), "no\n");
  expect(parked, "");
  shown.set(true);
  settle(s.scene);
  expect(root(s.scene), "yes\n");

  MutableState<int> choice(0);
  Fixture<MatchDefinition<int> > m(
      Match(choice).arm(0, doc::Paragraph("a")).arm(1, doc::Paragraph("b")).otherwise(doc::Paragraph("c")));
  const char *arms[] = {"a\n", "b\n", "c\n", "a\n"};
  for (int i = 0; i < 4; ++i)
  {
    choice.set(i % 3);
    settle(m.scene);
    expect(root(m.scene), arms[i]);
  }
  NullScenePlatformController controller;
  Scene scene((Boundary<KeyRoot>()));
  scene.mount(&controller);
  SceneTestAccess::updateAttached(scene, true);
  KeyRoot *owner = static_cast<KeyRoot *>(root(scene));
  expect(owner, "first\n");
  owner->generation.set(1);
  settle(scene);
  expect(owner, "second\n");
  const doc::ParagraphProps items[] = {
      doc::ParagraphProps("one"), doc::ParagraphProps("two"), doc::ParagraphProps("three")};
  ColumnDefinition all, window;
  all << For(32, items, ComponentItemFactory<doc::ParagraphProps>());
  window << For(32, items, ComponentItemFactory<doc::ParagraphProps>()).window(1, 1);
  golden(all, "one\n\ntwo\n\nthree\n");
  golden(window, "two\n");
  golden(NodeDefinition<doc::HeadingProps, EmptyHeading>(doc::HeadingProps(1, "empty projection")),
         "# empty projection\n");
#ifdef TEST_BUILD
  doc::HeadingDefinition valid = doc::Heading(1, "valid");
  loka::core::testing::failLokaAllocRaw("AttributedString", "Segments", 1);
  {
    Fixture<doc::HeadingDefinition> failed(valid, false);
    Node *child = failed.node()->asNestable()->childrenHead();
    LOKA_VERIFY(child && !child->asAttributedTextNode()->props.text_->get().valid());
    expect(failed.node(), "# valid\n");
  }
  loka::core::testing::allowLokaAllocRaw();
#endif
}
void testDocumentMarkdownLazyWindow()
{
  typedef doc::ParagraphProps P;
  typedef reservation::Nodes<doc::ParagraphNode, 1, reservation::Nodes<AttributedTextNode, 1> > Nodes;
  PushStateTracker tracker;
  ObservableList<P> list;
  MutableState<Frame> viewport(Frame(0, 0, 100, 30));
  tracker.addState(&viewport);
  LOKA_VERIFY(list.attach(&tracker, 6) == ATTACH_OK);
  const char *items[] = {"zero", "one", "two", "three", "four", "five"};
  for (int i = 0; i < 6; ++i)
    LOKA_VERIFY(list.insert(i, P(items[i])) == EDIT_OK);
  Fixture<LazyGrid<P> > f(LazyColumn(list, reservation::SeatNodes<Nodes>(), 4).cells(100, 30).viewport(viewport));
  expect(root(f.scene), "zero\n\none\n");
  {
    StateTrackerGuard guard(&tracker);
    viewport.set(Frame(0, 90, 100, 30));
  }
  f.scene.flushInvalidation();
  f.scene.flushInvalidation();
  expect(root(f.scene), "two\n\nthree\n\nfour\n");
  LOKA_VERIFY(list.update(list.at(3).id, P("edited")) == EDIT_OK);
  f.scene.flushInvalidation();
  f.scene.flushInvalidation();
  expect(root(f.scene), "two\n\nedited\n\nfour\n");
}
namespace
{
  // A projection built from generated glyphs, before packaging them into document props.
  struct Glyph
  {
    std::string text;
    unsigned flags;
    unsigned link;
    bool space() const
    {
      return this->text == " " || this->text == "\t" || this->text == "\n" || this->text == "　" || this->text == " ";
    }
    bool sameStyle(const Glyph &other) const
    {
      return this->flags == other.flags && this->link == other.link;
    }
  };
  unsigned pick(unsigned &state, unsigned limit)
  {
    state = state * 1664525u + 1013904223u;
    return (state >> 8) % limit;
  }
  std::string hexBytes(const std::string &value)
  {
    const char digits[] = "0123456789abcdef";
    std::string result;
    for (size_t i = 0; i < value.size(); ++i)
    {
      unsigned char c = static_cast<unsigned char>(value[i]);
      result += digits[c >> 4];
      result += digits[c & 15];
    }
    return result;
  }
  void expectedGlyphs(std::vector<Glyph> &input, bool heading)
  {
    std::vector<Glyph> folded;
    for (size_t i = 0; i < input.size(); ++i)
    {
      Glyph g = input[i];
      if (g.text == "\r" && i + 1 < input.size() && input[i + 1].text == "\n")
        continue;
      if (g.text == "\r" || g.text == "\n")
        g.text = heading || (g.flags & 4) ? " " : "\n";
      folded.push_back(g);
    }
    while (!folded.empty() && folded.back().text == "\n")
      folded.pop_back();
    while (!folded.empty() && folded.front().text == "\n")
      folded.erase(folded.begin());
    input.clear();
    for (size_t i = 0; i < folded.size(); ++i)
    {
      size_t left = i, right = i;
      while (left && folded[i].sameStyle(folded[left - 1]))
        --left;
      while (right + 1 < folded.size() && folded[i].sameStyle(folded[right + 1]))
        ++right;
      bool inkBefore = false, inkAfter = false;
      for (size_t j = left; j <= i; ++j)
        inkBefore |= !folded[j].space();
      for (size_t j = i; j <= right; ++j)
        inkAfter |= !folded[j].space();
      Glyph g = folded[i];
      if (!inkBefore || !inkAfter)
        g.flags &= 4;
      input.push_back(g);
    }
  }
} // namespace
int documentMarkdownCases(unsigned seed, unsigned count)
{
  const char *special[] = {"\t", "\n", "\r", " ",    "日",       "語",           "「",           "」", "€", "　",
                           " ",  "\v", "\f", "\x1c", "\xc2\x85", "\xe2\x80\xa8", "\xe2\x80\xa9", "*",  "`", "\\"};
  const char *targets[] = {"", "u", "a b", "a%20b", "x<&>\\", "javascript:x"};
  Fixture<doc::ParagraphDefinition> p(doc::Paragraph(""));
  Fixture<doc::HeadingDefinition> h(doc::Heading(2, ""));
  Fixture<doc::CodeBlockDefinition> code(doc::CodeBlock(""));
  for (unsigned n = 0; n < count; ++n)
  {
    char kind = pick(seed, 8) == 0 ? 'c' : pick(seed, 4) == 0 ? 'h' : 'p';
    if (n < 3)
      kind = 'p';
    std::string rawBody;
    AttributedString::Builder builder(8);
    std::vector<Glyph> projection;
    unsigned segments = n < 3 ? 3 : 1 + pick(seed, 8);
    for (unsigned s = 0; s < segments; ++s)
    {
      unsigned flags = pick(seed, 8), link = pick(seed, 3) ? 0 : pick(seed, 6);
      if (n < 3)
      {
        const unsigned triplet[] = {2, 3, 1};
        flags = n == 0 ? triplet[s] : (s == 1 ? 1 : 0);
        link = 0;
      }
      TextStyle style;
      if (flags & 1)
        style = style + Italic;
      if (flags & 2)
        style = style + Bold;
      if (flags & 4)
        style = style + Monospace;
      std::string body;
      unsigned length = n < 3 ? 1 : pick(seed, 7);
      for (unsigned i = 0; i < length; ++i)
      {
        std::string text;
        if (pick(seed, 2))
          text = static_cast<char>(32 + pick(seed, 95));
        else
          text = special[pick(seed, sizeof(special) / sizeof(special[0]))];
        if (n < 3)
        {
          const char *fixed[][3] = {{"a", "b", "c"}, {"a", "€", "b"}, {"日", "</strong>", "語"}};
          text = fixed[n][s];
        }
        Glyph glyph = {text, flags, link};
        projection.push_back(glyph);
        body += text;
      }
      rawBody += body;
      LOKA_VERIFY(builder.appendLink(String(body.c_str()), style, LinkTarget(String(targets[link]))));
    }
    AttributedString content = builder.build();
    Node *node = kind == 'c' ? code.node() : kind == 'h' ? h.node() : p.node();
    if (kind == 'c')
      LOKA_VERIFY(doc::CodeBlock(String(rawBody)).applyPropsToNode(node));
    else if (kind == 'h')
      LOKA_VERIFY(doc::Heading(2, content).applyPropsToNode(node));
    else
      LOKA_VERIFY(doc::Paragraph(content).applyPropsToNode(node));
    std::string md;
    bool ok = doc::WriteMarkdown(node, md);
    if (kind == 'c')
    {
      std::vector<Glyph> lines;
      for (size_t i = 0; i < projection.size(); ++i)
      {
        Glyph g = {projection[i].text, 0, 0};
        if (g.text == "\r")
        {
          if (i + 1 < projection.size() && projection[i + 1].text == "\n")
            ++i;
          g.text = "\n";
        }
        lines.push_back(g);
      }
      if (!lines.empty() && lines.back().text != "\n")
      {
        Glyph lf = {"\n", 0, 0};
        lines.push_back(lf);
      }
      projection.swap(lines);
    }
    else
      expectedGlyphs(projection, kind == 'h');
    std::printf("%c\t", kind);
    for (size_t i = 0; i < projection.size(); ++i)
      std::printf("%s:%u:%s,",
                  hexBytes(projection[i].text).c_str(),
                  projection[i].flags,
                  hexBytes(targets[projection[i].link]).c_str());
    std::printf("\t%s\n", ok ? hexBytes(md).c_str() : "REFUSED");
  }
  return 0;
}
