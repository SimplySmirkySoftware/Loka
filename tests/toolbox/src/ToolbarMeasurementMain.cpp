#if TOOLBAR_APP == 1
#include "FloppyBirdScenarioPresentation.hpp"
#include "FloppyBirdScenarios.hpp"
typedef loka::scenario_tests::FloppyBirdScenarioPresentation Presentation;
#elif TOOLBAR_APP == 2
#include "MyAppConfig.hpp"
typedef SmirkBenchAppConfig Presentation;
#elif TOOLBAR_APP == 3
#include "MyAppConfig.hpp"
typedef TutorialAppConfig Presentation;
#elif TOOLBAR_APP == 4
#include "CardNodes.hpp"
#include "CardWindowProps.hpp"
#include "ScenarioRunAudit.hpp"
#include "app/core/AppComposition.hpp"
#include "app/core/AppConfigurable.hpp"
#include "app/core/WindowDefinition.hpp"
typedef AppConfigurable Presentation;
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
#endif

#include "ToolboxWindow.hpp"
#include "ToolboxScenePlatformController.hpp"
#include "ToolbarMeasurementRun.hpp"
#include "ToolbarMeasurementClock.hpp"
#include "app/bootstrap/PlatformBootstrap.hpp"
#include "app/core/App.hpp"
#include "core/util/ScopedPtr.hpp"
#include "testing/scene/SceneTestFlow.hpp"
#include "platform/file/AppLocation.hpp"
#include "platform/file/FileIO.hpp"
#include <Gestalt.h>

#if LOKA_PROFILE_FUNC_TICKS
#error Toolbar measurement requires function profiling to be disabled
#endif

