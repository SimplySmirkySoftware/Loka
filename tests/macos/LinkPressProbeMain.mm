// Build with TARGET=LokaLinkPressProbeMacOS scripts/macos/build.sh (Tiger: scripts/macos/build-10_4.sh
// with the same TARGET). From Terminal run the bundle executable, e.g.
// build/macos/Release/apple/macos/LokaLinkPressProbeMacOS.app/Contents/MacOS/LokaLinkPressProbeMacOS
// Append --self-check for the posted 41/41, 42/99, 43/43 control. Omit it for physical input.
//
// Step  Physical gesture                          Required observation (activation delta)
// P0    Click alpha                               DOWN, UP, alpha +1; otherwise rig inconclusive
// P1    Click alpha                               Matching K1, clicks=1, alpha +1
// P2    Pause, click alpha again                  Matching K2, clicks=1, alpha +1
// P3    Double-click beta                         Matching K3 then K4, clicks=1 then 2, beta +2
// P4    Press alpha, drag to beta, release        DOWN and UP, +0
// P5    Press alpha, release outside the window   Record whether UP entered, +0
// P6    Control-click a link                      DOWN, +0
// P7    Click the wrapped link on its second line wrapped +1
// H1: K1..K4 distinct and nonzero on Tahoe and Mavericks (Tiger where possible).
// H2: exact P1/P3/P4/P7 targets and deltas on Tiger PPC and 10.4u-built i386 on Intel.
// Record H1/H2 failures for the configuration disable ruling. P5/P6 are observational.

#include "LinkPressProbeApp.hpp"
#include "../support/MacLinkPressEvent.hpp"
#include "testing/MacLinkPressHooks.hpp"
#include "app/bootstrap/PlatformBootstrap.hpp"
#include "app/core/App.hpp"
#include "core/util/ScopedPtr.hpp"
#include <cstring>

namespace
{
  using namespace loka::testing;
  using link_press_probe::ProbeApp;

  const char *Slice()
  {
#if defined(__ppc__) || defined(__POWERPC__)
    return "ppc";
#elif defined(__i386__)
    return "i386";
#elif defined(__x86_64__)
    return "x86_64";
#elif defined(__arm64__) || defined(__aarch64__)
    return "arm64";
#else
#error Unsupported link press probe slice
#endif
  }

  // This Column mounts exactly two native siblings under the probe window.
  bool FixtureViews(NSWindow *window, NSView *&leaf, NSTextField *&label)
  {
    NSArray *views = [[window contentView] subviews];
    if ([views count] != 2)
      return false;
    const bool labelFirst = [[views objectAtIndex:0] isKindOfClass:[NSTextField class]];
    leaf = [views objectAtIndex:labelFirst ? 1 : 0];
    label = (NSTextField *)[views objectAtIndex:labelFirst ? 0 : 1];
    return [leaf respondsToSelector:@selector(lokaLayoutManager)] && [label isKindOfClass:[NSTextField class]];
  }

  struct Pair
  {
    NSInteger down, up;
    NSString *after;
  };
  const Pair pairs[] = {
      {41, 41, @"last: alpha  count: 1"}, {42, 99, @"last: alpha  count: 1"}, {43, 43, @"last: alpha  count: 2"}};
  const unsigned pairCount = sizeof(pairs) / sizeof(pairs[0]);

  class SelfCheck : public ProbeApp
  {
  public:
    explicit SelfCheck(PlatformContext *context)
        : ProbeApp(context),
          app(0),
          posted_(0),
          entries_(0),
          elapsed_(0),
          failure_(0),
          passed_(false)
    {
    }

    virtual loka::app::IdlePolicy idlePolicy() const
    {
      return loka::app::IdlePolicy::interval(0.1);
    }

    void entry(MacLinkPressVisit visit, const MacLinkPressEntry &entry)
    {
      const bool down = this->entries_ % 2 == 0;
      const unsigned pair = this->entries_ / 2;
      if (pair >= this->posted_ || down != (visit == MAC_LINK_DOWN_ENTRY)
          || entry.eventNumber != (down ? pairs[pair].down : pairs[pair].up) || entry.clickCount != 1)
        this->failure_ = "unexpected entry direction, event number or click count";
      ++this->entries_;
    }

