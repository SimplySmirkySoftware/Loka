#include "SmirkBenchScenarioDriver.hpp"

#include <cassert>

#include "MainNode.hpp"
#include "SmirkBenchAttributedScenario.hpp"
#include "SmirkBenchPlainEditorScenario.hpp"
#include "ToolboxTextEditorScrollScenario.hpp"
#include "ToolboxTextEditorKeysScenario.hpp"
#include "ObservedMainDefinition.hpp"
#include "RectSurfaceScenarioObservation.hpp"
#include "ScenarioDriverSupport.hpp"
#include "ToolboxInputDoor.hpp"
#include "ToolboxScenePlatformController.hpp"
#include "ToolboxWindow.hpp"
#include "app/PlatformContext.hpp"
#include "app/bootstrap/PlatformBootstrap.hpp"
#include "app/core/App.hpp"
#include "app/core/AppComposition.hpp"
#include "app/core/AppConfigurable.hpp"
#include "app/core/WindowDefinition.hpp"
#include "core/util/ScopedPtr.hpp"
#include "testing/scene/ScenarioAudit.hpp"
#include "core/util/StateTrackerGuard.hpp"

namespace loka
{
  namespace toolbox_tests
  {
    namespace
    {
      bool IsSmirkBenchScenario(const std::string &name)
      {
        return scenario_tests::SmirkBenchToolboxCells().contains(name);
      }

      dsl::SnapRecord MakeRecord(const char *scenario, long tick, const char *status)
      {
        dsl::SnapRecord record;
        dsl::FlowError error;
        dsl::BuildSnapV1RecordAdapter("SmirkBench", scenario, "SmirkBench.Surface", tick, 1, status)
            .run(0, record, error);
        return record;
      }

      /** Toolbox-only measurement owner. Every model write gets its own idle
          turn and must finish apply/presentation before the next step. The
          audit owns completed facts; it never retains a Scene or controller. */
      class SmirkBenchScenarioAppConfig : public AppConfigurable
      {
      public:
        SmirkBenchScenarioAppConfig(PlatformContext *context, const dsl::SnapTestConfig::Settings &settings)
            : AppConfigurable(context),
              model_(640, 400),
              scenario_(settings.scenario),
              retainedTextA_(core::String::Literal("Rebind A")),
              retainedTextB_(core::String::Literal("Rebind B")),
              audit_(ResolveScenarioAuditFile(), settings.scenario.c_str()),
              terminal_(&this->audit_),
              borrowedApp_(0),
              tick_(0),
              lingerRemaining_(settings.hasLingerSeconds ? settings.lingerSeconds : 0.0),
              completionPublisher_()
        {
        }

        void setApp(App *app)
        {
          this->borrowedApp_ = app;
        }

        virtual void compose(AppComposition &composition)
        {
          // Deliberate presentation twin of SmirkBenchAppConfig::compose:
          // preserve its scene, replacing only the real-time idle source. The
          // frame differs from production (50, 50, 640, 400) on purpose: the MAME
          // rig's screen is 640x480 and the capture record refuses a window whose
          // structure rectangle leaves the screen, so the scenario window is
          // placed at (1, 41) with a 636-pixel content width (structure 0..638).
          WindowProps window;
          if (this->scenario_ == "text-editor-plain" || this->scenario_ == "text-editor-scroll"
              || this->scenario_ == "text-editor-keys")
          {
            window.scene(scenario_tests::ObservedMainDefinition<scenario_tests::SmirkBenchPlainEditorProps,
                                                               scenario_tests::SmirkBenchPlainEditorNode>(
                scenario_tests::SmirkBenchPlainEditorProps(&this->model_), 0));
          }
          else if (this->scenario_ == "attributed-editor-line")
          {
            window.scene(scenario_tests::ObservedMainDefinition<scenario_tests::SmirkBenchEditorLineProps,
                                                               scenario_tests::SmirkBenchEditorLineNode>(
                scenario_tests::SmirkBenchEditorLineProps(&this->model_), 0));
          }
          else
          {
            window.scene(scenario_tests::ObservedMainDefinition<smirkbench::MainProps, smirkbench::MainNode>(
                smirkbench::MainProps(&this->model_), 0));
          }
          composition << WindowDef(window
                                       .frame(1, 41, 636, 400)
                                       .title("LokaSmirkBench")
                                       .visible(true)
                                       .idlePolicy(app::IdlePolicy::everyTick())
                                       .onIdle(&SmirkBenchScenarioAppConfig::OnIdle, this));
        }

