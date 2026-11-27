#ifndef LOKA_TESTS_SCRAPBOOK_STANDALONE_FLOW_APP_CONFIG_HPP
#define LOKA_TESTS_SCRAPBOOK_STANDALONE_FLOW_APP_CONFIG_HPP

#include <cstdio>

#include "../../example/ScrapbookUI/src/MyAppConfig.hpp"
#include "../scenarios/ScrapbookScenarios.hpp"
#include "platform/file/FileHandle.hpp"
#include "StandaloneScenarioSupport.hpp"
#include "testing/scene/ScenarioAudit.hpp"

class App;
class Window;

namespace loka
{
  namespace standalone_tests
  {
    /** Owns Scrapbook's standalone presentation composition and scenario. */
    class ScrapbookStandaloneFlowAppConfig : public ScrapbookAppConfig
    {
    public:
      explicit ScrapbookStandaloneFlowAppConfig(
          PlatformContext *context,
          const platform::file::FileHandle *auditFile = 0,
          std::FILE *diagnostics = 0,
          StandaloneRunControl::CompletionMode mode = StandaloneRunControl::CONFIGURED_COMPLETION_MODE);
      virtual ~ScrapbookStandaloneFlowAppConfig();

      int exitCode() const;
      void setApp(App *app);
      virtual void compose(AppComposition &composition);

    private:
      static void OnWindowIdle(Window *window, double elapsedSeconds, void *userData);
      void tick(Window *window);

      dsl::testing::ScenarioAuditFile audit_;
      StandaloneScenarioRail<scenario_tests::ScrapbookScenario> scenario_;
      scrapbook::MainNode *borrowedMainNode_;
      StandaloneRunControl runControl_;
    };
  } // namespace standalone_tests
} // namespace loka

#endif // LOKA_TESTS_SCRAPBOOK_STANDALONE_FLOW_APP_CONFIG_HPP
