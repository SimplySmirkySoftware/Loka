#ifndef LOKA_WEB_ATTRIBUTED_FIXTURE_HPP
#define LOKA_WEB_ATTRIBUTED_FIXTURE_HPP
#include "WebFixture.hpp"
#include "app/nodes/AttributedText.hpp"
#include "app/nodes/nestable/ScrollView.hpp"
#include "app/nodes/nestable/Box.hpp"
#include "app/nodes/nestable/Grid.hpp"
namespace webtest
{
  inline int &attributedMode()
  {
    static int value = 0;
    return value;
  }
  class AttributedFixture;
  /** Cost: fixture setup/assertion once; one test-owned pointer, no rows. */
  inline AttributedFixture *&attributedFixture()
  {
    static AttributedFixture *value = 0;
    return value;
  }
  /** Shared mounted fixture; mutable content belongs to this boundary. */
  class AttributedFixture : public loka::app::scene::BoundaryNodeFor<AttributedFixture>
  {
  public:
    loka::app::scene::NodeState<loka::app::AttributedString> content;
    loka::app::scene::NodeState<loka::app::AttributedString> other;
    loka::app::scene::NodeState<bool> shown;
    loka::app::scene::NodeState<loka::app::LinkTarget> activated;
    explicit AttributedFixture(const loka::app::scene::BoundaryPropsFor<AttributedFixture> &p)
        : loka::app::scene::BoundaryNodeFor<AttributedFixture>(p)
    {
      attributedFixture() = this;
      this->state(this->shown, true);
      this->state(this->activated, loka::app::LinkTarget(loka::core::String::Literal("sentinel")));
      const loka::app::AttributedString value =
          loka::app::Styled("red", loka::app::Red) + loka::app::Styled("blue", loka::app::Blue + loka::app::Underline);
      this->state(this->content, value);
      this->state(this->other, value);
    }
    /** Cost: mount once; declares own two leaves. */
    virtual void composeNode(loka::app::scene::NodeComposition &c)
    {
      using namespace loka::app;
      const AttributedText text = AttributedText(this->content.state()).activatedLink(this->activated);
      if (attributedMode() == 1)
        c.declare(Show(*this->shown.state()) << text);
      else if (attributedMode() == 2)
        c.declare(Show(*this->shown.state()).destroyOnDetach() << text);
      else if (attributedMode() == 7)
        c.declare(Grid().rows(2).cols(1) << text << Text("after"));
      else if (attributedMode() == 8)
        c.declare(HStack().alignVertical(VERTICAL_ALIGNMENT_CENTER) << text << Text("after"));
      else if (attributedMode() >= 3)
      {
        if (attributedMode() >= 5)
          c.declare(ScrollView() << (VStack().gap(0)
                                     << Box().height(attributedMode() == 5 ? 32754 : 32755) << text << Text("after")));
        else
          c.declare(VStack().gap(0) << Box().height(attributedMode() == 3 ? 32754 : 32755) << text << Text("after"));
      }
      else
        c.declare(VStack().gap(0) << text << Text("after"));
    }
  };
  class AttributedConfig : public AppConfigurable
  {
  public:
    explicit AttributedConfig(PlatformContext *p)
        : AppConfigurable(p)
    {
    }
    /** Cost: mount once; declares one fixture window. */
    virtual void compose(AppComposition &c)
    {
      c << WindowDef(
          WindowProps().contentMargin(0).size(180, 120).scene(loka::app::scene::Boundary<AttributedFixture>()));
    }
  };
  /** Cost: bootstrap once; one owned config allocation, no rows. */
  inline AppConfigurable *CreateAttributed(PlatformContext *p)
  {
    return new AttributedConfig(p);
  }
} // namespace webtest
#endif
