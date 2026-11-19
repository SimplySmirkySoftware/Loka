#include "app/doc/CodeBlock.hpp"
#include "app/doc/Heading.hpp"
#include "app/doc/Paragraph.hpp"
#include "app/doc/Inline.hpp"
#include "app/doc/Render.hpp"
#include "app/nodes/boundary/StdComposition.hpp"
#include "app/nodes/nestable/For.hpp"
#include "app/nodes/nestable/Show.hpp"
#include <cstdio>

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
        : BoundaryNodeFor<FieldGuide>(props)
    {
      this->state(this->showNotes_, true);
    }

    /** Cost: once per mount, declares the guide and both checklist rows. */
    virtual void composeNode(NodeComposition &c)
    {
      const doc::ParagraphProps checklist[] = {doc::ParagraphProps("Pack a notebook."),
                                               doc::ParagraphProps("Bring water.")};
      c.declare(
          Column() << doc::Heading(1, "Field guide")
                   << doc::Paragraph(Styled("Download the ", TextStyle()) + doc::Link("map", "https://example.org/map"))
                   << doc::CodeBlock("survey --offline")
                   << (Column() << For(32, checklist, ComponentItemFactory<doc::ParagraphProps>()))
                   << (Show(*this->showNotes_.state()) << doc::Paragraph("Record the trail conditions.")));
    }
  };
} // namespace

/** Cost: renders one guide and writes its bytes once per invocation. */
int main()
{
  std::string out;
  if (!loka::app::doc::RenderMarkdown(loka::app::scene::Boundary<FieldGuide>(), out))
    return 1;
  return std::fwrite(out.data(), 1, out.size(), stdout) == out.size() && std::fflush(stdout) == 0 ? 0 : 1;
}
