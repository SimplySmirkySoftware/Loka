#include "DocumentContainerTests.hpp"
#include "CardTests.hpp"
#include "app/doc/Inline.hpp"
#include "platform/null/context/NullTextContext.hpp"
#include "app/doc/Markdown.hpp"
#include "app/doc/Html.hpp"
#include "app/nodes/nestable/For.hpp"
#include "app/nodes/nestable/Show.hpp"
#include "support/DocumentExportCases.hpp"
#include "app/doc/Render.hpp"
#include "platform/null/context/NullAttributedTextContext.hpp"

namespace
{
  using namespace loka::app;
  using namespace loka::app::scene;
  using namespace loka::core;
  using namespace PropsReconciliationSupport;

  struct Exit
  {
    bool html;
    void check(Node *root, const char *md, const char *ht) const
    {
      std::string out = "prefix";
      const bool ok = this->html ? doc::WriteHtml(root, out) : doc::WriteMarkdown(root, out);
      const char *want = this->html ? ht : md;
      if (out != std::string("prefix") + (want ? want : ""))
        std::fprintf(stderr, "expected [%s], got [%s]\n", want ? want : "REFUSED", out.c_str());
      LOKA_VERIFY(ok == (want != 0));
      LOKA_VERIFY(out == std::string("prefix") + (want ? want : ""));
    }
    template <class D> void golden(const D &d, const char *md, const char *ht) const
    {
      Fixture<D> f(d, false);
      this->check(f.node(), md, ht);
    }
  };
  /** Exercise terminal refusal at each typed event without inspecting a writer's frames.
      Cost: one traversal of this fixture per possible stopping point. */
  class RefusingReader : public doc::BlockReader
  {
    size_t stop_;
    bool event(char name)
    {
      this->events += name;
      return this->events.size() != this->stop_;
    }

  public:
    std::string events;
    explicit RefusingReader(size_t stop)
        : stop_(stop)
    {
    }
    virtual bool enter(const doc::QuoteProps &)
    {
      return this->event('Q');
    }
    virtual bool enter(const doc::ListProps &)
    {
      return this->event('L');
    }
    virtual bool enter(const doc::NumberedListProps &)
    {
      return this->event('N');
    }
    virtual bool read(const doc::ParagraphProps &)
    {
      return this->event('p');
    }
    virtual bool read(const doc::HeadingProps &)
    {
      return this->event('h');
    }
    virtual bool read(const doc::CodeBlockProps &)
    {
      return this->event('c');
    }
    virtual bool read(const doc::ImageProps &)
    {
      return this->event('I');
    }
    virtual bool read(const doc::ItemProps &)
    {
      return this->event('i');
    }
    virtual bool read(const doc::NumberedItemProps &)
    {
      return this->event('o');
    }
    virtual bool leave(const doc::ListProps &)
    {
      return this->event('l');
    }
    virtual bool leave(const doc::NumberedListProps &)
    {
      return this->event('n');
    }
    virtual bool leave(const doc::QuoteProps &)
    {
      return this->event('q');
    }
  };

