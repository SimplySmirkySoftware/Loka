#include "DocumentHtmlTests.hpp"
#include "app/doc/Inline.hpp"
#include "app/doc/Render.hpp"
#include "support/DocumentExportCases.hpp"

namespace
{
  using namespace loka::app;
  using namespace loka::app::scene;
  using loka::core::String;
  using PropsReconciliationSupport::Fixture;

  /** Cost: One explicit export of the lent subtree per observation. */
  void expect(Node *node, const char *expected)
  {
    std::string out = "prefix";
    const bool ok = doc::WriteHtml(node, out);
    LOKA_VERIFY(ok == (expected != 0));
    LOKA_VERIFY(out == std::string("prefix") + (expected ? expected : ""));
  }

  /** Cost: Each shared scene observation forwards its HTML golden once. */
  void checkHtml(Node *node, const char *, const char *html)
  {
    expect(node, html);
  }

  /** Cost: One fixture mount and subtree export per golden. */
  template <class D> void golden(const D &d, const char *expected)
  {
    Fixture<D> fixture(d, false);
    expect(fixture.node(), expected);
  }

  /** Cost: One two-block export per invalid inline input. */
  void refuseLate(const AttributedString &content)
  {
    ColumnDefinition column;
    column << doc::Heading(1, "successful") << doc::Paragraph(content);
    golden(column, 0);
  }

  class Page : public BoundaryNodeFor<Page>
  {
  public:
    /** Cost: One boundary initialization per standalone sample. */
    explicit Page(const BoundaryPropsFor<Page> &props)
        : loka::app::scene::BoundaryNodeFor<Page>(props)
    {
    }
    /** Cost: One heading declaration per page mount. */
    virtual void composeNode(NodeComposition &c)
    {
      c.declare(doc::Heading(2, "Page"));
    }
  };
} // namespace

void testDocumentHtmlBlocks()
{
  const char *headings[] = {
      "<h1>h</h1>\n", "<h2>h</h2>\n", "<h3>h</h3>\n", "<h4>h</h4>\n", "<h5>h</h5>\n", "<h6>h</h6>\n"};
  for (int level = 1; level <= 6; ++level)
    golden(doc::Heading(level, "h"), headings[level - 1]);
  golden(doc::Heading(2, ""), "<h2></h2>\n");
  golden(doc::Heading(3, "a\r\nb\rc"), "<h3>a b c</h3>\n");
  golden(doc::Paragraph(""), "");
  golden(doc::Paragraph("\n\r"), "");
  golden(doc::Paragraph("\nabc\r\n\rdef\n"), "<p>abc<br><br>def</p>\n");
  golden(doc::CodeBlock(""), "<pre><code></code></pre>\n");
  golden(doc::CodeBlock("x"), "<pre><code>x\n</code></pre>\n");
  golden(doc::CodeBlock("x\n"), "<pre><code>x\n</code></pre>\n");
  golden(doc::CodeBlock("\n"), "<pre><code>\n</code></pre>\n");
  golden(doc::CodeBlock("x\n\n"), "<pre><code>x\n\n</code></pre>\n");
  golden(doc::CodeBlock("\nx\r\ny\rz"), "<pre><code>\nx\ny\nz\n</code></pre>\n");
  golden(doc::CodeBlock("&<>"), "<pre><code>&amp;&lt;&gt;\n</code></pre>\n");
}

void testDocumentHtmlInline()
{
  const LinkTarget u(String("u")), v(String("v"));
  golden(doc::Paragraph(Styled(" a ", Italic)), "<p> <em>a</em> </p>\n");
  golden(doc::Paragraph(Styled(" ", Bold)), "<p> </p>\n");
  golden(doc::Paragraph(Styled("a", Underline + Red + FontSize<18>())), "<p>a</p>\n");
  golden(doc::Paragraph(Linked("a", Bold, u) + Linked("b", Bold + Italic + Monospace, u) + Linked("c", Italic, u)),
         "<p><a href=\"u\"><strong>a<em><code>b</code></em></strong><em>c</em></a></p>\n");
  golden(doc::Paragraph(doc::Link("x", "u") + doc::Link("", "v") + doc::Link("y", "u")),
         "<p><a href=\"u\">xy</a></p>\n");
  golden(doc::Paragraph(doc::Link("x", "u") + doc::Link("\r", "v") + doc::Link("\ny", "u")),
         "<p><a href=\"u\">x<br>y</a></p>\n");
  golden(doc::Paragraph(Linked("a", Bold, u) + Linked("b", Bold, v)),
         "<p><a href=\"u\"><strong>a</strong></a><a href=\"v\"><strong>b</strong></a></p>\n");
  golden(doc::Paragraph(Linked("a\nb", Italic, u)), "<p><a href=\"u\"><em>a<br>b</em></a></p>\n");
  golden(doc::Paragraph(Linked(" a ", Bold + Monospace, u)),
         "<p><a href=\"u\"><code> </code><strong><code>a</code></strong><code> </code></a></p>\n");
  golden(doc::Paragraph(doc::Link("&lt;<>\"", "a&quot;\"<>")),
         "<p><a href=\"a&amp;quot;&quot;&lt;&gt;\">&amp;lt;&lt;&gt;\"</a></p>\n");
  golden(doc::Paragraph(doc::Code(" \t ")), "<p><code> \t </code></p>\n");
  golden(doc::Paragraph(doc::Code("a\nb")), "<p><code>a b</code></p>\n");
  golden(doc::Paragraph("\v\f\xc2\x85"), "<p>\v\f\xc2\x85</p>\n");
}

void testDocumentHtmlFailure()
{
  expect(0, 0);
  DocumentExportCases::invalidInline(refuseLate);
  golden(doc::CodeBlock(DocumentExportCases::raw(std::string("x\0", 2))), 0);
}

void testDocumentHtmlWalk()
{
  DocumentExportCases::walk(checkHtml);
}

void testDocumentHtmlSeats()
{
  DocumentExportCases::seats(checkHtml);
}

void testDocumentHtmlRender()
{
  std::string out = "prefix";
  const bool ok = doc::RenderHtml(Boundary<Page>(), out);
  LOKA_VERIFY(ok);
  LOKA_VERIFY(out == "prefix<h2>Page</h2>\n");
}
