#include "DocumentContainerTests.hpp"
#include "CardTests.hpp"
#include "app/doc/Rule.hpp"
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
    virtual bool enter(const doc::NestedItemProps &)
    {
      return this->event('S');
    }
    virtual bool leave(const doc::NestedItemProps &)
    {
      return this->event('s');
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
    virtual bool read(const doc::RuleProps &)
    {
      return this->event('R');
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
    doc::List direct, child;
    direct.addChild(&child);
    e.golden(direct, 0, 0);
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
  Fixture<doc::Quote> f(doc::Quote() << doc::Rule() << doc::Image("map", "")
                                     << (doc::List() << doc::Item("first") << doc::Item("second"))
                                     << (doc::NumberedList() << (doc::Item("") << doc::List())));
  const std::string complete = "QRILiilNSoLlsnq";
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
    LOKA_VERIFY(!root(scene)->composeResult().allocationFailed); // the limit drops the item, the page mounts
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
  template <class D> void bothExits(const D &definition, const char *markdown, const char *html)
  {
    Fixture<D> mounted(definition, false);
    const Exit md = {false}, ht = {true};
    md.check(mounted.node(), markdown, html);
    ht.check(mounted.node(), markdown, html);
  }

  void nestedLists(const Exit &e)
  {
    doc::List bullet;
    bullet << doc::Item("y");
    doc::NumberedList number;
    number << doc::Item("n");
    e.golden(doc::List() << (doc::Item("x") << bullet) << doc::Item("z"),
             "- x\n  - y\n- z\n",
             "<ul>\n<li>x<ul>\n<li>y</li>\n</ul></li>\n<li>z</li>\n</ul>\n");
    e.golden(
        doc::List() << (doc::Item("x") << number), "- x\n  1. n\n", "<ul>\n<li>x<ol>\n<li>n</li>\n</ol></li>\n</ul>\n");
    e.golden(doc::NumberedList() << (doc::Item("x") << bullet),
             "1. x\n   - y\n",
             "<ol>\n<li>x<ul>\n<li>y</li>\n</ul></li>\n</ol>\n");
    e.golden(doc::NumberedList() << (doc::Item("x") << number),
             "1. x\n   1. n\n",
             "<ol>\n<li>x<ol>\n<li>n</li>\n</ol></li>\n</ol>\n");
    e.golden(
        doc::Quote() << (doc::List() << (doc::Item("A\nB") << (doc::NumberedList() << (doc::Item("z") << bullet)))),
        "> - A\\\n>   B\n>   1. z\n>      - y\n",
        "<blockquote>\n<ul>\n<li>A<br>B<ol>\n<li>z<ul>\n<li>y</li>\n</ul></li>\n</ol></li>\n</ul>\n</blockquote>\n");
    e.golden(doc::List() << (doc::Item("x") << bullet << bullet),
             "- x\n  - y\n  * y\n",
             "<ul>\n<li>x<ul>\n<li>y</li>\n</ul><ul>\n<li>y</li>\n</ul></li>\n</ul>\n");
    e.golden(doc::List() << (doc::Item("x") << number << number),
             "- x\n  1. n\n  1) n\n",
             "<ul>\n<li>x<ol>\n<li>n</li>\n</ol><ol>\n<li>n</li>\n</ol></li>\n</ul>\n");
    e.golden(doc::List() << (doc::Item("x") << number << bullet),
             "- x\n  1. n\n  - y\n",
             "<ul>\n<li>x<ol>\n<li>n</li>\n</ol><ul>\n<li>y</li>\n</ul></li>\n</ul>\n");
    e.golden(doc::List() << (doc::Item("x") << bullet << number),
             "- x\n  - y\n  1. n\n",
             "<ul>\n<li>x<ul>\n<li>y</li>\n</ul><ol>\n<li>n</li>\n</ol></li>\n</ul>\n");
    e.golden(doc::List() << (doc::Item("x") << doc::List()) << (doc::Item("") << doc::NumberedList()),
             "- x\n",
             "<ul>\n<li>x</li>\n</ul>\n");
    e.golden(doc::NumberedList() << (doc::Item("") << doc::List()), "1.\n", "<ol>\n<li></li>\n</ol>\n");
    e.golden(doc::NumberedList() << (doc::Item("") << bullet),
             "1. - y\n",
             "<ol>\n<li><ul>\n<li>y</li>\n</ul></li>\n</ol>\n");
    e.golden(doc::List() << (doc::Item("") << number), "- 1. n\n", "<ul>\n<li><ol>\n<li>n</li>\n</ol></li>\n</ul>\n");
    doc::List inner;
    inner << (doc::Item("") << (doc::NumberedList() << doc::Item("")));
    e.golden(doc::List() << (doc::Item("") << (doc::List() << (doc::Item("") << inner))),
             "- - - 1.\n",
             "<ul>\n<li><ul>\n<li><ul>\n<li><ol>\n<li></li>\n</ol></li>\n</ul></li>\n</ul></li>\n</ul>\n");
  }

  struct ListFactory
  {
    doc::List operator()(const doc::ItemProps &, std::size_t) const
    {
      return doc::List();
    }
  };
} // namespace

void testDocumentNestedListMarkdown()
{
  const Exit e = {false};
  nestedLists(e);
}
void testDocumentNestedListHtml()
{
  const Exit e = {true};
  nestedLists(e);
}

void testDocumentNestedListLostOrderedList()
{
  bothExits(
      doc::Quote() << (doc::List() << (doc::Item("Fruit")
                                       << doc::List() << (doc::NumberedList() << doc::Item("") << doc::Item("x")))),
      "> - Fruit\n>\n>   1.\n>   2. x\n",
      "<blockquote>\n<ul>\n<li>Fruit<ol>\n<li></li>\n<li>x</li>\n</ol></li>\n</ul>\n</blockquote>\n");
}

void testDocumentNestedListSetextHeading()
{
  bothExits(doc::List() << (doc::Item("Fruit") << (doc::List() << (doc::Item("") << (doc::List() << doc::Item("y"))))),
            "- Fruit\n  - - y\n",
            "<ul>\n<li>Fruit<ul>\n<li><ul>\n<li>y</li>\n</ul></li>\n</ul></li>\n</ul>\n");
}

void testDocumentNestedListLooseSeparator()
{
  bothExits(doc::List() << (doc::Item("Fruit") << (doc::List() << doc::Item("a"))
                                               << (doc::NumberedList() << doc::Item("") << doc::Item("b"))),
            "- Fruit\n  - a\n  1.\n  2. b\n",
            "<ul>\n<li>Fruit<ul>\n<li>a</li>\n</ul><ol>\n<li></li>\n<li>b</li>\n</ol></li>\n</ul>\n");
}

void testDocumentNestedListOrdinal()
{
  doc::NumberedList parent;
  std::string html = "<ol>\n";
  for (unsigned i = 0; i < 9; ++i)
  {
    parent << doc::Item("a");
    html += "<li>a</li>\n";
  }
  parent << (doc::Item("ten") << (doc::List() << doc::Item("y"))) << doc::Item("end");
  html += "<li>ten<ul>\n<li>y</li>\n</ul></li>\n<li>end</li>\n</ol>\n";
  bothExits(parent, "1. a\n2. a\n3. a\n4. a\n5. a\n6. a\n7. a\n8. a\n9. a\n10. ten\n    - y\n11. end\n", html.c_str());
}

void testDocumentNestedListGrammar()
{
  doc::List direct, child;
  direct.addChild(&child);
  bothExits(direct, 0, 0);
  doc::List bypass;
  static_cast<INestableDefinition &>(bypass) << doc::NumberedList();
  bothExits(bypass, 0, 0);
  const doc::ItemProps items[] = {doc::ItemProps("one")};
  bothExits(doc::List() << For(32, items, ListFactory()), 0, 0);
  MutableState<bool> visible(false);
  Fixture<doc::List> seats(doc::List() << (Show(visible) << doc::List()));
  const Exit md = {false}, ht = {true};
  md.check(seats.node(), "", "");
  ht.check(seats.node(), "", "");
  visible.set(true);
  settle(seats.scene);
  md.check(seats.node(), 0, 0);
  ht.check(seats.node(), 0, 0);

  doc::List parent;
  parent << (doc::Item("head") << doc::List());
  bothExits(doc::Quote() << *parent.childrenHead(), 0, 0);
  Fixture<doc::List> valid(parent);
  Node *shell = valid.node()->asNestable()->childrenHead();
  md.check(shell, 0, 0);
  ht.check(shell, 0, 0);
  Node *head = shell->asNestable()->childrenHead();
  NotifySubtreeNodeDetached(head);
  md.check(valid.node(), 0, 0);
  ht.check(valid.node(), 0, 0);
  NotifySubtreeNodeDetached(head->nextInComposition);
  md.check(valid.node(), 0, 0);
  ht.check(valid.node(), 0, 0);

  doc::List second(parent), block(parent);
  *second.childrenHead()->asNestableDefinition() << doc::Item("second");
  *block.childrenHead()->asNestableDefinition() << doc::Paragraph("paragraph");
  bothExits(second, 0, 0);
  // The empty child list must refuse at entry even if a valid head would follow it.
  Fixture<doc::List> lateHead(second, false);
  Node *lateShell = lateHead.node()->asNestable()->childrenHead();
  NotifySubtreeNodeDetached(lateShell->asNestable()->childrenHead());
  md.check(lateHead.node(), 0, 0);
  ht.check(lateHead.node(), 0, 0);
  bothExits(block, 0, 0);
  doc::NumberedList crossed;
  crossed.addChild(parent.childrenHead());
  bothExits(crossed, 0, 0);
}

void testDocumentNestedListDepth()
{
  // 13 quotes and List / shell / empty child list consume all sixteen entries.
  doc::Quote deep;
  deep << (doc::List() << (doc::Item("head") << doc::List()));
  std::string md = "> - head\n", ht = "<blockquote>\n<ul>\n<li>head</li>\n</ul>\n</blockquote>\n";
  for (unsigned i = 1; i < 13; ++i)
  {
    deep = doc::Quote() << deep;
    md = "> " + md;
    ht = "<blockquote>\n" + ht + "</blockquote>\n";
  }
  bothExits(deep, md.c_str(), ht.c_str());
  bothExits(doc::Quote() << deep, 0, 0);
}

namespace
{
  template <class List> void checkNestedScreen(int indent)
  {
    doc::ItemDefinition authored = doc::Item("head");
    authored.setTestId("nested-head");
    List parent;
    parent << (authored.tag(17) << (doc::List() << doc::Item("child")));
    Fixture<List> mounted(parent);
    LayoutState offer;
    offer.x = 13;
    offer.width = 200;
    offer.height = 200;
    offer.spacing = 4;
    offer.lineHeight = 12;
    mounted.controller.projectLayoutForTesting(mounted.node(), offer);
    const PaintQuery query = {mounted.controller.paintScope(), PLACEMENT_ELIGIBLE};
    Node *shell = mounted.node()->asNestable()->childrenHead();
    Node *head = shell->asNestable()->childrenHead();
    LOKA_VERIFY(head->nodeTag() == 17 && head->testId() == "nested-head");
    LOKA_VERIFY(shell->nodeTag() != 17 && shell->testId() != "nested-head");
    Node *marker = head->asNestable()->childrenHead()->asStackNode()->childrenHead();
    AttributedTextNode *body = marker->nextInComposition->asAttributedTextNode();
    NullAttributedTextContext *bodyContext = static_cast<NullAttributedTextContext *>(body->getContext());
    LOKA_VERIFY(bodyContext->commitPresented(body->props.text_->get(), query.scope));
    const PaintAnswer bodyPlacement = bodyContext->queryPaintDamage(query);
    LOKA_VERIFY(bodyPlacement.kind == PAINT_ANSWER_EXACT && bodyPlacement.damage.x == offer.x + indent);
    Node *column = head->nextInComposition->asStackNode()->childrenHead()->nextInComposition;
    Node *child = column->asStackNode()->childrenHead()->asStackNode()->childrenHead();
    TextNode *bullet =
        child->asNestable()->childrenHead()->asStackNode()->childrenHead()->asBoxNode()->childrenHead()->asTextNode();
    NullTextContext *bulletContext = static_cast<NullTextContext *>(bullet->getContext());
    LOKA_VERIFY(bulletContext->commitPresented(bullet->props.text_->get(), query.scope));
    const PaintAnswer childPlacement = bulletContext->queryPaintDamage(query);
    LOKA_VERIFY(childPlacement.kind == PAINT_ANSWER_EXACT && childPlacement.damage.x == bodyPlacement.damage.x);
  }

  /** Clones share the test's refusal switch, so a later explicit copy or row capture can fail. */
  class RefusingList : public doc::List
  {
    const bool &refuse_;

  public:
    explicit RefusingList(const bool &refuse)
        : refuse_(refuse)
    {
    }
    virtual NodeDefinitionBase *clone() const
    {
      return this->refuse_ ? 0 : new RefusingList(*this);
    }
  };

  void checkRefusedInsertion(const doc::NestedItem &value)
  {
    doc::List bullet;
    bullet << doc::Item("old");
    NodeDefinitionBase *oldBullet = bullet.childrenHead();
    bullet << value;
    LOKA_VERIFY(bullet.childrenCount() == 1 && bullet.childrenHead() == oldBullet && !oldBullet->nextInComposition);
    bothExits(bullet, "- old\n", "<ul>\n<li>old</li>\n</ul>\n");

    doc::NumberedList parent;
    parent << doc::Item("old");
    NodeDefinitionBase *old = parent.childrenHead();
    parent << value;
    LOKA_VERIFY(parent.childrenCount() == 1 && parent.childrenHead() == old && !old->nextInComposition);
    parent << doc::Item("next");
    bothExits(parent, "1. old\n2. next\n", "<ol>\n<li>old</li>\n<li>next</li>\n</ol>\n");
  }

  template <unsigned Count> class NestedAdmissionBoundary;
  template <unsigned Count> struct NestedAdmissionProps : NodePropsBase<NestedAdmissionProps<Count> >
  {
    typedef NestedAdmissionBoundary<Count> NodeType;
    struct TypeTag
    {
    };
    const doc::NestedItem *value;
    explicit NestedAdmissionProps(const doc::NestedItem &item)
        : value(&item)
    {
    }
    bool operator<(const PropsBase &rhs) const
    {
      return this->value < static_cast<const NestedAdmissionProps &>(rhs).value;
    }
  };

  /** Offer an externally built capture while the composition's refusal ledger is active. */
  template <unsigned InitialCount>
  class NestedAdmissionBoundary : public StdCompositionBoundaryNodeBase<NestedAdmissionProps<InitialCount> >
  {
    typedef NestedAdmissionProps<InitialCount> Input;

  public:
    explicit NestedAdmissionBoundary(const Input &p)
        : StdCompositionBoundaryNodeBase<Input>(p)
    {
    }
    virtual void composeNode(NodeComposition &composition)
    {
      doc::NumberedList parent;
      for (unsigned i = 0; i < InitialCount; ++i)
        parent << doc::Item("old");
      LOKA_VERIFY(NodeComposition::current() == &composition);
      parent << *this->props.value;
      LOKA_VERIFY(parent.childrenCount() == InitialCount);
      composition.declare(parent);
    }
  };

  template <unsigned Count> void checkWindow(const doc::NestedItem &value, bool allocationFailed)
  {
    NullScenePlatformController controller;
    Scene scene((Boundary<NestedAdmissionBoundary<Count> >(NestedAdmissionProps<Count>(value))));
    scene.mount(&controller);
    loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);
    BoundaryNode *owner = root(scene);
    LOKA_VERIFY(owner && owner->composeResult().allocationFailed == allocationFailed);
    if (!allocationFailed)
      LOKA_VERIFY(owner->childrenHead() && owner->childrenHead()->asNestable()->childrenCount() == Count);
  }
} // namespace

