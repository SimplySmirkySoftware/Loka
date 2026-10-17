#include "HelloWorldScenarioDriver.hpp"

#include <cassert>
#include <cstdio>
#include <cstring>

#include "HelloWorldScenarios.hpp"
#include "HelloWorldScenarioPresentation.hpp"
#include "ScenarioDriverSupport.hpp"
#include "StartupScenarios.hpp"
#include "ToolboxScenePlatformController.hpp"
#include "ToolboxWindow.hpp"
#include "app/PlatformContext.hpp"
#include "app/bootstrap/PlatformBootstrap.hpp"
#include "app/core/App.hpp"
#include "app/core/AppComposition.hpp"
#include "app/core/AppConfigurable.hpp"
#include "app/nodes/controls/EditText.hpp"
#include "context/ToolboxEditTextContext.hpp"
#include "core/util/ScopedPtr.hpp"
#include "core/util/StateTrackerGuard.hpp"
#include "platform/StringUTF8.hpp"
#include "testing/scene/ScenarioAudit.hpp"
#include "testing/scene/SceneTestFlow.hpp"

namespace loka
{
  namespace toolbox_tests
  {
    namespace
    {
      const char *kConfigPath = "LokaTest.cfg";
      const char *kDefaultScenarioName = "toggle-action-probe";
      const char *kBmiRoundtripScenarioName = "bmi-roundtrip";
      const char *kDirtyReplayProbeKey = "probe_dirty_replay";

      /** The probe stays opt-in. Its original reason is gone -- the BMI fields
          no longer sit below the fold, so a probe run can no longer red the
          rail for visibility rather than replay -- but "the reason went away"
          is not evidence that the probe still fails when it should. Turning it
          on by default needs a run that shows it going red on a reintroduced
          defect; #436 tracks that. run-scenario.sh --probe writes this key;
          the tracked cell does not. SnapTestConfig ignores unknown keys, so
          this stays driver-local. */
      bool DirtyReplayProbeRequested(const char *configPath)
      {
        FILE *fp = std::fopen(configPath, "rb");
        if (!fp)
        {
          return false;
        }
        char line[256];
        bool requested = false;
        while (std::fgets(line, sizeof(line), fp) != 0)
        {
          const char *cursor = line;
          while (*cursor == ' ' || *cursor == '\t')
          {
            ++cursor;
          }
          const size_t keyLength = std::strlen(kDirtyReplayProbeKey);
          if (std::strncmp(cursor, kDirtyReplayProbeKey, keyLength) != 0)
          {
            continue;
          }
          cursor += keyLength;
          while (*cursor == ' ' || *cursor == '\t')
          {
            ++cursor;
          }
          if (*cursor == '1')
          {
            requested = true;
          }
        }
        std::fclose(fp);
        return requested;
      }
      const short kDirtyReplayProbeMaxWidth = 256;

      dsl::testing::NodeSelector<app::EditTextNode> BmiHeightInputSelector()
      {
        return dsl::testing::Within("HelloWorld.Bmi").descendant<app::EditTextNode>(1);
      }

      struct ProgrammaticTextChange
      {
        core::MutableState<core::String> *state;
        core::StateTracker *tracker;
        core::String value;

        ProgrammaticTextChange(core::MutableState<core::String> *stateValue,
                               core::StateTracker *trackerValue,
                               const core::String &nextValue)
            : state(stateValue),
              tracker(trackerValue),
              value(nextValue)
        {
        }

        static void Apply(void *userData)
        {
          ProgrammaticTextChange *change = static_cast<ProgrammaticTextChange *>(userData);
          if (!change || !change->state || !change->tracker)
          {
            return;
          }
          core::StateTrackerGuard guard(change->tracker);
          change->state->set(change->value, true);
        }
      };

