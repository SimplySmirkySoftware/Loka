#include <cstdio>
#include <cstdarg>
#include <Processes.h>

#include "StandaloneFlowRunner.hpp"
#include "ToolboxProbeTiming.hpp"
#include "ToolboxScenePlatformController.hpp"
#include "ToolboxWindow.hpp"
#include "app/core/AppComposition.hpp"
#include "app/core/AppConfigurable.hpp"
#include "app/nodes/boundary/StdComposition.hpp"
#include "app/nodes/controls/Ribbon.hpp"
#include "app/nodes/controls/TextEditor.hpp"
#include "app/nodes/nestable/RowColumn.hpp"
#include "app/nodes/Text.hpp"
#include "core/io/File.hpp"
#include "core/util/StateTrackerGuard.hpp"
#include "platform/file/AppLocation.hpp"
#include "platform/file/FileIO.hpp"
#include "testing/scene/SceneTestFlow.hpp"

namespace
{
  using namespace loka::app;
  using namespace loka::app::scene;
  using namespace loka::core;
  using loka_toolbox_probe::Timer;
  const int kKeys = 32;

  /** Config-owned durable log; each record commits stdio and the HFS volume. */
  class ProbeLog
  {
  public:
    ProbeLog()
        : stream_(0),
          good_(false)
    {
      if (loka::platform::file::ResolveApplicationSidecar(
              loka::file::File::Application() << loka::file::File("LOG.TXT"), this->file_))
        this->stream_ = loka::platform::file::OpenWriteTruncate(this->file_);
      if (this->stream_)
      {
        std::setvbuf(this->stream_, 0, _IONBF, 0);
        this->good_ = true;
        this->record("start ticks=%lu", TickCount());
      }
    }
    ~ProbeLog()
    {
      if (this->stream_)
        std::fclose(this->stream_);
    }
    bool good() const
    {
      return this->good_;
    }
    void record(const char *format, ...) __attribute__((format(printf, 2, 3)))
    {
      if (!this->good_)
        return;
      va_list args;
      va_start(args, format);
      std::vfprintf(this->stream_, format, args);
      va_end(args);
      std::fprintf(this->stream_, "\r");
      const bool flushed = loka::platform::file::FlushWrite(this->stream_, this->file_);
      this->good_ = flushed && !std::ferror(this->stream_);
    }

  private:
    ProbeLog(const ProbeLog &);
    ProbeLog &operator=(const ProbeLog &);
    loka::platform::file::FileHandle file_;
    std::FILE *stream_;
    bool good_;
  };

  class TypingNode;
  struct TypingProps : NodePropsBase<TypingProps>
  {
    typedef TypingNode NodeType;
    struct TypeTag
    {
    };
    bool grouped;
    ProbeLog *log;
    explicit TypingProps(bool value = false, ProbeLog *sink = 0)
        : grouped(value),
          log(sink)
    {
    }
    bool operator<(const PropsBase &rhs) const
    {
      return rhs.propsTypeId() == this->propsTypeId()
             && (this->grouped != static_cast<const TypingProps &>(rhs).grouped
                     ? this->grouped < static_cast<const TypingProps &>(rhs).grouped
                     : std::less<ProbeLog *>()(this->log, static_cast<const TypingProps &>(rhs).log));
    }
  };