void testDocumentNestedListCaptureRefusal()
{
  LOKA_VERIFY(!NodeComposition::current());
  bool refuse = true;
  RefusingList probe(refuse);
  doc::NestedItem first = doc::Item("head") << probe;
  first << doc::List();
  checkRefusedInsertion(first);
  checkWindow<1>(first, true);
  doc::NestedItem middle = doc::Item("head") << doc::List();
  middle << probe << doc::List();
  checkRefusedInsertion(middle);
  doc::NestedItem later = doc::Item("head") << doc::NumberedList();
  later << doc::List() << probe;
  checkRefusedInsertion(later);

  refuse = false;
  doc::NestedItem source = doc::Item("head") << doc::List();
  source << probe;
  refuse = true;
  const doc::NestedItem copied(source);
  refuse = false;
  checkRefusedInsertion(copied);
  refuse = true;
  checkRefusedInsertion(source); // capturing the completed column into the candidate row

#ifdef LOKA_CARD_CLONE_FAULT_PINS
  // The value owns a concrete copy of its head. Fail the allocation made by that concrete clone.
  refuse = false;
  doc::NumberedList parent;
  parent << doc::Item("old");
  NodeDefinitionBase *old = parent.childrenHead();
  refuseNextDefinitionAllocation(sizeof(NodeDefinition<doc::NumberedItemProps, doc::NumberedItemNode>));
  parent << source;
  LOKA_VERIFY(parent.childrenCount() == 1 && parent.childrenHead() == old);
  parent << doc::Item("next");
  bothExits(parent, "1. old\n2. next\n", "<ol>\n<li>old</li>\n<li>next</li>\n</ol>\n");
#else
  std::puts("[skip] nested head clone refusal needs the wrapped-new allocation hook (LOKA_CARD_CLONE_FAULT_PINS).");
#endif
}