      bool ProbeBmiHeightStateSync(Window *window, const char *&failureMessage)
      {
        failureMessage = "EditText state-sync probe was not run";
        ToolboxWindow *toolboxWindow = window ? window->asToolboxWindow() : 0;
        WindowPtr nativeWindow = toolboxWindow ? toolboxWindow->window() : 0;
        ToolboxScenePlatformController *controller =
            toolboxWindow ? toolboxWindow->scenePlatformController() : 0;
        if (!window || !window->scene() || !toolboxWindow || !nativeWindow || !controller)
        {
          failureMessage = "EditText state-sync probe window was unavailable";
          return false;
        }

        app::EditTextNode *heightInput = 0;
        dsl::FlowError lookupError;
        if (dsl::testing::ResolveSelector<app::EditTextNode>(
                window->scene(), BmiHeightInputSelector(), heightInput, lookupError)
            != dsl::FLOW_STEP_SUCCEEDED)
        {
          failureMessage = "EditText state-sync probe could not find the BMI height input";
          return false;
        }
        ToolboxEditTextContext *editContext =
            heightInput ? static_cast<ToolboxEditTextContext *>(heightInput->getContext()) : 0;
        core::MutableState<core::String> *heightState =
            heightInput && heightInput->props.text_.isValid()
                ? static_cast<core::MutableState<core::String> *>(heightInput->props.text_.state()->asMutableState())
                : 0;
        app::scene::BoundaryNode *rootBoundary =
            dsl::testing::SceneTestAccess::rootBoundary(*window->scene());
        if (!editContext || !heightState || !rootBoundary || !rootBoundary->tracker())
        {
          failureMessage = "EditText state-sync probe found no live mutable Toolbox binding";
          return false;
        }
        const Rect chrome = editContext->chromeRect();
        if (chrome.left < nativeWindow->portRect.left || chrome.top < nativeWindow->portRect.top
            || chrome.right > nativeWindow->portRect.right || chrome.bottom > nativeWindow->portRect.bottom)
        {
          failureMessage = "EditText state-sync probe input was outside the window port";
          return false;
        }

        std::string beforeState;
        std::string beforeNative;
        if (!platform::CollectUtf8(heightState->get(), beforeState)
            || !controller->queryEditTextValueForTesting(editContext, beforeNative))
        {
          failureMessage = "EditText state-sync probe could not read its positive-control values";
          return false;
        }
        const char *nextBytes = beforeState == "181.5" ? "182.5" : "181.5";
        ProgrammaticTextChange change(
            heightState, rootBoundary->tracker(), core::String::Literal(nextBytes));
        core::EmitterState batchEvent;
        batchEvent.bind(&ProgrammaticTextChange::Apply, &change, false, false, 0);
        // emitHitEmitter owns the controller's ordinary begin/end batch pair.
        // This keeps the probe on the same always-on Release path as a native
        // batched action without adding a mutable testing door to production.
        controller->emitHitEmitter(&batchEvent);
        batchEvent.unbind(&ProgrammaticTextChange::Apply, &change);

        std::string afterState;
        if (!platform::CollectUtf8(heightState->get(), afterState) || afterState != nextBytes
            || afterState == beforeState)
        {
          failureMessage = "EditText state-sync probe positive control did not change the State";
          return false;
        }

        // Read the TE record BEFORE any flush or presentation. The defect's
        // seam is that a programmatic write must reach TESetText through the
        // state listener itself: in the reel a batch is open when the walk
        // runs, so the render-time repair in ensureEditTextControl is gated
        // off and nothing else heals the record. A read after a flush would
        // let that healing walk hide the missing listener, and the probe
        // would pass with the fix reverted.
        std::string afterNative;
        if (!controller->queryEditTextValueForTesting(editContext, afterNative))
        {
          failureMessage = "EditText state-sync probe could not read the TE record";
          return false;
        }
        if (afterNative != afterState)
        {
          failureMessage = "EditText TE text did not match its programmatically changed State";
          return false;
        }

        // Put the completed scenario's value back before anything paints: the
        // runner captures this scene and compares it against the same golden a
        // non-probe run produces, so a probe that leaves the field changed
        // could only pass by replacing a golden the presentation rail then
        // fails. Restoring is also a second measurement of the same seam --
        // the listener has to carry this write to the TE record too.
        ProgrammaticTextChange restore(
            heightState, rootBoundary->tracker(), core::String::Literal(beforeState.c_str()));
        core::EmitterState restoreEvent;
        restoreEvent.bind(&ProgrammaticTextChange::Apply, &restore, false, false, 0);
        controller->emitHitEmitter(&restoreEvent);
        restoreEvent.unbind(&ProgrammaticTextChange::Apply, &restore);

        std::string restoredState;
        std::string restoredNative;
        if (!platform::CollectUtf8(heightState->get(), restoredState) || restoredState != beforeState)
        {
          failureMessage = "EditText state-sync probe could not restore the completed scenario value";
          return false;
        }
        if (!controller->queryEditTextValueForTesting(editContext, restoredNative)
            || restoredNative != restoredState)
        {
          failureMessage = "EditText TE text did not follow the State back to its scenario value";
          return false;
        }

        // Then present normally so the screen and any later capture see the
        // settled scene; this is cleanup, not the assertion.
        window->flushSceneInvalidation();
        toolboxWindow->flushInvalidate();
        failureMessage = 0;
        return true;
      }

      void CapturePixelRow(const Rect &row, unsigned char *pixels)
      {
        for (short x = row.left; x < row.right; ++x)
        {
          pixels[x - row.left] = GetPixel(x, row.top) ? 1 : 0;
        }
      }

      bool PixelRowMatches(const Rect &row, const unsigned char *pixels)
      {
        for (short x = row.left; x < row.right; ++x)
        {
          const unsigned char pixel = GetPixel(x, row.top) ? 1 : 0;
          if (pixel != pixels[x - row.left])
          {
            return false;
          }
        }
        return true;
      }

