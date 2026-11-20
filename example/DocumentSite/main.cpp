#include "SiteWriter.hpp"
#include "app/doc/Inline.hpp"
#include "app/doc/List.hpp"
#include "app/doc/Quote.hpp"
#include "app/nodes/boundary/StdComposition.hpp"

namespace
{
  using namespace loka::app;
  using namespace loka::app::scene;

  const char MapRoomTitle[] = "The map room";
  const char TrailTitle[] = "Above the tide line";

  /** The first leaf of a pocket guide to a tidal path. */
  class MapRoom : public BoundaryNodeFor<MapRoom>
  {
  public:
    /** Cost: one boundary initialization per mounted guide. */
    explicit MapRoom(const BoundaryPropsFor<MapRoom> &props)
        : loka::app::scene::BoundaryNodeFor<MapRoom>(props)
    {
    }
    /** Cost: one declaration of this page's four blocks and two checklist items per mount. */
    virtual void composeNode(NodeComposition &c)
    {
      c.declare(Column() << doc::Heading(1, MapRoomTitle)
                         << doc::Paragraph("On the map room wall, blue pencil marks the reach of the spring tide.")
                         << (doc::List() << doc::Item("Trace the ridge from the old kiln.")
                                         << doc::Item("Mark the steps where the path turns inland."))
                         << doc::Paragraph(doc::Link("Walk the ridge", "trail.html")));
    }
  };

  /** The return leaf records a landmark visible from the ridge. */
  class Trail : public BoundaryNodeFor<Trail>
  {
  public:
    /** Cost: one boundary initialization per mounted trail. */
    explicit Trail(const BoundaryPropsFor<Trail> &props)
        : loka::app::scene::BoundaryNodeFor<Trail>(props)
    {
    }
    /** Cost: one declaration of this page's four blocks and one quoted paragraph per mount. */
    virtual void composeNode(NodeComposition &c)
    {
      c.declare(Column() << doc::Heading(1, TrailTitle)
                         << doc::Paragraph("At the top step, look back. The kiln chimney lines up with the bell buoy.")
                         << (doc::Quote() << doc::Paragraph("From here the blue pencil line looks almost straight."))
                         << doc::Paragraph(doc::Link("Return to the map room", "index.html")));
    }
  };
} // namespace

/** Cost: one site of two authored boundaries per invocation. */
int main(int argc, char **argv)
{
  if (argc != 3)
  {
    std::fputs("Usage: DocumentSite <out-dir> <stylesheet-file>\n", stderr);
    return 2;
  }
  return document_site::writeSite(argv[1], argv[2], MapRoomTitle, Boundary<MapRoom>(), TrailTitle, Boundary<Trail>())
             ? 0
             : 1;
}