  void images(const Exit &e)
  {
    e.golden(doc::Image("map.png", "Route"), "![Route](<map.png>)\n", "<p><img src=\"map.png\" alt=\"Route\"></p>\n");
    e.golden(doc::Image("map (1)<&>\\\"", "[]\\*`_<&\""),
             "![\\[\\]\\\\\\*\\`\\_\\<\\&\"](<map (1)\\<\\&\\>\\\\\">)\n",
             "<p><img src=\"map (1)&lt;&amp;&gt;\\&quot;\" alt=\"[]\\*`_&lt;&amp;&quot;\"></p>\n");
    e.golden(doc::Image("rule", ""), "![](<rule>)\n", "<p><img src=\"rule\" alt=\"\"></p>\n");
    e.golden(doc::Image("map", "Trail #2"), "![Trail #2](<map>)\n", "<p><img src=\"map\" alt=\"Trail #2\"></p>\n");
    e.golden(doc::Quote() << doc::Image("map", "Route"),
             "> ![Route](<map>)\n",
             "<blockquote>\n<p><img src=\"map\" alt=\"Route\"></p>\n</blockquote>\n");
    e.golden(Column() << doc::Paragraph("before") << doc::Image("map", "Route") << doc::Paragraph("after"),
             "before\n\n![Route](<map>)\n\nafter\n",
             "<p>before</p>\n<p><img src=\"map\" alt=\"Route\"></p>\n<p>after</p>\n");
    e.golden(Column() << doc::Paragraph("before") << doc::Image("", "Route"), 0, 0);
    e.golden(Column() << doc::Paragraph("before") << (doc::List() << doc::Image("map", "")), 0, 0);
    const doc::ImageDefinition sized = doc::Image(String("map&1"), String("Route <\"A\">"), doc::ImageSize(480, 320));
    e.golden(doc::Quote() << doc::Image("map", "") << sized << doc::Paragraph("after"),
             "> ![](<map>)\n>\n> <img src=\"map&amp;1\" alt=\"Route &lt;&quot;A&quot;&gt;\" width=\"480\" "
             "height=\"320\">\n>\n> after\n",
             "<blockquote>\n<p><img src=\"map\" alt=\"\"></p>\n"
             "<p><img src=\"map&amp;1\" alt=\"Route &lt;&quot;A&quot;&gt;\" width=\"480\" "
             "height=\"320\"></p>\n<p>after</p>\n</blockquote>\n");
    e.golden(Column() << doc::Paragraph("before") << doc::Image("map", "", doc::ImageSize(0, 1)), 0, 0);
    e.golden(Column() << doc::Paragraph("before") << doc::Image("map", "", doc::ImageSize(1, 0)), 0, 0);
    const std::string invalid[] = {"a\rb", "a\nb", std::string("a\0b", 3), "a\xff"};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i)
    {
      const String bad = DocumentExportCases::raw(invalid[i]);
      e.golden(Column() << doc::Paragraph("before") << doc::Image(bad, String("Route")), 0, 0);
      e.golden(Column() << doc::Paragraph("before") << doc::Image(String("map"), bad), 0, 0);
    }
  }

  void containers(const Exit &e)
  {
    doc::List a, b;
    a << doc::Item("a");
    b << doc::Item("b");
    const char *adjacent = "- a\n\n* b\n";
    const char *lists = "<ul>\n<li>a</li>\n</ul>\n<ul>\n<li>b</li>\n</ul>\n";
    e.golden(Column() << a << b, adjacent, lists);
    e.golden(Column() << a << doc::Paragraph("") << b, adjacent, lists);
    e.golden(Column() << a << doc::List() << b, adjacent, lists);
    e.golden(Column() << a << doc::Paragraph("p") << b,
             "- a\n\np\n\n- b\n",
             "<ul>\n<li>a</li>\n</ul>\n<p>p</p>\n<ul>\n<li>b</li>\n</ul>\n");
    e.golden(doc::Quote() << a << b,
             "> - a\n>\n> * b\n",
             (std::string("<blockquote>\n") + lists + "</blockquote>\n").c_str());
    e.golden(
        Column() << (doc::Quote() << (doc::Quote() << doc::CodeBlock("a\n\nb"))) << doc::Paragraph("after"),
        "> > ```\n> > a\n> >\n> > b\n> > ```\n\nafter\n",
        "<blockquote>\n<blockquote>\n<pre><code>a\n\nb\n</code></pre>\n</blockquote>\n</blockquote>\n<p>after</p>\n");
    e.golden(doc::List() << doc::Item("") << doc::Item("a\r\nb") << doc::Item("- x") << doc::Item("1. x")
                         << doc::Item("* * *") << doc::Item("#") << doc::Item(">"),
             "- a\\\n  b\n- \\- x\n- 1\\. x\n- \\* \\* \\*\n- \\#\n- \\>\n",
             "<ul>\n<li>a<br>b</li>\n<li>- x</li>\n<li>1. x</li>\n<li>* * *</li>\n<li>#</li>\n<li>&gt;</li>\n</ul>\n");
    e.golden(doc::Quote() << (doc::List() << doc::Item("")) << doc::Quote(), "", "");
    e.golden(doc::Item(""), 0, 0);
    e.golden(doc::Quote() << doc::Item("outside"), 0, 0);
    e.golden(doc::List() << doc::Quote(), 0, 0);
    e.golden(doc::List() << doc::Paragraph("x"), 0, 0);
    e.golden(doc::List() << doc::Paragraph(""), 0, 0);
    e.golden(doc::List() << doc::Heading(2, "h"), 0, 0);
    e.golden(doc::List() << doc::CodeBlock(""), 0, 0);
    e.golden(doc::List() << doc::List(), 0, 0);
    e.golden(doc::List() << (doc::Quote() << doc::Paragraph("x")), 0, 0);
    e.golden(doc::List() << (Column() << doc::Paragraph("x")), 0, 0);
    e.golden(doc::List() << Text("ordinary"), "", "");
    e.golden(
        Column() << a
                 << (doc::Quote() << (doc::Quote() << (doc::List() << doc::Item("visible") << doc::Paragraph("")))),
        0,
        0);
    e.golden(doc::Quote() << (doc::List() << doc::Item(doc::Link("a\nb", "u"))),
             "> - [a\\\n>   b](<u>)\n",
             "<blockquote>\n<ul>\n<li><a href=\"u\">a<br>b</a></li>\n</ul>\n</blockquote>\n");
    Fixture<doc::List> mounted(a);
    e.check(mounted.node()->asNestable()->childrenHead(), 0, 0);
    doc::Quote deep;
    deep << a;
    std::string md = "> - a\n", ht = "<blockquote>\n<ul>\n<li>a</li>\n</ul>\n</blockquote>\n";
    for (unsigned depth = 2; depth < 16; ++depth)
    {
      deep = doc::Quote() << deep;
      md = "> " + md;
      ht = "<blockquote>\n" + ht + "</blockquote>\n";
    }
    e.golden(deep, md.c_str(), ht.c_str());
    e.golden(doc::Quote() << deep, 0, 0);
    MutableState<bool> shown(false);
    Fixture<doc::List> seats(doc::List() << (Show(shown) << doc::Item("shown")));
    e.check(seats.node(), "", "");
    shown.set(true);
    settle(seats.scene);
    e.check(seats.node(), "- shown\n", "<ul>\n<li>shown</li>\n</ul>\n");
    Node *branch = seats.node()->asNestable()->childrenHead();
    shown.set(false);
    settle(seats.scene);
    e.check(branch, "", "");
  }
} // namespace
void testDocumentImageMarkdown()
{
  const Exit e = {false};
  images(e);
}

