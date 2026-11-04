#include "CardWindowProps.hpp"
#include "SmirkyCardScenarioDriver.hpp"
#include "ToolboxScenePlatformController.hpp"
#include "ToolboxWindow.hpp"
#include "app/bootstrap/PlatformBootstrap.hpp"
#include "app/core/App.hpp"
#include "app/core/AppComposition.hpp"
#include "app/core/WindowDefinition.hpp"
#include "core/util/ScopedPtr.hpp"
#include <cassert>
#include <cstdio>
#include <Controls.h>
#include <Resources.h>

#ifdef TEST_BUILD
#include "ToolbarMeasurementClock.hpp"
#include "platform/file/AppLocation.hpp"
#include "platform/file/FileIO.hpp"

SmirkyCardDrawTiming::SmirkyCardDrawTiming()
    : last_(0, 0, false),
      count_(0),
      copy_()
{
}
unsigned long SmirkyCardDrawTiming::now() const
{
  return loka::testing::MeasurementMicroseconds();
}
void SmirkyCardDrawTiming::expectDraw()
{
  this->last_ = loka::testing::WholeDrawSample(0, 0, false);
}
void SmirkyCardDrawTiming::accept(const loka::testing::WholeDrawSample &sample)
{
  this->last_ = sample;
  ++this->count_;
}
/** Arrow settled idle, once; O(1). Reject a later draw mislabeled as the moved pass. */
void SmirkyCardDrawTiming::rememberCopy(const ToolboxPresentResult &result, unsigned long currentFullDraws)
{
  this->copy_.elapsed = this->last_;
  this->copy_.elapsed.complete =
      this->last_.complete && this->count_ != 0 && result.totalRenderCalls() == currentFullDraws;
  this->copy_.executed = result.executed();
  this->copy_.domain = result.copyDomain();
  this->copy_.dv = -result.dy();
}
/** First post-completion linger tick only; one full draw, then bounded stdio output. */
void SmirkyCardDrawTiming::writeEraseComparison(ToolboxWindow *window)
{
  if (!window || !window->scenePlatformController())
    return;
  const unsigned long before = this->count_;
  // The capture count is read before the forced erase: the tracked audit must end with the flow's terminal
  // record, so the "no full draw between the copy and the capture" fact lives here, beside the timing.
  const unsigned long captureFullDraws =
      static_cast<unsigned long>(window->scenePlatformController()->debugStatsForTesting().totalRenderCalls);
  window->requestInvalidate();
  window->flushInvalidate();
  const loka::testing::WholeDrawSample erased = this->last_;
  const ToolboxPresentExecuted executed =
      window->scenePlatformController()->debugStatsForTesting().lastPresentObservation.executed();
  loka::platform::file::FileHandle destination;
  if (!loka::platform::file::ResolveApplicationSidecar(
          loka::file::File::Application() << loka::file::File("TIMING.TXT"), destination))
    return;
  // Same sidecar resolution and C stdio open as the scenario audit.
  std::FILE *file = loka::platform::file::OpenWriteTruncate(destination);
  if (!file)
    return;
  std::fprintf(file, "capture full_draws=%lu\n", captureFullDraws);
  if (this->copy_.elapsed.complete && erased.complete && this->count_ == before + 1)
  {
    std::fprintf(file,
                 "copy executed=%s paint_us=%lu layout_us=%lu\n",
                 ToolboxPresentExecutedName(this->copy_.executed),
                 this->copy_.elapsed.paintUs,
                 this->copy_.elapsed.layoutUs);
    std::fprintf(file,
                 "erase executed=%s paint_us=%lu layout_us=%lu\n",
                 ToolboxPresentExecutedName(executed),
                 erased.paintUs,
                 erased.layoutUs);
    std::fprintf(file,
                 "domain=%d,%d,%d,%d dv=%d\n",
                 this->copy_.domain.top,
                 this->copy_.domain.left,
                 this->copy_.domain.bottom,
                 this->copy_.domain.right,
                 this->copy_.dv);
  }
  else
    std::fprintf(file, "timing unavailable: missing, incomplete, or intervening draw\n");
  std::fflush(file);
  std::fclose(file);
}
#endif

