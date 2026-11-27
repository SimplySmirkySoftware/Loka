#ifndef LOKA_TESTS_TUTORIAL_STANDALONE_FLOW_APP_CONFIG_HPP
#define LOKA_TESTS_TUTORIAL_STANDALONE_FLOW_APP_CONFIG_HPP

#include <cstdio>

#include "../../example/Tutorial/src/Step4Node.hpp"
#include "app/core/AppConfigurable.hpp"
#include "../scenarios/TutorialScenarios.hpp"
#include "StandaloneScenarioSupport.hpp"
#include "platform/file/FileHandle.hpp"
#include "testing/scene/ScenarioAudit.hpp"

class App;
class Window;

namespace loka
{
  namespace standalone_tests
  {
    /** Owns Tutorial Step 4's standalone presentation and scenario. */
    class TutorialStandaloneFlowAppConfig : public AppConfigurable
    {
    public:
      explicit TutorialStandaloneFlowAppConfig(
          PlatformContext *context,
          const platform::file::FileHandle *auditFile = 0,
          std::FILE *diagnostics = 0,
          StandaloneRunControl::CompletionMode mode = StandaloneRunControl::CONFIGURED_COMPLETION_MODE);
      virtual ~TutorialStandaloneFlowAppConfig();

      int exitCode() const;
      void setApp(App *app);
      virtual void compose(AppComposition &composition);
      virtual void composeDefaultMenu(app::MenuComposition &composition);

    private:
      static void OnWindowIdle(Window *window, double elapsedSeconds, void *userData);
      void tick(Window *window);

      dsl::testing::ScenarioAuditFile audit_;
      StandaloneScenarioRail<scenario_tests::TutorialScenario> scenario_;
      tutorial::Step4Node *borrowedMainNode_;
      StandaloneRunControl runControl_;
    };
  } // namespace standalone_tests
} // namespace loka

#endif // LOKA_TESTS_TUTORIAL_STANDALONE_FLOW_APP_CONFIG_HPP
