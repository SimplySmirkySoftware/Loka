#ifndef LOKA_SHOWCASE_APP_CONFIG_HPP
#define LOKA_SHOWCASE_APP_CONFIG_HPP

#include "app/core/AppComposition.hpp"
#include "app/core/AppConfigurable.hpp"
#include "app/core/WindowDefinition.hpp"
#include "MainNode.hpp"

class ShowcaseAppConfig : public AppConfigurable
{
public:
  explicit ShowcaseAppConfig(PlatformContext *ctx)
      : AppConfigurable(ctx)
  {
  }

  virtual void compose(AppComposition &c)
  {
    c << WindowDef(WindowProps()
                       .frame(60, 60, 480, 420)
                       .contentMargin(0)
                       .scene(loka::app::scene::Boundary<showcase::MainNode>())
                       .title("Showcase")
                       .visible(true));
  }
};

#endif // LOKA_SHOWCASE_APP_CONFIG_HPP