  /** Owns the document and focus; the driver only borrows this mounted Boundary. */
  class TypingNode : public StdCompositionBoundaryNodeBase<TypingProps>
  {
  public:
    typedef TypingProps::TypeTag TypeTag;
    explicit TypingNode(const TypingProps &props)
        : StdCompositionBoundaryNodeBase<TypingProps>(props)
    {
      this->props.log->record("node construct begin group=%d", this->props.grouped);
      this->setTestId("Typing.Owner");
      this->state(this->cursor_, LineCursor::None());
      this->state(this->caret_, LineCursor::None());
      this->state(this->error_, String());
      this->state(this->focus_);
      this->props.log->record("node construct end group=%d", this->props.grouped);
    }
    virtual void attachNode(NodeComposition &)
    {
      this->props.log->record("attach begin group=%d", this->props.grouped);
      StateTracker *owner = 0;
      if (this->lines_.queryMutationTracker(owner) == EDIT_OK)
        return;
      {
        StateTrackerGuard transaction(this->tracker());
        if (this->lines_.attach(this->tracker()->asPushTracker(), TextEditorProps::kMaxLines) != ATTACH_OK
            || this->lines_.insert(0, String()) != EDIT_OK)
          this->lines_.detach();
      }
      this->props.log->record("attach end group=%d ready=%d", this->props.grouped, this->ready());
    }
    virtual void composeNode(NodeComposition &composition)
    {
      this->props.log->record("compose begin group=%d ready=%d", this->props.grouped, this->ready());
      if (!this->ready())
        return;
      RibbonControl band;
      if (this->props.grouped)
        band << (RibbonGroup("File") << RibbonItem("New") << RibbonItem("Open...") << RibbonItem("Save")
                                     << RibbonItem("Save As..."));
      else
        band << RibbonItem("New") << RibbonItem("Open...") << RibbonItem("Save") << RibbonItem("Save As...");
      composition.declare(
          VStack() << band << Text(this->error_.state())
                   << TextEditor(this->lines_, this->cursor_).moveCaretTo(this->caret_).focusedAs(this->focus_, 1));
      this->props.log->record("compose end group=%d", this->props.grouped);
    }
    bool ready() const
    {
      return this->lines_.size() == 1 && this->cursor_.isValid() && this->caret_.isValid() && this->error_.isValid()
             && this->focus_.isValid();
    }
    bool reset()
    {
      if (!this->ready())
        return false;
      StateTrackerGuard transaction(this->tracker());
      if (this->lines_.update(this->lines_.at(0).id, String()) != EDIT_OK)
        return false;
      this->caret_.set(LineCursor(this->lines_.at(0).id, 0));
      this->focus_.post(1);
      return true;
    }
    void requestFocus()
    {
      StateTrackerGuard transaction(this->tracker());
      this->focus_.post(1);
    }
    bool focused() const
    {
      return this->focus_.isValid() && this->focus_.state()->get().is(1);
    }
    bool characterCount(std::size_t &out) const
    {
      // All injected characters are ASCII x, so UTF-8 units equal characters.
      return this->lines_.size() == 1 && this->lines_.at(0).value.requiredUnits(StringEncodingUtf8, out);
    }

  private:
    ObservableList<String> lines_;
    Reported<LineCursor> cursor_;
    Request<LineCursor> caret_;
    NodeState<String> error_;
    Focus<int> focus_;
  };

  /** Factory observation follows WindowClamp; the App retains ownership. */
  class ObservedWindowDefinition : public WindowDefinition<WindowProps>
  {
  public:
    ObservedWindowDefinition(const WindowProps &props, ToolboxWindow **out, ProbeLog *log)
        : WindowDefinition<WindowProps>(props),
          out_(out),
          log_(log)
    {
    }
    virtual WindowDefinitionBase *clone() const
    {
      return new (std::nothrow) ObservedWindowDefinition(*this);
    }
    virtual Window *create(PlatformContext *context) const
    {
      this->log_->record("window create begin");
      Window *window = WindowDefinition<WindowProps>::create(context);
      *this->out_ = window ? window->asToolboxWindow() : 0;
      this->log_->record("window create end window=%d scene=%d idle=%d",
                         window != 0,
                         window && window->scene(),
                         static_cast<int>(this->props.idlePolicyValue.mode));
      return window;
    }

  private:
    ToolboxWindow **out_;
    ProbeLog *log_;
  };

  /** Finite event-loop driver. All borrows end before the App destroys its windows. */
  class ProbeConfig : public AppConfigurable
  {
  public:
    explicit ProbeConfig(PlatformContext *context)
        : AppConfigurable(context),
          app_(0),
          log_(),
          result_(0),
          phase_(SETUP),
          arm_(0),
          keys_(0),
          totalUs_(0),
          totalTicks_(0),
          keyStartUs_(0),
          keyStartTicks_(0),
          runStart_(TickCount()),
          lastDiagnostic_(runStart_ - 60),
          timer_(),
          before_()
    {
      this->windows_[0] = this->windows_[1] = 0;
      this->nodes_[0] = this->nodes_[1] = 0;
      if (!this->log_.good())
        this->result_ = 1;
    }
    virtual IdlePolicy idlePolicy() const
    {
      return IdlePolicy::everyTick();
    }
    virtual void onIdle(double)
    {
      this->step();
    }
    void setApp(App *app)
    {
      this->app_ = app;
      this->log_.record("app created; entering run next");
    }
    int exitCode() const
    {
      return this->result_;
    }
    virtual void compose(AppComposition &composition)
    {
      this->log_.record("app compose begin");
      for (int i = 0; i < 2; ++i)
        composition << ObservedWindowDefinition(
            WindowProps()
                .title(i ? "Typing group" : "Typing plain")
                .frame(20, 40, 500, 300)
                .visible(true)
                .scene(BoundaryDefinition<TypingProps, TypingNode>(TypingProps(i != 0, &this->log_)))
                .idlePolicy(IdlePolicy::everyTick()),
            &this->windows_[i],
            &this->log_);
      this->log_.record("app compose end");
    }