void testDocumentImageHtml()
{
  const Exit e = {true};
  images(e);
}

void testDocumentImageRender()
{
  ColumnDefinition page;
  page << doc::Paragraph("before") << doc::Image("map (1)", "Route");
  typedef Props<ColumnDefinition> P;
  std::string markdown, html;
  LOKA_VERIFY(doc::RenderMarkdown(Boundary<P::NodeType>(P(&page)), markdown));
  LOKA_VERIFY(doc::RenderHtml(Boundary<P::NodeType>(P(&page)), html));
  LOKA_VERIFY(markdown == "before\n\n![Route](<map (1)>)\n");
  LOKA_VERIFY(html == "<p>before</p>\n<p><img src=\"map (1)\" alt=\"Route\"></p>\n");
  const doc::ImageSize invalid[] = {doc::ImageSize(0, 1), doc::ImageSize(1, 0)};
  for (size_t i = 0; i < 2; ++i)
  {
    ColumnDefinition bad;
    bad << doc::Paragraph("before") << doc::Image("map", "Route", invalid[i]);
    markdown = html = "prefix";
    LOKA_VERIFY(!doc::RenderMarkdown(Boundary<P::NodeType>(P(&bad)), markdown));
    LOKA_VERIFY(!doc::RenderHtml(Boundary<P::NodeType>(P(&bad)), html));
    LOKA_VERIFY(markdown == "prefix" && html == "prefix");
  }
}