void testDocumentNestedListCap()
{
  const doc::NestedItem value = doc::Item("over cap") << doc::List();
  checkWindow<99>(value, false);
  // The cap takes precedence even when the offered capture already refused.
  const bool refuse = true;
  checkWindow<99>(doc::Item("over cap") << RefusingList(refuse), false);
}

void testDocumentNestedListScreen()
{
  checkNestedScreen<doc::List>(16);
  checkNestedScreen<doc::NumberedList>(28);
}

void testDocumentRuleBlocks()
{
  bothExits(doc::Rule().tag(2), "***\n", "<hr>\n");
  bothExits(Column() << doc::Paragraph("* * *") << doc::Rule(), "\\* \\* \\*\n\n***\n", "<p>* * *</p>\n<hr>\n");
  bothExits(Column() << doc::Rule() << doc::Paragraph("a") << doc::Rule() << doc::Paragraph("b") << doc::Rule()
                     << doc::Rule(),
            "***\n\na\n\n***\n\nb\n\n***\n\n***\n",
            "<hr>\n<p>a</p>\n<hr>\n<p>b</p>\n<hr>\n<hr>\n");
  bothExits(Column() << (doc::List() << doc::Item("x")) << doc::Rule() << (doc::NumberedList() << doc::Item("y"))
                     << doc::Rule() << doc::CodeBlock("c") << doc::Rule() << doc::Heading(2, "h") << doc::Rule(),
            "- x\n\n***\n\n1. y\n\n***\n\n```\nc\n```\n\n***\n\n## h\n\n***\n",
            "<ul>\n<li>x</li>\n</ul>\n<hr>\n<ol>\n<li>y</li>\n</ol>\n<hr>\n"
            "<pre><code>c\n</code></pre>\n<hr>\n<h2>h</h2>\n<hr>\n");
  bothExits(
      Column() << doc::Image("m", "a", doc::ImageSize(1, 2)) << doc::Rule() << doc::Image("m", "a") << doc::Rule(),
      "<img src=\"m\" alt=\"a\" width=\"1\" height=\"2\">\n\n***\n\n![a](<m>)\n\n***\n",
      "<p><img src=\"m\" alt=\"a\" width=\"1\" height=\"2\"></p>\n<hr>\n<p><img src=\"m\" alt=\"a\"></p>\n<hr>\n");
}

