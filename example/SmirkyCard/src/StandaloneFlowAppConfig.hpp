#ifndef SMIRKYCARD_STANDALONE_FLOW_APP_CONFIG_HPP
#define SMIRKYCARD_STANDALONE_FLOW_APP_CONFIG_HPP

#include "CardNodes.hpp"
#include "ScenarioRunAudit.hpp"
#include "app/core/AppConfigurable.hpp"
#include "testing/scene/ScenarioAudit.hpp"
#include "testing/scene/SceneTestFlow.hpp"

/** Owns the baked scenario's services until RunStandaloneFlowWithConfig destroys its App. */
class SmirkyCardStandaloneFlowAppConfig : public AppConfigurable
{
public:
  explicit SmirkyCardStandaloneFlowAppConfig(PlatformContext *context,
                                             const loka::platform::file::FileHandle *auditFile = 0);
  int exitCode() const;
  void setApp(App *app);
  virtual void compose(AppComposition &composition);

private:
  static void OnWindowIdle(Window *window, double elapsedSeconds, void *userData);
  ScenarioRunAudit audit_;
  loka::dsl::testing::ScenarioClock clock_;
  smirkycard::ScriptRuntime runtime_;
  App *app_;
};
#endif