void testDocumentImageRetainedSize()
{
  Fixture<doc::ImageDefinition> f(doc::Image("map", "Route", doc::ImageSize(2, 1)));
  const doc::ImageDefinition resized = doc::Image("map", "Route", doc::ImageSize(2, 3));
  const doc::ImageProps unsized = doc::Image("map", "Route").props;
  const doc::ImageProps zero = doc::Image("map", "Route", doc::ImageSize(0, 0)).props;
  LOKA_VERIFY(unsized < zero && !(zero < unsized));
  const doc::ImageProps wider = doc::Image("map", "Route", doc::ImageSize(3, 1)).props;
  LOKA_VERIFY(resized.props < wider && !(wider < resized.props));
  LOKA_VERIFY(f.definition.props < resized.props && !(resized.props < f.definition.props));
  const Exit html = {true};
  html.check(f.node(), 0, "<p><img src=\"map\" alt=\"Route\" width=\"2\" height=\"1\"></p>\n");
  Node *resident = f.node();
  applyProps(f.scene, resized);
  LOKA_VERIFY(f.node() == resident);
  html.check(f.node(), 0, "<p><img src=\"map\" alt=\"Route\" width=\"2\" height=\"3\"></p>\n");
}

void testDocumentMarkdownContainers()
{
  const Exit e = {false};
  containers(e);
  Fixture<doc::Quote> f(doc::Quote() << doc::Image("map", "")
                                     << (doc::List() << doc::Item("first") << doc::Item("second"))
                                     << (doc::NumberedList() << doc::Item("")));
  const std::string complete = "QILiilNonq";
  for (size_t stop = 1; stop <= complete.size() + 1; ++stop)
  {
    RefusingReader reader(stop);
    const bool ok = doc::detail::WalkDocumentBlocks(f.node(), reader);
    LOKA_VERIFY(ok == (stop > complete.size()));
    LOKA_VERIFY(reader.events == complete.substr(0, stop));
  }
}
void testDocumentHtmlContainers()
{
  const Exit e = {true};
  containers(e);
}

