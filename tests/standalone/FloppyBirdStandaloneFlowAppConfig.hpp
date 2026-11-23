#ifndef LOKA_TESTS_FLOPPY_BIRD_STANDALONE_FLOW_APP_CONFIG_HPP
#define LOKA_TESTS_FLOPPY_BIRD_STANDALONE_FLOW_APP_CONFIG_HPP

#include <cstdio>

#include "../../example/FloppyBird/src/GameModel.hpp"
#include "../scenarios/FloppyBirdScenarios.hpp"
#include "StandaloneScenarioSupport.hpp"
#include "app/core/AppConfigurable.hpp"
#include "platform/file/FileHandle.hpp"
#include "testing/scene/ScenarioAudit.hpp"

class App;
class Window;

namespace loka
{
  namespace standalone_tests
  {
    /** Owns FloppyBird's deterministic fixed-step standalone presentation. */
    class FloppyBirdStandaloneFlowAppConfig : public AppConfigurable
    {
    public:
      explicit FloppyBirdStandaloneFlowAppConfig(
          PlatformContext *context,
          const platform::file::FileHandle *auditFile = 0,
          std::FILE *diagnostics = 0,
          StandaloneRunControl::CompletionMode mode = StandaloneRunControl::CONFIGURED_COMPLETION_MODE);
      virtual ~FloppyBirdStandaloneFlowAppConfig();

      int exitCode() const;
      void setApp(App *app);
      virtual void compose(AppComposition &composition);

    private:
      static void OnWindowIdle(Window *window, double elapsedSeconds, void *userData);
      void tick(Window *window);

      dsl::testing::ScenarioAuditFile audit_;
      StandaloneScenarioRail<scenario_tests::FloppyBirdScenario> scenario_;
      floppybird::GameModel game_;
      floppybird::MainNode *borrowedMainNode_;
      StandaloneRunControl runControl_;
    };
  } // namespace standalone_tests
} // namespace loka

#endif // LOKA_TESTS_FLOPPY_BIRD_STANDALONE_FLOW_APP_CONFIG_HPP
