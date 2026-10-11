#include "StandaloneScenarioSupport.hpp"

#include <cstdio>

#include "app/core/App.hpp"
#include "app/core/Window.hpp"
#include "core/io/File.hpp"
#include "platform/file/AppLocation.hpp"

namespace loka
{
  namespace standalone_tests
  {
    scenario_tests::CaptureContentBounds StandaloneContentBounds(Window *window)
    {
      scenario_tests::CaptureContentBounds result;
      if (!window)
      {
        return result;
      }
      const core::Frame frame = window->frameState().get();
      if (frame.width <= 0 || frame.height <= 0)
      {
        return result;
      }
      result.available = true;
      result.right = frame.width;
      result.bottom = frame.height;
      return result;
    }

    platform::file::FileHandle ResolveStandaloneAuditFile()
    {
      platform::file::FileHandle result;
      if (!platform::file::ResolveApplicationSidecar(file::File::Application() << file::File("LOG.TXT"), result))
      {
        return platform::file::FileHandle();
      }
      return result;
    }

    const char *StandaloneAuditRefusal(const platform::file::FileHandle &auditFile)
    {
      if (!auditFile.displayPath.empty())
      {
        return 0;
      }
      return "The audit sidecar LOG.TXT could not be resolved beside the application. "
             "Move the application to a writable folder (for example, out of Downloads into Applications), "
             "or remove the quarantine attribute, then try again.";
    }

    StandaloneRunControl::StandaloneRunControl(const char *applicationName,
                                               const scenario_tests::ScenarioCellTable &cells,
                                               std::FILE *diagnostics,
                                               CompletionMode completionMode)
        : borrowedApp_(0),
          applicationName_(applicationName ? applicationName : "application"),
          diagnostics_(diagnostics ? diagnostics : stderr),
          completionMode_(completionMode),
          position_(cells, 0),
          operatorTitle_(position_.cell(), position_.completedCycles()),
          tick_(0),
          mountFailed_(false),
          completed_(false)
    {
    }

    void StandaloneRunControl::setApp(App *app)
    {
      this->borrowedApp_ = app;
#if LOKA_STANDALONE_PERFORMANCE_RUNS > 0
      if (app)
      {
        this->scenarioVerdict_.begin();
      }
#endif
    }

    core::State<core::String> *StandaloneRunControl::displayTitleState(const char *productionTitle)
    {
      this->operatorTitle_.decorateBeforeProjection(
          productionTitle ? core::String::Literal(productionTitle) : core::String());
      return this->operatorTitle_.state();
    }

    StandaloneRunControl::Advance StandaloneRunControl::advance(bool mainNodeMounted)
    {
      if (this->mountFailed_)
      {
        return ADVANCE_FAILED;
      }
      ++this->tick_;
      if (mainNodeMounted)
      {
        return ADVANCE_MOUNTED;
      }
      if (this->tick_ < MOUNT_DEADLINE_TICKS)
      {
        return ADVANCE_WAITING;
      }

      this->mountFailed_ = true;
      std::fprintf(this->diagnostics_,
                   "Loka %s standalone startup failed: "
                   "MainNode was not mounted after %d idle ticks.\n",
                   this->applicationName_,
                   MOUNT_DEADLINE_TICKS);
      std::fflush(this->diagnostics_);
      if (this->borrowedApp_)
      {
        this->borrowedApp_->quit();
      }
      return ADVANCE_FAILED;
    }

    bool StandaloneRunControl::observeScenarioAdvance(scenario_tests::ScenarioAdvance advance,
                                                      const dsl::SnapRecord &record,
                                                      Window *window)
    {
      if (window)
      {
        this->operatorTitle_.synchronizeProductionTitle(window->titleState().get(), window->getTracker());
      }
      switch (advance)
      {
      case scenario_tests::SCENARIO_ADVANCE_PENDING:
      case scenario_tests::SCENARIO_ADVANCE_DRIVER_COMPLETION_READY:
        return false;
      case scenario_tests::SCENARIO_ADVANCE_FINAL_SCENE_HELD:
        break;
      }
#if LOKA_STANDALONE_PERFORMANCE_RUNS > 0
      this->scenarioVerdict_.observe(record);
#else
      (void)record;
#endif
      this->completed_ = true;
      if (this->completionMode_ != HOLD_FINAL_SCENE && this->borrowedApp_)
      {
        if (this->completionMode_ == QUIT_COMPLETED_PASS)
        {
          this->borrowedApp_->quit();
        }
      }
      return this->completionMode_ == REARM_COMPLETED_SCENE;
    }

    const char *StandaloneRunControl::nextScenarioName() const
    {
      scenario_tests::ScenarioReelPosition next(this->position_);
      next.advance();
      return next.cell();
    }

    void StandaloneRunControl::completeSceneRearm(bool succeeded, Window *window)
    {
      assert(this->completionMode_ == REARM_COMPLETED_SCENE && "Scene re-arm is a loop-only completion");
      assert(this->completed_ && "Scene re-arm follows a completed scenario");
      if (succeeded)
      {
        assert(window && "A successful Scene re-arm has a Window owner");
        this->position_.advance();
        this->operatorTitle_.publish(
            this->position_.cell(), this->position_.completedCycles(), window ? window->getTracker() : 0);
        this->tick_ = 0;
        this->completed_ = false;
        return;
      }
      this->mountFailed_ = true;
      std::fprintf(this->diagnostics_,
                   "Loka %s standalone loop failed: "
                   "the next scenario rail or Scene could not be prepared.\n",
                   this->applicationName_);
      std::fflush(this->diagnostics_);
      if (this->borrowedApp_)
      {
        this->borrowedApp_->quit();
      }
    }

    long StandaloneRunControl::tick() const
    {
      return this->tick_;
    }

    bool StandaloneRunControl::failed() const
    {
#if LOKA_STANDALONE_PERFORMANCE_RUNS > 0
      return this->mountFailed_ || this->scenarioVerdict_.refusesCompletedPass();
#else
      return this->mountFailed_;
#endif
    }

  } // namespace standalone_tests
} // namespace loka
