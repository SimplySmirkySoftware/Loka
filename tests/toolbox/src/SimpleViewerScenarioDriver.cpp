#include "SimpleViewerScenarioDriver.hpp"

#include <cassert>
#include <MacMemory.h>

#include "MyAppConfig.hpp"
#include "ObservedMainDefinition.hpp"
#include "ScenarioDriverSupport.hpp"
#include "ToolboxFileChoice.hpp"
#include "ToolboxScenePlatformController.hpp"
#include "ToolboxWindow.hpp"
#include "app/bootstrap/PlatformBootstrap.hpp"
#include "app/core/App.hpp"
#include "core/util/ScopedPtr.hpp"
#include "platform/StringUTF8.hpp"
#include "testing/scene/ScenarioAudit.hpp"
#include "testing/scene/SceneTestFlow.hpp"

/** Test-only access to the production menu, chooser result and completed image.
    No dialog is mounted; the same ImageLoadSession receives the same result. */
class SimpleViewerTestAccess
{
public:
  static simpleviewer::MainProps props(SimpleViewerAppConfig &config)
  {
    return simpleviewer::MainProps()
        .platformContext(config.getPlatformContext())
        .openDialogEvent(&config.openDialogEvent_);
  }

  static void open(simpleviewer::MainNode &node, const loka::app::FileChooserResult &result)
  {
    loka::core::StateTrackerGuard guard(node.tracker());
    node.imageLoad_.begin(node, node.props.platformContext(), node.chooserResult_.state(),
                         static_cast<loka::core::PushStateTracker *>(node.tracker()));
    node.chooserResult_.set(result, true);
  }

  static void showActualScrolling(simpleviewer::MainNode &node)
  {
    loka::core::StateTrackerGuard guard(node.tracker());
    node.showActualScrolling();
  }

  static bool isActualScrolling(const simpleviewer::MainNode &node)
  {
    return node.displayMode_.get() == simpleviewer::DISPLAY_ACTUAL_SCROLL;
  }

  /** Failure diagnostics only: what the session left behind. */
  static void diagnose(const simpleviewer::MainNode &node, loka::dsl::SnapRecord &record)
  {
    std::string message;
    (void)loka::platform::CollectUtf8(node.chooserMessage_.get(), message);
    record.set("diag.message", message.c_str());
    record.setInt("diag.image_valid", node.image_.get().isValid() ? 1 : 0);
    record.setInt("diag.flow_valid", node.imageLoad_.flow_.isValid() ? 1 : 0);
  }

  enum LoadOutcome
  {
    LOAD_PENDING,
    LOAD_FAILED,
    LOAD_SUCCEEDED
  };

  static LoadOutcome capture(const simpleviewer::MainNode &node, const loka::file::File &requested,
                      loka::dsl::SnapRecord &record)
  {
    // A retained image is not the result of the outstanding request.
    if (node.imageLoad_.flow_.isValid()) return LOAD_PENDING;
    const loka::core::resource::Image &image = node.image_.get();
    record.setInt("image.width", image.width());
    record.setInt("image.height", image.height());
    // The production session consumes handled errors and releases its Flow.
    // Decode its completed message using the production formatter, not a
    // second error-message table or a callback that changes Flow lifetime.
    using namespace simpleviewer;
    const SimpleViewerFlowErrorCode codes[] = {
      SIMPLE_VIEWER_FLOW_ERROR_CODE_PLATFORM_CONTEXT_MISSING,
      SIMPLE_VIEWER_FLOW_ERROR_CODE_IMAGE_DECODE_FAILED,
      SIMPLE_VIEWER_FLOW_ERROR_CODE_FILE_READ_FAILED,
      SIMPLE_VIEWER_FLOW_ERROR_CODE_NO_FILE_SELECTED,
      SIMPLE_VIEWER_FLOW_ERROR_CODE_PLATFORM_OPENFILE_FAILED,
      SIMPLE_VIEWER_FLOW_ERROR_CODE_CLASSIC_NO_FSSPEC,
      SIMPLE_VIEWER_FLOW_ERROR_CODE_CLASSIC_OPEN_DF_FAILED,
      SIMPLE_VIEWER_FLOW_ERROR_CODE_CLASSIC_GETEOF_FAILED,
      SIMPLE_VIEWER_FLOW_ERROR_CODE_CLASSIC_READ_FAILED,
      SIMPLE_VIEWER_FLOW_ERROR_CODE_STDIO_OPEN_FAILED,
      SIMPLE_VIEWER_FLOW_ERROR_CODE_STDIO_SEEK_FAILED,
      SIMPLE_VIEWER_FLOW_ERROR_CODE_STDIO_READ_FAILED,
      SIMPLE_VIEWER_FLOW_ERROR_CODE_IMAGE_LOAD_REQUIRES_RELEASE
    };
    for (unsigned i = 0; i < sizeof(codes) / sizeof(codes[0]); ++i)
    {
      loka::dsl::FlowError error;
      error.code = codes[i];
      if (node.chooserMessage_.get().equals(ImageLoadSession::buildErrorMessage(error)))
      {
        record.setInt("image.load", codes[i]);
        return LOAD_FAILED;
      }
    }
    ChooserContext expected;
    loka::dsl::FlowError error;
    ChooserToContextAdapter().run(loka::app::FileChooserResult::File(requested), expected, error);
    if (image.isValid() && node.chooserMessage_.get().equals(expected.message))
    {
      record.set("image.load", "ok");
      return LOAD_SUCCEEDED;
    }
    return LOAD_PENDING;
  }
};

