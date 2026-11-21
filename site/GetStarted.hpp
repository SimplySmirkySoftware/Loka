#ifndef LOKA_SITE_GETSTARTED_HPP
#define LOKA_SITE_GETSTARTED_HPP

#include "Links.hpp"
#include "app/doc/CodeBlock.hpp"
#include "app/doc/Heading.hpp"
#include "app/doc/Inline.hpp"
#include "app/doc/List.hpp"
#include "app/doc/Paragraph.hpp"
#include "app/nodes/boundary/StdComposition.hpp"

namespace loka_site
{
  class GetStarted : public loka::app::scene::BoundaryNodeFor<GetStarted>
  {
  public:
    /** Cost: the renderer initializes one boundary per mounted page. */
    explicit GetStarted(const loka::app::scene::BoundaryPropsFor<GetStarted> &props)
        : loka::app::scene::BoundaryNodeFor<GetStarted>(props)
    {
    }

    /** Cost: the renderer declares this page's blocks and link items once per mount. */
    virtual void composeNode(loka::app::scene::NodeComposition &c)
    {
      using namespace loka::app;
      using namespace loka::app::scene;
      c.declare(
          Column() << doc::Heading(1, "Get started")
                   << doc::Paragraph(
                          "Loka builds with CMake. The commands below run the headless tests on Linux or WSL. "
                          "You need Ninja and a compiler that accepts C++98.")
                   << doc::CodeBlock("cmake --preset testing\ncmake --build --preset testing\nctest --preset testing")
                   << doc::Paragraph("A build that opens windows needs the toolchain of its target: macOS, Windows, or "
                                     "Retro68 for Classic Mac OS.")
                   << doc::Paragraph(Styled("Start with HelloWorld's BMI calculator in the ", TextStyle())
                                     + doc::Code("example") + Styled(" directory.", TextStyle()))
                   << (doc::List() << doc::Item(doc::Link("Build environments and toolchains",
                                                          LOKA_SITE_REPOSITORY "/blob/main/docs/environments.md"))
                                   << doc::Item(doc::Link("Back to the front page", "index.html"))));
    }
  };
} // namespace loka_site
#endif