  private:
    enum Phase
    {
      SETUP,
      SETTLING,
      KEY_DELIVERY,
      KEY_PRESENTATION,
      DONE
    };
    App *app_;
    ProbeLog log_;
    int result_;
    Phase phase_;
    int arm_, keys_;
    unsigned long totalUs_, totalTicks_, keyStartUs_, keyStartTicks_, runStart_, lastDiagnostic_;
    Timer timer_;
    ToolboxSceneDebugStats before_;
    ToolboxWindow *windows_[2];
    TypingNode *nodes_[2];

    void finish(const char *error)
    {
      this->phase_ = DONE;
      if (error)
      {
        this->result_ = 1;
        this->log_.record("ERROR arm=%d keys=%d %s", this->arm_, this->keys_, error);
      }
      this->log_.record("done result=%d", this->result_);
      if (!this->log_.good())
        this->result_ = 1;
      this->app_->quit();
    }
    bool activationQueued() const
    {
      EventRecord event;
      return EventAvail(activMask, &event);
    }
    bool quiet(ToolboxWindow *window) const
    {
      return !window->hasPendingScenePlatformSync() && !window->hasPendingInvalidate()
             && EmptyRgn(reinterpret_cast<WindowPeek>(window->window())->updateRgn);
    }
    const char *phaseName() const
    {
      switch (this->phase_)
      {
      case SETUP:
        return "SETUP";
      case SETTLING:
        return "SETTLING";
      case KEY_DELIVERY:
        return "KEY_DELIVERY";
      case KEY_PRESENTATION:
        return "KEY_PRESENTATION";
      case DONE:
        return "DONE";
      }
      return "invalid";
    }
    void diagnoseWait()
    {
      const unsigned long now = TickCount();
      if (now - this->lastDiagnostic_ < 60)
        return;
      this->lastDiagnostic_ = now;
      const unsigned long startUs = this->timer_.elapsed();
      ToolboxWindow *window = this->windows_[this->arm_ % 2];
      TypingNode *node = this->nodes_[this->arm_ % 2];
      std::size_t characters = 0;
      const bool counted = node && node->characterCount(characters);
      const bool native = window && window->window();
      this->log_.record("wait phase=%s arm=%d keys=%d ticks=%lu window=%d node=%d sync=%d invalidate=%d update=%d "
                        "quiet=%d front=%d active=%d activation_queued=%d focused=%d characters=%ld",
                        this->phaseName(),
                        this->arm_,
                        this->keys_,
                        now - this->runStart_,
                        native,
                        node != 0,
                        window ? window->hasPendingScenePlatformSync() : -1,
                        window ? window->hasPendingInvalidate() : -1,
                        native ? !EmptyRgn(reinterpret_cast<WindowPeek>(window->window())->updateRgn) : -1,
                        native ? this->quiet(window) : -1,
                        native ? FrontWindow() == window->window() : -1,
                        window ? this->app_->activeWindow() == window : -1,
                        this->activationQueued(),
                        node ? node->focused() : -1,
                        counted ? static_cast<long>(characters) : -1L);
      // Durable diagnostics are outside the measured key latency.
      if (this->phase_ == KEY_DELIVERY || this->phase_ == KEY_PRESENTATION)
      {
        this->keyStartUs_ += this->timer_.elapsed() - startUs;
        this->keyStartTicks_ += TickCount() - now;
      }
    }
    void inject()
    {
      // PostEvent enters exactly the ordinary ToolboxApp keyDown dispatch.
      // The keyboard must have no held modifiers during the unattended run.
      this->keyStartTicks_ = TickCount();
      this->keyStartUs_ = this->timer_.elapsed();
      if (PostEvent(keyDown, (7L << 8) | 'x') != noErr)
      {
        this->finish("PostEvent refused");
        return;
      }
      this->phase_ = KEY_DELIVERY;
    }
    void step()
    {
      if (this->phase_ == DONE)
        return;
      if (!this->log_.good())
      {
        this->finish("log write/flush failed");
        return;
      }
      this->diagnoseWait();
      if (TickCount() - this->runStart_ > 110UL * 60)
      {
        this->finish("110 second watchdog expired");
        return;
      }
      if (!this->windows_[0] || !this->windows_[1])
        return;
      const int index = this->arm_ % 2;
      ToolboxWindow *window = this->windows_[index];
      if (!this->nodes_[index])
      {
        Node *found = 0;
        loka::dsl::FlowError error;
        loka::dsl::testing::LookupNodeById<Node>(window->scene(), "Typing.Owner", found, error);
        // The unique ID belongs only to TypingNode in this self-contained fixture.
        this->nodes_[index] = found ? static_cast<TypingNode *>(found) : 0;
      }
      TypingNode *node = this->nodes_[index];
      if (!node || !window->scenePlatformController() || !window->window() || !node->ready())
      {
        this->finish("window/editor allocation refused");
        return;
      }
      std::size_t characters = 0;
      if (!node->characterCount(characters))
      {
        this->finish("character count unavailable");
        return;
      }
      switch (this->phase_)
      {
      case SETUP:
        SelectWindow(window->window());
        this->app_->setActiveWindow(window);
        if (!node->reset())
        {
          this->finish("document reset refused");
          return;
        }
        this->keys_ = 0;
        this->totalUs_ = this->totalTicks_ = 0;
        this->phase_ = SETTLING;
        return;
      case SETTLING:
        // Queued activateEvt includes deactivation; the rail currently assigns
        // activeWindow for either. Restore our selected arm before focus settles.
        if (FrontWindow() != window->window() || this->app_->activeWindow() != window)
        {
          SelectWindow(window->window());
          this->app_->setActiveWindow(window);
          node->requestFocus();
          return;
        }
        if (this->activationQueued() || !this->quiet(window) || !node->focused())
          return;
        if (characters != 0)
        {
          this->finish("reset did not land");
          return;
        }
        this->before_ = window->scenePlatformController()->debugStatsForTesting();
        this->inject();
        return;
      case KEY_DELIVERY:
        if (characters == static_cast<std::size_t>(this->keys_))
          return; // OS update/activation events may precede the posted key.
        if (characters != static_cast<std::size_t>(this->keys_ + 1))
        {
          this->finish("unexpected text length");
          return;
        }
        // This callback precedes present() in the key's turn. Never sample here.
        this->phase_ = KEY_PRESENTATION;
        return;
      case KEY_PRESENTATION:
        if (!this->quiet(window))
          return;
        this->totalUs_ += this->timer_.elapsed() - this->keyStartUs_;
        this->totalTicks_ += TickCount() - this->keyStartTicks_;
        if (characters != static_cast<std::size_t>(++this->keys_) || !node->focused())
        {
          this->finish("text/focus sanity failed");
          return;
        }
        if (this->keys_ < kKeys)
        {
          this->inject();
          return;
        }
        this->report(characters);
        if (++this->arm_ == 4)
          this->finish(0);
        else
          this->phase_ = SETUP;
        return;
      case DONE:
        return;
      }
    }
    void report(std::size_t characters)
    {
      const ToolboxSceneDebugStats &after =
          this->windows_[this->arm_ % 2]->scenePlatformController()->debugStatsForTesting();
      const char *name = this->arm_ % 2 ? "group" : "plain";
      this->log_.record(
          "arm=%s round=%d keys=%d total_us=%lu us_per_key=%lu ticks=%lu timing=queue_to_present_observed",
          name,
          this->arm_ / 2 + 1,
          this->keys_,
          this->totalUs_,
          this->totalUs_ / this->keys_,
          this->totalTicks_);
      this->log_.record("sanity arm=%s round=%d characters=%lu expected=%d",
                        name,
                        this->arm_ / 2 + 1,
                        static_cast<unsigned long>(characters),
                        kKeys);
      this->log_.record("counts changes=%lu draw=%d draw_dirty=%d render=%d render_dirty=%d collector_visits=%d "
                        "boundary_apply=%d replay=unavailable fast_path=unavailable",
                        after.totalChanges - this->before_.totalChanges,
                        after.totalDrawCalls - this->before_.totalDrawCalls,
                        after.totalDrawDirtyCalls - this->before_.totalDrawDirtyCalls,
                        after.totalRenderCalls - this->before_.totalRenderCalls,
                        after.totalRenderDirtyCalls - this->before_.totalRenderDirtyCalls,
                        after.totalCollectorVisitCount - this->before_.totalCollectorVisitCount,
                        after.totalBoundaryApplyCount - this->before_.totalBoundaryApplyCount);
    }
  };
} // namespace

int main(int, char **)
{
  const int result = loka::standalone_tests::RunStandaloneFlowWithConfig<ProbeConfig>(0, 0);
  ExitToShell();
  return result;
}
