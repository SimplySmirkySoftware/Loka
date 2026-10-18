#ifndef LOKA_CARDSHOWCASE_APP_CONFIG_HPP
#define LOKA_CARDSHOWCASE_APP_CONFIG_HPP

#include "app/core/AppComposition.hpp"
#include "app/core/AppConfigurable.hpp"
#include "app/core/WindowDefinition.hpp"
#include "MainNode.hpp"

class CardShowcaseAppConfig : public AppConfigurable
{
public:
  explicit CardShowcaseAppConfig(PlatformContext *ctx)
      : AppConfigurable(ctx)
  {
  }

  virtual void compose(AppComposition &c)
  {
    c << WindowDef(WindowProps()
                       .frame(60, 60, 480, 420)
                       .contentMargin(0)
                       .scene(loka::app::scene::Boundary<cardshowcase::MainNode>(
                           cardshowcase::MainProps().platformContext(this->getPlatformContext())))
                       .title("Card Showcase")
                       .visible(true));
  }
};

#endif // LOKA_CARDSHOWCASE_APP_CONFIG_HPP