void testDocumentContainerNative()
{
  MutableState<LinkTarget> oldTarget, newTarget;
  NodeState<LinkTarget> oldBox(&oldTarget, 0), newBox(&newTarget, 0);
  const doc::ItemProps items[] = {doc::ItemProps("one"), doc::ItemProps("two")};
  Fixture<doc::Quote> f(
      doc::Quote() << (doc::List() << doc::Item("one two three four five").activatedLink(oldBox) << doc::Item("next")
                                   << (Column() << For(32, items, ComponentItemFactory<doc::ItemProps>()))));
  StackNode *quote = f.node()->asStackNode();
  LOKA_VERIFY(quote && quote->props.axis_ == STACK_AXIS_ROW);
  BoxNode *spacer = quote->childrenHead()->asBoxNode();
  LOKA_VERIFY(spacer && !spacer->childrenHead() && spacer->props.effectiveWidth() == doc::QuoteIndent
              && quote->props.gap_ == layout::LayoutGap::of(0));
  StackNode *column = spacer->nextInComposition->asStackNode();
  LOKA_VERIFY(column && column->props.axis_ == STACK_AXIS_COLUMN);
  StackNode *list = column->childrenHead()->asStackNode();
  LOKA_VERIFY(list && list->props.axis_ == STACK_AXIS_COLUMN && list->nodeTypeKey() == NodeTypeToken<StackNode>());
  Node *first = list->childrenHead();
  Node *second = first->nextInComposition;
  LayoutState offer;
  offer.x = 10;
  offer.y = 20;
  offer.width = 44;
  offer.height = 200;
  offer.lineHeight = 12;
  offer.spacing = 8;
  f.controller.projectLayoutForTesting(list, offer);
  const PaintQuery query = {f.controller.paintScope(), PLACEMENT_ELIGIBLE};
  int bottom = offer.y;
  for (Node *item = first; item != second->nextInComposition; item = item->nextInComposition)
  {
    StackNode *row = item->asNestable()->childrenHead()->asStackNode();
    LOKA_VERIFY(row && row->props.axis_ == STACK_AXIS_ROW);
    BoxNode *marker = row->childrenHead()->asBoxNode();
    LOKA_VERIFY(marker && marker->childrenHead()->asTextNode()->props.text_->get().equals(String(doc::ListBullet())));
    NullTextContext *bullet = static_cast<NullTextContext *>(marker->childrenHead()->getContext());
    LOKA_VERIFY(bullet && bullet->measurement().width() > 0 && bullet->measurement().width() <= doc::MarkerWidth);
    AttributedTextNode *body = marker->nextInComposition->asAttributedTextNode();
    NullAttributedTextContext *context = static_cast<NullAttributedTextContext *>(body->getContext());
    const AttributedString &content = body->props.text_->get();
    LOKA_VERIFY(context->commitPresented(content, query.scope));
    const PaintAnswer answer = context->queryPaintDamage(query);
    LOKA_VERIFY(answer.kind == PAINT_ANSWER_EXACT && answer.damage.x == offer.x + doc::MarkerWidth + doc::ItemGap);
    LOKA_VERIFY(answer.damage.y == bottom);
    if (item == first)
    {
      LOKA_VERIFY(context->measurement().lineCount() > 1 && context->measurement().height() > 12);
      bottom += context->measurement().height() + doc::ListGap;
      LOKA_VERIFY(body->props.activatedLink_.state() == &oldTarget);
      LOKA_VERIFY(doc::Item("replacement").activatedLink(newBox).applyPropsToNode(item));
      LOKA_VERIFY(body->props.activatedLink_.state() == &newTarget);
      LOKA_VERIFY(body->props.text_->get().segment(0).text.equals(String("replacement")));
    }
  }
  doc::ItemNode absent(doc::ItemProps("before"));
  absent.setPropsTypeId(doc::ItemProps::staticTypeId());
  LOKA_VERIFY(doc::Item("after").activatedLink(newBox).applyPropsToNode(&absent));
  LOKA_VERIFY(!absent.childrenHead() && absent.props.activatedLink_.state() == &newTarget);
  const Exit e = {false};
  doc::Quote forwarded;
  doc::ParagraphDefinition borrowed = doc::Paragraph("borrowed");
  const doc::ParagraphDefinition constant = doc::Paragraph("constant");
  std::vector<NodeDefinitionBase *> vector(1, &borrowed);
  forwarded << borrowed << constant << doc::Paragraph("owned").clone() << vector;
  e.golden(forwarded, "> borrowed\n>\n> constant\n>\n> owned\n>\n> borrowed\n", "");
  const doc::ParagraphProps repeated[] = {doc::ParagraphProps("a"), doc::ParagraphProps("b")};
  e.golden(doc::Quote() << For(32, repeated, ComponentItemFactory<doc::ParagraphProps>()), "> a\n>\n> b\n", "");
#ifdef LOKA_CARD_CLONE_FAULT_PINS
  refuseNextDefinitionAllocation(sizeof(Column));
  doc::Quote refused;
  LOKA_VERIFY(refused.childrenCount() == 0);
  refused << doc::Paragraph("owned by refused shell").clone();
  LOKA_VERIFY(!refused.clone());
  // Assigning a refused shell must not discard the destination's completed value.
  forwarded = refused;
  LOKA_VERIFY(forwarded.childrenCount() == 2);
  refuseNextDefinitionAllocation(sizeof(Column));
  LOKA_VERIFY(!forwarded.clone());
  e.golden(forwarded, "> borrowed\n>\n> constant\n>\n> owned\n>\n> borrowed\n", "");
#endif
  e.check(list, "- replacement\n- next\n- one\n- two\n", "");
}

