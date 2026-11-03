#ifndef LOKA_TESTS_TOOLBOX_SMIRKYCARD_SCENARIO_DRIVER_HPP
#define LOKA_TESTS_TOOLBOX_SMIRKYCARD_SCENARIO_DRIVER_HPP

#include "CardNodes.hpp"
#include "ScenarioDriverSupport.hpp"
#include "ScenarioRunAudit.hpp"
#include "app/core/AppConfigurable.hpp"
#include "testing/scene/SceneTestFlow.hpp"
#include "testing/snap/SnapFormat.hpp"

#ifdef TEST_BUILD
#include "ToolboxDrawMeasurement.hpp"
#include "ToolboxPresentObservation.hpp"

class ToolboxWindow;
/** Vehicle-owned samples; the receiver scope ends before App/window destruction. */
class SmirkyCardDrawTiming : public loka::testing::WholeDrawReceiver
{
public:
  SmirkyCardDrawTiming();
  virtual unsigned long now() const;
  virtual void expectDraw();
  virtual void accept(const loka::testing::WholeDrawSample &sample);
  void rememberCopy(const ToolboxPresentResult &result, unsigned long currentFullDraws);
  void writeEraseComparison(ToolboxWindow *window);

private:
  loka::testing::WholeDrawSample last_;
  unsigned long count_;
  struct ArrowSample
  {
    loka::testing::WholeDrawSample elapsed;
    ToolboxPresentExecuted executed;
    Rect domain;
    int dv;
    ArrowSample()
        : elapsed(0, 0, false),
          executed(TOOLBOX_PRESENT_ERASED),
          domain(),
          dv(0)
    {
    }
  };
  ArrowSample copy_;
};
#endif

/** Owns the JS runner and completion publisher until the App has shut down. */
class SmirkyCardScenarioAppConfig : public AppConfigurable
{
public:
  SmirkyCardScenarioAppConfig(PlatformContext *context, const loka::dsl::SnapTestConfig::Settings &settings);
  void setApp(App *app);
  virtual void compose(AppComposition &composition);
#ifdef TEST_BUILD
  loka::testing::WholeDrawReceiver &drawReceiver()
  {
    return this->drawTiming_;
  }
#endif

private:
  static void OnWindowIdle(Window *window, double elapsedSeconds, void *userData);
  void tick(Window *window, double elapsedSeconds);
  ScenarioRunAudit audit_;
  loka::dsl::testing::ScenarioClock clock_;
  smirkycard::ScriptRuntime runtime_;
  App *app_;
  double lingerRemaining_;
  double arrowWaitRemaining_;
  enum Phase
  {
    WAITING_ARROW,
    RUNNING,
    RUNNING_ARROW,
    LINGERING
#ifdef TEST_BUILD
        ,
    LINGERING_TIMING
#endif
  };
  Phase phase_;
#ifdef TEST_BUILD
  SmirkyCardDrawTiming drawTiming_;
#endif
  loka::toolbox_tests::ScenarioCompletionPublisher completionPublisher_;
};
int RunSmirkyCardScenarioApplication();
#endif