        virtual void composeDefaultMenu(app::MenuComposition &composition)
        {
          // Keep paired with SmirkBenchAppConfig::composeDefaultMenu.
          using namespace app;
          composition.declare(AppMenu() << MenuItem("About").actionType(MENU_ACTION_ABOUT_APP) << MenuSeparator()
                                        << MenuItem("Quit").actionType(MENU_ACTION_QUIT_APP));
        }

      private:
        static void OnIdle(Window *window, double elapsedSeconds, void *userData)
        {
          static_cast<SmirkBenchScenarioAppConfig *>(userData)->tick(window, elapsedSeconds);
        }

        bool recordStep(const char *name)
        {
          return this->audit_.recordStep(dsl::testing::ScenarioStepTerminal(static_cast<int>(this->tick_),
                                                                            name,
                                                                            this->tick_,
                                                                            this->tick_,
                                                                            dsl::FLOW_STEP_SUCCEEDED,
                                                                            dsl::FlowError()));
        }

        bool capture(Window *window, ToolboxScenePlatformController &controller, const char *checkpoint)
        {
          dsl::SnapRecord record = MakeRecord(this->scenario_.c_str(), this->tick_, dsl::SnapStatusOk());
          dsl::FlowError error;
          std::string rectangles;
          if (scenario_tests::CaptureRectSurfaceModel(window->scene(), "SmirkBench.Surface", rectangles, error)
              != dsl::FLOW_STEP_SUCCEEDED)
          {
            return false;
          }
          dsl::SnapRecord text;
          if (dsl::testing::SnapText("SmirkBench.FaceCount", "SmirkBench", this->scenario_.c_str(), this->tick_, 1)
                  .run(window->scene(), text, error)
              != dsl::FLOW_STEP_SUCCEEDED)
          {
            return false;
          }
          app::ButtonNode *button = 0;
          if (dsl::testing::LookupNodeById<app::ButtonNode>(window->scene(), "SmirkBench.AddFace", button, error)
                  != dsl::FLOW_STEP_SUCCEEDED
              || !button || !dsl::testing::SceneClickTraits<app::ButtonNode>::enabled(button))
          {
            return false;
          }
          record.set("button.enabled", "true");
          std::string label;
          const char *expectedLabel = this->scenario_ == "retained-text-rebind" && this->tick_ == 7
              ? "Rebind B updated" : (this->model_.faceCount() == 1 ? "Faces: 1" : "Faces: 2");
          if (!text.get("text.value", label) || label != expectedLabel)
          {
            return false;
          }
          record.set("rail", "toolbox");
          record.set("checkpoint", checkpoint);
          record.set("surface.rects", rectangles.c_str());
          record.set("text.value", label.c_str());
          record.setInt("face_count", this->model_.faceCount());
          const ToolboxSceneDebugStats &stats = controller.debugStatsForTesting();
          record.setInt("total.control_draws", stats.totalControlDrawCount);
          record.setInt("total.collector_visits", stats.totalCollectorVisitCount);
          record.setInt("total.boundary_applies", stats.totalBoundaryApplyCount);
          record.setInt("last.control_draws", stats.controlDrawCount);
          scenario_tests::SetContentBounds(record, ContentLocalBounds(QueryCaptureContentBounds(window)));
          return this->recordStep(checkpoint) && this->audit_.recordVerdict(record);
        }

