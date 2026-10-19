// PROBE (not for merge): measures #1471 from the app side only. One window: a
// ZStack whose ground holds an EditText and whose unit (a small Box) partly
// covers it, and a Target button below. Lua presses the real ADB mouse button
// on Target; the app counts Target's onClick actions per phase. No framework
// code is instrumented, so the same probe runs on main and on a fix branch.
// Phase A keeps the EditText focused (its caret blinks under the unit);
// phase B clears focus first (no caret) as the control.
#include <cstdio>
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

namespace
{
  using namespace loka::app;
  using namespace loka::app::scene;

  unsigned long gTargetActions = 0;

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
    virtual void declareBindings(BindingToken &t)
    {
      t.action(this->click_, this, &ClickGateNode::onTarget);
    }
    virtual void composeNode(NodeComposition &composition)
    {
      composition.declare(Column() << (ZStack() << (Column() << EditText(this->text_)) << Box().size(40, 8))
                                   << loka::app::Button("Target").onClick(&this->click_));
    }

  private:
    NodeState<loka::core::String> text_;
    loka::core::EmitterState click_;

    void onTarget() { ++gTargetActions; }
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
          phaseActions_(0),
          focusOk_(false),
          keptOk_(false),
          clearOk_(false),
          result_(0)
    {
      if (loka::platform::file::ResolveApplicationSidecar(
              loka::file::File::Application() << loka::file::File("LOG.TXT"), this->file_))
        this->log_ = loka::platform::file::OpenWriteTruncate(this->file_);
      if (!this->log_)
        this->result_ = 1;
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
      PHASE_A,
      CLEAR,
      PHASE_B,
      DONE
    };
    // Lua starts pressing 14 s after launch: 200 presses in phase A take at
    // most 100 s; phase B presses start 135 s after launch, 100 presses in at
    // most 50 s. The phases below cover both windows with margin.
    static const unsigned long kPhaseATicks = 7200;
    static const unsigned long kPhaseBTicks = 4200;
    App *app_;
    Window *window_;
    loka::platform::file::FileHandle file_;
    std::FILE *log_;
    Phase phase_;
    unsigned long phaseStart_;
    unsigned long phaseActions_;
    bool focusOk_, keptOk_, clearOk_;
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
    // Focus moves and the clear use posted clicks; only Target's presses are real ADB.
    void postClick(Point where)
    {
      EvQElPtr q = 0;
      if (PPostEvent(mouseDown, 0, &q) == noErr && q)
        q->evtQWhere = where;
      q = 0;
      if (PPostEvent(mouseUp, 0, &q) == noErr && q)
        q->evtQWhere = where;
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
          self->phaseActions_ = gTargetActions;
          self->phase_ = PHASE_A;
          self->phaseStart_ = now;
        }
        return;
      case PHASE_A:
        if (now - self->phaseStart_ >= kPhaseATicks)
        {
          bool focused = false;
          self->keptOk_ = self->nativeFocus(focused) && focused;
          std::fprintf(self->log_, "phase-a-focused target_actions=%lu focus_kept=%s\r",
                       gTargetActions - self->phaseActions_, self->keptOk_ ? "PASS" : "FAIL");
          self->postClick(self->global(x, 130));
          self->phase_ = CLEAR;
          self->phaseStart_ = now;
        }
        return;
      case CLEAR:
        if (now - self->phaseStart_ >= 60)
        {
          bool focused = true;
          self->clearOk_ = self->nativeFocus(focused) && !focused;
          std::fprintf(self->log_, "clear-focus %s\r", self->clearOk_ ? "PASS" : "FAIL");
          self->phaseActions_ = gTargetActions;
          self->phase_ = PHASE_B;
          self->phaseStart_ = now;
        }
        return;
      case PHASE_B:
        if (now - self->phaseStart_ >= kPhaseBTicks)
        {
          std::fprintf(self->log_, "phase-b-no-focus target_actions=%lu\r", gTargetActions - self->phaseActions_);
          std::fprintf(self->log_, "click-gate done focus=%d kept=%d clear=%d\r", self->focusOk_ ? 1 : 0,
                       self->keptOk_ ? 1 : 0, self->clearOk_ ? 1 : 0);
          loka::platform::file::FlushWrite(self->log_, self->file_);
          self->phase_ = DONE;
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