namespace loka
{
  namespace toolbox_tests
  {
    namespace
    {
      dsl::SnapRecord MakeRecord(const char *scenario, long tick, const char *status)
      {
        dsl::SnapRecord record;
        dsl::FlowError error;
        dsl::BuildSnapV1RecordAdapter("SimpleViewer", scenario, "SimpleViewer.Image", tick, 1, status)
            .run(0, record, error);
        return record;
      }

      /** Immutable cell parameters; churn counts replacements after the initial Sun. */
      struct ImageCell
      {
        const char *name;
        const char *firstPicture;
        unsigned churnReplacements;
        bool scrollAfterLoad;

        unsigned loadCount() const { return this->churnReplacements ? this->churnReplacements + 2 : 1; }
        const char *picture(unsigned number) const
        {
          return this->churnReplacements ? (number % 2 ? "Sun.pict" : "Bulb.pict") : this->firstPicture;
        }
      };

      const ImageCell *FindImageCell(const std::string &name)
      {
        static const ImageCell cells[] = {{"open-sun", "Sun.pict", 0, false},
                                          {"open-bulb", "Bulb.pict", 0, false},
                                          {"churn-replace", "Sun.pict", 8, false},
                                          {"open-wide-scroll", "Wide.pict", 0, true}};
        for (unsigned i = 0; i < sizeof(cells) / sizeof(cells[0]); ++i)
          if (name == cells[i].name) return &cells[i];
        return 0;
      }

      /** One issued attempt, replaced as a value only after its own result is captured.
          Number zero means no request; no Image/Flow lifetime is extended. */
      class PendingImageLoad
      {
      public:
        PendingImageLoad() : number_(0), issuedTick_(0), chosen_() {}
        PendingImageLoad(unsigned number, long tick, const char *picture)
            : number_(number), issuedTick_(tick), chosen_(picture) {}
        unsigned nextNumber() const { return this->number_ + 1; }
        bool isLast(const ImageCell &cell) const { return this->number_ == cell.loadCount(); }
        bool expired(long tick) const { return tick >= this->issuedTick_ + 60; }
        const file::File &chosen() const { return this->chosen_; }
      private:
        unsigned number_;
        long issuedTick_;
        file::File chosen_;
      };

      /** Measurement owner; the borrowed MainNode is used only during idle
          while its App-owned Window/Scene are alive, never during teardown. */
      class SimpleViewerScenarioAppConfig : public SimpleViewerAppConfig
      {
      public:
        SimpleViewerScenarioAppConfig(PlatformContext *context, const dsl::SnapTestConfig::Settings &settings)
            : SimpleViewerAppConfig(context), scenario_(settings.scenario),
              audit_(ResolveScenarioAuditFile(), settings.scenario.c_str()), terminal_(&this->audit_),
              borrowedApp_(0), borrowedMain_(0), tick_(0),
              lingerRemaining_(settings.hasLingerSeconds ? settings.lingerSeconds : 0.0),
              loadFacts_(), pendingLoad_(), completionPublisher_()
        {
        }

        void setApp(App *app) { this->borrowedApp_ = app; }

        virtual void compose(AppComposition &composition)
        {
          // Presentation twin of SimpleViewerAppConfig::compose. Move the
          // production 480x280 content below the rig's menu/title bars.
          scenario_tests::ObservedMainDefinition<simpleviewer::MainProps, simpleviewer::MainNode> mainDefinition(
              SimpleViewerTestAccess::props(*this), &this->borrowedMain_);
          composition << WindowDef(WindowProps().frame(16, 41, 480, 280)
                                       .scene(mainDefinition).title("LokaSimpleViewer").visible(true)
                                       .idlePolicy(app::IdlePolicy::everyTick())
                                       .onIdle(&SimpleViewerScenarioAppConfig::OnIdle, this));
        }