    virtual void onIdle(double elapsed)
    {
      this->elapsed_ += elapsed;
      if (this->failure_)
      {
        this->app->quit();
        return;
      }
      NSWindow *window = [NSApp keyWindow];
      if (this->entries_ != this->posted_ * 2)
        return this->waitFor("missing DOWN or UP entry");
      if (!window || ![window isVisible])
        return this->waitFor("window did not become key and visible");
      NSView *leaf;
      NSTextField *label;
      if (!FixtureViews(window, leaf, label))
        return this->waitFor("linked text or result label unavailable");
      NSString *expected = this->posted_ ? pairs[this->posted_ - 1].after : @"last: none  count: 0";
      if (![[label stringValue] isEqualToString:expected])
        return this->waitFor("unexpected activation target or count");
      if (this->posted_ == pairCount)
      {
        this->passed_ = true;
        this->app->quit();
        return;
      }
      [window displayIfNeeded];
      NSPoint centre;
      if (!queryMacPressGlyph(leaf, 2, centre))
        return this->waitFor("alpha glyph unavailable");
      NSPoint point = [leaf convertPoint:centre toView:nil];
      const Pair &pair = pairs[this->posted_];
      NSEvent *down = macPressEvent(window, PRESS_DOWN, point, pair.down, 0, 1);
      NSEvent *up = macPressEvent(window, PRESS_UP, point, pair.up, 0, 1);
      if (!down || !up)
      {
        this->failure_ = "could not create stamped pair";
        this->app->quit();
        return;
      }
      ++this->posted_;
      this->elapsed_ = 0;
      [NSApp postEvent:down atStart:NO];
      [NSApp postEvent:up atStart:NO];
    }

    int result() const
    {
      if (this->passed_)
        std::puts("self-check PASS");
      else
        std::printf("self-check FAIL %s (pairs posted=%u, entries=%u)\n",
                    this->failure_ ? this->failure_ : "run ended early",
                    this->posted_,
                    this->entries_);
      std::fflush(stdout);
      return this->passed_ ? 0 : 1;
    }

    App *app;

  private:
    void waitFor(const char *reason)
    {
      if (this->elapsed_ >= 5)
      {
        this->failure_ = reason;
        this->app->quit();
      }
    }

    unsigned posted_, entries_;
    double elapsed_;
    const char *failure_;
    bool passed_;
  };

  struct Observe
  {
    explicit Observe(SelfCheck *check)
    {
      observeMacLinkPress(&visit, check);
    }
    ~Observe()
    {
      observeMacLinkPress(0, 0);
    }
    static void visit(MacLinkPressVisit v, void *data)
    {
      if (v != MAC_LINK_DOWN_ENTRY && v != MAC_LINK_UP_ENTRY)
        return;
      MacLinkPressEntry entry;
      if (!queryMacLinkPressEntry(entry))
        return;
      std::printf(
          "%s n=%ld clicks=%ld\n", v == MAC_LINK_DOWN_ENTRY ? "down" : "up", entry.eventNumber, entry.clickCount);
      std::fflush(stdout);
      if (data)
        static_cast<SelfCheck *>(data)->entry(v, entry);
    }
  };

  int Run(bool checking)
  {
    std::printf("probe rev=%s compiler=%s sdk=%d os=%s slice=%s\n",
                LOKA_PROBE_REVISION,
                __VERSION__,
                MAC_OS_X_VERSION_MAX_ALLOWED,
                [[[NSProcessInfo processInfo] operatingSystemVersionString] UTF8String],
                Slice());
    std::fflush(stdout);
    loka::platform::InitPlatformRuntime();
    loka::core::ScopedPtr<PlatformContext> context(loka::platform::CreatePlatformContext());
    if (context.get())
    {
      ProbeApp manual(context.get());
      SelfCheck selfCheck(context.get());
      AppConfigurable *config = checking ? &selfCheck : &manual;
      loka::core::ScopedPtr<App> app(context->createApp(config, 0, 0));
      if (app.get())
      {
        selfCheck.app = app.get();
        {
          Observe observe(checking ? &selfCheck : 0);
          app->run();
        }
        return checking ? selfCheck.result() : 0;
      }
    }
    std::puts(checking ? "self-check FAIL bootstrap" : "probe bootstrap failed");
    std::fflush(stdout);
    return 1;
  }
} // namespace

int main(int argc, char **argv)
{
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  bool checking = false;
  for (int i = 1; i < argc; ++i)
    if (std::strcmp(argv[i], "--self-check") == 0)
      checking = true;
  const int result = Run(checking);
  [pool drain];
  return result;
}