void testDocumentRuleQuotes()
{
  const Box wrapped = Box() << (Column() << doc::Rule());
  bothExits(Column() << wrapped
                     << (doc::Quote() << doc::Rule() << doc::Paragraph("a") << wrapped << (doc::Quote() << doc::Rule())
                                      << doc::Rule()),
            "***\n\n> ***\n>\n> a\n>\n> ***\n>\n> > ***\n>\n> ***\n",
            "<hr>\n<blockquote>\n<hr>\n<p>a</p>\n<hr>\n<blockquote>\n<hr>\n</blockquote>\n<hr>\n</blockquote>\n");
}

void testDocumentRuleGrammar()
{
  bothExits(Column() << doc::Paragraph("before") << (doc::List() << doc::Rule()), 0, 0);
  doc::RuleDefinition rule = doc::Rule();
  doc::NumberedList numbered;
  numbered << doc::Item("first");
  static_cast<INestableDefinition &>(numbered) << rule;
  bothExits(numbered, 0, 0);
  doc::List list = doc::List() << (doc::Item("head") << doc::CodeBlock("c"));
  list.childrenHead()->asNestableDefinition()->addChild(&rule);
  bothExits(list, 0, 0);
}

void testDocumentRuleScreen()
{
  Fixture<doc::RuleDefinition> f(doc::Rule().tag(2));
  Node *rule = f.node();
  LOKA_VERIFY(rule && rule->nodeTag() == 2);
  LOKA_VERIFY(doc::Rule().tag(3).applyPropsToNode(rule) && rule->nodeTag() == 3);
  Node *child = rule->asNestable()->childrenHead();
  LOKA_VERIFY(child && !child->nextInComposition);
  TextNode *text = child->asTextNode();
  LOKA_VERIFY(text && text->props.text_->get().equals(String("* * *")));
  LayoutState offer;
  offer.width = 100;
  offer.height = 100;
  offer.lineHeight = 12;
  f.controller.projectLayoutForTesting(rule, offer);
  NullTextContext *context = static_cast<NullTextContext *>(text->getContext());
  LOKA_VERIFY(context && context->measurement().lineCount() == 1);
  const Frame ink = loka::app::testing::NullTextMeasurementAccess::line(context->measurement(), 0);
  LOKA_VERIFY(ink.width > 1 && ink.x > 0 && ink.x == (100 - ink.width) / 2);
  offer.width = 5;
  f.controller.projectLayoutForTesting(rule, offer);
  LOKA_VERIFY(context->measurement().reusable(5) && context->measurement().lineCount() == 1 && ink.width > 5);
}