using namespace loka;
namespace
{
  /** The fixture owns the journal; the App owns the single product window.
      Receiver scope in main ends before that App can destroy any window. */
  class MeasurementApp : public Presentation, public testing::WholeDrawReceiver
  {
  public:
    explicit MeasurementApp(PlatformContext *context)
#if TOOLBAR_APP == 1
        : Presentation(context, scenario_tests::FloppyBirdScenarioSeed()),
#else
        : Presentation(context),
#endif
          app_(0),
          tick_(0),
          log_(0)
#if TOOLBAR_APP == 1
          ,
          scenario_()
#elif TOOLBAR_APP == 4
          ,
          audit_(auditFile(), "main-result"),
          runtime_(context)
#endif
    {
      platform::file::FileHandle file;
      if (platform::file::ResolveApplicationSidecar(file::File::Application() << file::File("LOG.TXT"), file))
        this->log_ = platform::file::OpenWriteTruncate(file);
#if TOOLBAR_APP == 4
      core::String error;
      if (!this->runtime_.enableRunner(&this->audit_, &this->clock_, 7, "MAIN.FLOW.JS", CompanionSource())
          || !this->runtime_.loadBuiltin(CardSource(), error))
        this->audit_.recordTerminal(dsl::testing::SCENARIO_AUDIT_FAILED);
#endif
      this->journal_.begin("launch", 0, false);
    }
    ~MeasurementApp()
    {
      if (this->log_)
        std::fclose(this->log_);
    }
    virtual unsigned long now() const
    {
      return testing::MeasurementMicroseconds();
    }
    virtual void expectDraw()
    {
      this->journal_.expectDraw();
    }
    virtual void accept(const testing::WholeDrawSample &sample)
    {
      this->journal_.accept(sample);
    }
    void setApp(App *app)
    {
      this->app_ = app;
    }
    int result() const
    {
      return this->log_ && this->journal_.passed() ? 0 : 1;
    }

#if TOOLBAR_APP != 1
    virtual void compose(AppComposition &composition)
    {
#if TOOLBAR_APP == 2
      // Same product scene; fit the complete window on the 640x480 rig as the existing add-face vehicle does.
      WindowProps props = WindowProps()
                              .frame(1, 41, 636, 400)
                              .title("LokaSmirkBench")
                              .scene(app::scene::Boundary<smirkbench::MainNode>(smirkbench::MainProps(&this->model())));
#elif TOOLBAR_APP == 3
      WindowProps props = this->productionWindowProps(app::scene::Boundary<tutorial::Step2Node>());
#elif TOOLBAR_APP == 4
      WindowProps props = smirkycard::CardWindowProps()
                              .title("SmirkyCard")
                              .scene(smirkycard::CreateCard(SMIRKY_CARD_FIRST, this->runtime_));
#endif
      composition << WindowDef(props.visible(true).idlePolicy(app::IdlePolicy::everyTick()).onIdle(&OnIdle, this));
    }
#endif
  private:
#if TOOLBAR_APP == 4
    static platform::file::FileHandle auditFile()
    {
      platform::file::FileHandle file;
      platform::file::ResolveApplicationSidecar(file::File::Application() << file::File("MAIN.AUD"), file);
      return file;
    }
#endif
    static void OnIdle(Window *window, double, void *owner)
    {
      static_cast<MeasurementApp *>(owner)->step(window);
    }
    virtual void onScenarioIdle(Window *window, double)
    {
      this->step(window);
    }
    bool click(Window *window, const char *id)
    {
      app::scene::Scene *out = 0;
      dsl::FlowError error;
      return dsl::testing::ClickButtonByIdAdapter(id).run(window->scene(), out, error) == dsl::FLOW_STEP_SUCCEEDED;
    }
    void fail()
    {
      this->journal_.begin("driver-failure", this->tick_);
      this->finish();
    }
    void finish()
    {
      this->journal_.close();
      if (this->log_)
      {
        this->journal_.write(this->log_);
        std::fflush(this->log_);
      }
#if TOOLBAR_APP == 1
      if (this->scenario_.get())
        this->scenario_->stop();
#endif
      this->app_->quit();
    }
    bool metadata(Window *window)
    {
      if (!this->log_)
        return false;
#if TOOLBAR_APP == 1
      std::fprintf(this->log_,
                   "toolbar_inputs driver=fixed-step-flaps seed=0x13579BDF fixed_step=1/60 flap_ticks=2,40,78,116,154 "
                   "final_tick=273 repetitions=10 score_operations=10 launch_and_death=diagnostic\n");
#elif TOOLBAR_APP == 2
      std::fprintf(this->log_,
                   "toolbar_inputs driver=add-face initial_faces=1 last_successful_faces=16 button=SmirkBench.AddFace "
                   "window_geometry=existing-636x400-rig-fit\n");
#elif TOOLBAR_APP == 3
      std::fprintf(this->log_,
                   "toolbar_inputs driver=step2-increment button=Tutorial.Step2.Increment values=1..10 "
                   "presentation=product-Step2\n");
#elif TOOLBAR_APP == 4
      std::fprintf(this->log_,
                   "toolbar_inputs driver=MAIN.FLOW.JS button=SmirkyCard.RunScript "
                   "expression=globalThis.toolbarResult=(globalThis.toolbarResult||100)+1 results=101..110\n");
#endif
      const unsigned long start = this->now();
      const unsigned long end = this->now();
      unsigned long resolution = ~0UL;
      unsigned long previous = end;
      for (int i = 0; i < 1024; ++i)
      {
        const unsigned long current = this->now();
        const unsigned long delta = current - previous;
        if (delta && delta < resolution)
          resolution = delta;
        previous = current;
      }
      long system = 0;
      const OSErr systemStatus = Gestalt(gestaltSystemVersion, &system);
      const Rect screen = qd.screenBits.bounds;
      GDHandle device = GetMainDevice();
      if (systemStatus != noErr || !device || !(*device)->gdPMap || resolution == ~0UL)
        return false;
      const int depth = (*(*device)->gdPMap)->pixelSize;
      const Rect port = window->asToolboxWindow()->window()->portRect;
      std::fprintf(this->log_,
                   "toolbar_environment system_hex=%lx screen=%dx%d depth=%d scene=%dx%d clock=Microseconds "
                   "observed_resolution_us=%lu empty_bracket_us=%lu TEST_BUILD=1 profile_func_ticks=0 build=Release-Os "
                   "emulator_machine_rom=see-host-manifest reduced_fixture=0 app=%d\n",
                   system,
                   screen.right - screen.left,
                   screen.bottom - screen.top,
                   depth,
                   port.right - port.left,
                   port.bottom - port.top,
                   resolution == ~0UL ? 0 : resolution,
                   end - start,
                   TOOLBAR_APP);
      return true;
    }
    void step(Window *window)
    {
      if (!window || !window->scene())
      {
        this->journal_.begin("missing-scene", this->tick_);
        this->finish();
        return;
      }
      ToolboxWindow *native = window->asToolboxWindow();
      ToolboxScenePlatformController *controller = static_cast<ToolboxScenePlatformController *>(
          dsl::testing::SceneTestAccess::platformController(*window->scene()));
      if (window->scene()->hasPendingInvalidation() || (controller && controller->hasPendingSync())
          || native->hasPendingInvalidate())
        return;
      this->journal_.close();
      if (this->tick_ == 0 && !this->metadata(window))
      {
        this->fail();
        return;
      }
      ++this->tick_;
#if TOOLBAR_APP == 1
      // Repeat the original fixed-step-flaps flow without changing any due_tick.
      const long local = (this->tick_ - 1) % 274;
      if (this->tick_ > 2740)
      {
        this->finish();
        return;
      }
      if (local == 0)
      {
        this->gameModel().reset(scenario_tests::FloppyBirdScenarioSeed());
        this->scenario_.reset(
            new (std::nothrow) scenario_tests::FloppyBirdScenario(scenario_tests::SCENARIO_COMPLETION_DRIVER_OWNED, 0));
        if (!this->scenario_.get())
        {
          this->fail();
          return;
        }
        this->journal_.begin("launch", this->tick_, false);
        native->requestInvalidateWithReason("toolbar_measurement");
        return;
      }
      const int score = this->gameModel().score();
      const loka_floppy_bird::GameState phase = this->gameModel().gameState();
      this->gameModel().advanceFrame(loka_floppy_bird::kFixedStepSeconds);
      dsl::SnapRecord record;
      const scenario_tests::ScenarioAdvance advance = this->scenario_->step(
          local, window->scene(), this->gameModel(), scenario_tests::CaptureContentBounds(), record);
      if (advance == scenario_tests::SCENARIO_ADVANCE_DRIVER_COMPLETION_READY)
      {
        std::string status;
        if (!record.get("status", status) || status != dsl::SnapStatusOk())
        {
          this->journal_.begin("driver-failure", this->tick_);
          this->finish();
          return;
        }
      }
      if (this->gameModel().score() != score)
        this->journal_.begin("fixed-step-flaps-score", this->tick_);
      else if (this->gameModel().gameState() != phase)
        this->journal_.begin(
            this->gameModel().gameState() == loka_floppy_bird::GAME_DEAD ? "death" : "launch", this->tick_, false);
      else
        return;
#elif TOOLBAR_APP == 2
      if (this->model().faceCount() == app::RectSurfaceModel::kMaxRects)
      {
        this->finish();
        return;
      }
      const int before = this->model().faceCount();
      this->journal_.begin("add-face", before + 1);
      if (!this->click(window, "SmirkBench.AddFace") || this->model().faceCount() != before + 1)
      {
        this->fail();
        return;
      }
#elif TOOLBAR_APP == 3
      if (this->tick_ > 10)
      {
        this->finish();
        return;
      }
      this->journal_.begin("step2-increment", this->tick_);
      if (!this->click(window, "Tutorial.Step2.Increment"))
      {
        this->fail();
        return;
      }
      dsl::SnapRecord text;
      dsl::FlowError error;
      std::string value;
      char expected[32];
      std::sprintf(expected, "Count: %ld", this->tick_);
      if (dsl::testing::SnapText("Tutorial.Step2.Count", "Tutorial", "step2", this->tick_, 1)
                  .run(window->scene(), text, error)
              != dsl::FLOW_STEP_SUCCEEDED
          || !text.get("text.value", value) || value != expected)
      {
        this->fail();
        return;
      }
#elif TOOLBAR_APP == 4
      if (this->audit_.failed())
      {
        this->journal_.begin("driver-failure", this->tick_);
        this->finish();
        return;
      }
      if (this->audit_.finished())
      {
        this->finish();
        return;
      }
      if (this->tick_ == 1)
      {
        app::scene::Scene *out = 0;
        dsl::FlowError error;
        if (dsl::testing::EnterText("SmirkyCard.Script", "globalThis.toolbarResult=(globalThis.toolbarResult||100)+1")
                .run(window->scene(), out, error)
            != dsl::FLOW_STEP_SUCCEEDED)
        {
          this->journal_.begin("driver-failure", this->tick_);
          this->finish();
        }
        return;
      }
      this->clock_.advanceTo(this->clock_.currentTick() + 1);
      // The companion runs its first step at the invocation tick, then one per tick.
      if (this->clock_.currentTick() <= 10)
        this->journal_.begin("main-result", this->clock_.currentTick());
      static_cast<smirkycard::CardScene *>(window->scene())->tickScenario();
#endif
      native->requestInvalidateWithReason("toolbar_measurement");
    }
    App *app_;
    long tick_;
    std::FILE *log_;
    testing::ToolbarMeasurementRun journal_;
#if TOOLBAR_APP == 1
    core::ScopedPtr<scenario_tests::FloppyBirdScenario> scenario_;
#elif TOOLBAR_APP == 4
    ScenarioRunAudit audit_;
    dsl::testing::ScenarioClock clock_;
    smirkycard::ScriptRuntime runtime_;
#endif
  };
} // namespace
int main()
{
  platform::InitPlatformRuntime();
  core::ScopedPtr<PlatformContext> context(platform::CreatePlatformContext());
  if (!context.get())
    return 1;
  MeasurementApp config(context.get());
  core::ScopedPtr<App> app(context->createApp(&config, 0, 0));
  if (!app.get())
    return 1;
  config.setApp(app.get());
  {
    testing::WholeDrawReceiverScope receiver(config);
    app->run();
  }
  return config.result();
}
