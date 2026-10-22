#ifndef SMIRKYCARD_APP_CONFIG_HPP
#define SMIRKYCARD_APP_CONFIG_HPP

#include "CardNodes.hpp"
#include "CardWindowProps.hpp"
#include "app/core/AppComposition.hpp"
#include "app/core/AppConfigurable.hpp"
#include "app/core/WindowDefinition.hpp"
#include "app/Menu.hpp"

/** RunApp destroys the App and its windows before this configuration. */
class SmirkyCardAppConfig : public AppConfigurable
{
public:
  explicit SmirkyCardAppConfig(PlatformContext *context)
      : AppConfigurable(context),
        runtime_()
  {
    runtime_.loadMain(context);
  }

  virtual void compose(AppComposition &composition)
  {
    composition << WindowDef(smirkycard::CardWindowProps()
                                 .title("SmirkyCard")
                                 .visible(true)
                                 .scene(smirkycard::CreateCard(SMIRKY_CARD_FIRST, this->runtime_)));
  }

  virtual void composeDefaultMenu(loka::app::MenuComposition &composition)
  {
    using namespace loka::app;
    composition.declare(QuitMenu());
  }

private:
  smirkycard::ScriptRuntime runtime_;
};
#endif
