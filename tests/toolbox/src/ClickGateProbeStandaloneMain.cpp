// PROBE (not for merge): does the mouse gate in ToolboxApp drop content clicks
// while caret-blink overlap damage is pending? One window: a ZStack whose
// ground holds a focused EditText and whose unit (a small Box) partly covers
// it, and a Button below. Clicks are posted on the Button through the Event
// Manager (PPostEvent, where rewritten) so they take the real
// WaitNextEvent -> FindWindow -> gate path. Phase A keeps the EditText
// focused (caret blinks); phase B clears focus first (no caret) as the control.
#include <cstdio>
#include <string>
#include <Events.h>

#include "ToolboxWindow.hpp"
#include "ToolboxScenePlatformController.hpp"
#include "ToolboxLayoutMetrics.hpp"
#include "StandaloneFlowRunner.hpp"
#include "ObservedMainDefinition.hpp"
#include "app/core/AppComposition.hpp"
#include "app/core/AppConfigurable.hpp"
#include "app/nodes/boundary/StdComposition.hpp"
#include "app/nodes/nestable/Box.hpp"
#include "app/nodes/nestable/RowColumn.hpp"
#include "app/nodes/nestable/ZStack.hpp"
#include "app/nodes/controls/EditText.hpp"
#include "app/nodes/controls/Button.hpp"
#include "core/io/File.hpp"
#include "platform/file/AppLocation.hpp"
#include "platform/file/FileIO.hpp"

#if !defined(LOKA_RETRO68) || !defined(TEST_BUILD)
#error Click gate probe requires a Toolbox test build
#endif

extern unsigned long gLokaProbeContentClicks;
extern unsigned long gLokaProbeDroppedClicks;
extern unsigned long gLokaProbeDroppedAfterIdlePending;
extern unsigned long gLokaProbeIdlePendingIterations;
extern unsigned long gLokaProbeCaretToggles;
extern unsigned long gLokaProbeOverlapCalls;
extern unsigned long gLokaProbeOverlapRows;
extern unsigned long gLokaProbeOverlapContained;

namespace
{
  using namespace loka::app;
  using namespace loka::app::scene;

  class ObservedWindowDefinition : public WindowDefinition<WindowProps>
  {
  public:
    ObservedWindowDefinition(const WindowProps &props, Window **result)
        : WindowDefinition<WindowProps>(props),
          result_(result)
    {
    }
    virtual WindowDefinitionBase *clone() const
    {
      return new (std::nothrow) ObservedWindowDefinition(*this);
    }
    virtual Window *create(PlatformContext *context) const
    {
      Window *window = WindowDefinition<WindowProps>::create(context);
      *this->result_ = window;
      return window;
    }

  private:
    Window **result_;
  };

  class ClickGateNode : public StdCompositionBoundaryNodeBase<BoundaryPropsFor<ClickGateNode> >
  {
  public:
    typedef BoundaryPropsFor<ClickGateNode> Props;
    typedef Props::TypeTag TypeTag;
    explicit ClickGateNode(const Props &props)
        : StdCompositionBoundaryNodeBase<Props>(props)
    {
      this->state(this->text_, loka::core::String::Literal("caret blinks here"));
    }
    virtual void composeNode(NodeComposition &composition)
    {
      composition.declare(Column() << (ZStack() << (Column() << EditText(this->text_)) << Box().size(40, 8))
                                   << loka::app::Button("Target").onClick(&this->click_));
    }

  private:
    NodeState<loka::core::String> text_;
    loka::core::EmitterState click_;
  };

  struct Counters
  {
    unsigned long posted, delivered, dropped, droppedAfterIdle, idlePending, caret, calls, rows, contained;
  };

