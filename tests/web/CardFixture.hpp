#ifndef LOKA_WEB_CARD_FIXTURE_HPP
#define LOKA_WEB_CARD_FIXTURE_HPP
#include "WebFixture.hpp"
#include "app/nodes/controls/Card.hpp"
#include "app/nodes/nestable/Box.hpp"
#include "app/nodes/nestable/ScrollView.hpp"
namespace webtest
{
  /** Shared native/browser scene. A structural Box sets the content height without painting pixels. */
  template <loka::app::CardShadowExtent Extent, bool Minimum = false, bool Scrolled = false>
  class CardFixture : public loka::app::scene::BoundaryNodeFor<CardFixture<Extent, Minimum, Scrolled> >
  {
  public:
    typedef CardFixture<Extent, Minimum, Scrolled> Self;
    loka::app::scene::NodeState<bool> shown;
    loka::app::scene::NodeState<loka::core::Frame> viewport;
    /** Cost: Scene once per mount; no rows. */
    explicit CardFixture(const loka::app::scene::BoundaryPropsFor<Self> &props)
        : loka::app::scene::BoundaryNodeFor<Self>(props)
    {
      this->state(this->shown, true);
      if (Scrolled)
        this->state(this->viewport, loka::app::ScrollViewProps::topOrigin());
    }
    /** Cost: mount once; builds this fixture's single Card subtree. */
    virtual void composeNode(loka::app::scene::NodeComposition &c)
    {
      using namespace loka::app;
      Card card((CardStyle(Extent)));
      if (!Minimum)
        card << Box().height(Extent == CARD_SHADOW_DROP ? 71 : 50);
      if (Scrolled)
        c.declare(ScrollView(this->viewport).contentInsets(7, 0) << (Show(*this->shown.state()) << card));
      else
        c.declare(Show(*this->shown.state()) << card);
    }
  };
  template <loka::app::CardShadowExtent Extent, bool Minimum, bool Scrolled = false>
  class CardConfig : public AppConfigurable
  {
  public:
    /** Cost: browser bootstrap once; one fixture config, no rows. */
    explicit CardConfig(PlatformContext *platform)
        : AppConfigurable(platform)
    {
    }
    /** Cost: app composition once; declares one fixture-owned window. */
    virtual void compose(AppComposition &c)
    {
      const int width = Minimum ? (Extent == loka::app::CARD_SHADOW_DROP ? 17 : 60) : 100;
      const int margin = Scrolled ? 12 : 0;
      c << WindowDef(WindowProps()
                         .contentMargin(margin)
                         .size(width + 2 * margin, 80 + 2 * margin)
                         .scene(loka::app::scene::Boundary<CardFixture<Extent, Minimum, Scrolled> >()));
    }
  };
} // namespace webtest
#endif