void testDocumentItemBlocksOrder()
{
  const doc::CodeBlockDefinition code = doc::CodeBlock("c");
  bothExits(doc::NumberedList() << (doc::Item("Configure:") << doc::CodeBlock("cmake --preset testing")),
            "1. Configure:\n   ```\n   cmake --preset testing\n   ```\n",
            "<ol>\n<li>Configure:<pre><code>cmake --preset testing\n</code></pre></li>\n</ol>\n");
  const doc::NumberedList bare = doc::NumberedList() << doc::Item("");
  bothExits(doc::List() << (doc::Item("h") << doc::CodeBlock("") << bare << code << code),
            "- h\n  ```\n  ```\n  1.\n  ```\n  c\n  ```\n  ```\n  c\n  ```\n",
            "<ul>\n<li>h<pre><code></code></pre><ol>\n<li></li>\n</ol>"
            "<pre><code>c\n</code></pre><pre><code>c\n</code></pre></li>\n</ul>\n");
  bothExits(doc::List() << (doc::Item("h") << doc::List() << bare << code) << doc::Item("s"),
            "- h\n\n  1.\n  ```\n  c\n  ```\n- s\n",
            "<ul>\n<li>h<ol>\n<li></li>\n</ol><pre><code>c\n</code></pre></li>\n<li>s</li>\n</ul>\n");
}

