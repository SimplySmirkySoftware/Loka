#include "ToolboxWindow.hpp"
#include "ToolboxScenePlatformController.hpp"
#include "ToolbarRowMeasurementScene.hpp"
#include "ToolbarMeasurementClock.hpp"
#include "ToolboxNodeDispatch.hpp"
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
  /** Time a complete retained Row layout, including its two height consultations.
      Construction, painting, leaf comparison and report IO stay outside the bracket. */
  template <class Clock>
  bool MeasureAlignedTitle(app::scene::Scene &scene,
                           ToolboxScenePlatformController &controller,
                           Clock &clock,
                           std::FILE *out)
  {
    app::scene::Node *row = 0, *title = 0;
    dsl::FlowError error;
    if (dsl::testing::LookupNodeById<app::scene::Node>(&scene, "aligned-title", row, error) != dsl::FLOW_STEP_SUCCEEDED
        || dsl::testing::LookupNodeById<app::scene::Node>(&scene, "aligned-title-label", title, error)
               != dsl::FLOW_STEP_SUCCEEDED
        || !title->context)
      return false;
    app::scene::LayoutState offer;
    offer.width = 600;
    offer.height = 100;
    offer.lineHeight = ToolboxLayoutMetrics::kDefaultLineHeight;
    offer.spacing = ToolboxLayoutMetrics::kDefaultGap;
    offer.inputs = app::scene::NODE_DIRTY_LAYOUT;
    unsigned long samples[31];
    int occupied = 0;
    for (unsigned i = 0; i != 31; ++i)
    {
      app::scene::LayoutState state = offer;
      const unsigned long start = clock.now();
      occupied = LayoutNode(row, state, &controller, 0);
      samples[i] = clock.now() - start;
    }
    std::sort(samples, samples + 31);
    short answer = -1;
    const bool answered = controller.queryNaturalHeight(title, answer);
    app::scene::LayoutState leaf = offer;
    title->context->layout(&controller, leaf);
    std::fprintf(out, "toolbar_row_inputs case=aligned-title k=6 scans=0 source=toolbox clock=Microseconds\n");
    std::fprintf(out,
                 "toolbar_row case=aligned-title repetitions=31 median_us=%lu maximum_us=%lu offer=600 occupied=%d\n",
                 samples[15],
                 samples[30],
                 occupied);
    std::fprintf(out,
                 "toolbar_row_height case=aligned-title answered=%d query=%d leaf=%d equal=%d\n",
                 answered,
                 answer,
                 leaf.y - offer.y,
                 answered && answer == leaf.y - offer.y);
    // A declined answer is recorded for the same vehicle on the 1c-2 base.
    return !answered || answer == leaf.y - offer.y;
  }

  /** Include full layout, paint and cursor return in each independent draw. */
  template <class Clock> bool MeasureAlignedTitleDraw(ToolboxWindow &window, Clock &clock, std::FILE *out)
  {
    app::scene::Node *title = 0;
    dsl::FlowError error;
    if (dsl::testing::LookupNodeById<app::scene::Node>(window.scene(), "aligned-title-label", title, error)
        != dsl::FLOW_STEP_SUCCEEDED)
      return false;
    unsigned long samples[31];
    for (unsigned i = 0; i != 31; ++i)
    {
      // drawForFlush always walks layout and paints; prevent Title leaf reuse too.
      title->requeueLayoutInputs(app::scene::NODE_DIRTY_LAYOUT);
      const unsigned long start = clock.now();
      window.drawForFlush();
      samples[i] = clock.now() - start;
    }
    std::sort(samples, samples + 31);
    std::fprintf(out, "toolbar_row_inputs case=aligned-title-draw k=6 scans=0 source=toolbox clock=Microseconds\n");
    std::fprintf(out,
                 "toolbar_row case=aligned-title-draw repetitions=31 median_us=%lu maximum_us=%lu\n",
                 samples[15],
                 samples[30]);
    return true;
  }

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
                          && testing::RunToolbarRows(*window->scene(), *controller, self, out)
                          && MeasureAlignedTitle(*window->scene(), *controller, self, out)
                          && MeasureAlignedTitleDraw(*window->asToolboxWindow(), self, out);
          std::fprintf(out, "toolbar_row_summary complete=%d cases=8\n", ok);
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
