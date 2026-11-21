#include "../tools/site/SiteWriter.hpp"
#include "app/nodes/boundary/StdComposition.hpp"
#include "support/TestVerify.hpp"
#include <cstring>

namespace
{
  using namespace loka::app;
  using namespace loka::app::scene;
  using loka::core::String;

  /** A heading followed by a semantic item outside any list. */
  class LooseItem : public BoundaryNodeFor<LooseItem>
  {
  public:
    /** Cost: the fixture initializes one boundary per invalid mount. */
    explicit LooseItem(const BoundaryPropsFor<LooseItem> &props)
        : loka::app::scene::BoundaryNodeFor<LooseItem>(props)
    {
    }
    /** Cost: once per invalid mount, declare two blocks in walk order. */
    virtual void composeNode(NodeComposition &c)
    {
      c.declare(Column() << doc::Heading(1, "Already visited") << doc::Item("Outside the list"));
    }
  };

  /** The scene receives a valid definition whose root cannot be mounted. */
  struct RefusedRoot : StdCompositionBoundaryDefinition
  {
    /** Cost: one definition copy for the fixture scene. */
    virtual NodeDefinitionBase *clone() const
    {
      return new RefusedRoot(*this);
    }
    /** Cost: refuse each root creation attempt without allocating a node. */
    virtual Node *create() const
    {
      return 0;
    }
  };

  /** Cost: one render per input, each lending the same prefixed buffer. */
  void checkRendering()
  {
    const StdCompositionBoundaryDefinition empty;
    std::string out = "prefix";
    LOKA_VERIFY(document_site::renderPage(String("</title> &amp; 岬"), empty, out));
    LOKA_VERIFY(out.find("prefix<!doctype html>") == 0);
    LOKA_VERIFY(out.find("<title>&lt;/title&gt; &amp;amp; 岬</title>") != std::string::npos);
    LOKA_VERIFY(out.find("<main></main>") != std::string::npos);
    const std::string previous = out;
    const String blank[] = {
        String(""), String(" \r\t\n"), String("　"), String(std::string("a\0b", 3)), String("\xff")};
    for (size_t i = 0; i < sizeof(blank) / sizeof(blank[0]); ++i)
    {
      LOKA_VERIFY(!document_site::renderPage(blank[i], empty, out));
      LOKA_VERIFY(out == previous);
    }
    LOKA_VERIFY(!document_site::renderPage(String("Mount"), RefusedRoot(), out));
    LOKA_VERIFY(out == previous);
    LOKA_VERIFY(!document_site::renderPage(String("Walk"), Boundary<LooseItem>(), out));
    LOKA_VERIFY(out == previous);
  }
} // namespace

/** Cost: one render-pin sequence or one site per fixture invocation. */
int main(int argc, char **argv)
{
  if (argc == 1)
  {
    checkRendering();
    return 0;
  }
  if (argc != 4)
    return 2;
  document_site::SiteOutputs outputs;
  if (std::strcmp(argv[1], "empty") == 0)
    return document_site::writeSite(argv[2], argv[3], outputs) ? 0 : 1;
  const StdCompositionBoundaryDefinition page;
  if (!document_site::addPage(outputs, "index.html", "First", page))
    return 1;
  LOKA_VERIFY(!outputs.add("index.html", "duplicate"));
  LOKA_VERIFY(!outputs.add("", "empty"));
  LOKA_VERIFY(!outputs.add("nested/page", "slash"));
  LOKA_VERIFY(!outputs.add("site.css", "reserved"));
  LOKA_VERIFY(!document_site::addPage(outputs, "unrendered.html", " ", page));
  LOKA_VERIFY(!outputs.add(std::string("index.html\0alias", 16), "truncated"));
  if (std::strcmp(argv[1], "refuse") == 0)
    return document_site::addPage(outputs, "trail.html", "Second", Boundary<LooseItem>())
                   && document_site::writeSite(argv[2], argv[3], outputs)
               ? 0
               : 1;
  if (!document_site::addPage(outputs, "trail.html", "Second", page))
    return 1;
  const bool valid = std::strcmp(argv[1], "valid") == 0;
  if (!valid && std::strcmp(argv[1], "blank") != 0)
    return 2;
  return document_site::addPage(outputs, "third.html", valid ? "Third landmark" : " ", page)
                 && document_site::writeSite(argv[2], argv[3], outputs)
             ? 0
             : 1;
}
