#include "MineSweeperStandaloneFlowAppConfig.hpp"

#include "../scenarios/ScenarioWindow.hpp"
#include "app/core/AppComposition.hpp"
#include "app/core/Window.hpp"

namespace loka
{
  namespace standalone_tests
  {
    namespace
    {
      const char *const kMineSweeperStandaloneTitle = "Loka MineSweeper Standalone Flow";
#if LOKA_STANDALONE_FLOW_LOOP
      const char *const kConfiguredMineSweeperCells[] = {"seeded-reveal"};
#else
      const char *const kConfiguredMineSweeperCells[] = {"new-game-twice"};
#endif

      const char *ConfiguredMineSweeperScenarioName()
      {
        return kConfiguredMineSweeperCells[0];
      }

      scenario_tests::ScenarioCellTable ConfiguredMineSweeperScenarioCells()
      {
        return scenario_tests::ScenarioCellTable(kConfiguredMineSweeperCells, 1);
      }
    } // namespace

    MineSweeperStandaloneFlowAppConfig::MineSweeperStandaloneFlowAppConfig(
        PlatformContext *context,
        const platform::file::FileHandle *auditFile,
        std::FILE *diagnostics)
        : AppConfigurable(context),
          audit_(auditFile ? *auditFile : ResolveStandaloneAuditFile(), ConfiguredMineSweeperScenarioName()),
          scenario_(new (std::nothrow) scenario_tests::MineSweeperScenario(
              ConfiguredMineSweeperScenarioName(),
              scenario_tests::SCENARIO_COMPLETION_HOLD_FINAL_SCENE,
              &this->audit_)),
          borrowedMainNode_(0),
          runControl_("MineSweeper", ConfiguredMineSweeperScenarioCells(), diagnostics)
    {
    }

    MineSweeperStandaloneFlowAppConfig::~MineSweeperStandaloneFlowAppConfig()
    {
    }

    int MineSweeperStandaloneFlowAppConfig::exitCode() const
    {
      return this->audit_.isValid() && this->scenario_.isValid() && !this->runControl_.failed() ? 0 : 1;
    }

    void MineSweeperStandaloneFlowAppConfig::setApp(App *app)
    {
      this->runControl_.setApp(app);
    }

    void MineSweeperStandaloneFlowAppConfig::compose(AppComposition &composition)
    {
      composition << StandaloneWindowDefinition(
          scenario_tests::MakeScenarioWindowProps<minesweeper::MainProps, minesweeper::MainNode>(
              minesweeper::MainProps(scenario_tests::MineSweeperScenarioSeed()),
              &this->borrowedMainNode_,
              220,
              240,
              kMineSweeperStandaloneTitle,
              app::IdlePolicy::interval(0.1),
              &MineSweeperStandaloneFlowAppConfig::OnWindowIdle,
              this,
              this->runControl_.displayTitleState(kMineSweeperStandaloneTitle)),
          this->runControl_);
    }

    void MineSweeperStandaloneFlowAppConfig::OnWindowIdle(Window *window,
                                                          double elapsedSeconds,
                                                          void *userData)
    {
      (void)elapsedSeconds;
      MineSweeperStandaloneFlowAppConfig *self =
          static_cast<MineSweeperStandaloneFlowAppConfig *>(userData);
      if (self)
      {
        self->tick(window);
      }
    }

    void MineSweeperStandaloneFlowAppConfig::tick(Window *window)
    {
      const StandaloneRunControl::Advance mountAdvance = this->runControl_.advance(this->borrowedMainNode_ != 0);
      switch (mountAdvance)
      {
      case StandaloneRunControl::ADVANCE_WAITING:
      case StandaloneRunControl::ADVANCE_FAILED:
        return;
      case StandaloneRunControl::ADVANCE_MOUNTED:
        break;
      }
      if (!window || !window->scene())
      {
        return;
      }
      dsl::SnapRecord record;
      const scenario_tests::ScenarioAdvance advance =
          this->scenario_->step(this->runControl_.tick(),
                                window->scene(),
                                StandaloneContentBounds(window),
                                record);
      if (this->runControl_.observeScenarioAdvance(advance, record, window))
      {
        this->runControl_.completeSceneRearm(
            this->scenario_.replaceAndRearmScene(
                new (std::nothrow) scenario_tests::MineSweeperScenario(
                    ConfiguredMineSweeperScenarioName(),
                    scenario_tests::SCENARIO_COMPLETION_HOLD_FINAL_SCENE,
                    0),
                window, this->runControl_.app()),
            window);
      }
    }
  } // namespace standalone_tests
} // namespace loka
