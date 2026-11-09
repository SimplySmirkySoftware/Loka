#ifndef LOKA_WEB_SCROLL_FIXTURE_HPP
#define LOKA_WEB_SCROLL_FIXTURE_HPP
#include "WebFixture.hpp"
#include "app/nodes/nestable/ScrollView.hpp"
#include "app/nodes/nestable/Box.hpp"
namespace webtest
{
  class ScrollFixture;
  inline ScrollFixture *&scrollFixture()
  {
    static ScrollFixture *value = 0;
    return value;
  }
  inline int &scrollMode()
  {
    static int value = 0;
    return value;
  }
  class ScrollFixture : public loka::app::scene::BoundaryNodeFor<ScrollFixture>
  {
  public:
    loka::app::scene::NodeState<loka::core::Frame> viewport, innerViewport;
    loka::app::scene::NodeState<int> width;
    loka::app::scene::NodeState<bool> shown;
    loka::app::scene::NodeState<loka::core::String> text;
    loka::app::scene::Request<loka::app::ScrollTarget> request, innerRequest;
    explicit ScrollFixture(const loka::app::scene::BoundaryPropsFor<ScrollFixture> &props)
        : loka::app::scene::BoundaryNodeFor<ScrollFixture>(props)
    {
      scrollFixture() = this;
      this->state(this->viewport, loka::core::Frame(0, 23, 0, 0));
      this->state(this->innerViewport, loka::core::Frame());
      this->state(this->width, 400);
      this->state(this->shown, true);
      this->state(this->text, loka::core::String::Literal("scroll content"));
      this->state(this->request,
                  scrollMode() == 3 ? loka::app::ScrollTarget::Origin(0, 90) : loka::app::ScrollTarget::None());
      this->state(this->innerRequest, loka::app::ScrollTarget::Top());
    }
    virtual void composeNode(loka::app::scene::NodeComposition &c)
    {
      using namespace loka::app;
      VStack content;
      if (scrollMode() == 1)
        content << ScrollView(this->innerViewport).scrollTo(this->innerRequest) << Text("nested sibling");
      else if (scrollMode() == 2)
        content << Box().height(32760) << Text("must not materialize");
      else if (scrollMode() == 3)
        content << (Box() << Text("refused first") << ScrollView(this->innerViewport).scrollTo(this->innerRequest)
                          << Text("unreached second"));
      else
      {
        content << Box().width(this->width.state()).height(40) << Text(this->text.state());
        for (int i = 0; i < 12; ++i)
          content << Text("scroll row");
      }
      c.declare(Box().padding(10) << (Show(*this->shown.state())
                                      << (ScrollView(this->viewport).scrollTo(this->request).contentInsets(7, 5)
                                          << content)));
    }
  };
  class ScrollConfig : public AppConfigurable
  {
  public:
    explicit ScrollConfig(PlatformContext *platform)
        : AppConfigurable(platform)
    {
    }
    virtual void compose(AppComposition &c)
    {
      for (unsigned i = 0; i < (fixture && fixture->roots > 0 && fixture->roots < 10 ? fixture->roots : 1); ++i)
        c << WindowDef(WindowProps().size(220, 120).scene(loka::app::scene::Boundary<ScrollFixture>()));
    }
  };
  inline AppConfigurable *CreateScroll(PlatformContext *platform)
  {
    return new ScrollConfig(platform);
  }
} // namespace webtest
#endif