void testDocumentItemBlocksPayload()
{
  bothExits(
      doc::Quote() << (doc::List() << (doc::Item("h") << doc::CodeBlock(" x\r\n\r\n\ty\r  \n\t\n```\n>\n-\n1.\n#"))),
      "> - h\n>   ````\n>    x\n>\n>   \ty\n>     \n>   \t\n>   ```\n>   >\n>   -\n>   1.\n>   #\n>   ````\n",
      "<blockquote>\n<ul>\n<li>h<pre><code> x\n\n\ty\n  "
      "\n\t\n```\n&gt;\n-\n1.\n#\n</code></pre></li>\n</ul>\n</blockquote>\n");
}

void testDocumentItemBlocksIndent()
{
  const doc::List inner = doc::List() << (doc::Item("") << doc::CodeBlock("x"));
  bothExits(inner, "- ```\n  x\n  ```\n", "<ul>\n<li><pre><code>x\n</code></pre></li>\n</ul>\n");
  bothExits(doc::List() << (doc::Item("") << (doc::List() << (doc::Item("") << inner))),
            "- - - ```\n      x\n      ```\n",
            "<ul>\n<li><ul>\n<li><ul>\n<li><pre><code>x\n</code></pre></li>\n</ul></li>\n</ul></li>\n</ul>\n");
  doc::NumberedList parent;
  std::string html = "<ol>\n";
  for (unsigned i = 1; i < 10; ++i)
  {
    parent << doc::Item("");
    html += "<li></li>\n";
  }
  parent << (doc::Item("ten") << doc::CodeBlock("x"));
  html += "<li>ten<pre><code>x\n</code></pre></li>\n</ol>\n";
  bothExits(parent, "1.\n2.\n3.\n4.\n5.\n6.\n7.\n8.\n9.\n10. ten\n    ```\n    x\n    ```\n", html.c_str());
}