namespace
{
  void numberedLists(const Exit &e)
  {
    doc::OrderedList list;
    list << doc::Item("") << doc::Item("B") << doc::Item("") << doc::Item("D") << doc::Item("");
    e.golden(
        list, "1.\n2. B\n3.\n4. D\n5.\n", "<ol>\n<li></li>\n<li>B</li>\n<li></li>\n<li>D</li>\n<li></li>\n</ol>\n");
    doc::NumberedList emptyItems;
    emptyItems << doc::Item("") << doc::Item("");
    e.golden(doc::NumberedList(), "", "");
    e.golden(doc::Quote() << emptyItems << doc::NumberedList() << doc::Paragraph("") << emptyItems,
             "> 1.\n> 2.\n>\n> 1)\n> 2)\n",
             "<blockquote>\n<ol>\n<li></li>\n<li></li>\n</ol>\n<ol>\n<li></li>\n<li></li>\n</ol>\n</blockquote>\n");
    doc::NumberedList one;
    one << doc::Item("A");
    e.golden(Column() << one << one << (doc::List() << doc::Item("B")) << one,
             "1. A\n\n1) A\n\n- B\n\n1. A\n",
             "<ol>\n<li>A</li>\n</ol>\n<ol>\n<li>A</li>\n</ol>\n<ul>\n<li>B</li>\n</ul>\n<ol>\n<li>A</li>\n</ol>\n");
    doc::NumberedList ten;
    std::string html = "<ol>\n";
    for (unsigned i = 0; i < 9; ++i)
    {
      ten << doc::Item("x");
      html += "<li>x</li>\n";
    }
    ten << doc::Item("a\nb");
    html += "<li>a<br>b</li>\n</ol>\n";
    e.golden(ten, "1. x\n2. x\n3. x\n4. x\n5. x\n6. x\n7. x\n8. x\n9. x\n10. a\\\n    b\n", html.c_str());

    // Deliberately bypass the typed authoring door to exercise both exits' grammar.
    doc::NumberedList crossed(one), gap(one), paragraph(one), nested(one);
    static_cast<INestableDefinition &>(crossed) << doc::Item("");
    static_cast<INestableDefinition &>(gap) << *list.childrenHead()->nextInComposition->nextInComposition;
    static_cast<INestableDefinition &>(paragraph) << doc::Paragraph("");
    static_cast<INestableDefinition &>(nested) << doc::Quote();
    doc::List unordered;
    static_cast<INestableDefinition &>(unordered) << *emptyItems.childrenHead();
    e.golden(crossed, 0, 0);
    e.golden(gap, 0, 0);
    e.golden(paragraph, 0, 0);
    e.golden(nested, 0, 0);
    e.golden(unordered, 0, 0);
  }

  template <unsigned Count> class CappedList : public BoundaryNodeFor<CappedList<Count> >
  {
  public:
    explicit CappedList(const BoundaryPropsFor<CappedList<Count> > &props)
        : BoundaryNodeFor<CappedList<Count> >(props)
    {
    }
    virtual void composeNode(NodeComposition &composition)
    {
      doc::NumberedList list;
      for (unsigned i = 0; i < Count; ++i)
        list << doc::Item("x");
      composition.declare(list);
    }
  };
  template <unsigned Count> void checkCap()
  {
    NullScenePlatformController platform;
    Scene scene((Boundary<CappedList<Count> >()));
    scene.mount(&platform);
    loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);
    LOKA_VERIFY(root(scene)->composeResult().allocationFailed == (Count > 99));
  }
} // namespace

void testDocumentNumberedListMarkdown()
{
  const Exit e = {false};
  numberedLists(e);
}
void testDocumentNumberedListHtml()
{
  const Exit e = {true};
  numberedLists(e);
}
void testDocumentNumberedListCap()
{
  checkCap<99>();
  checkCap<100>();
  LOKA_VERIFY(!NodeComposition::current());
  doc::NumberedList list;
  for (unsigned i = 0; i < 100; ++i)
    list << doc::Item("");
  LOKA_VERIFY(list.childrenCount() == 99);
}