  class ClickGateConfig : public AppConfigurable
  {
  public:
    explicit ClickGateConfig(PlatformContext *context)
        : AppConfigurable(context),
          app_(0),
          window_(0),
          file_(),
          log_(0),
          phase_(SETTLE),
          phaseStart_(0),
          posted_(0),
          focusOk_(false),
          clearOk_(false),
          toggleSeen_(0),
          toggleTick_(0),
          cycleClicked_(2),
          inFlightOffset_(99),
          droppedBefore_(0),
          afterBefore_(0),
          cycles_(0),
          pendingOffset_(0),
          focusKept_(0),
          traced_(0),
          result_(0)
    {
      if (loka::platform::file::ResolveApplicationSidecar(
              loka::file::File::Application() << loka::file::File("LOG.TXT"), this->file_))
        this->log_ = loka::platform::file::OpenWriteTruncate(this->file_);
      if (!this->log_)
        this->result_ = 1;
      for (int i = 0; i < 16; ++i)
        this->offsetClicks_[i] = this->offsetDrops_[i] = this->offsetAfterIdle_[i] = 0;
    }
    virtual ~ClickGateConfig()
    {
      if (this->log_)
        std::fclose(this->log_);
    }
    void setApp(App *app) { this->app_ = app; }
    int exitCode() const { return this->result_; }
    virtual void compose(AppComposition &composition)
    {
      composition << ObservedWindowDefinition(
          WindowProps()
              .frame(40, 60, 280, 160)
              .title("Click gate probe")
              .scene(loka::scenario_tests::ObservedMainDefinition<ClickGateNode::Props, ClickGateNode>(
                  ClickGateNode::Props(), 0))
              .visible(true)
              .idlePolicy(IdlePolicy::everyTick())
              .onIdle(&ClickGateConfig::OnIdle, this),
          &this->window_);
    }

  private:
    enum Phase
    {
      SETTLE,
      FOCUS,
      QUIET,
      PHASE_A,
      CLEAR,
      PHASE_B,
      DONE
    };
    App *app_;
    Window *window_;
    loka::platform::file::FileHandle file_;
    std::FILE *log_;
    Phase phase_;
    unsigned long phaseStart_;
    unsigned long posted_;
    Counters a_, b_, start_;
    bool focusOk_, clearOk_;
    unsigned long toggleSeen_, toggleTick_, cycleClicked_, inFlightOffset_, droppedBefore_, afterBefore_;
    unsigned long offsetClicks_[16], offsetDrops_[16], offsetAfterIdle_[16];
    unsigned long cycles_, pendingOffset_, focusKept_, traced_;
    int result_;