namespace
{
  /** Settled arrow cell polls once per idle; walks this window's native controls. */
  short TallListArrowValue(Window *window)
  {
    ToolboxWindow *native = window ? window->asToolboxWindow() : 0;
    if (!native || !native->window())
      return 0;
    ControlHandle control =
        reinterpret_cast<ControlHandle>(reinterpret_cast<WindowPeek>(native->window())->controlList);
    for (; control; control = (**control).nextControl)
    {
      const Rect &rect = (**control).contrlRect;
      short resourceId = 0;
      ResType type = 0;
      Str255 name;
      GetResInfo((**control).contrlDefProc, &resourceId, &type, name);
      if (resourceId == 1 /* Classic scrollBarProc (16) uses CDEF 1. */ && type == 'CDEF' && (**control).contrlVis
          && rect.right - rect.left == 16 && rect.bottom - rect.top > 48 && GetControlMaximum(control) > 0)
        return GetControlValue(control);
    }
    return 0;
  }
  const char *CardSource()
  {
#include "ScenarioCard.inc"
  }
  const char *CompanionSource()
  {
#include "ScenarioFlow.inc"
  }
} // namespace

SmirkyCardScenarioAppConfig::SmirkyCardScenarioAppConfig(PlatformContext *context,
                                                         const loka::dsl::SnapTestConfig::Settings &settings)
    : AppConfigurable(context),
      audit_(loka::toolbox_tests::ResolveScenarioAuditFile(), settings.scenario.c_str()),
      runtime_(context),
      app_(0),
      lingerRemaining_(settings.hasLingerSeconds ? settings.lingerSeconds : 0.0),
      arrowWaitRemaining_(100.0),
      phase_(settings.scenario == "laptop-arrow" ? WAITING_ARROW : RUNNING)
{
  const std::string companion = "globalThis.LAPTOP_CELL = \"" + settings.scenario + "\";\n" + CompanionSource();
  loka::core::String error;
  if (!this->audit_.recordLog("seed=7")
      || !this->runtime_.enableRunner(&this->audit_, &this->clock_, 7, "LAPTOP.FLOW.JS", companion.c_str())
      || !this->runtime_.loadBuiltin(CardSource(), error))
    this->audit_.recordTerminal(loka::dsl::testing::SCENARIO_AUDIT_FAILED);
}

void SmirkyCardScenarioAppConfig::setApp(App *app)
{
  this->app_ = app;
}

void SmirkyCardScenarioAppConfig::compose(AppComposition &composition)
{
  composition << WindowDef(smirkycard::CardWindowProps()
                               .title("SmirkyTracker")
                               .visible(true)
                               .idlePolicy(loka::app::IdlePolicy::everyTick())
                               .onIdle(&SmirkyCardScenarioAppConfig::OnWindowIdle, this)
                               .scene(smirkycard::CreateCard(SMIRKY_CARD_FIRST, this->runtime_)));
}

void SmirkyCardScenarioAppConfig::OnWindowIdle(Window *window, double elapsedSeconds, void *userData)
{
  static_cast<SmirkyCardScenarioAppConfig *>(userData)->tick(window, elapsedSeconds);
}

