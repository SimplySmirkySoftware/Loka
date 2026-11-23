#ifndef LOKA_TESTS_MINESWEEPER_STANDALONE_FLOW_APP_CONFIG_HPP
#define LOKA_TESTS_MINESWEEPER_STANDALONE_FLOW_APP_CONFIG_HPP

#include <cstdio>

#include "../../example/MineSweeper/src/MainNode.hpp"
#include "app/core/AppConfigurable.hpp"
#include "../scenarios/MineSweeperScenarios.hpp"
#include "StandaloneScenarioSupport.hpp"
#include "platform/file/FileHandle.hpp"
#include "testing/scene/ScenarioAudit.hpp"

class App;
class Window;

namespace loka
{
  namespace standalone_tests
  {
    /** Owns MineSweeper's deterministic standalone presentation. */
    class MineSweeperStandaloneFlowAppConfig : public AppConfigurable
    {
    public:
      explicit MineSweeperStandaloneFlowAppConfig(
          PlatformContext *context,
          const platform::file::FileHandle *auditFile = 0,
          std::FILE *diagnostics = 0,
          StandaloneRunControl::CompletionMode mode = StandaloneRunControl::CONFIGURED_COMPLETION_MODE);
      virtual ~MineSweeperStandaloneFlowAppConfig();

      int exitCode() const;
      void setApp(App *app);
      virtual void compose(AppComposition &composition);

    private:
      static void OnWindowIdle(Window *window, double elapsedSeconds, void *userData);
      void tick(Window *window);

      dsl::testing::ScenarioAuditFile audit_;
      StandaloneScenarioRail<scenario_tests::MineSweeperScenario> scenario_;
      minesweeper::MainNode *borrowedMainNode_;
      StandaloneRunControl runControl_;
    };
  } // namespace standalone_tests
} // namespace loka

#endif // LOKA_TESTS_MINESWEEPER_STANDALONE_FLOW_APP_CONFIG_HPP
