#include "app/doc/List.hpp"
#include "app/doc/Quote.hpp"
#include "app/doc/CodeBlock.hpp"
#include "app/doc/Heading.hpp"
#include "app/doc/Paragraph.hpp"
#include "app/doc/Inline.hpp"
#include "app/doc/Render.hpp"
#include "app/nodes/boundary/StdComposition.hpp"
#include "app/nodes/nestable/For.hpp"
#include "app/nodes/nestable/Show.hpp"
#include <cstdio>
#include <cstring>

#if defined(TEST_BUILD) || defined(LOKA_DIAG)
#error DocumentPage requires the common shipping configuration
#endif

namespace
{
  using namespace loka::app;
  using namespace loka::app::scene;

  class FieldGuide : public BoundaryNodeFor<FieldGuide>
  {
    NodeState<bool> showNotes_;

  public:
    /** Cost: one notes setting per mounted guide. */
    explicit FieldGuide(const BoundaryPropsFor<FieldGuide> &props)
        : loka::app::scene::BoundaryNodeFor<FieldGuide>(props)
    {
      this->state(this->showNotes_, true);
    }

    /** Cost: once per mount, declares the guide and both checklist rows. */
    virtual void composeNode(NodeComposition &c)
    {
      if (!this->showNotes_.isValid())
        return; // state storage refused; the render reports the recorded refusal
      const doc::ItemProps checklist[] = {doc::ItemProps("Pack a notebook."), doc::ItemProps("Bring water.")};
      c.declare(
          Column() << doc::Heading(1, "Field guide")
                   << doc::Paragraph(Styled("Download the ", TextStyle()) + doc::Link("map", "https://example.org/map"))
                   << doc::CodeBlock("survey --offline")
                   << (doc::List() << (Column() << For(32, checklist, ComponentItemFactory<doc::ItemProps>())))
                   << (Show(*this->showNotes_.state())
                       << (doc::Quote() << doc::Paragraph("Record the trail conditions."))));
    }
  };
} // namespace

/** Cost: renders one guide and writes its bytes once per invocation. */
int main(int argc, char **argv)
{
  if (argc > 2 || (argc == 2 && std::strcmp(argv[1], "--html") != 0))
  {
    std::fputs("Usage: DocumentPage [--html]\n", stderr);
    return 2;
  }
  using namespace loka::app;
  std::string out;
  const bool written = argc == 2 ? doc::RenderHtml(scene::Boundary<FieldGuide>(), out)
                                 : doc::RenderMarkdown(scene::Boundary<FieldGuide>(), out);
  if (!written)
    return 1;
  return std::fwrite(out.data(), 1, out.size(), stdout) == out.size() && std::fflush(stdout) == 0 ? 0 : 1;
}
