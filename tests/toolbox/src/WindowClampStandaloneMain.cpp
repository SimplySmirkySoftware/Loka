#include <cstdio>

#include "ToolboxWindow.hpp"
#include "StandaloneFlowRunner.hpp"
#include "app/core/AppComposition.hpp"
#include "app/core/AppConfigurable.hpp"
#include "core/io/File.hpp"
#include "core/util/StateTrackerGuard.hpp"
#include "platform/file/AppLocation.hpp"
#include "platform/file/FileIO.hpp"

#if !defined(LOKA_RETRO68) || !defined(TEST_BUILD)
#error Window clamp pin requires a Toolbox test build
#endif

namespace
{
  /** Borrows the ordinary factory result while the App retains ownership. */
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
      return cloneWindowDefinition(*this);
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

  /** One finite native contract run; all borrowed windows die with the App. */
  class WindowClampConfig : public AppConfigurable
  {
  public:
    explicit WindowClampConfig(PlatformContext *context)
        : AppConfigurable(context),
          app_(0),
          fullWindow_(0),
          offscreenWindow_(0),
          file_(),
          log_(0),
          phase_(INITIAL_FRAMES),
          result_(0)
    {
      if (loka::platform::file::ResolveApplicationSidecar(
              loka::file::File::Application() << loka::file::File("LOG.TXT"), this->file_))
        this->log_ = loka::platform::file::OpenWriteTruncate(this->file_);
      if (!this->log_)
        this->result_ = 1;
    }

    virtual ~WindowClampConfig()
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
      const Rect screen = qd.screenBits.bounds;
      composition << ObservedWindowDefinition(WindowProps()
                                                  .frame(0, 0, screen.right - screen.left, screen.bottom - screen.top)
                                                  .title("Clamp full screen")
                                                  .visible(true)
                                                  .idlePolicy(loka::app::IdlePolicy::everyTick())
                                                  .onIdle(&WindowClampConfig::OnIdle, this),
                                              &this->fullWindow_);
      composition << ObservedWindowDefinition(WindowProps()
                                                  .frame(600, 440, 200, 150)
                                                  .title("Clamp bottom right")
                                                  .visible(true)
                                                  .idlePolicy(loka::app::IdlePolicy::everyTick())
                                                  .onIdle(&WindowClampConfig::OnIdle, this),
                                              &this->offscreenWindow_);
    }

  private:
    enum Phase
    {
      INITIAL_FRAMES,
      LATER_FRAME,
      FITTING_FRAME,
      COMPLETE
    };
    App *app_;
    Window *fullWindow_;
    Window *offscreenWindow_;
    loka::platform::file::FileHandle file_;
    std::FILE *log_;
    Phase phase_;
    int result_;

    bool check(Window *window, const char *arm, bool fullScreen)
    {
      ToolboxWindow *native = window ? window->asToolboxWindow() : 0;
      if (!native || !native->window())
        return false;
      const WindowPeek peek = reinterpret_cast<WindowPeek>(native->window());
      const Rect structure = (*peek->strucRgn)->rgnBBox;
      const Rect content = (*peek->contRgn)->rgnBBox;
      const Rect screen = qd.screenBits.bounds;
      const short menu = GetMBarHeight();
      // This existing publication door calls the private nativeContentFrame().
      native->storeCurrentNativeContentFrame();
      const loka::core::Frame reported = window->nativeFrame().get();
      const bool inside = structure.left >= screen.left && structure.right <= screen.right
                          && structure.top >= screen.top + menu && structure.bottom <= screen.bottom;
      const bool reportsActual =
          reported
          == loka::core::Frame(
              content.left, content.top - menu - 1, content.right - content.left, content.bottom - content.top);
      const int leftInset = content.left - structure.left;
      const int rightInset = structure.right - content.right;
      const bool widthFits = !fullScreen
                             || (content.right - content.left == screen.right - screen.left - leftInset - rightInset
                                 && content.right - content.left < screen.right - screen.left);
      const loka::core::Frame requested = window->frameState().get();
      const bool requestedSizeApplied =
          fullScreen || (reported.width == requested.width && reported.height == requested.height);
      const bool fittingFrameUnchanged = this->phase_ != FITTING_FRAME || reported == requested;
      const bool passed = inside && reportsActual && widthFits && requestedSizeApplied && fittingFrameUnchanged;
      const int written = std::fprintf(
          this->log_,
          "%s %s content=(%d,%d,%d,%d) structure=(%d,%d,%d,%d) chrome=(%d,%d,%d,%d) reported=(%d,%d,%d,%d)\r",
          arm,
          passed ? "PASS" : "FAIL",
          content.left,
          content.top,
          content.right,
          content.bottom,
          structure.left,
          structure.top,
          structure.right,
          structure.bottom,
          leftInset,
          content.top - structure.top,
          rightInset,
          structure.bottom - content.bottom,
          reported.x,
          reported.y,
          reported.width,
          reported.height);
      return passed && written >= 0;
    }

    void finish(bool passed)
    {
      this->phase_ = COMPLETE;
      this->result_ = passed ? 0 : 1;
      if (std::fprintf(this->log_, "window-clamp %s\r", passed ? "PASS" : "FAIL") < 0
          || !loka::platform::file::FlushWrite(this->log_, this->file_))
        this->result_ = 1;
      this->app_->quit();
    }

    static void OnIdle(Window *, double, void *data)
    {
      WindowClampConfig *self = static_cast<WindowClampConfig *>(data);
      Window *window = self->offscreenWindow_;
      switch (self->phase_)
      {
      case COMPLETE:
        return;
      case INITIAL_FRAMES:
        if (!self->check(self->fullWindow_, "full-screen-open", true)
            || !self->check(window, "bottom-right-open", false))
        {
          self->finish(false);
          return;
        }
        {
          loka::core::StateTrackerGuard transaction(window->getTracker());
          window->frameState().set(loka::core::Frame(900, 700, 220, 160));
        }
        self->phase_ = LATER_FRAME;
        return;
      case LATER_FRAME:
        if (!self->check(window, "later-frame-write", false))
        {
          self->finish(false);
          return;
        }
        {
          loka::core::StateTrackerGuard transaction(window->getTracker());
          window->frameState().set(loka::core::Frame(50, 50, 200, 150));
        }
        self->phase_ = FITTING_FRAME;
        return;
      case FITTING_FRAME:
        self->finish(self->check(window, "fitting-frame-write", false));
        return;
      }
    }
  };
} // namespace

int main(int, char **)
{
  return loka::standalone_tests::RunStandaloneFlowWithConfig<WindowClampConfig>(0, 0);
}
