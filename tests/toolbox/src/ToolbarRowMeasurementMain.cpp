#include "ToolboxWindow.hpp"
#include "ToolboxScenePlatformController.hpp"
#include "ToolbarRowMeasurementScene.hpp"
#include "ToolbarMeasurementClock.hpp"
#include "app/bootstrap/PlatformBootstrap.hpp"
#include "app/core/AppConfigurable.hpp"
#include "app/core/AppComposition.hpp"
#include "app/core/WindowDefinition.hpp"
#include "app/core/App.hpp"
#include "core/util/ScopedPtr.hpp"
#include "platform/file/AppLocation.hpp"
#include "platform/file/FileIO.hpp"
#include <Gestalt.h>
#ifndef TEST_BUILD
#error Row measurement is test-only
#endif
#if LOKA_PROFILE_FUNC_TICKS
#error Row measurement requires function profiling disabled
#endif
using namespace loka;
namespace
{
  /** The App owns the window; the synchronous idle callback owns its report. */
  class RowMeasurementApp : public AppConfigurable
  {
    App *app_;

  public:
    explicit RowMeasurementApp(PlatformContext *context)
        : AppConfigurable(context),
          app_(0)
    {
    }
    void setApp(App *app)
    {
      this->app_ = app;
    }
    unsigned long now() const
    {
      return testing::MeasurementMicroseconds();
    }
    virtual void compose(AppComposition &c)
    {
      c << WindowDef(WindowProps()
                         .frame(1, 41, 636, 400)
                         .title("Toolbar Row cost")
                         .scene(app::scene::Boundary<testing::ToolbarRows>())
                         .visible(true)
                         .idlePolicy(app::IdlePolicy::everyTick())
                         .onIdle(&OnIdle, this));
    }
    static void OnIdle(Window *window, double, void *owner)
    {
      RowMeasurementApp &self = *static_cast<RowMeasurementApp *>(owner);
      ToolboxScenePlatformController *controller = static_cast<ToolboxScenePlatformController *>(
          dsl::testing::SceneTestAccess::platformController(*window->scene()));
      if (window->scene()->hasPendingInvalidation() || controller->hasPendingSync()
          || window->asToolboxWindow()->hasPendingInvalidate())
        return;
      platform::file::FileHandle file;
      if (platform::file::ResolveApplicationSidecar(file::File::Application() << file::File("LOG.TXT"), file))
      {
        std::FILE *out = platform::file::OpenWriteTruncate(file);
        if (out)
        {
          const unsigned long start = self.now();
          const unsigned long end = self.now();
          unsigned long resolution = ~0UL;
          unsigned long previous = end;
          for (unsigned i = 0; i != 1024; ++i)
          {
            const unsigned long current = self.now();
            const unsigned long delta = current - previous;
            if (delta && delta < resolution)
              resolution = delta;
            previous = current;
          }
          long system = 0;
          const OSErr systemStatus = Gestalt(gestaltSystemVersion, &system);
          GDHandle device = GetMainDevice();
          const bool environmentKnown = systemStatus == noErr && device && (*device)->gdPMap;
          const int depth = environmentKnown ? (*(*device)->gdPMap)->pixelSize : 0;
          const Rect screen = qd.screenBits.bounds;
          const Rect port = window->asToolboxWindow()->window()->portRect;
          std::fprintf(out,
                       "toolbar_row_environment clock=Microseconds empty_bracket_us=%lu observed_resolution_us=%lu "
                       "TEST_BUILD=1 profile_func_ticks=0 system_hex=%lx screen=%dx%d depth=%d scene=%dx%d\n",
                       end - start,
                       resolution == ~0UL ? 0 : resolution,
                       system,
                       screen.right - screen.left,
                       screen.bottom - screen.top,
                       depth,
                       port.right - port.left,
                       port.bottom - port.top);
          const bool ok = environmentKnown && resolution != ~0UL
                          && testing::RunToolbarRows(*window->scene(), *controller, self, out);
          std::fprintf(out, "toolbar_row_summary complete=%d cases=6\n", ok);
          std::fclose(out);
        }
      }
      self.app_->quit();
    }
  };
} // namespace
int main()
{
  platform::InitPlatformRuntime();
  core::ScopedPtr<PlatformContext> context(platform::CreatePlatformContext());
  if (!context.get())
    return 1;
  RowMeasurementApp config(context.get());
  core::ScopedPtr<App> app(context->createApp(&config, 0, 0));
  if (!app.get())
    return 1;
  config.setApp(app.get());
  app->run();
  return 0;
}
