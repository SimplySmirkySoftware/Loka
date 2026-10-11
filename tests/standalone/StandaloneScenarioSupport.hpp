#ifndef LOKA_TESTS_STANDALONE_SCENARIO_SUPPORT_HPP
#define LOKA_TESTS_STANDALONE_SCENARIO_SUPPORT_HPP

#include <cstdio>
#include <new>

#include "../scenarios/ScenarioReel.hpp"
#include "StandalonePerformanceConfig.hpp"
#if LOKA_STANDALONE_PERFORMANCE_RUNS > 0
#include "StandalonePerformance.hpp"
#endif
#include "platform/file/FileHandle.hpp"
#include "core/util/ScopedPtr.hpp"

class App;
class Window;

namespace loka
{
  namespace standalone_tests
  {
    /** Reports a standalone Window's content-local bounds when available. */
    scenario_tests::CaptureContentBounds StandaloneContentBounds(Window *window);

    /** Resolves the application-side audit file shared by presentation apps. */
    platform::file::FileHandle ResolveStandaloneAuditFile();

    /** Returns the startup refusal text for an unresolved audit sidecar, or null
        for a resolved location. This does not diagnose later open/write failures. */
    const char *StandaloneAuditRefusal(const platform::file::FileHandle &auditFile);

    /** Shows a modal startup refusal before an App exists (native entry seam). */
    void ShowStandaloneAuditRefusal(const char *message);

    /** Owns the bounded startup wait and terminal App control shared by the
        TEST-only Standalone Flow presentations. A refused MainNode mount
        produces one diagnostic, requests shutdown, and remains terminal. */
    class StandaloneRunControl
    {
    public:
      enum Advance
      {
        ADVANCE_WAITING,
        ADVANCE_MOUNTED,
        ADVANCE_FAILED
      };

      enum CompletionMode
      {
        HOLD_FINAL_SCENE = 0,
        QUIT_COMPLETED_PASS,
        REARM_COMPLETED_SCENE
      };

#if LOKA_STANDALONE_PERFORMANCE_RUNS > 0
      static const CompletionMode CONFIGURED_COMPLETION_MODE = QUIT_COMPLETED_PASS;
#elif LOKA_STANDALONE_FLOW_LOOP
      static const CompletionMode CONFIGURED_COMPLETION_MODE = REARM_COMPLETED_SCENE;
#else
      static const CompletionMode CONFIGURED_COMPLETION_MODE = HOLD_FINAL_SCENE;
#endif

      explicit StandaloneRunControl(
          const char *applicationName,
          const scenario_tests::ScenarioCellTable &cells,
          std::FILE *diagnostics = 0,
          CompletionMode completionMode = CONFIGURED_COMPLETION_MODE);

      void setApp(App *app);
      /** Existing App borrow used to admit a scenario rearm between steps. */
      App *app() const { return this->borrowedApp_; }
      /** Decorates the application title and returns the existing reel-owned
          read-only native projection. Call while constructing the Window. */
      core::State<core::String> *displayTitleState(const char *productionTitle);
      Advance advance(bool mainNodeMounted);
      /** Returns true when the loop build must replace its completed rail and
          re-arm the Scene. The App and native Window remain alive. */
      bool observeScenarioAdvance(scenario_tests::ScenarioAdvance advance,
                                  const dsl::SnapRecord &record,
                                  Window *window);
      /** Returns the next registered cell without consuming the current one. */
      const char *nextScenarioName() const;
      /** Commits a successful Scene re-arm as the next pass, or terminates a
          loop whose replacement could not be prepared. */
      void completeSceneRearm(bool succeeded, Window *window);
      long tick() const;
      bool failed() const;

    private:
      enum
      {
        MOUNT_DEADLINE_TICKS = 5
      };

      App *borrowedApp_;
      const char *applicationName_;
      std::FILE *diagnostics_;
      const CompletionMode completionMode_;
      scenario_tests::ScenarioReelPosition position_;
      scenario_tests::ScenarioReelTitle operatorTitle_;
      long tick_;
      bool mountFailed_;
      bool completed_;
#if LOKA_STANDALONE_PERFORMANCE_RUNS > 0
      StandaloneScenarioVerdict scenarioVerdict_;
#endif
    };

    /** Owns one scenario rail and replaces it failure-atomically between
        Scene passes. The old rail stops before its observed nodes detach. */
    template <class ScenarioT> class StandaloneScenarioRail
    {
    public:
      explicit StandaloneScenarioRail(ScenarioT *scenario)
          : scenario_(scenario)
      {
      }

      ~StandaloneScenarioRail()
      {
        this->stop();
      }

      bool isValid() const
      {
        return this->scenario_.get() != 0;
      }

      ScenarioT *operator->() const
      {
        return this->scenario_.get();
      }

      bool replace(ScenarioT *candidate)
      {
        core::ScopedPtr<ScenarioT> replacement(candidate);
        if (!replacement.get())
        {
          return false;
        }
        this->stop();
        this->scenario_.reset(replacement.release());
        return true;
      }

      /** Installs a prepared rail before re-arming the Scene it will observe.

          A refused candidate leaves both the live rail and Scene untouched;
          a successful replacement stops the old rail before Scene teardown. */
      bool replaceAndRearmScene(ScenarioT *candidate, Window *window, App *app)
      {
        return this->replace(candidate) && scenario_tests::RearmScenarioScene(window, app);
      }

      void stop()
      {
        if (this->scenario_.get())
        {
          this->scenario_->stop();
        }
      }

    private:
      core::ScopedPtr<ScenarioT> scenario_;

      StandaloneScenarioRail(const StandaloneScenarioRail &);
      StandaloneScenarioRail &operator=(const StandaloneScenarioRail &);
    };
  } // namespace standalone_tests
} // namespace loka

#endif // LOKA_TESTS_STANDALONE_SCENARIO_SUPPORT_HPP