        /** Exercise the production retained-apply door on the existing Text.
            The config owns both sources until after App/Scene destruction. */
        bool rebindTextStep(Window *window, ToolboxScenePlatformController &controller)
        {
          app::TextNode *text = 0;
          dsl::FlowError error;
          if (dsl::testing::LookupNodeById<app::TextNode>(window->scene(), "SmirkBench.FaceCount", text, error)
                  != dsl::FLOW_STEP_SUCCEEDED || !text || !text->getContext())
          {
            return false;
          }
          app::scene::NodeContext *context = text->getContext();
          if (this->tick_ == 3 || this->tick_ == 4)
          {
            core::State<core::String> *source = this->tick_ == 3 ? &this->retainedTextA_ : &this->retainedTextB_;
            app::Text declaration(source);
            if (!declaration.applyPropsToNode(text) || text->getContext() != context
                || context->projectedTextState() != source)
            {
              return false;
            }
            // Replay BEFORE any layout/draw can replace the recorded hit.
            // The fixed scenario content frame is declared in compose above.
            Rect rect;
            SetRect(&rect, 0, 0, 636, 400);
            ToolboxInputDoor::renderDirty(controller, rect);
            return this->recordStep(this->tick_ == 3 ? "apply-A" : "apply-B-replay");
          }
          if (this->tick_ == 5 || this->tick_ == 6)
          {
            const long before = controller.debugStatsForTesting().textChangedTextCount;
            {
              core::StateTrackerGuard guard(window->getTracker());
              if (this->tick_ == 5)
                this->retainedTextA_.set(core::String::Literal("Retired A changed"));
              else
                this->retainedTextB_.set(core::String::Literal("Rebind B updated"));
            }
            const long after = controller.debugStatsForTesting().textChangedTextCount;
            if (context->projectedTextState() != &this->retainedTextB_
                || (this->tick_ == 5 ? after != before : after != before + 1))
            {
              return false;
            }
            return this->recordStep(this->tick_ == 5 ? "A-does-not-notify-hit" : "B-notifies-hit");
          }
          return false;
        }

        void fail(Window *window)
        {
          const dsl::SnapRecord record = MakeRecord(this->scenario_.c_str(), this->tick_, dsl::SnapStatusError());
          (void)this->terminal_.emit(dsl::testing::SCENARIO_AUDIT_FAILED, record);
          (void)this->completionPublisher_.publish(window);
        }