void testDocumentItemBlocksGrammar()
{
  doc::CodeBlockDefinition code = doc::CodeBlock("valid");
  doc::List direct;
  static_cast<INestableDefinition &>(direct) << code;
  bothExits(direct, 0, 0);
  doc::NumberedList numbered;
  numbered.addChild(&code);
  bothExits(numbered, 0, 0);

  doc::detail::NestedItemDefinition beforeHead;
  beforeHead << code;
  doc::List misplaced;
  misplaced.addChild(&beforeHead);
  bothExits(misplaced, 0, 0);
  *misplaced.childrenHead()->asNestableDefinition() << doc::Item("late");
  bothExits(misplaced, 0, 0);

  const doc::List valid = doc::List() << (doc::Item("head") << code);
  const doc::ParagraphDefinition paragraph = doc::Paragraph("p");
  const doc::ImageDefinition image = doc::Image("src", "alt");
  const doc::Quote quote;
  const doc::HeadingDefinition heading = doc::Heading(2, "h");
  const NodeDefinitionBase *forbidden[] = {&paragraph, &image, &quote, &heading};
  for (size_t i = 0; i < sizeof(forbidden) / sizeof(*forbidden); ++i)
  {
    doc::detail::NestedItemDefinition shell;
    shell << doc::Item("head") << *forbidden[i];
    doc::List first;
    first.addChild(&shell);
    bothExits(first, 0, 0);

    doc::List invalid(valid);
    *invalid.childrenHead()->asNestableDefinition() << *forbidden[i];
    bothExits(invalid, 0, 0);
  }
  bothExits(
      doc::List() << (doc::Item("head") << doc::CodeBlock(DocumentExportCases::raw(std::string("a\0b", 3)))), 0, 0);
}

namespace
{
  class RefusingCode : public doc::CodeBlockDefinition
  {
  public:
    RefusingCode()
        : doc::CodeBlockDefinition(doc::CodeBlock("refuse"))
    {
    }
    virtual NodeDefinitionBase *clone() const
    {
      return 0;
    }
  };

  class ItemCodeLayout : public NullScenePlatformController
  {
  public:
    std::vector<int> cardEdges;
    virtual bool prepareProjectedLayout(Node *node, LayoutState &offer)
    {
      CardBandNode *band = node ? node->asCardBandNode() : 0;
      if (band && band->props.role == CARD_BAND_TOP && offer.width > 0)
        this->cardEdges.push_back(offer.x);
      return NullScenePlatformController::prepareProjectedLayout(node, offer);
    }
  };
} // namespace

void testDocumentItemBlocksCaptureRefusal()
{
  RefusingCode probe;
  doc::NestedItem first = doc::Item("head") << probe;
  first << doc::CodeBlock("later");
  checkRefusedInsertion(first);
  doc::NestedItem later = doc::Item("head") << doc::CodeBlock("first");
  later << probe << doc::List();
  checkRefusedInsertion(later);
}

