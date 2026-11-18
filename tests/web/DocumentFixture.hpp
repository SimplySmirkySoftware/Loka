#ifndef LOKA_WEB_DOCUMENT_FIXTURE_HPP
#define LOKA_WEB_DOCUMENT_FIXTURE_HPP
#include "WebFixture.hpp"
#include "app/doc/Heading.hpp"
#include "app/doc/Paragraph.hpp"
#include "app/doc/CodeBlock.hpp"
#include "app/doc/Inline.hpp"
namespace webtest
{
  class DocumentFixture : public loka::app::scene::BoundaryNodeFor<DocumentFixture>
  {
  public:
    explicit DocumentFixture(const loka::app::scene::BoundaryPropsFor<DocumentFixture> &p)
        : loka::app::scene::BoundaryNodeFor<DocumentFixture>(p)
    {
    }
    /** Cost: once per mount, three fixture-owned blocks and one sentinel. */
    virtual void composeNode(loka::app::scene::NodeComposition &c)
    {
      using namespace loka::app;
      c.declare(Column().gap(3) << doc::Heading(
                    1, Styled("Title ", Red) + doc::Code("code") + doc::Link("link", "https://example.com"))
                                << doc::Paragraph("paragraph") << doc::CodeBlock("first\nsecond\n")
                                << AttributedText(Styled("after", TextStyle())));
    }
  };
  class DocumentConfig : public AppConfigurable
  {
  public:
    explicit DocumentConfig(PlatformContext *p)
        : AppConfigurable(p)
    {
    }
    /** Cost: once per bootstrap, one owned fixture window. */
    virtual void compose(AppComposition &c)
    {
      c << WindowDef(
          WindowProps().contentMargin(0).size(240, 240).scene(loka::app::scene::Boundary<DocumentFixture>()));
    }
  };
  /** Cost: one configuration allocation per driver cell. */
  inline AppConfigurable *CreateDocument(PlatformContext *p)
  {
    return new DocumentConfig(p);
  }
} // namespace webtest
#endif
