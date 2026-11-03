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
#include <LowMem.h>
#include <Script.h>
#include "platform/ToolboxPascalText.hpp"
#include <cstring>
#include <climits>
#ifndef TOOLBAR_ROWS_LAPTOP
#error TOOLBAR_ROWS_LAPTOP must select the measurement vehicle
#elif TOOLBAR_ROWS_LAPTOP != 0 && TOOLBAR_ROWS_LAPTOP != 1
#error TOOLBAR_ROWS_LAPTOP must be 0 or 1
#endif
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

#if TOOLBAR_ROWS_LAPTOP == 1
  /** One complete band layout per bracket, matching the aligned-title probe.
      The vehicle owns eight bands; lookup and report IO are outside timing. */
  template <class Clock>
  bool
  MeasureLaptopBands(app::scene::Scene &scene, ToolboxScenePlatformController &controller, Clock &clock, std::FILE *out)
  {
    for (unsigned variant = 0; variant != 8; ++variant)
    {
      app::scene::Node *band = 0;
      dsl::FlowError error;
      if (dsl::testing::LookupNodeById<app::scene::Node>(&scene, testing::LaptopBandId(variant), band, error)
          != dsl::FLOW_STEP_SUCCEEDED)
        return false;
      app::scene::LayoutState offer;
      offer.width = 420;
      offer.height = 30;
      offer.lineHeight = ToolboxLayoutMetrics::kDefaultLineHeight;
      offer.spacing = ToolboxLayoutMetrics::kDefaultGap;
      offer.inputs = app::scene::NODE_DIRTY_LAYOUT;
      unsigned long samples[31];
      int occupied = 0;
      for (unsigned i = 0; i != 31; ++i)
      {
        app::scene::LayoutState state = offer;
        const unsigned long start = clock.now();
        occupied = LayoutNode(band, state, &controller, 0);
        samples[i] = clock.now() - start;
      }
      std::sort(samples, samples + 31);
      std::fprintf(out,
                   "laptop_band case=laptop-band-%s lead='%s' title='%s' repetitions=31 median_us=%lu "
                   "maximum_us=%lu offer=420 occupied=%d font=Chicago-12 title_font=18-bold source=toolbox\n",
                   variant < 4 ? "row" : "toolbar",
                   variant % 4 < 2 ? "Wide" : "< Back",
                   variant % 2 ? "PowerBook G4 Titanium" : "SmirkyTracker",
                   samples[15],
                   samples[30],
                   occupied);
    }
    return true;
  }

#endif

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

