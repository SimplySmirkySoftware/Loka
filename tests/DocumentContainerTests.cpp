#include "DocumentContainerTests.hpp"
#include "CardTests.hpp"
#include "app/doc/Inline.hpp"
#include "platform/null/context/NullTextContext.hpp"
#include "app/doc/Markdown.hpp"
#include "app/doc/Html.hpp"
#include "app/nodes/nestable/For.hpp"
#include "app/nodes/nestable/Show.hpp"
#include "support/DocumentFixture.hpp"
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
    virtual bool read(const doc::ItemProps &)
    {
      return this->event('i');
    }
    virtual bool leave(const doc::ListProps &)
    {
      return this->event('l');
    }
    virtual bool leave(const doc::QuoteProps &)
    {
      return this->event('q');
    }
  };

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
void testDocumentMarkdownContainers()
{
  const Exit e = {false};
  containers(e);
  Fixture<doc::Quote> f(doc::Quote() << (doc::List() << doc::Item("first") << doc::Item("second")));
  const std::string complete = "QLiilq";
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
    void append(INestableDefinition &parent, unsigned depth, bool inList)
    {
      const unsigned count = this->pick(4);
      this->authored += '[';
      for (unsigned i = 0; i < count; ++i)
      {
        if (i)
          this->authored += ',';
        const unsigned kind = inList ? 5 : this->pick(depth < 4 ? 6 : 3);
        if (!inList && kind >= 3)
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
          const char *values[] = {"", "a", "b\nc", "- x", "1. y", "> z", "* * *", "a\n\nb", "日本語", "a & b"};
          const char *value = values[this->pick(sizeof(values) / sizeof(*values))];
          const char *tag = inList ? "li" : kind == 0 ? "p" : kind == 1 ? "h2" : "pre";
          this->authored += "[\"";
          this->authored += tag;
          this->authored += "\",\"";
          for (const char *c = value; *c; ++c)
            if (*c == '\n')
              this->authored += "\\n";
            else
              this->authored += *c;
          this->authored += '"';
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
    gen.authored += "]]";
    page << quote << list;
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
