#include "StandaloneFlowAppConfig.hpp"

#include "StandaloneScenarioSupport.hpp"
#include "app/core/App.hpp"
#include "app/core/AppComposition.hpp"
#include "app/core/WindowDefinition.hpp"

namespace
{
  const unsigned long kScenarioSeed = SMIRKYCARD_SCENARIO_SEED;
  const char *CardSource()
  {
#include "ScenarioCard.inc"
  }
  const char *CompanionSource()
  {
#include "ScenarioFlow.inc"
  }
} // namespace

SmirkyCardStandaloneFlowAppConfig::SmirkyCardStandaloneFlowAppConfig(PlatformContext *context,
                                                                     const loka::platform::file::FileHandle *auditFile)
    : AppConfigurable(context),
      audit_(auditFile ? *auditFile : loka::standalone_tests::ResolveStandaloneAuditFile(), SMIRKYCARD_SCENARIO_NAME),
      runtime_(context),
      app_(0)
{
  char seed[40];
  std::sprintf(seed, "seed=%lu", kScenarioSeed);
  loka::core::String error;
  if (!this->audit_.recordLog(seed)
      || !this->runtime_.enableRunner(
          &this->audit_, &this->clock_, kScenarioSeed, SMIRKYCARD_SCENARIO_COMPANION, CompanionSource())
      || !this->runtime_.loadBuiltin(CardSource(), error))
    this->audit_.recordTerminal(loka::dsl::testing::SCENARIO_AUDIT_FAILED);
}

int SmirkyCardStandaloneFlowAppConfig::exitCode() const
{
  return this->audit_.failed() ? 1 : 0;
}

void SmirkyCardStandaloneFlowAppConfig::setApp(App *app)
{
  this->app_ = app;
}

void SmirkyCardStandaloneFlowAppConfig::compose(AppComposition &composition)
{
  composition << WindowDef(WindowProps()
                               .frame(60, 60, 420, 340)
                               .title(SMIRKYCARD_SCENARIO_APP_NAME)
                               .visible(true)
                               .idlePolicy(loka::app::IdlePolicy::interval(0.1))
                               .onIdle(&SmirkyCardStandaloneFlowAppConfig::OnWindowIdle, this)
                               .scene(smirkycard::CreateCard(SMIRKY_CARD_FIRST, this->runtime_)));
}

void SmirkyCardStandaloneFlowAppConfig::OnWindowIdle(Window *window, double elapsedSeconds, void *userData)
{
  (void)elapsedSeconds;
  SmirkyCardStandaloneFlowAppConfig *self = static_cast<SmirkyCardStandaloneFlowAppConfig *>(userData);
  if (!self->audit_.finished() && window && window->scene())
  {
    self->clock_.advanceTo(self->clock_.currentTick() + 1);
    static_cast<smirkycard::CardScene *>(window->scene())->tickScenario();
  }
  if ((self->audit_.finished() || self->audit_.failed()) && self->app_)
    self->app_->quit();
}