#if TOOLBAR_ROWS_LAPTOP == 0
  /** Each state owns a fresh App and native window, so both build and hit use
      the real App cache without a test reset door or production counters. */
  class WidthAuditApp : public ToolboxApp
  {
  public:
    explicit WidthAuditApp(AppConfigurable &config)
        : ToolboxApp(&config)
    {
    }
    virtual ~WidthAuditApp() {}
  };

  bool AuditFont(const char *name, short &font)
  {
    Str255 requested, actual;
    requested[0] = static_cast<unsigned char>(std::strlen(name));
    std::memcpy(requested + 1, name, requested[0]);
    GetFNum(requested, &font);
    GetFontName(font, actual);
    return actual[0] == requested[0] && !std::memcmp(actual + 1, requested + 1, requested[0]);
  }

  /** Include native scope entry and restoration in the warmed baseline. */
  short FirstUseNative(const ToolboxScenePlatformController &controller,
                       const ToolboxTextSelection &selection,
                       const unsigned char *text)
  {
    ToolboxTextMeasureScope scope(controller, selection);
    return StringWidth(text);
  }

  /** Fresh owner per key; setup, encoding and report IO are outside the brackets.
      The complete door still includes its own encoding and both allocations. */
  template <class Clock>
  bool FirstUseState(AppConfigurable &config,
                     Clock &clock,
                     const char *name,
                     short font,
                     short size,
                     Style face,
                     std::FILE *out,
                     long &extra)
  {
    WidthAuditApp appOwner(config);
    ToolboxWindow window(config.getPlatformContext(), WindowProps().frame(1, 41, 100, 80).title("First use"));
    window.setApp(&appOwner);
    window.open();
    if (!window.window())
    {
      std::fprintf(out, "[fail] first_use sel=%s no-window\n", name);
      return false;
    }
    ToolboxScenePlatformController controller(&window);
    ToolboxTextMeasureScope restore(controller);
    TextFont(font);
    TextSize(size);
    TextFace(face);
    const app::TextStyle style;
    const ToolboxTextSelection selection = ToolboxTextSelection::resolve(
        ToolboxTextFontDescriptor(style), ToolboxTextPortFont::read(reinterpret_cast<GrafPtr>(window.window())));
    const core::String text = core::String::Literal("Medium title");
    Str255 encoded;
    if (!ToolboxEncodePascal(text, encoded))
    {
      std::fprintf(out, "[fail] first_use sel=%s encode\n", name);
      return false;
    }
    for (unsigned warm = 0; warm < 3; ++warm)
      FirstUseNative(controller, selection, encoded);
    unsigned long start = clock.now();
    const short nativeWidth = FirstUseNative(controller, selection, encoded);
    const unsigned long nativeUs = clock.now() - start;
    short firstWidth = 0, hitWidth = 0;
    start = clock.now();
    const bool firstAnswered = controller.measureTextAdvance(style, text, firstWidth);
    const unsigned long firstUs = clock.now() - start;
    start = clock.now();
    const bool hitAnswered = controller.measureTextAdvance(style, text, hitWidth);
    const unsigned long hitUs = clock.now() - start;
    extra = static_cast<long>(firstUs) - static_cast<long>(nativeUs);
    // Cache presence cannot prove that the second call opened no native scope.
    std::fprintf(out,
                 "first_use sel=%s native_us=%lu first_us=%lu hit_us=%lu extra_us=%ld table=?\n",
                 name,
                 nativeUs,
                 firstUs,
                 hitUs,
                 extra);
    const ToolboxCharWidthCache::Slot *slot = appOwner.charWidthCache().find(selection);
    if (!firstAnswered || !hitAnswered || firstWidth != nativeWidth || hitWidth != nativeWidth || !slot
        || !slot->widths)
    {
      std::fprintf(out, "[fail] first_use sel=%s missing-table-or-width-mismatch\n", name);
      return false;
    }
    return true;
  }

  template <class Clock> bool RunFirstUse(AppConfigurable &config, Clock &clock, std::FILE *out)
  {
    /** Names are log tokens; null family preserves the application-font alias. */
    const struct Selection
    {
      const char *name;
      const char *family;
      short size;
      Style face;
    } selections[] = {{"Geneva-12-plain", "Geneva", 12, normal},
                      {"Times-12-bold", "Times", 12, bold},
                      {"Monaco-9-plain", "Monaco", 9, normal},
                      {"application-1-12", 0, 12, normal},
                      {"Osaka-12-plain", "Osaka", 12, normal}};
    long maxExtra = LONG_MIN;
    bool ok = true;
    for (unsigned i = 0; i < sizeof(selections) / sizeof(selections[0]); ++i)
    {
      const Selection &selection = selections[i];
      short font = 1;
      if (selection.family && !AuditFont(selection.family, font))
      {
        std::fprintf(out, "[fail] first_use sel=%s missing-font\n", selection.name);
        ok = false;
        continue;
      }
      long extra = 0;
      const bool measured =
          FirstUseState(config, clock, selection.name, font, selection.size, selection.face, out, extra);
      if (extra > maxExtra)
        maxExtra = extra;
      if (!measured || extra > 3000)
        ok = false;
    }
    std::fprintf(out,
                 "first_use summary max_extra_us=%ld gate_us=3000 verdict=%s\n",
                 maxExtra == LONG_MIN ? 0 : maxExtra,
                 ok ? "PASS" : "FAIL");
    return ok;
  }

  /** Compare the complete door, including encoding, against the selected native
      answer. Unencodable native probe bytes are reported separately as skips. */
  unsigned AuditWidth(ToolboxScenePlatformController &controller, const unsigned char *text, unsigned &skipped)
  {
    core::String logical;
    if (!ToolboxDecodeNative(text + 1, text[0], logical))
    {
      ++skipped;
      return 0;
    }
    Str255 encoded;
    if (!ToolboxEncodePascal(logical, encoded) || encoded[0] != text[0] || std::memcmp(encoded + 1, text + 1, text[0]))
    {
      ++skipped;
      return 0;
    }
    short measured = 0;
    const bool answered = controller.measureTextAdvance(app::TextStyle(), logical, measured);
    ToolboxTextMeasureScope scope(controller);
    return !answered || measured != StringWidth(encoded) ? 1 : 0;
  }

  void AuditRandom(Str255 text, unsigned long &seed, bool ascii)
  {
    seed = seed * 1664525UL + 1013904223UL;
    text[0] = static_cast<unsigned char>(1 + ((seed >> 16) % 64));
    for (unsigned i = 1; i <= text[0]; ++i)
    {
      seed = seed * 1664525UL + 1013904223UL;
      unsigned byte = 32 + ((seed >> 16) % (ascii ? 95 : 223));
      if (!ascii && byte >= 127)
        ++byte;
      text[i] = static_cast<unsigned char>(byte);
    }
  }

  bool AuditWidthState(AppConfigurable &config,
                       const char *name,
                       short font,
                       short size,
                       Style face,
                       bool ascii,
                       int outside,
                       std::FILE *out)
  {
    WidthAuditApp appOwner(config);
    ToolboxWindow window(config.getPlatformContext(), WindowProps().frame(1, 41, 100, 80).title("Width audit"));
    window.setApp(&appOwner);
    window.open();
    if (!window.window())
    {
      std::fprintf(out, "width_audit group=%s [fail] no-window\n", name);
      return false;
    }
    ToolboxScenePlatformController controller(&window);
    ToolboxTextMeasureScope restore(controller);
    TextFont(font);
    TextSize(size);
    TextFace(face);
    if (outside == 2)
      SpaceExtra(65536L);
    const ToolboxTextSelection selection = ToolboxTextSelection::resolve(
        ToolboxTextFontDescriptor(), ToolboxTextPortFont::read(reinterpret_cast<GrafPtr>(window.window())));
    unsigned skipped = 0, failures = 0;
    const unsigned char first[] = {2, 'A', 'V'};
    failures += AuditWidth(controller, first, skipped);
    const ToolboxCharWidthCache::Slot *slot = appOwner.charWidthCache().find(selection);
    const bool built = slot && slot->widths;
    failures += AuditWidth(controller, first, skipped);
    if (!outside && !built)
      ++failures;
    if (outside)
    {
      if (outside >= 2 && built)
        ++failures;
      const unsigned char control[] = {3, 'A', 1, 'V'};
      const unsigned char high[] = {2, 0x80, 0xA0};
      failures += AuditWidth(controller, control, skipped);
      failures += AuditWidth(controller, high, skipped);
      std::fprintf(out, "width_audit group=%s mismatch=%u skipped=%u table=%d\n", name, failures, skipped, built);
      if (skipped)
        std::fprintf(out, "[skip] native high bytes cannot round-trip through this system script\n");
      return failures == 0;
    }
    Str255 text;
    for (unsigned byte = 32; byte <= (ascii ? 126U : 255U); ++byte)
      if (byte != 127)
      {
        text[0] = 1;
        text[1] = static_cast<unsigned char>(byte);
        failures += AuditWidth(controller, text, skipped);
      }
    unsigned long seed = 0x165302UL;
    const unsigned strings = ascii ? 200 : 80;
    for (unsigned i = 0; i < strings; ++i)
    {
      if (!ascii && i < 20)
      {
        const unsigned firstByte = i == 0 ? 32 : i == 1 ? 128 : 32 + (i - 2) * 11;
        const unsigned count = i == 0 ? 95 : i == 1 ? 128 : 24;
        text[0] = 0;
        for (unsigned byte = firstByte; byte < firstByte + count; ++byte)
          if (byte != 127 && byte <= 255)
            text[++text[0]] = static_cast<unsigned char>(byte);
      }
      else
        AuditRandom(text, seed, ascii);
      failures += AuditWidth(controller, text, skipped);
    }
    if (ascii)
      for (unsigned a = 32; a <= 126; ++a)
        for (unsigned b = 32; b <= 126; ++b)
        {
          const unsigned char pair[] = {2, static_cast<unsigned char>(a), static_cast<unsigned char>(b)};
          failures += AuditWidth(controller, pair, skipped);
        }
    const char *pairs[] = {"AV", "AW", "AY", "AT", "To", "Te",     "Ta",       "Tr",   "Wa",
                           "We", "Yo", "Ya", "LT", "LV", "LY",     "P.",       "F.",   "V.",
                           "y.", "r.", "ff", "fi", "fl", "AVAVAV", "Tomorrow", "WAVY", "LYTTA"};
    unsigned kern = 0;
    for (unsigned i = 0; i < sizeof(pairs) / sizeof(pairs[0]); ++i)
    {
      text[0] = static_cast<unsigned char>(std::strlen(pairs[i]));
      std::memcpy(text + 1, pairs[i], text[0]);
      kern += AuditWidth(controller, text, skipped);
    }
    std::fprintf(out,
                 "width_audit group=%s size=%d face=%u first_and_second=2 table=%d mismatch=%u kern=%u skipped=%u\n",
                 name,
                 size,
                 static_cast<unsigned>(face),
                 built,
                 failures,
                 kern,
                 skipped);
    if (skipped)
      std::fprintf(out,
                   "[skip] %u native probe strings cannot round-trip through this system script's logical encoder\n",
                   skipped);
    return failures == 0 && kern == 0;
  }

  bool RunWidthAudit(AppConfigurable &config, std::FILE *out)
  {
    const Boolean originalFractional = LMGetFractEnable();
    const long originalForce = GetScriptManagerVariable(smFontForce);
    LMSetFractEnable(false);
    const OSErr forceError = SetScriptManagerVariable(smFontForce, 0);
    if (forceError || GetScriptManagerVariable(smFontForce))
    {
      std::fprintf(out, "[skip] width audit cannot disable font forcing\nwidth_audit verdict=SKIP\n");
      LMSetFractEnable(originalFractional);
      SetScriptManagerVariable(smFontForce, originalForce);
      return false;
    }
    const char *names[] = {"Times", "Helvetica", "Geneva", "New York", "Courier", "Monaco"};
    const short sizes[] = {9, 10, 12, 14, 18, 24};
    const Style faces[] = {normal,
                           bold,
                           italic,
                           bold | italic,
                           underline,
                           outline,
                           shadow,
                           condense,
                           extend,
                           bold | italic | underline | shadow};
    bool ok = true;
    for (unsigned f = 0; f < 6; ++f)
    {
      short font = 0;
      if (!AuditFont(names[f], font))
      {
        std::fprintf(out, "[skip] missing font %s\n", names[f]);
        continue;
      }
      for (unsigned s = 0; s < 6; ++s)
        for (unsigned face = 0; face < 10; ++face)
          ok = AuditWidthState(config, names[f], font, sizes[s], faces[face], false, 0, out) && ok;
    }
    short osaka = 0;
    if (AuditFont("Osaka", osaka))
    {
      const short osakaSizes[] = {12, 12, 9, 10, 14, 18};
      for (unsigned s = 0; s < 6; ++s)
        ok = AuditWidthState(config, "Osaka", osaka, osakaSizes[s], s == 1 ? bold : normal, true, 0, out) && ok;
      ok = AuditWidthState(config, "controls-Japanese-high", osaka, 12, normal, true, 1, out) && ok;
    }
    else
      std::fprintf(out, "[skip] missing font Osaka\n");
    // Alias 1 is deliberately classified as 1, not merely logged as an app-font number.
    ok = AuditWidthState(config, "application-alias-1", 1, 12, normal, true, 0, out) && ok;
    ok = AuditWidthState(config, "SpaceExtra", 3, 12, normal, true, 2, out) && ok;
    LMSetFractEnable(true);
    ok = AuditWidthState(config, "fractional", 3, 12, normal, true, 3, out) && ok;
    LMSetFractEnable(false);
    if (!SetScriptManagerVariable(smFontForce, 1) && GetScriptManagerVariable(smFontForce))
      ok = AuditWidthState(config, "font-forcing", 3, 12, normal, true, 4, out) && ok;
    else
      std::fprintf(out, "[skip] cannot enable font forcing\n");
    SetScriptManagerVariable(smFontForce, originalForce);
    LMSetFractEnable(originalFractional);
    std::fprintf(out, "[skip] native trap-call counts unavailable; host pins discriminate fallback routing\n");
    std::fprintf(out, "[skip] chExtra: audit windows are monochrome NewWindow ports\n");
    std::fprintf(out, "width_audit verdict=%s\n", ok ? "PASS" : "FAIL");
    return ok;
  }
#endif

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
#if TOOLBAR_ROWS_LAPTOP == 1
                         .title("Toolbar LAPTOP band cost")
                         .scene(app::scene::Boundary<testing::LaptopBands>())
#else
                         .title("Toolbar Row cost")
                         .scene(app::scene::Boundary<testing::ToolbarRows>())
#endif
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
#if TOOLBAR_ROWS_LAPTOP == 1
          const bool ok =
              environmentKnown && resolution != ~0UL && MeasureLaptopBands(*window->scene(), *controller, self, out);
          std::fprintf(out, "laptop_band_summary complete=%d cases=8\n", ok);
#else
          const bool ok = environmentKnown && resolution != ~0UL
                          && testing::RunToolbarRows(*window->scene(), *controller, self, out)
                          && MeasureAlignedTitle(*window->scene(), *controller, self, out)
                          && MeasureAlignedTitleDraw(*window->asToolboxWindow(), self, out);
          std::fprintf(out, "toolbar_row_summary complete=%d cases=8\n", ok);
          RunFirstUse(self, self, out);
          RunWidthAudit(self, out);
#endif
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
