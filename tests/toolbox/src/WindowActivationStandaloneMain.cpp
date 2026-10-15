#include <cstdio>

#include "ToolboxWindow.hpp"
#include "StandaloneFlowRunner.hpp"
#include "app/core/AppComposition.hpp"
#include "app/core/AppConfigurable.hpp"
#include "core/io/File.hpp"
#include "platform/file/AppLocation.hpp"
#include "platform/file/FileIO.hpp"

#include <Windows.h>

#if !defined(LOKA_RETRO68) || !defined(TEST_BUILD)
#error Window activation pin requires a Toolbox test build
#endif

namespace
{
  /** One finite native activation run. The Window Manager reports a change of
      front window as two activateEvt records: the old front window without
      activeFlag, then the new one with it. Idle goes to the App's active
      window, so the idle recipient shows what the rail did with each record.

      launch-pair: the second window is created last, so the first idle ticks
      arrive with the first window's deactivate record and the second window's
      activate record. Every recipient must be the front window; a deactivate
      record that made its window active hands one tick to the back window.
      select-back: SelectWindow on the back window must move the recipient to
      it. This is the positive control: it proves the recipient follows an
      activate record, so a launch-pair PASS is not a silent instrument. */
  class WindowActivationConfig : public AppConfigurable
  {
  public:
    explicit WindowActivationConfig(PlatformContext *context)
        : AppConfigurable(context),
          app_(0),
          file_(),
          log_(0),
          phase_(LAUNCH_PAIR),
          ticks_(0),
          launchFront_(0),
          launchBack_(0),
          result_(0)
    {
      if (loka::platform::file::ResolveApplicationSidecar(
              loka::file::File::Application() << loka::file::File("LOG.TXT"), this->file_))
        this->log_ = loka::platform::file::OpenWriteTruncate(this->file_);
      if (!this->log_)
        this->result_ = 1;
    }

    virtual ~WindowActivationConfig()
    {
      if (this->log_)
        std::fclose(this->log_);
    }

    void setApp(App *app)
    {
      this->app_ = app;
    }
    int exitCode() const
    {
      return this->result_;
    }

    virtual void compose(AppComposition &composition)
    {
      composition << WindowDefinition<WindowProps>(WindowProps()
                                                       .frame(40, 40, 200, 120)
                                                       .title("Activation first")
                                                       .visible(true)
                                                       .idlePolicy(loka::app::IdlePolicy::everyTick())
                                                       .onIdle(&WindowActivationConfig::OnIdle, this));
      composition << WindowDefinition<WindowProps>(WindowProps()
                                                       .frame(140, 100, 200, 120)
                                                       .title("Activation second")
                                                       .visible(true)
                                                       .idlePolicy(loka::app::IdlePolicy::everyTick())
                                                       .onIdle(&WindowActivationConfig::OnIdle, this));
    }

  private:
    enum Phase
    {
      LAUNCH_PAIR,
      SELECT_BACK,
      COMPLETE
    };
    static const int kLaunchTicks = 3;
    static const int kSelectTicks = 30;
    App *app_;
    loka::platform::file::FileHandle file_;
    std::FILE *log_;
    Phase phase_;
    int ticks_;
    WindowPtr launchFront_;
    WindowPtr launchBack_;
    int result_;

    const char *label(WindowPtr window) const
    {
      if (!window)
        return "none";
      if (window == this->launchFront_)
        return "front";
      if (window == this->launchBack_)
        return "back";
      return "other";
    }

    bool arm(const char *name, bool passed, WindowPtr recipient)
    {
      return std::fprintf(this->log_,
                          "%s %s tick=%d recipient=%s front=%s\r",
                          name,
                          passed ? "PASS" : "FAIL",
                          this->ticks_,
                          this->label(recipient),
                          this->label(FrontWindow()))
                 >= 0
             && passed;
    }

    void finish(bool passed)
    {
      this->phase_ = COMPLETE;
      this->result_ = passed ? 0 : 1;
      if (std::fprintf(this->log_, "window-activation %s\r", passed ? "PASS" : "FAIL") < 0
          || !loka::platform::file::FlushWrite(this->log_, this->file_))
        this->result_ = 1;
      this->app_->quit();
    }

    void observe(WindowPtr recipient)
    {
      switch (this->phase_)
      {
      case COMPLETE:
        return;
      case LAUNCH_PAIR:
        if (!this->launchFront_)
        {
          this->launchFront_ = FrontWindow();
          this->launchBack_ = this->launchFront_
                                  ? reinterpret_cast<WindowPtr>(
                                        reinterpret_cast<WindowPeek>(this->launchFront_)->nextWindow)
                                  : 0;
          if (!this->launchBack_)
          {
            this->finish(this->arm("launch-pair", false, recipient));
            return;
          }
        }
        ++this->ticks_;
        if (recipient != this->launchFront_)
        {
          this->finish(this->arm("launch-pair", false, recipient));
          return;
        }
        if (this->ticks_ < kLaunchTicks)
          return;
        if (!this->arm("launch-pair", true, recipient))
        {
          this->finish(false);
          return;
        }
        this->phase_ = SELECT_BACK;
        this->ticks_ = 0;
        SelectWindow(this->launchBack_);
        return;
      case SELECT_BACK:
        ++this->ticks_;
        if (recipient == this->launchBack_ && FrontWindow() == this->launchBack_)
        {
          this->finish(this->arm("select-back", true, recipient));
          return;
        }
        if (recipient != this->launchFront_ || this->ticks_ >= kSelectTicks)
          this->finish(this->arm("select-back", false, recipient));
        return;
      }
    }

    static void OnIdle(Window *window, double, void *data)
    {
      ToolboxWindow *native = window ? window->asToolboxWindow() : 0;
      static_cast<WindowActivationConfig *>(data)->observe(native ? native->window() : 0);
    }
  };
} // namespace

int main(int, char **)
{
  return loka::standalone_tests::RunStandaloneFlowWithConfig<WindowActivationConfig>(0, 0);
}