        void tick(Window *window, double elapsedSeconds)
        {
          if (this->terminal_.isSettled())
          {
            this->lingerRemaining_ -= elapsedSeconds;
            if (this->lingerRemaining_ <= 0.0 && this->borrowedApp_)
            {
              this->borrowedApp_->quit();
            }
            return;
          }
          if (!window || !window->scene())
          {
            this->fail(window);
            return;
          }
          app::scene::Scene *scene = window->scene();
          ToolboxScenePlatformController *controller =
              static_cast<ToolboxScenePlatformController *>(dsl::testing::SceneTestAccess::platformController(*scene));
          if (!controller)
          {
            this->fail(window);
            return;
          }
          // Toolbox hasPendingSync covers retirement, not pending paint.
          // Include the Window's invalidation queue before reading counters.
          if (scene->hasPendingInvalidation() || controller->hasPendingSync()
              || window->asToolboxWindow()->hasPendingInvalidate())
          {
            return;
          }
          ++this->tick_;
          if (this->tick_ < 2)
          {
            return;
          }
          if (this->scenario_ == "text-editor-scroll")
          {
            dsl::SnapRecord record;
            if (this->scrollScenario_.advance(
                    elapsedSeconds, scene, *window->asToolboxWindow(), *controller, this->audit_, record)
                == scenario_tests::SCENARIO_ADVANCE_DRIVER_COMPLETION_READY)
            {
              std::string status;
              const bool ok = record.get("status", status) && status == dsl::SnapStatusOk();
              (void)this->terminal_.emit(
                  ok ? dsl::testing::SCENARIO_AUDIT_SUCCEEDED : dsl::testing::SCENARIO_AUDIT_FAILED, record);
              (void)this->completionPublisher_.publish(window);
            }
            return;
          }
          if (this->scenario_ == "text-editor-keys")
          {
            dsl::SnapRecord record;
            if (this->keysScenario_.advance(
                    elapsedSeconds, scene, *window->asToolboxWindow(), *controller, this->audit_, record)
                == scenario_tests::SCENARIO_ADVANCE_DRIVER_COMPLETION_READY)
            {
              std::string status;
              const bool ok = record.get("status", status) && status == dsl::SnapStatusOk();
              (void)this->terminal_.emit(
                  ok ? dsl::testing::SCENARIO_AUDIT_SUCCEEDED : dsl::testing::SCENARIO_AUDIT_FAILED, record);
              (void)this->completionPublisher_.publish(window);
            }
            return;
          }
          if (this->scenario_ == "attributed-editor-line" || this->scenario_ == "text-editor-plain")
          {
            dsl::SnapRecord record;
            if ((this->scenario_ == "text-editor-plain"
                    ? scenario_tests::AdvanceSmirkBenchPlainEditor(this->tick_, scene, *controller, this->audit_, record)
                    : scenario_tests::AdvanceSmirkBenchEditor(this->tick_, scene, this->audit_, record))
                == scenario_tests::SCENARIO_ADVANCE_DRIVER_COMPLETION_READY)
            {
              std::string status;
              const bool ok = record.get("status", status) && status == dsl::SnapStatusOk();
              (void)this->terminal_.emit(ok ? dsl::testing::SCENARIO_AUDIT_SUCCEEDED
                                           : dsl::testing::SCENARIO_AUDIT_FAILED, record);
              (void)this->completionPublisher_.publish(window);
            }
            return;
          }
          const bool addFace = this->scenario_ == "add-face";
          const long finalTick = this->scenario_ == "startup" ? 2
              : this->scenario_ == "retained-text-rebind" ? 7 : (addFace ? 10 : 33);
          if (this->tick_ == 2 || (addFace && this->tick_ == 4) || this->tick_ == finalTick)
          {
            const char *checkpoint = this->tick_ == 2 ? "post-settle" : this->tick_ == 4 ? "post-add-face" : "final";
            if (!this->capture(window, *controller, checkpoint))
            {
              this->fail(window);
              return;
            }
            if (this->tick_ == finalTick)
            {
              (void)this->terminal_.emit(dsl::testing::SCENARIO_AUDIT_SUCCEEDED);
              (void)this->completionPublisher_.publish(window);
            }
            return;
          }
          if (this->scenario_ == "retained-text-rebind")
          {
            if (!this->rebindTextStep(window, *controller)) this->fail(window);
            return;
          }
          if (addFace && this->tick_ == 3)
          {
            app::scene::Scene *out = 0;
            dsl::FlowError error;
            if (dsl::testing::ClickButtonByIdAdapter("SmirkBench.AddFace").run(scene, out, error)
                    != dsl::FLOW_STEP_SUCCEEDED
                || this->model_.faceCount() != 2 || !this->recordStep("add-face"))
            {
              this->fail(window);
            }
            return;
          }
          // surface-ticks: turns 3..32 (30 steps); add-face: 5..9 (5 steps).
          this->model_.advanceFrame(smirkbench::kFixedStepSeconds);
          if (!this->recordStep("surface-tick"))
          {
            this->fail(window);
          }
        }

        smirkbench::SmirkModel model_;
        const std::string scenario_;
        core::MutableState<core::String> retainedTextA_;
        core::MutableState<core::String> retainedTextB_;
        dsl::testing::ScenarioAuditFile audit_;
        dsl::testing::scenario_audit_detail::TerminalEmitter terminal_;
        App *borrowedApp_;
        scenario_tests::ToolboxTextEditorScrollScenario scrollScenario_;
        scenario_tests::ToolboxTextEditorKeysScenario keysScenario_;
        long tick_;
        double lingerRemaining_;
        ScenarioCompletionPublisher completionPublisher_;
      };
    } // namespace

    int RunSmirkBenchScenarioApplication()
    {
      dsl::SnapTestConfig::Settings settings;
      if (!dsl::SnapTestConfig::load("LokaTest.cfg", settings) || !settings.hasScenario
          || !IsSmirkBenchScenario(settings.scenario))
      {
        (void)WriteScenarioErrorAudit("startup", MakeRecord("startup", 0, dsl::SnapStatusError()));
        return 0;
      }
      platform::InitPlatformRuntime();
      core::ScopedPtr<PlatformContext> context(platform::CreatePlatformContext());
      assert(context.get() && "PlatformContext is required");
      if (!context.get())
      {
        return 1;
      }
      SmirkBenchScenarioAppConfig config(context.get(), settings);
      core::ScopedPtr<App> app(context->createApp(&config, 0, 0));
      assert(app.get() && "App is required");
      if (!app.get())
      {
        return 1;
      }
      config.setApp(app.get());
      app->run();
      return 0;
    }
  } // namespace toolbox_tests
} // namespace loka
