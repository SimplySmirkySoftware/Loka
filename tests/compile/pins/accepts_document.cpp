#include "app/doc/Heading.hpp"
#include "app/doc/Paragraph.hpp"
#include "app/doc/CodeBlock.hpp"
#include "app/doc/Inline.hpp"
#include "app/nodes/boundary/StdComposition.hpp"
#include "app/scene/state/FlowSlot.hpp"
#include "app/url/OpenWebLink.hpp"
#include "app/url/URLOpener.hpp"
void pin(const loka::app::scene::NodeState<loka::app::LinkTarget> &box)
{
  using namespace loka::app;
  doc::Heading(2, "x").activatedLink(box);
  doc::Paragraph(doc::Link(loka::core::String("guide"), "https://example.com") + doc::Code(loka::core::String("x")))
      .activatedLink(box);
  doc::CodeBlock(loka::core::String("x"));
}
namespace document_sample
{
  using namespace loka::app;
  using namespace loka::dsl;
  class Article;
  struct ArticleProps : scene::NodePropsBase<ArticleProps>
  {
    typedef Article NodeType;
    typedef Article TypeTag;
    URLOpenerBase *opener;
    explicit ArticleProps(URLOpenerBase *value = 0)
        : opener(value)
    {
    }
    bool operator<(const scene::PropsBase &rhs) const
    {
      return this->opener < static_cast<const ArticleProps &>(rhs).opener;
    }
  };
  class Article : public scene::StdCompositionBoundaryNodeBase<ArticleProps>
  {
    scene::NodeState<LinkTarget> activated_;
    scene::FlowSlot<FlowChain<LinkTarget, bool> > open_;

  public:
    explicit Article(const ArticleProps &p)
        : scene::StdCompositionBoundaryNodeBase<ArticleProps>(p),
          open_(*this)
    {
      this->state(this->activated_, LinkTarget());
    }
    virtual void composeNode(scene::NodeComposition &c)
    {
      this->open_.set(Flow() | Match<LinkTarget, bool>(0).otherwise(&OpenWebLink, this->props.opener))
          .bindTrigger(this->activated_.state())
          .withTracker(this->tracker());
      c.declare(Column() << doc::Heading(1, "Guide")
                         << doc::Paragraph(doc::Link("Visit the guide", "https://example.com/guide"))
                                .activatedLink(this->activated_)
                         << doc::CodeBlock("Column() << Text(\"hello\")"));
    }
  };
} // namespace document_sample