      bool ProbeBmiHeightDirtyReplay(Window *window, const char *&failureMessage)
      {
        failureMessage = "dirty replay probe was not run";
        ToolboxWindow *toolboxWindow = window ? window->asToolboxWindow() : 0;
        WindowPtr nativeWindow = toolboxWindow ? toolboxWindow->window() : 0;
        if (!window || !window->scene() || !toolboxWindow || !nativeWindow)
        {
          failureMessage = "dirty replay probe window was unavailable";
          return false;
        }

        app::EditTextNode *heightInput = 0;
        dsl::FlowError lookupError;
        if (dsl::testing::ResolveSelector<app::EditTextNode>(
                window->scene(), BmiHeightInputSelector(), heightInput, lookupError)
            != dsl::FLOW_STEP_SUCCEEDED)
        {
          failureMessage = "dirty replay probe could not find the BMI height input";
          return false;
        }
        ToolboxEditTextContext *editContext =
            heightInput ? static_cast<ToolboxEditTextContext *>(heightInput->getContext()) : 0;
        if (!editContext)
        {
          failureMessage = "dirty replay probe found no Toolbox EditText context";
          return false;
        }

        const Rect chrome = editContext->chromeRect();
        const int width = static_cast<int>(chrome.right) - static_cast<int>(chrome.left);
        if (width <= 0 || width > kDirtyReplayProbeMaxWidth || chrome.top < nativeWindow->portRect.top
            || chrome.top >= nativeWindow->portRect.bottom || chrome.left < nativeWindow->portRect.left
            || chrome.right > nativeWindow->portRect.right)
        {
          failureMessage = "dirty replay probe found invalid EditText chrome bounds";
          return false;
        }

        // EditText layout insets textRect.top by two pixels. Damage only the
        // one-pixel chrome row so replay has to gate on the extent it frames.
        Rect row = chrome;
        row.bottom = static_cast<short>(row.top + 1);
        unsigned char savedPixels[kDirtyReplayProbeMaxWidth];
        GrafPtr previousPort = 0;
        GetPort(&previousPort);
        SetPort(nativeWindow);
        CapturePixelRow(row, savedPixels);
        EraseRect(&row);
        const bool damageObserved = !PixelRowMatches(row, savedPixels);
        SetPort(previousPort);
        if (!damageObserved)
        {
          failureMessage = "dirty replay probe positive control did not change pixels";
          return false;
        }

        toolboxWindow->drawDirty(row);

        SetPort(nativeWindow);
        const bool replayRestoredPixels = PixelRowMatches(row, savedPixels);
        SetPort(previousPort);
        if (!replayRestoredPixels)
        {
          failureMessage = "dirty replay did not restore the EditText chrome row";
          return false;
        }
        failureMessage = 0;
        return true;
      }

      bool IsSuccessfulRecord(const dsl::SnapRecord &record)
      {
        std::string status;
        return record.get("status", status) && status == dsl::SnapStatusOk();
      }

      void SetDirtyReplayProbeFailure(dsl::SnapRecord &record,
                                      int code,
                                      const char *step,
                                      const char *node,
                                      const char *failureMessage)
      {
        record = scenario_tests::MakeHelloWorldDriverErrorRecord(code, failureMessage);
        record.set("step", step);
        record.set("node", node);
      }

      class HelloWorldScenarioAppConfig : public scenario_tests::HelloWorldScenarioPresentation
      {
      public:
        HelloWorldScenarioAppConfig(PlatformContext *context, const dsl::SnapTestConfig::Settings &settings)
            : scenario_tests::HelloWorldScenarioPresentation(context, HelloWorldMenuSeed::FromWallClock(0x13579BDFUL)),
              startup_(scenario_tests::IsStartupScenario(settings.scenario)),
              audit_(ResolveScenarioAuditFile(), settings.scenario.c_str()),
              startupScenario_(scenario_tests::STARTUP_EXAMPLE_HELLO_WORLD,
                               scenario_tests::SCENARIO_COMPLETION_DRIVER_OWNED,
                               &this->audit_),
              scenario_(settings.scenario,
                        scenario_tests::SCENARIO_COMPLETION_DRIVER_OWNED,
                        &this->audit_),
              borrowedApp_(0),
              recorded_(false),
              tickCount_(0),
              lingerRemaining_(settings.hasLingerSeconds ? static_cast<double>(settings.lingerSeconds) : 0.0),
              completionPublisher_()
        {
        }

        virtual ~HelloWorldScenarioAppConfig()
        {
          if (this->startup_)
          {
            this->startupScenario_.stop();
          }
          else
          {
            this->scenario_.stop();
          }
        }

