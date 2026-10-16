#ifndef SMIRKYCARD_TOOLBOX_SCENARIO_APP_CONFIG_HPP
#define SMIRKYCARD_TOOLBOX_SCENARIO_APP_CONFIG_HPP

#include "CardNodes.hpp"
#include "ScenarioDriverSupport.hpp"
#include "ScenarioRunAudit.hpp"
#include "app/core/AppConfigurable.hpp"
#include "testing/scene/SceneTestFlow.hpp"
#include "testing/snap/SnapFormat.hpp"

/** Owns the JS runner and completion publisher until the App has shut down. */
class SmirkyCardToolboxScenarioAppConfig : public AppConfigurable
{
public:
  SmirkyCardToolboxScenarioAppConfig(PlatformContext *context, const loka::dsl::SnapTestConfig::Settings &settings);
  void setApp(App *app);
  virtual void compose(AppComposition &composition);

private:
  static void OnWindowIdle(Window *window, double elapsedSeconds, void *userData);
  void tick(Window *window, double elapsedSeconds);
  ScenarioRunAudit audit_;
  loka::dsl::testing::ScenarioClock clock_;
  smirkycard::ScriptRuntime runtime_;
  App *app_;
  double lingerRemaining_;
  enum Phase
  {
    RUNNING,
    LINGERING
  };
  Phase phase_;
  loka::toolbox_tests::ScenarioCompletionPublisher completionPublisher_;
};
int RunSmirkyCardScenarioApplication();
#endif
