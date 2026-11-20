#if defined(LOKA_RETRO68)
#include <Quickdraw.h>
#elif defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <Carbon/Carbon.h>
#else
#error Native SDK include-order probe requires its target SDK
#endif
#include "app/doc/List.hpp"
#include "app/doc/Quote.hpp"
#include "app/doc/Heading.hpp"
#include "app/doc/Paragraph.hpp"
#include "app/doc/CodeBlock.hpp"
#include "app/doc/Inline.hpp"
#include "app/doc/Markdown.hpp"
#include "app/doc/Html.hpp"
#include "app/doc/Render.hpp"
#include "app/nodes/boundary/StdComposition.hpp"

void documentVocabularyAfterSdk(loka::app::scene::NodeComposition &composition)
{
  using namespace loka::app;
  composition.declare(Column() << doc::Heading(1, "Guide")
                               << doc::Paragraph(doc::Link("Guide", "https://example.com/guide") + doc::Code("x"))
                               << (doc::List() << doc::Item("entry")) << (doc::Quote() << doc::Paragraph("quoted"))
                               << doc::CodeBlock("one\ntwo"));
}

namespace
{
  class PinPage : public loka::app::scene::BoundaryNodeFor<PinPage>
  {
  public:
    explicit PinPage(const loka::app::scene::BoundaryPropsFor<PinPage> &p)
        : loka::app::scene::BoundaryNodeFor<PinPage>(p)
    {
    }
    virtual void composeNode(loka::app::scene::NodeComposition &c)
    {
      documentVocabularyAfterSdk(c);
    }
  };
} // namespace

bool renderDocumentAfterSdk(std::string &out)
{
  return loka::app::doc::RenderMarkdown(loka::app::scene::Boundary<PinPage>(), out)
         && loka::app::doc::RenderHtml(loka::app::scene::Boundary<PinPage>(), out);
}
