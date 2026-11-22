#include "TutorialStandaloneFlowAppConfig.hpp"

#include "../scenarios/ScenarioWindow.hpp"
#include "app/core/AppComposition.hpp"
#include "app/core/Window.hpp"

namespace loka
{
  namespace standalone_tests
  {
    namespace
    {
      const char *const kTutorialStandaloneTitle = "Loka Tutorial Standalone Flow";
      const char *const kTutorialStandaloneCells[] = {"increment-summary-toggle"};
    }

    TutorialStandaloneFlowAppConfig::TutorialStandaloneFlowAppConfig(
        PlatformContext *context,
        const platform::file::FileHandle *auditFile,
        std::FILE *diagnostics)
        : AppConfigurable(context),
          audit_(auditFile ? *auditFile : ResolveStandaloneAuditFile(), kTutorialStandaloneCells[0]),
          scenario_(new (std::nothrow) scenario_tests::TutorialScenario(
              scenario_tests::SCENARIO_COMPLETION_HOLD_FINAL_SCENE, &this->audit_)),
          borrowedMainNode_(0),
          runControl_("Tutorial", scenario_tests::ScenarioCellTable(kTutorialStandaloneCells, 1), diagnostics)
    {
    }

    TutorialStandaloneFlowAppConfig::~TutorialStandaloneFlowAppConfig()
    {
    }

    int TutorialStandaloneFlowAppConfig::exitCode() const
    {
      return this->audit_.isValid() && this->scenario_.isValid() && !this->runControl_.failed() ? 0 : 1;
    }

    void TutorialStandaloneFlowAppConfig::setApp(App *app)
    {
      this->runControl_.setApp(app);
    }

    void TutorialStandaloneFlowAppConfig::compose(AppComposition &composition)
    {
      composition << StandaloneWindowDefinition(
          scenario_tests::MakeScenarioWindowProps<tutorial::Step4Node::PropsType, tutorial::Step4Node>(
              tutorial::Step4Node::PropsType(),
              &this->borrowedMainNode_,
              360,
              280,
              kTutorialStandaloneTitle,
              app::IdlePolicy::interval(0.1),
              &TutorialStandaloneFlowAppConfig::OnWindowIdle,
              this,
              this->runControl_.displayTitleState(kTutorialStandaloneTitle)),
          this->runControl_);
    }

    void TutorialStandaloneFlowAppConfig::composeDefaultMenu(app::MenuComposition &composition)
    {
      tutorial::DeclareTutorialMenu(composition);
    }

    void TutorialStandaloneFlowAppConfig::OnWindowIdle(Window *window, double elapsedSeconds, void *userData)
    {
      (void)elapsedSeconds;
      TutorialStandaloneFlowAppConfig *self = static_cast<TutorialStandaloneFlowAppConfig *>(userData);
      if (self)
      {
        self->tick(window);
      }
    }

    void TutorialStandaloneFlowAppConfig::tick(Window *window)
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
          this->scenario_->step(this->runControl_.tick(), window->scene(), StandaloneContentBounds(window), record);
      if (this->runControl_.observeScenarioAdvance(advance, record, window))
      {
        this->runControl_.completeSceneRearm(
            this->scenario_.replaceAndRearmScene(
                new (std::nothrow) scenario_tests::TutorialScenario(
                    scenario_tests::SCENARIO_COMPLETION_HOLD_FINAL_SCENE, 0),
                window, this->runControl_.app()),
            window);
      }
    }
  } // namespace standalone_tests
} // namespace loka
