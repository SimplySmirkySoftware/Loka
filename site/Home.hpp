#ifndef LOKA_SITE_HOME_HPP
#define LOKA_SITE_HOME_HPP

#include "Links.hpp"
#include "app/doc/CodeBlock.hpp"
#include "app/doc/Heading.hpp"
#include "app/doc/Inline.hpp"
#include "app/doc/List.hpp"
#include "app/doc/Paragraph.hpp"
#include "app/nodes/boundary/StdComposition.hpp"

namespace loka_site
{
  class Home : public loka::app::scene::BoundaryNodeFor<Home>
  {
  public:
    /** Cost: the renderer initializes one boundary per mounted page. */
    explicit Home(const loka::app::scene::BoundaryPropsFor<Home> &props)
        : loka::app::scene::BoundaryNodeFor<Home>(props)
    {
    }

    /** Cost: the renderer declares this page's blocks and link items once per mount. */
    virtual void composeNode(loka::app::scene::NodeComposition &c)
    {
      using namespace loka::app;
      using namespace loka::app::scene;
      c.declare(
          Column()
          << doc::Heading(1, "Loka")
          << doc::Paragraph(
                 "Loka is a declarative UI framework written in C++98. One declarative application model is shared "
                 "across targets and projected onto each native toolkit, from System 7 on a 68030 to current Windows "
                 "and macOS.")
          << doc::Paragraph(
                 "The cast of concepts is small: Node, Boundary, State, Props and Flow are reused for UI, events and "
                 "asynchronous work. Ownership and cleanup stay visible in the code.")
          << doc::CodeBlock("return F()\n"
                            "       << Text(\"BMI Calculator\")\n"
                            "       << Text(\"Height (cm)\")\n"
                            "       << EditText(heightInput)\n"
                            "       << Text(\"Weight (kg)\")\n"
                            "       << EditText(weightInput)\n"
                            "       << Text(bmiResult);")
          << (doc::List() << doc::Item(doc::Link("Get started", "start.html"))
                          << doc::Item(doc::Link("Supported environments",
                                                 LOKA_SITE_REPOSITORY
                                                 "/blob/main/README.md#supported-target-environments"))
                          << doc::Item(doc::Link("Programming guide",
                                                 LOKA_SITE_REPOSITORY "/blob/main/docs/ProgrammingGuide.en.md"))
                          << doc::Item(doc::Link("Source on GitHub", LOKA_SITE_REPOSITORY))
                          << doc::Item(doc::Link("Releases", LOKA_SITE_REPOSITORY "/releases"))));
    }
  };
} // namespace loka_site
#endif
