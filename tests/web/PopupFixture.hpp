#ifndef LOKA_WEB_POPUP_FIXTURE_HPP
#define LOKA_WEB_POPUP_FIXTURE_HPP
#include "WebFixture.hpp"
#include "app/nodes/controls/PopupMenu.hpp"
#include "app/nodes/nestable/ScrollView.hpp"
#include "app/nodes/nestable/Box.hpp"
namespace webtest
{
  inline int &popupMode()
  {
    static int value = 0;
    return value;
  }
  class PopupFixture;
  inline PopupFixture *&popupFixture()
  {
    static PopupFixture *value = 0;
    return value;
  }
  class PopupFixture : public loka::app::scene::BoundaryNodeFor<PopupFixture>
  {
  public:
    loka::app::scene::NodeState<int> selected;
    loka::app::scene::NodeState<bool> enabled, shown;
    loka::Vector<loka::core::String> items;
    loka::core::EmitterState changed;
    unsigned hits;
    void (*action)();
    void (*watchAction)();
    explicit PopupFixture(const loka::app::scene::BoundaryPropsFor<PopupFixture> &props)
        : loka::app::scene::BoundaryNodeFor<PopupFixture>(props),
          hits(0),
          action(0),
          watchAction(0)
    {
      popupFixture() = this;
      this->state(this->selected, 0);
      this->state(this->enabled, true);
      this->state(this->shown, true);
      this->items.push_back(loka::core::String::Literal("Apple"));
      this->items.push_back(loka::core::String::Literal("<b>Banana"));
      this->items.push_back(loka::core::String::Literal("Cherry"));
    }
    void change()
    {
      ++this->hits;
      if (this->action)
        this->action();
    }
    virtual void declareBindings(loka::app::scene::BindingToken &token)
    {
      token.action(this->changed, this, &PopupFixture::change);
      token.watch(*this->selected.state(), this, &PopupFixture::observe);
    }
    void observe()
    {
      if (this->watchAction)
        this->watchAction();
    }
    virtual void composeNode(loka::app::scene::NodeComposition &c)
    {
      using namespace loka::app;
      const PopupMenu popup =
          PopupMenu(&this->items).selectedIndex(this->selected).enabled(this->enabled.state()).onChange(&this->changed);
      if (popupMode() == 1)
        c.declare(HStack().alignVertical(VERTICAL_ALIGNMENT_CENTER) << Button("peer") << popup);
      else if (popupMode() == 2)
        c.declare(ScrollView() << (VStack().gap(1) << Box().height(1) << (Show(*this->shown.state()) << popup)));
      else
        c.declare(Show(*this->shown.state()) << popup);
    }
  };
  class PopupConfig : public AppConfigurable
  {
  public:
    explicit PopupConfig(PlatformContext *platform)
        : AppConfigurable(platform)
    {
    }
    virtual void compose(AppComposition &c)
    {
      c << WindowDef(WindowProps().contentMargin(0).size(220, 240).scene(loka::app::scene::Boundary<PopupFixture>()));
    }
  };
  inline AppConfigurable *CreatePopups(PlatformContext *platform)
  {
    return new PopupConfig(platform);
  }
} // namespace webtest
#endif
