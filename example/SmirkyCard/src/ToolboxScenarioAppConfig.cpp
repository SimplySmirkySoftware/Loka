#include "ToolboxScenarioAppConfig.hpp"
#include "ToolboxScenePlatformController.hpp"
#include "ToolboxWindow.hpp"
#include "app/bootstrap/PlatformBootstrap.hpp"
#include "app/core/App.hpp"
#include "app/core/AppComposition.hpp"
#include "app/core/WindowDefinition.hpp"
#include "core/util/ScopedPtr.hpp"
#include <cassert>

namespace
{
  const char *CardSource()
  {
#include "ScenarioCard.inc"
  }
  const char *CompanionSource()
  {
#include "ScenarioFlow.inc"
  }
} // namespace

SmirkyCardToolboxScenarioAppConfig::SmirkyCardToolboxScenarioAppConfig(
    PlatformContext *context, const loka::dsl::SnapTestConfig::Settings &settings)
    : AppConfigurable(context),
      audit_(loka::toolbox_tests::ResolveScenarioAuditFile(), settings.scenario.c_str()),
      runtime_(context),
      app_(0),
      lingerRemaining_(settings.hasLingerSeconds ? settings.lingerSeconds : 0.0),
      phase_(RUNNING)
{
  const std::string companion = "globalThis.LAPTOP_CELL = \"" + settings.scenario + "\";\n" + CompanionSource();
  loka::core::String error;
  if (!this->audit_.recordLog("seed=7")
      || !this->runtime_.enableRunner(&this->audit_, &this->clock_, 7, "LAPTOP.FLOW.JS", companion.c_str())
      || !this->runtime_.loadBuiltin(CardSource(), error))
    this->audit_.recordTerminal(loka::dsl::testing::SCENARIO_AUDIT_FAILED);
}

void SmirkyCardToolboxScenarioAppConfig::setApp(App *app)
{
  this->app_ = app;
}

void SmirkyCardToolboxScenarioAppConfig::compose(AppComposition &composition)
{
  composition << WindowDef(WindowProps()
                               .frame(60, 60, 420, 340)
                               .title("LaptopTracker")
                               .visible(true)
                               .idlePolicy(loka::app::IdlePolicy::everyTick())
                               .onIdle(&SmirkyCardToolboxScenarioAppConfig::OnWindowIdle, this)
                               .scene(smirkycard::CreateCard(SMIRKY_CARD_FIRST, this->runtime_)));
}

void SmirkyCardToolboxScenarioAppConfig::OnWindowIdle(Window *window, double elapsedSeconds, void *userData)
{
  static_cast<SmirkyCardToolboxScenarioAppConfig *>(userData)->tick(window, elapsedSeconds);
}

void SmirkyCardToolboxScenarioAppConfig::tick(Window *window, double elapsedSeconds)
{
  if (this->phase_ == LINGERING)
  {
    this->lingerRemaining_ -= elapsedSeconds;
    if (this->lingerRemaining_ <= 0.0 && this->app_)
      this->app_->quit();
    return;
  }
  if (!window || !window->scene())
    this->audit_.recordTerminal(loka::dsl::testing::SCENARIO_AUDIT_FAILED);
  else
  {
    loka::app::scene::Scene *scene = window->scene();
    ToolboxScenePlatformController *controller =
        static_cast<ToolboxScenePlatformController *>(loka::dsl::testing::SceneTestAccess::platformController(*scene));
    if (!controller)
      this->audit_.recordTerminal(loka::dsl::testing::SCENARIO_AUDIT_FAILED);
    else
    {
      // Presentation twin of SimpleViewer: settle native paint before advancing or capturing.
      if (scene->hasPendingInvalidation() || controller->hasPendingSync()
          || window->asToolboxWindow()->hasPendingInvalidate())
        return;
      if (!this->audit_.finished() && !this->audit_.failed())
      {
        this->clock_.advanceTo(this->clock_.currentTick() + 1);
        static_cast<smirkycard::CardScene *>(scene)->tickScenario();
        return; // The terminal step may have changed the presentation; capture on the next settled idle.
      }
    }
  }
  (void)this->completionPublisher_.publish(window);
  this->phase_ = LINGERING;
}

int RunSmirkyCardScenarioApplication()
{
  using namespace loka;
  dsl::SnapTestConfig::Settings settings;
  if (!dsl::SnapTestConfig::load("LokaTest.cfg", settings) || !settings.hasScenario
      || (settings.scenario != "startup" && settings.scenario != "laptop-carousel"
          && settings.scenario != "laptop-detail"))
  {
    (void)toolbox_tests::WriteScenarioErrorAudit(settings.hasScenario ? settings.scenario.c_str() : "startup",
                                                 dsl::SnapRecord());
    return 0;
  }
  platform::InitPlatformRuntime();
  core::ScopedPtr<PlatformContext> context(platform::CreatePlatformContext());
  assert(context.get() && "PlatformContext is required");
  if (!context.get())
    return 1;
  SmirkyCardToolboxScenarioAppConfig config(context.get(), settings);
  core::ScopedPtr<App> app(context->createApp(&config, 0, 0));
  assert(app.get() && "App is required");
  if (!app.get())
    return 1;
  config.setApp(app.get());
  app->run();
  return 0;
}
