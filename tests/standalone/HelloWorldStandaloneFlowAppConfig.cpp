#include "HelloWorldStandaloneFlowAppConfig.hpp"

#include "../scenarios/ObservedMainDefinition.hpp"
#include "StandaloneScenarioSupport.hpp"
#include "app/core/App.hpp"
#include "app/core/Window.hpp"

namespace loka
{
  namespace standalone_tests
  {
    namespace
    {
      const char *const kHelloWorldStandaloneTitle = "Loka HelloWorld Standalone Flow";
    }

    HelloWorldStandaloneFlowAppConfig::HelloWorldStandaloneFlowAppConfig(PlatformContext *context,
                                                                         const platform::file::FileHandle *auditFile,
                                                                         std::FILE *diagnostics)
        : HelloWorldAppConfig(context, 0x13579BDFUL),
          audit_(auditFile ? *auditFile : ResolveStandaloneAuditFile(), "toggle-action-probe"),
          scenario_(new (std::nothrow) scenario_tests::HelloWorldScenario(
              scenario_tests::SCENARIO_COMPLETION_HOLD_FINAL_SCENE, &this->audit_)),
          borrowedMainNode_(0),
          runControl_("HelloWorld", scenario_tests::HelloWorldReelCells().dropFirst(1), diagnostics)
    {
    }

    HelloWorldStandaloneFlowAppConfig::~HelloWorldStandaloneFlowAppConfig()
    {
    }

    int HelloWorldStandaloneFlowAppConfig::exitCode() const
    {
      return this->audit_.isValid() && this->scenario_.isValid() && !this->runControl_.failed() ? 0 : 1;
    }

    void HelloWorldStandaloneFlowAppConfig::setApp(App *app)
    {
      this->runControl_.setApp(app);
    }

    void HelloWorldStandaloneFlowAppConfig::compose(AppComposition &composition)
    {
      composition << WindowDef(
          this->productionWindowProps(
                  scenario_tests::ObservedMainDefinition<helloworld::MainProps, helloworld::MainNode>(
                      this->productionMainProps(), &this->borrowedMainNode_))
              .title(kHelloWorldStandaloneTitle)
              .idlePolicy(app::IdlePolicy::interval(0.1))
              .onIdle(&HelloWorldStandaloneFlowAppConfig::OnWindowIdle, this)
              .displayTitleState(this->runControl_.displayTitleState(kHelloWorldStandaloneTitle)));
    }

    void HelloWorldStandaloneFlowAppConfig::OnWindowIdle(Window *window, double elapsedSeconds, void *userData)
    {
      (void)elapsedSeconds;
      HelloWorldStandaloneFlowAppConfig *self = static_cast<HelloWorldStandaloneFlowAppConfig *>(userData);
      if (self)
      {
        self->tick(window);
      }
    }

    void HelloWorldStandaloneFlowAppConfig::tick(Window *window)
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
        const char *nextScenario = this->runControl_.nextScenarioName();
        this->runControl_.completeSceneRearm(
            nextScenario
                && this->scenario_.replaceAndRearmScene(
                    new (std::nothrow) scenario_tests::HelloWorldScenario(
                        std::string(nextScenario),
                        scenario_tests::SCENARIO_COMPLETION_HOLD_FINAL_SCENE,
                        0),
                    window, this->runControl_.app()),
            window);
      }
    }
  } // namespace standalone_tests
} // namespace loka