void testDocumentItemBlocksScreen()
{
  const doc::NestedItem step = doc::Item("h") << doc::CodeBlock("c");
  Column page;
  page << (doc::List() << step) << (doc::NumberedList() << step);
  ItemCodeLayout platform;
  Scene scene((Boundary<Tree<Column> >(Props<Column>(&page))));
  LOKA_VERIFY(scene.mount(&platform));
  loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);
  settle(scene);
  LayoutState offer;
  offer.x = 13;
  offer.width = 200;
  offer.height = 200;
  platform.cardEdges.clear();
  platform.projectLayoutForTesting(root(scene)->childrenHead(), offer);
  LOKA_VERIFY(platform.cardEdges.size() == 2);
  LOKA_VERIFY(platform.cardEdges[0] == offer.x + 16 && platform.cardEdges[1] == offer.x + 28);
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
    const char *recordText(const char *value)
    {
      this->authored += '"';
      for (const char *c = value; *c; ++c)
      {
        switch (*c)
        {
        case '\n':
          this->authored += "\\n";
          break;
        case '\r':
          this->authored += "\\r";
          break;
        case '\t':
          this->authored += "\\t";
          break;
        case '"':
        case '\\':
          this->authored += '\\';
          this->authored += *c;
          break;
        default:
          this->authored += *c;
        }
      }
      this->authored += '"';
      return value;
    }
    const char *body()
    {
      const char *values[] = {"", "a", "b\nc", "- x", "1. y", "> z", "* * *", "a\n\nb", "日本語", "a & b"};
      return this->recordText(values[this->pick(static_cast<unsigned>(sizeof(values) / sizeof(*values)))]);
    }
    doc::CodeBlockDefinition code()
    {
      const char *values[] = {
          "", "\n", "a\n\nb", " a\r\n\tb\rc", "  \n\t\n", "```", ">\n-\n1.\n#", "\"<&>\\", "日本語"};
      this->authored += "[\"pre\",";
      const char *text = this->recordText(values[this->pick(static_cast<unsigned>(sizeof(values) / sizeof(*values)))]);
      this->authored += ']';
      return doc::CodeBlock(text);
    }

    // The first insertion returns a value, subsequent insertions extend it in place.
    template <class Result, class Item> Result appendChild(Item &item, unsigned depth)
    {
      switch (this->pick(3))
      {
      case 0:
        return item << this->list<doc::List>("ul", depth, this->pick(4));
      case 1:
        return item << this->list<doc::NumberedList>("ol", depth, this->pick(4));
      default:
        return item << this->code();
      }
    }

    template <class List> List list(const char *tag, unsigned depth, unsigned count)
    {
      List parent;
      this->authored += std::string("[\"") + tag + "\",[";
      for (unsigned i = 0; i < count; ++i)
      {
        if (i)
          this->authored += ',';
        this->authored += "[\"li\",{\"body\":";
        const char *value = this->body();
        const bool linked = this->pick(4) == 0;
        this->authored += linked ? ",\"link\":\"map\"," : ",\"link\":\"\",";
        const doc::ItemDefinition item = linked ? doc::Item(doc::Link(value, "map")) : doc::Item(value);
        this->authored += "\"children\":[";
        const unsigned children = depth < 3 && this->pick(4) == 0 ? this->pick(3) + 1 : 0;
        if (children)
        {
          doc::NestedItem nested = this->appendChild<doc::NestedItem>(item, depth + 1);
          for (unsigned child = 1; child < children; ++child)
          {
            this->authored += ',';
            this->appendChild<doc::NestedItem &>(nested, depth + 1);
          }
          parent << nested;
        }
        else
          parent << item;
        this->authored += "]}]";
      }
      this->authored += "]]";
      return parent;
    }

    void quoteContents(doc::Quote &quote, unsigned depth)
    {
      const unsigned count = this->pick(4);
      this->authored += '[';
      for (unsigned i = 0; i < count; ++i)
      {
        if (i)
          this->authored += ',';
        const unsigned kind = this->pick(depth < 4 ? 7 : 4);
        if (kind == 3)
        {
          this->authored += "[\"hr\",\"\"]";
          quote << doc::Rule();
        }
        else if (kind == 6)
          quote << this->list<doc::NumberedList>("ol", 1, this->pick(14));
        else if (kind == 5)
        {
          doc::Quote child;
          this->authored += "[\"blockquote\",";
          this->quoteContents(child, depth + 1);
          this->authored += ']';
          quote << child;
        }
        else if (kind == 4)
          quote << this->list<doc::List>("ul", 1, this->pick(4));
        else
        {
          this->authored += kind == 0 ? "[\"p\"," : kind == 1 ? "[\"h2\"," : "[\"pre\",";
          const char *value = this->body();
          if (!kind)
            quote << doc::Paragraph(value);
          else if (kind == 1)
            quote << doc::Heading(2, value);
          else
            quote << doc::CodeBlock(value);
          this->authored += ']';
        }
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
    Column page;
    // Every case authors a container, including empty and adjacent siblings.
    gen.authored = "[[\"blockquote\",";
    gen.quoteContents(quote, 1);
    gen.authored += "],";
    page << quote << gen.list<doc::List>("ul", 1, gen.pick(4));
    gen.authored += ',';
    if (gen.pick(2))
    {
      page << doc::Rule();
      gen.authored += "[\"hr\",\"\"],";
    }
    page << gen.list<doc::NumberedList>("ol", 1, gen.pick(14));
    gen.authored += ']';
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