      private:
        static void OnIdle(Window *window, double elapsedSeconds, void *userData)
        {
          static_cast<SimpleViewerScenarioAppConfig *>(userData)->tick(window, elapsedSeconds);
        }

        bool recordStep(const char *name)
        {
          return this->audit_.recordStep(dsl::testing::ScenarioStepTerminal(
              static_cast<int>(this->tick_), name, this->tick_, this->tick_,
              dsl::FLOW_STEP_SUCCEEDED, dsl::FlowError()));
        }

        bool openImage(const ImageCell &cell)
        {
          const unsigned number = this->pendingLoad_.nextNumber();
          const PendingImageLoad pending(number, this->tick_, cell.picture(number));
          const file::File &chosen = pending.chosen();
          const file::File item = file::File::Application() << chosen;
          platform::file::FileHandle handle;
          if (!this->getPlatformContext()->openFile(item, handle) || !handle.hasSpec) return false;
          short refNum = 0;
          if (FSpOpenDF(&handle.spec, fsRdPerm, &refNum) != noErr) return false;
          long bytes = 0;
          const OSErr sizeError = GetEOF(refNum, &bytes);
          const OSErr closeError = FSClose(refNum);
          if (sizeError != noErr || closeError != noErr || bytes < 0) return false;
          file::File captured;
          if (!ToolboxCaptureChosenFile(handle.spec, captured)) return false;
          const app::FileChooserResult result = app::FileChooserResult::File(captured);
          // Sample before the flow allocates. Record construction happens
          // afterwards so it cannot perturb the measured pre-load heap.
          const long freeBytes = FreeMem();
          std::size_t maxBlock = 0;
          if (!this->getPlatformContext()->queryLargestContiguousAllocation(maxBlock)) return false;
          this->pendingLoad_ = pending;
          SimpleViewerTestAccess::open(*this->borrowedMain_, result);
          (void)freeBytes;
          // Raw heap numbers move with the application's code size; keep the
          // fact #614 turns on. The numbers themselves go to the PR body.
          this->loadFacts_.set("heap.probe_covers_image",
                               maxBlock >= static_cast<std::size_t>(bytes) ? "yes" : "no");
          this->loadFacts_.setInt("image.bytes", bytes);
          return this->recordStep("open-image");
        }

        void fail(Window *window)
        {
          dsl::SnapRecord record = MakeRecord(this->scenario_.c_str(), this->tick_, dsl::SnapStatusError());
          if (this->borrowedMain_) SimpleViewerTestAccess::diagnose(*this->borrowedMain_, record);
          (void)this->terminal_.emit(dsl::testing::SCENARIO_AUDIT_FAILED, record);
          (void)this->completionPublisher_.publish(window);
        }