namespace
{
  void checkNumberedScreen(unsigned count)
  {
    doc::NumberedList list;
    for (unsigned i = 0; i < count; ++i)
      list << doc::Item(i ? "body" : "");
    Fixture<doc::NumberedList> mounted(list);
    LayoutState offer;
    offer.x = 10;
    offer.width = 100;
    offer.height = 2000;
    offer.lineHeight = 12;
    mounted.controller.projectLayoutForTesting(mounted.node(), offer);
    const PaintQuery query = {mounted.controller.paintScope(), PLACEMENT_ELIGIBLE};
    Node *item = mounted.node()->asNestable()->childrenHead();
    for (unsigned i = 1; i <= count; ++i, item = item->nextInComposition)
    {
      LOKA_VERIFY(item);
      Node *marker = item->asNestable()->childrenHead()->asStackNode()->childrenHead();
      TextNode *text = marker->asBoxNode()->childrenHead()->asTextNode();
      char expected[16];
      ::snprintf(expected, sizeof(expected), "%u.", i);
      LOKA_VERIFY(text->props.text_->get().equals(String(expected)));
      const NullTextMeasurement &measurement = static_cast<NullTextContext *>(text->getContext())->measurement();
      LOKA_VERIFY(measurement.lineCount() == 1);
      const loka::core::Frame &ink = loka::app::testing::NullTextMeasurementAccess::line(measurement, 0);
      LOKA_VERIFY(ink.width == (i < 10 ? 8 : 12) && ink.x >= 0 && ink.x + ink.width == doc::NumberedMarkerWidth);
      if (i > 1)
      {
        AttributedTextNode *body = marker->nextInComposition->asAttributedTextNode();
        NullAttributedTextContext *context = static_cast<NullAttributedTextContext *>(body->getContext());
        LOKA_VERIFY(context->commitPresented(body->props.text_->get(), query.scope));
        const PaintAnswer answer = context->queryPaintDamage(query);
        LOKA_VERIFY(answer.kind == PAINT_ANSWER_EXACT
                    && answer.damage.x == offer.x + doc::NumberedMarkerWidth + doc::ItemGap);
      }
    }
    LOKA_VERIFY(!item);
  }
} // namespace
void testDocumentNumberedListScreen()
{
  checkNumberedScreen(10);
  checkNumberedScreen(99);
}

void testDocumentNumberedListRetention()
{
  MutableState<LinkTarget> oldTarget, newTarget;
  NodeState<LinkTarget> oldBox(&oldTarget, 0), newBox(&newTarget, 0);
  doc::NumberedList list, replacement;
  doc::ItemDefinition original = doc::Item("same").activatedLink(oldBox);
  original.setTestId("source");
  original.setCompositionSeatSlot(13);
  for (unsigned i = 0; i < 10; ++i)
  {
    list << original;
    replacement << doc::Item("same").activatedLink(newBox);
  }
  Fixture<doc::NumberedList> mounted(list);
  Node *resident = mounted.node()->asNestable()->childrenHead();
  NodeDefinitionBase *ninth = list.childrenHead(), *updated = replacement.childrenHead();
  for (unsigned i = 1; i < 9; ++i)
  {
    resident = resident->nextInComposition;
    ninth = ninth->nextInComposition;
    updated = updated->nextInComposition;
  }
  LOKA_VERIFY(ninth->hasTestId() && ninth->testIdValue() == "source" && ninth->compositionSeatSlot() == 13);
  NodeDefinitionBase *tenth = ninth->nextInComposition;
  LOKA_VERIFY(*ninth->propsBase() < *tenth->propsBase() && !(*tenth->propsBase() < *ninth->propsBase()));
  Node *row = resident->asNestable()->childrenHead();
  BoxNode *marker = row->asStackNode()->childrenHead()->asBoxNode();
  AttributedTextNode *body = marker->nextInComposition->asAttributedTextNode();
  LOKA_VERIFY(!tenth->isCompatibleWithNode(resident));
  LOKA_VERIFY(!tenth->applyPropsToNode(resident));
  LOKA_VERIFY(marker->childrenHead()->asTextNode()->props.text_->get().equals(String("9.")));
  LOKA_VERIFY(body->props.text_->get().segment(0).text.equals(String("same")));
  LOKA_VERIFY(body->props.activatedLink_.state() == &oldTarget);
  LOKA_VERIFY(updated->isCompatibleWithNode(resident) && updated->applyPropsToNode(resident));
  LOKA_VERIFY(resident->asNestable()->childrenHead() == row && body->props.activatedLink_.state() == &newTarget);
}

