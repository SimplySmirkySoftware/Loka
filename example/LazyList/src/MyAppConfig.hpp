#ifndef LOKA_LAZYLIST_APP_CONFIG_HPP
#define LOKA_LAZYLIST_APP_CONFIG_HPP

#include "MainNode.hpp"
#include "app/Menu.hpp"
#include "app/core/AppComposition.hpp"
#include "app/core/AppConfigurable.hpp"
#include "app/core/WindowDefinition.hpp"

class LazyListAppConfig : public AppConfigurable
{
public:
  explicit LazyListAppConfig(PlatformContext *context)
      : AppConfigurable(context),
        model_()
  {
  }

  virtual void compose(AppComposition &composition)
  {
    composition << WindowDef(
        WindowProps()
            .frame(50, 50, lazylist::kWindowWidth, lazylist::kWindowHeight)
            .scene(loka::app::scene::Boundary<lazylist::MainNode>(lazylist::MainProps(&this->model_)))
            .title("LokaLazyList")
            .visible(true));
  }
  virtual void composeDefaultMenu(loka::app::MenuComposition &composition)
  {
    using namespace loka::app;
    composition.declare(StandardMenuPresets());
  }

private:
  lazylist::LazyListModel model_;
};
#endif