        void tick(Window *window, double elapsedSeconds)
        {
          if (this->terminal_.isSettled())
          {
            this->lingerRemaining_ -= elapsedSeconds;
            if (this->lingerRemaining_ <= 0.0 && this->borrowedApp_) this->borrowedApp_->quit();
            return;
          }
          if (!window || !window->scene() || !this->borrowedMain_)
          {
            this->fail(window);
            return;
          }
          app::scene::Scene *scene = window->scene();
          ToolboxScenePlatformController *controller = static_cast<ToolboxScenePlatformController *>(
              dsl::testing::SceneTestAccess::platformController(*scene));
          if (!controller) { this->fail(window); return; }
          // Same settled-presentation wall as SmirkBench: retirement alone
          // does not account for pending Toolbox paint.
          if (scene->hasPendingInvalidation() || controller->hasPendingSync()
              || window->asToolboxWindow()->hasPendingInvalidate()) return;
          ++this->tick_;
          if (this->tick_ < 2) return;
          if (this->tick_ == 2 && this->scenario_ == "startup")
          {
            // The startup cell captures the settled viewer without opening
            // anything: it is the bundle's per-example startup golden and the
            // heap facts before any load.
            dsl::SnapRecord record = MakeRecord(this->scenario_.c_str(), this->tick_, dsl::SnapStatusOk());
            const long freeBytes = FreeMem();
            std::size_t maxBlock = 0;
            if (!this->getPlatformContext()->queryLargestContiguousAllocation(maxBlock)) { this->fail(window); return; }
            (void)freeBytes;
            (void)maxBlock;
            // Raw heap numbers move with the application's code size, so the
            // audit keeps only the fact #614 turns on: whether the platform's
            // capacity probe covers the picture (no picture in this cell).
            record.set("heap.probe_covers_image", "n/a");
            record.setInt("image.bytes", 0);
            record.setInt("image.width", 0);
            record.setInt("image.height", 0);
            record.set("image.load", "none");
            record.set("rail", "toolbox");
            record.set("checkpoint", "post-settle");
            scenario_tests::SetContentBounds(record, ContentLocalBounds(QueryCaptureContentBounds(window)));
            if (!this->recordStep("post-settle") || !this->audit_.recordVerdict(record)) { this->fail(window); return; }
            (void)this->terminal_.emit(dsl::testing::SCENARIO_AUDIT_SUCCEEDED);
            (void)this->completionPublisher_.publish(window);
            return;
          }
          const ImageCell *cell = FindImageCell(this->scenario_);
          if (!cell) { this->fail(window); return; }
          if (this->tick_ == 2)
          {
            if (!this->recordStep("post-settle") || !this->openImage(*cell)) this->fail(window);
            return;
          }
          dsl::SnapRecord record = MakeRecord(this->scenario_.c_str(), this->tick_, dsl::SnapStatusOk());
          const char *keys[] = {"heap.probe_covers_image", "image.bytes"};
          for (unsigned i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i)
          {
            std::string value;
            if (!this->loadFacts_.get(keys[i], value)) { this->fail(window); return; }
            record.set(keys[i], value.c_str());
          }
          record.set("rail", "toolbox");
          // The production mode is the phase: no duplicate pending-scroll flag.
          const bool postScroll =
              cell->scrollAfterLoad && SimpleViewerTestAccess::isActualScrolling(*this->borrowedMain_);
          const char *checkpoint = postScroll ? "post-scroll" : "post-open";
          record.set("checkpoint", checkpoint);
          if (postScroll)
            record.set("display.mode", "actual-scroll");
          scenario_tests::SetContentBounds(record, ContentLocalBounds(QueryCaptureContentBounds(window)));
          // The production session advances its Flow over later settled turns;
          // wait for this request to finish, with a fresh bound for each load.
          const SimpleViewerTestAccess::LoadOutcome outcome =
              SimpleViewerTestAccess::capture(*this->borrowedMain_, this->pendingLoad_.chosen(), record);
          if (outcome == SimpleViewerTestAccess::LOAD_PENDING)
          {
            if (cell->churnReplacements ? !this->pendingLoad_.expired(this->tick_) : this->tick_ < 60) return;
            this->fail(window);
            return;
          }
          if (!this->recordStep(checkpoint) || !this->audit_.recordVerdict(record))
          {
            this->fail(window);
            return;
          }
          if (cell->scrollAfterLoad)
          {
            if (outcome != SimpleViewerTestAccess::LOAD_SUCCEEDED)
            {
              this->fail(window);
              return;
            }
            if (!postScroll)
            {
              SimpleViewerTestAccess::showActualScrolling(*this->borrowedMain_);
              return;
            }
          }
          if (cell->churnReplacements)
          {
            if (outcome != SimpleViewerTestAccess::LOAD_SUCCEEDED)
            {
              this->fail(window);
              return;
            }
            if (!this->pendingLoad_.isLast(*cell))
            {
              if (!this->openImage(*cell)) this->fail(window);
              return;
            }
          }
          (void)this->terminal_.emit(dsl::testing::SCENARIO_AUDIT_SUCCEEDED);
          (void)this->completionPublisher_.publish(window);
        }

        const std::string scenario_;
        dsl::testing::ScenarioAuditFile audit_;
        dsl::testing::scenario_audit_detail::TerminalEmitter terminal_;
        App *borrowedApp_;
        simpleviewer::MainNode *borrowedMain_;
        long tick_;
        double lingerRemaining_;
        dsl::SnapRecord loadFacts_;
        PendingImageLoad pendingLoad_;
        ScenarioCompletionPublisher completionPublisher_;
      };
    } // namespace

    int RunSimpleViewerScenarioApplication()
    {
      dsl::SnapTestConfig::Settings settings;
      if (!dsl::SnapTestConfig::load("LokaTest.cfg", settings) || !settings.hasScenario
          || (settings.scenario != "startup" && !FindImageCell(settings.scenario)))
      {
        (void)WriteScenarioErrorAudit("startup", MakeRecord("startup", 0, dsl::SnapStatusError()));
        return 0;
      }
      platform::InitPlatformRuntime();
      core::ScopedPtr<PlatformContext> context(platform::CreatePlatformContext());
      assert(context.get() && "PlatformContext is required");
      if (!context.get()) return 1;
      SimpleViewerScenarioAppConfig config(context.get(), settings);
      core::ScopedPtr<App> app(context->createApp(&config, 0, 0));
      assert(app.get() && "App is required");
      if (!app.get()) return 1;
      config.setApp(app.get());
      app->run();
      return 0;
    }
  } // namespace toolbox_tests
} // namespace loka