        void setApp(App *app)
        {
          this->borrowedApp_ = app;
        }

      private:
        virtual void onScenarioIdle(Window *window, double elapsedSeconds)
        {
          this->tick(window, elapsedSeconds);
        }

        void tick(Window *window, double elapsedSeconds)
        {
          ++this->tickCount_;
          if (!this->recorded_)
          {
            dsl::SnapRecord record;
            bool done = false;
            if (!window || !window->scene())
            {
              record = this->startup_ ? scenario_tests::MakeStartupDriverErrorRecord(
                                            scenario_tests::STARTUP_EXAMPLE_HELLO_WORLD, 2802, "Scene was not mounted")
                                      : scenario_tests::MakeHelloWorldDriverErrorRecord(2402, "Scene was not mounted");
              done = true;
            }
            else
            {
              const scenario_tests::CaptureContentBounds captureBounds = QueryCaptureContentBounds(window);
              const scenario_tests::ScenarioAdvance advance =
                  this->startup_ ? this->startupScenario_.step(
                                       this->tickCount_, window->scene(), ContentLocalBounds(captureBounds), record)
                                 : this->scenario_.step(
                                       this->tickCount_, window->scene(), ContentLocalBounds(captureBounds), record);
              switch (advance)
              {
              case scenario_tests::SCENARIO_ADVANCE_PENDING:
                break;
              case scenario_tests::SCENARIO_ADVANCE_DRIVER_COMPLETION_READY:
                done = true;
                break;
              case scenario_tests::SCENARIO_ADVANCE_FINAL_SCENE_HELD:
                return;
              }
            }
            if (done)
            {
              if (!this->startup_ && this->scenario_.name() == kBmiRoundtripScenarioName
                  && IsSuccessfulRecord(record) && DirtyReplayProbeRequested(kConfigPath))
              {
                // Success leaves the completed portable record untouched;
                // only a failed rail-local probe replaces the verdict.
                const char *probeFailure = 0;
                if (!ProbeBmiHeightDirtyReplay(window, probeFailure))
                {
                  SetDirtyReplayProbeFailure(
                      record,
                      2404,
                      kBmiRoundtripScenarioName,
                      BmiHeightInputSelector().describe().c_str(),
                      probeFailure);
                }
                else if (!ProbeBmiHeightStateSync(window, probeFailure))
                {
                  SetDirtyReplayProbeFailure(
                      record,
                      2406,
                      kBmiRoundtripScenarioName,
                      BmiHeightInputSelector().describe().c_str(),
                      probeFailure);
                }
              }
              if (this->startup_)
              {
                (void)this->startupScenario_.publishVerdict(record);
              }
              else
              {
                (void)this->scenario_.publishVerdict(record);
              }
              this->recorded_ = true;
              (void)this->completionPublisher_.publish(window);
            }
          }
          if (!this->recorded_)
          {
            return;
          }
          this->lingerRemaining_ -= elapsedSeconds;
          if (this->lingerRemaining_ <= 0.0 && this->borrowedApp_)
          {
            this->borrowedApp_->quit();
          }
        }

        const bool startup_;
        dsl::testing::ScenarioAuditFile audit_;
        scenario_tests::StartupScenario startupScenario_;
        scenario_tests::HelloWorldScenario scenario_;
        App *borrowedApp_;
        bool recorded_;
        long tickCount_;
        double lingerRemaining_;
        ScenarioCompletionPublisher completionPublisher_;
      };
    } // namespace

    int RunHelloWorldScenarioApplication()
    {
      dsl::SnapTestConfig::Settings settings;
      const bool configLoaded = dsl::SnapTestConfig::load(kConfigPath, settings);
      if (!configLoaded)
      {
        (void)WriteScenarioErrorAudit(
            kDefaultScenarioName,
            scenario_tests::MakeHelloWorldDriverErrorRecord(2400, "LokaTest.cfg is missing or invalid"));
        return 0;
      }
      if (!settings.hasScenario
          || (!scenario_tests::IsStartupScenario(settings.scenario)
              && !scenario_tests::IsHelloWorldScenario(settings.scenario)))
      {
        (void)WriteScenarioErrorAudit(
            settings.hasScenario ? settings.scenario.c_str() : kDefaultScenarioName,
            scenario_tests::MakeHelloWorldDriverErrorRecord(2401, "scenario is missing or not registered"));
        return 0;
      }

      platform::InitPlatformRuntime();
      core::ScopedPtr<PlatformContext> platformContext(platform::CreatePlatformContext());
      assert(platformContext.get() && "PlatformContext is required");
      if (!platformContext.get())
      {
        return 1;
      }
      HelloWorldScenarioAppConfig config(platformContext.get(), settings);
      core::ScopedPtr<App> app(platformContext->createApp(&config, 0, 0));
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