    WindowPtr native() const
    {
      ToolboxWindow *w = this->window_ ? this->window_->asToolboxWindow() : 0;
      return w ? w->window() : 0;
    }
    bool nativeFocus(bool &focused) const
    {
      ToolboxWindow *w = this->window_ ? this->window_->asToolboxWindow() : 0;
      ToolboxScenePlatformController *c = w ? w->scenePlatformController() : 0;
      NodeContext *ctx = 0;
      if (!c || !c->readNativeFocus(ctx))
        return false;
      focused = ctx != 0;
      return true;
    }
    Point global(short h, short v) const
    {
      Point p;
      p.h = h;
      p.v = v;
      GrafPtr previous = 0;
      GetPort(&previous);
      SetPort(this->native());
      LocalToGlobal(&p);
      SetPort(previous);
      return p;
    }
    bool buttonCenter(Point &out) const
    {
      WindowPtr w = this->native();
      if (!w)
        return false;
      ControlHandle control = reinterpret_cast<ControlHandle>(reinterpret_cast<WindowPeek>(w)->controlList);
      if (!control)
        return false;
      const Rect r = (**control).contrlRect;
      out = this->global(static_cast<short>((r.left + r.right) / 2), static_cast<short>((r.top + r.bottom) / 2));
      return true;
    }
    // Every posted click lands in this window's content, so the gate sees each one.
    void postClick(Point where)
    {
      EvQElPtr q = 0;
      if (PPostEvent(mouseDown, 0, &q) == noErr && q)
        q->evtQWhere = where;
      q = 0;
      if (PPostEvent(mouseUp, 0, &q) == noErr && q)
        q->evtQWhere = where;
      ++this->posted_;
    }
    static unsigned long Processed()
    {
      return gLokaProbeContentClicks + gLokaProbeDroppedClicks;
    }
    void snapshot(Counters &c) const
    {
      c.posted = this->posted_;
      c.delivered = gLokaProbeContentClicks;
      c.dropped = gLokaProbeDroppedClicks;
      c.droppedAfterIdle = gLokaProbeDroppedAfterIdlePending;
      c.idlePending = gLokaProbeIdlePendingIterations;
      c.caret = gLokaProbeCaretToggles;
      c.calls = gLokaProbeOverlapCalls;
      c.rows = gLokaProbeOverlapRows;
      c.contained = gLokaProbeOverlapContained;
    }
    void report(const char *name, const Counters &end, const Counters &begin)
    {
      std::fprintf(this->log_,
                   "%s posted=%lu delivered=%lu dropped=%lu dropped_after_idle_pending=%lu idle_pending_iterations=%lu"
                   " caret_toggles=%lu overlap_calls=%lu overlap_rows=%lu overlap_contained=%lu\r",
                   name,
                   end.posted - begin.posted,
                   end.delivered - begin.delivered,
                   end.dropped - begin.dropped,
                   end.droppedAfterIdle - begin.droppedAfterIdle,
                   end.idlePending - begin.idlePending,
                   end.caret - begin.caret,
                   end.calls - begin.calls,
                   end.rows - begin.rows,
                   end.contained - begin.contained);
    }
    // One click in flight at a time: post the next only after the gate saw the last.
    void pump(unsigned long now, Phase next, Counters &end)
    {
      if (Processed() < this->posted_)
        return;
      if (now - this->phaseStart_ >= 600)
      {
        this->snapshot(end);
        this->phase_ = next;
        this->phaseStart_ = now;
        return;
      }
      Point where;
      if (this->buttonCenter(where))
        this->postClick(where);
    }
    // Phase A cycle: click the EditText (blinking restarts), wait for a caret
    // toggle, then click the Button k = 24..35 ticks after it.
    void sweep(unsigned long now)
    {
      if (this->inFlightOffset_ != 99 && Processed() >= this->posted_)
      {
        if (this->traced_ < 24)
        {
          ++this->traced_;
          NodeContext *ctx = 0;
          ToolboxWindow *tw = this->window_ ? this->window_->asToolboxWindow() : 0;
          ToolboxScenePlatformController *c = tw ? tw->scenePlatformController() : 0;
          const bool answered = c && c->readNativeFocus(ctx);
          std::fprintf(this->log_, "trace kind=%s dropped=%d front=%d answered=%d focused=%d toggles=%lu tick=%lu\r",
                       this->inFlightOffset_ == 98 ? "edit" : "button",
                       gLokaProbeDroppedClicks > this->droppedBefore_ ? 1 : 0,
                       FrontWindow() == this->native() ? 1 : 0, answered ? 1 : 0, ctx ? 1 : 0,
                       gLokaProbeCaretToggles, now);
        }
        if (this->inFlightOffset_ == 98)
        {
          this->cycleClicked_ = 2; // refocus processed; now wait for a toggle
          this->toggleSeen_ = gLokaProbeCaretToggles;
        }
        else
        {
          const unsigned long k = this->inFlightOffset_ - 24;
          ++this->offsetClicks_[k];
          if (gLokaProbeDroppedClicks > this->droppedBefore_)
            ++this->offsetDrops_[k];
          if (gLokaProbeDroppedAfterIdlePending > this->afterBefore_)
            ++this->offsetAfterIdle_[k];
          bool focused = false;
          if (this->nativeFocus(focused) && focused)
            ++this->focusKept_;
          this->cycleClicked_ = 2;
          this->toggleSeen_ = gLokaProbeCaretToggles;
        }
        this->inFlightOffset_ = 99;
      }
      if (this->inFlightOffset_ != 99)
        return;
      if (now - this->phaseStart_ >= 2400)
      {
        this->snapshot(this->a_);
        this->phase_ = CLEAR;
        this->phaseStart_ = now;
        return;
      }
      const short x = static_cast<short>(ToolboxLayoutMetrics::kRootInset + 80);
      if (this->cycleClicked_ == 0)
      {
        this->inFlightOffset_ = 98;
        this->droppedBefore_ = gLokaProbeDroppedClicks;
        this->afterBefore_ = gLokaProbeDroppedAfterIdlePending;
        this->postClick(this->global(x, static_cast<short>(ToolboxLayoutMetrics::kRootInset + 10)));
        return;
      }
      if (this->cycleClicked_ == 2)
      {
        if (gLokaProbeCaretToggles != this->toggleSeen_)
        {
          this->toggleSeen_ = gLokaProbeCaretToggles;
          this->toggleTick_ = now;
          this->cycleClicked_ = 3;
        }
        return;
      }
      if (this->cycleClicked_ == 3)
      {
        this->pendingOffset_ = 24 + (this->cycles_++ % 12);
        this->cycleClicked_ = 4;
      }
      return;
    }
    void fire(unsigned long now)
    {
      if (this->cycleClicked_ != 4 || this->inFlightOffset_ != 99 || now - this->toggleTick_ < this->pendingOffset_)
        return;
      Point where;
      if (!this->buttonCenter(where))
        return;
      this->droppedBefore_ = gLokaProbeDroppedClicks;
      this->afterBefore_ = gLokaProbeDroppedAfterIdlePending;
      this->inFlightOffset_ = this->pendingOffset_;
      this->postClick(where);
    }
    static void OnIdle(Window *, double, void *data)
    {
      ClickGateConfig *self = static_cast<ClickGateConfig *>(data);
      const unsigned long now = TickCount();
      if (self->phaseStart_ == 0)
        self->phaseStart_ = now;
      const short x = static_cast<short>(ToolboxLayoutMetrics::kRootInset + 80);
      switch (self->phase_)
      {
      case SETTLE:
        if (now - self->phaseStart_ >= 120)
        {
          self->postClick(self->global(x, static_cast<short>(ToolboxLayoutMetrics::kRootInset + 10)));
          self->phase_ = FOCUS;
          self->phaseStart_ = now;
        }
        return;
      case FOCUS:
        if (now - self->phaseStart_ >= 60)
        {
          bool focused = false;
          self->focusOk_ = self->nativeFocus(focused) && focused;
          std::fprintf(self->log_, "focus-edittext %s\r", self->focusOk_ ? "PASS" : "FAIL");
          self->snapshot(self->start_);
          self->phase_ = QUIET;
          self->phaseStart_ = now;
        }
        return;
      case QUIET:
        if (now - self->phaseStart_ >= 300)
        {
          Counters quiet;
          self->snapshot(quiet);
          self->report("phase-0-focused-no-clicks", quiet, self->start_);
          self->snapshot(self->start_);
          self->toggleSeen_ = gLokaProbeCaretToggles;
          self->phase_ = PHASE_A;
          self->phaseStart_ = now;
        }
        return;
      case PHASE_A:
        // Passive window: Lua presses the real ADB mouse button on Target.
        if (now - self->phaseStart_ >= 3600)
        {
          self->snapshot(self->a_);
          self->phase_ = CLEAR;
          self->phaseStart_ = now;
        }
        if (self->phase_ == CLEAR)
        {
          for (int k = 0; k < 12; ++k)
            std::fprintf(self->log_, "sweep offset=%d clicks=%lu dropped=%lu dropped_after_idle_pending=%lu\r", 24 + k,
                         self->offsetClicks_[k], self->offsetDrops_[k], self->offsetAfterIdle_[k]);
          std::fprintf(self->log_, "sweep focus_kept_after_button=%lu of %lu\r", self->focusKept_, self->cycles_);
          self->postClick(self->global(x, 130));
        }
        return;
      case CLEAR:
        if (now - self->phaseStart_ >= 60)
        {
          bool focused = true;
          self->clearOk_ = self->nativeFocus(focused) && !focused;
          std::fprintf(self->log_, "clear-focus %s\r", self->clearOk_ ? "PASS" : "FAIL");
          self->report("phase-a-real-adb-clicks", self->a_, self->start_);
          self->snapshot(self->start_);
          self->phase_ = PHASE_B;
          self->phaseStart_ = now;
        }
        return;
      case PHASE_B:
        self->pump(now, DONE, self->b_);
        if (self->phase_ == DONE)
        {
          self->report("phase-b-no-focus-control", self->b_, self->start_);
          std::fprintf(self->log_, "click-gate done focus=%d clear=%d\r", self->focusOk_ ? 1 : 0,
                       self->clearOk_ ? 1 : 0);
          loka::platform::file::FlushWrite(self->log_, self->file_);
          self->app_->quit();
        }
        return;
      case DONE:
        return;
      }
    }
  };
} // namespace

int main(int, char **)
{
  return loka::standalone_tests::RunStandaloneFlowWithConfig<ClickGateConfig>(0, 0);
}
