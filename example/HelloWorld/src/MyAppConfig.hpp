#ifndef LOKA_MY_APP_CONFIG_HPP
#define LOKA_MY_APP_CONFIG_HPP

#include "app/core/AppComposition.hpp"
#include "app/core/App.hpp"
#include "app/core/AppConfigurable.hpp"
#include "app/core/WindowDefinition.hpp"
#include "app/Menu.hpp"
#include "MainNode.hpp"

class HelloWorldAppConfig : public AppConfigurable
{
public:
  HelloWorldAppConfig(PlatformContext *ctx, unsigned long menuSeed)
      : AppConfigurable(ctx),
        mainProps_(menuSeed)
  {
  }

  virtual void compose(AppComposition &c)
  {
    c << WindowDef(this->productionWindowProps(
        loka::app::scene::Boundary<helloworld::MainNode>(this->mainProps_)));
  }


protected:
  const helloworld::MainProps &productionMainProps() const { return this->mainProps_; }

  /** Declares HelloWorld's production window presentation around a supplied
      scene so non-production vehicles cannot drift its title or frame. */
  WindowProps productionWindowProps(const loka::app::scene::NodeDefinitionBase &scene) const
  {
    return WindowProps()
        .frame(50, 50, 420, 330)
        .contentMargin(0)
        .scene(scene)
        .title("LokaSample")
        .visible(true);
  }

private:
  const helloworld::MainProps mainProps_;
};

#endif // LOKA_MY_APP_CONFIG_HPP