namespace
{
  /** Builds authored definitions and records their input forest before either export. */
  struct ForestGenerator
  {
    unsigned state;
    std::string authored;
    unsigned pick(unsigned limit)
    {
      this->state = this->state * 1664525u + 1013904223u;
      return (this->state >> 8) % limit;
    }
    const char *leaf(const char *tag)
    {
      const char *values[] = {"", "a", "b\nc", "- x", "1. y", "> z", "* * *", "a\n\nb", "日本語", "a & b"};
      const char *value = values[this->pick(sizeof(values) / sizeof(*values))];
      this->authored += "[\"";
      this->authored += tag;
      this->authored += "\",\"";
      for (const char *c = value; *c; ++c)
        if (*c == '\n')
          this->authored += "\\n";
        else
          this->authored += *c;
      this->authored += '"';
      return value;
    }

    void ordered(doc::NumberedList &list)
    {
      const unsigned count = this->pick(14);
      this->authored += '[';
      for (unsigned i = 0; i < count; ++i)
      {
        if (i)
          this->authored += ',';
        list << doc::Item(this->leaf("li"));
        this->authored += ']';
      }
      this->authored += ']';
    }
    void append(INestableDefinition &parent, unsigned depth, bool inList)
    {
      const unsigned count = this->pick(4);
      this->authored += '[';
      for (unsigned i = 0; i < count; ++i)
      {
        if (i)
          this->authored += ',';
        const unsigned kind = inList ? 5 : this->pick(depth < 4 ? 6 : 3);
        if (!inList && kind == 5)
        {
          doc::NumberedList list;
          this->authored += "[\"ol\",";
          this->ordered(list);
          parent << list;
        }
        else if (!inList && kind >= 3)
        {
          doc::List list;
          doc::Quote quote;
          const bool items = kind != 4;
          this->authored += items ? "[\"ul\"," : "[\"blockquote\",";
          INestableDefinition &container =
              items ? static_cast<INestableDefinition &>(list) : static_cast<INestableDefinition &>(quote);
          this->append(container, depth + 1, items);
          parent << *container.asNodeDefinitionBase();
        }
        else
        {
          const char *value = this->leaf(inList ? "li" : kind == 0 ? "p" : kind == 1 ? "h2" : "pre");
          if (inList)
            parent << doc::Item(value);
          else if (!kind)
            parent << doc::Paragraph(value);
          else if (kind == 1)
            parent << doc::Heading(2, value);
          else
            parent << doc::CodeBlock(value);
        }
        this->authored += ']';
      }
      this->authored += ']';
    }
  };
  void printHex(const std::string &s)
  {
    for (size_t i = 0; i < s.size(); ++i)
      std::printf("%02x", static_cast<unsigned char>(s[i]));
  }
} // namespace
int documentContainerCases(unsigned seed, unsigned count)
{
  ForestGenerator gen = {seed, ""};
  for (unsigned i = 0; i < count; ++i)
  {
    doc::Quote quote;
    doc::List list;
    Column page;
    // Every case authors a container, including empty and adjacent siblings.
    gen.authored = "[[\"blockquote\",";
    gen.append(quote, 1, false);
    gen.authored += "],[\"ul\",";
    gen.append(list, 1, true);
    gen.authored += "],[\"ol\",";
    doc::NumberedList ordered;
    gen.ordered(ordered);
    gen.authored += "]]";
    page << quote << list << ordered;
    Fixture<Column> f(page, false);
    std::string md, html;
    std::printf("%s\t", gen.authored.c_str());
    if (doc::WriteMarkdown(f.node(), md))
      printHex(md);
    else
      std::printf("REFUSED");
    std::putchar('\t');
    if (doc::WriteHtml(f.node(), html))
      printHex(html);
    else
      std::printf("REFUSED");
    std::putchar('\n');
  }
  return 0;
}