void SmirkyCardScenarioAppConfig::tick(Window *window, double elapsedSeconds)
{
#ifdef TEST_BUILD
  if (this->phase_ == LINGERING_TIMING)
  {
    this->phase_ = LINGERING;
    this->drawTiming_.writeEraseComparison(window ? window->asToolboxWindow() : 0);
  }
#endif
  if (this->phase_ == LINGERING)
  {
    this->lingerRemaining_ -= elapsedSeconds;
    if (this->lingerRemaining_ <= 0.0 && this->app_)
      this->app_->quit();
    return;
  }
  if (this->phase_ == WAITING_ARROW)
  {
    this->arrowWaitRemaining_ -= elapsedSeconds;
    if (this->arrowWaitRemaining_ <= 0.0)
    {
      // The flow still runs so the terminal record stays last; the tracked
      // audit carries "arrow value=", so this line fails the comparison.
      this->audit_.recordLog("arrow timeout");
      this->phase_ = RUNNING;
    }
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
      if (this->phase_ == WAITING_ARROW)
      {
        // The gesture precedes the JS flow: its log line lands before every
        // step record and the flow's terminal record remains the last line.
        const short value = TallListArrowValue(window);
        if (value <= 0)
          return;
        char line[320];
        std::sprintf(line, "arrow value=%d", value);
        this->audit_.recordLog(line);
        const ToolboxSceneDebugStats &stats = controller->debugStatsForTesting();
#ifdef TEST_BUILD
        this->drawTiming_.rememberCopy(stats.lastMovedPresentObservation, stats.totalRenderCalls);
#endif
        std::sprintf(line,
                     "arrow attempt=%lu executed=%s full_draws=%lu candidate=%s refusal=%s paint_first=%s "
                     "rect_requests=%d used_viewports=%u outside_not_translated=%u outside_kinds=%u unit_changed=%d",
                     stats.lastMovedPresentObservation.renderCalls(),
                     ToolboxPresentExecutedName(stats.lastMovedPresentObservation.executed()),
                     stats.lastMovedPresentObservation.totalRenderCalls(),
                     ToolboxPresentVerdictName(stats.lastMovedPresentObservation.verdict()),
                     ToolboxPresentRefusalName(stats.lastMovedPresentObservation.refusal()),
                     ToolboxPaintRefusalName(stats.lastPresentObservation.firstRefusal().reason),
                     stats.lastMovedPresentObservation.rectRequests() ? 1 : 0,
                     stats.lastMovedPresentObservation.usedViewports(),
                     stats.lastMovedPresentObservation.outsideNotTranslated(),
                     stats.lastMovedPresentObservation.outsideKinds(),
                     stats.lastMovedPresentObservation.unitChanged() ? 1 : 0);
        this->audit_.recordLog(line);
        {
          const ToolboxPresentResult &moved = stats.lastMovedPresentObservation;
          const Rect &d = moved.copyDomain();
          const Rect &dm = moved.damage();
          const Rect &b = moved.foreground().bar();
          const Rect u = moved.foreground().count() ? moved.foreground().unit(0) : Rect();
          std::sprintf(line,
                       "arrow repair domain=%d,%d,%d,%d damage=%d,%d,%d,%d units=%u unit0=%d,%d,%d,%d bar=%d,%d,%d,%d "
                       "sealed=%u refused=%d preserved=%u",
                       d.top,
                       d.left,
                       d.bottom,
                       d.right,
                       dm.top,
                       dm.left,
                       dm.bottom,
                       dm.right,
                       moved.foreground().count(),
                       u.top,
                       u.left,
                       u.bottom,
                       u.right,
                       b.top,
                       b.left,
                       b.bottom,
                       b.right,
                       moved.sealedRects().count(),
                       moved.foreground().refused() ? 1 : 0,
                       moved.foreground().preservedCount());
          this->audit_.recordLog(line);
          for (unsigned i = 0; i < moved.sealedRects().count(); ++i)
          {
            const Rect &r = moved.sealedRects().at(i);
            std::sprintf(line, "arrow sealed%u=%d,%d,%d,%d", i, r.top, r.left, r.bottom, r.right);
            this->audit_.recordLog(line);
          }
        }
        this->phase_ = RUNNING_ARROW;
        return;
      }
      if (!this->audit_.finished() && !this->audit_.failed())
      {
        this->clock_.advanceTo(this->clock_.currentTick() + 1);
        static_cast<smirkycard::CardScene *>(scene)->tickScenario();
        return; // The terminal step may have changed the presentation; capture on the next settled idle.
      }
    }
  }
#ifdef TEST_BUILD
  const bool published = this->completionPublisher_.publish(window);
  this->phase_ = published && this->phase_ == RUNNING_ARROW ? LINGERING_TIMING : LINGERING;
#else
  (void)this->completionPublisher_.publish(window);
  this->phase_ = LINGERING;
#endif
}

int RunSmirkyCardScenarioApplication()
{
  using namespace loka;
  dsl::SnapTestConfig::Settings settings;
  if (!dsl::SnapTestConfig::load("LokaTest.cfg", settings) || !settings.hasScenario
      || (settings.scenario != "startup" && settings.scenario != "laptop-carousel"
          && settings.scenario != "laptop-detail" && settings.scenario != "laptop-arrow"))
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
  SmirkyCardScenarioAppConfig config(context.get(), settings);
  core::ScopedPtr<App> app(context->createApp(&config, 0, 0));
  assert(app.get() && "App is required");
  if (!app.get())
    return 1;
  config.setApp(app.get());
#ifdef TEST_BUILD
  // App construction precedes installation; scope teardown precedes App teardown.
  loka::testing::WholeDrawReceiverScope receiver(config.drawReceiver());
#endif
  app->run();
  return 0;
}
