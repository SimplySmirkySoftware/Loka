#include "app/bootstrap/RunApp.hpp"
#include "MyAppConfig.hpp"
#include <AvailabilityMacros.h>
#include <Cocoa/Cocoa.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <cerrno>
#include <climits>
#include <mach/mach_time.h>
#if defined(MAC_OS_X_VERSION_10_5) && MAC_OS_X_VERSION_MAX_ALLOWED >= MAC_OS_X_VERSION_10_5
#import <QuartzCore/CALayer.h>
#define CARDSHOWCASE_PROBE_LAYERS 1
#endif

// PROBE ONLY: LOKA_PROBE_SNAPSHOT=<png> captures this process's first visible
// window from the window server after a short delay, then exits. The window
// list API arrived in 10.5, so a 10.4 SDK build reports the capture as unavailable.
#if defined(MAC_OS_X_VERSION_10_5) && MAC_OS_X_VERSION_MAX_ALLOWED >= MAC_OS_X_VERSION_10_5
#define CARDSHOWCASE_PROBE_CAPTURE 1
typedef CGImageRef (*CardShowcaseWindowImageFn)(CGRect, CGWindowListOption, CGWindowID, CGWindowImageOption);
#endif

@interface CardShowcaseProbeSnapshot : NSObject
- (void)capture:(NSTimer *)timer;
- (void)scrollBy:(NSTimer *)timer;
- (void)benchmark:(NSTimer *)timer;
@end
@implementation CardShowcaseProbeSnapshot
// PROBE ONLY: LOKA_PROBE_SCROLL_BY=<points> scrolls the first NSScrollView through
// its clip view (the user's scrolling path, copiesOnScroll included) before the capture.
static NSScrollView *FindScrollView(NSView *view)
{
  if ([view isKindOfClass:[NSScrollView class]])
    return (NSScrollView *)view;
  NSArray *subviews = [view subviews];
  for (unsigned int i = 0; i < (unsigned int)[subviews count]; ++i)
  {
    NSScrollView *found = FindScrollView([subviews objectAtIndex:i]);
    if (found)
      return found;
  }
  return nil;
}
// PROBE ONLY: synchronous scroll/display work; no snapshot API or CA headers.
- (void)benchmark:(NSTimer *)timer
{
  const long steps = [[timer userInfo] longValue];
  NSWindow *window = nil;
  NSScrollView *scroll = nil;
  NSArray *windows = [NSApp windows];
  for (unsigned int i = 0; i < (unsigned int)[windows count]; ++i)
  {
    NSWindow *candidate = [windows objectAtIndex:i];
    if (![candidate isVisible])
      continue;
    scroll = FindScrollView([candidate contentView]);
    if (scroll)
    {
      window = candidate;
      break;
    }
  }
  if (!scroll || ![scroll documentView])
  {
    std::fprintf(stderr, "probe: bench scroll target not found\n");
    std::exit(2);
  }
  NSClipView *clip = [scroll contentView];
  const NSRect document = [[scroll documentView] bounds];
  const double top = NSMinY(document);
  const double end = NSMaxY(document) - [clip bounds].size.height;
  Class transaction = NSClassFromString(@"CATransaction");
  const bool canFlush = transaction && [transaction respondsToSelector:@selector(flush)];
  mach_timebase_info_data_t timebase = {0, 0};
  if (mach_timebase_info(&timebase) != KERN_SUCCESS || !timebase.denom)
  {
    std::fprintf(stderr, "probe: bench timebase unavailable\n");
    std::exit(2);
  }
  [window displayIfNeeded];
  if (canFlush)
    [transaction performSelector:@selector(flush)];
  const uint64_t started = mach_absolute_time();
  for (long step = 0; step < steps; ++step)
  {
    NSAutoreleasePool *iterationPool = [[NSAutoreleasePool alloc] init];
    NSPoint origin = [clip bounds].origin;
    origin.y += 4;
    if (origin.y >= end)
      origin.y = top;
    [clip scrollToPoint:origin];
    [scroll reflectScrolledClipView:clip];
    [window displayIfNeeded];
    if (canFlush)
      [transaction performSelector:@selector(flush)];
    [iterationPool release];
  }
  const double milliseconds = static_cast<double>(mach_absolute_time() - started) *
                              timebase.numer / timebase.denom / 1000000.0;
  std::fprintf(stderr, "probe: bench steps=%ld total=%.3f per-step=%.3f\n",
               steps, milliseconds, milliseconds / steps);
  std::exit(0);
}

- (void)scrollBy:(NSTimer *)timer
{
  const double dy = [[timer userInfo] doubleValue];
  NSArray *windows = [NSApp windows];
  for (unsigned int i = 0; i < (unsigned int)[windows count]; ++i)
  {
    NSWindow *window = [windows objectAtIndex:i];
    if (![window isVisible])
      continue;
    NSScrollView *scroll = FindScrollView([window contentView]);
    if (!scroll)
      break;
    NSClipView *clip = [scroll contentView];
    // DIAG (owner idea): LOKA_PROBE_NULLFILTER=scroll|clip|doc gives that view an
    // identity content filter before the scroll, so Core Animation renders it as
    // its own group.
    const char *nullTarget = std::getenv("LOKA_PROBE_NULLFILTER");
    Class filterClass = NSClassFromString(@"CIFilter");
    if (nullTarget && filterClass)
    {
      NSView *target = std::strcmp(nullTarget, "clip") == 0  ? (NSView *)clip
                       : std::strcmp(nullTarget, "doc") == 0 ? [scroll documentView]
                                                             : (NSView *)scroll;
      id identity = [filterClass performSelector:@selector(filterWithName:) withObject:@"CIAffineTransform"];
      [identity performSelector:@selector(setDefaults)];
      if (identity && [target respondsToSelector:@selector(setContentFilters:)])
      {
        [target setWantsLayer:YES];
        [target performSelector:@selector(setContentFilters:) withObject:[NSArray arrayWithObject:identity]];
        std::fprintf(stderr, "probe: null content filter on %s\n", nullTarget);
      }
    }
    NSPoint origin = [clip bounds].origin;
    origin.y += dy;
    [clip scrollToPoint:origin];
    [scroll reflectScrolledClipView:clip];
    // DIAG: 10.5/10.6 blur the whole scrolled area once the clip scrolls under a
    // filtered sibling; LOKA_PROBE_REDISPLAY=1 asks the document and clip to redraw.
    // DIAG: report the blurred unit's masksToBounds at scroll time; LOKA_PROBE_MASK_AT_SCROLL=1
    // sets it again right before the scroll (AppKit may reset properties of a layer it owns).
    Class blurUnit = NSClassFromString(@"LokaStackBlurUnitView");
#ifdef CARDSHOWCASE_PROBE_LAYERS
    if (blurUnit)
    {
      NSArray *peers = [[scroll superview] subviews];
      for (unsigned int k = 0; k < (unsigned int)[peers count]; ++k)
      {
        NSView *unit = [peers objectAtIndex:k];
        if (![unit isKindOfClass:blurUnit])
          continue;
        CALayer *layer = [unit layer];
        std::fprintf(stderr, "probe: unit layer=%p class=%s masksToBounds=%d (before)\n", (void *)layer,
                     layer ? [NSStringFromClass([layer class]) UTF8String] : "-", layer ? (int)[layer masksToBounds] : -1);
        if (layer && std::getenv("LOKA_PROBE_MASK_AT_SCROLL"))
        {
          [layer setMasksToBounds:YES];
          std::fprintf(stderr, "probe: unit masksToBounds=%d (set)\n", (int)[layer masksToBounds]);
        }
      }
    }
#endif
    // DIAG: LOKA_PROBE_REAPPLY=1 removes and re-sets the blurred unit's filters.
    if (std::getenv("LOKA_PROBE_REAPPLY") && blurUnit)
    {
      NSArray *siblings = [[scroll superview] subviews];
      for (unsigned int k = 0; k < (unsigned int)[siblings count]; ++k)
      {
        NSView *unit = [siblings objectAtIndex:k];
        if (![unit isKindOfClass:blurUnit] || ![unit respondsToSelector:@selector(backgroundFilters)])
          continue;
        NSArray *filters = [[unit performSelector:@selector(backgroundFilters)] retain];
        [unit performSelector:@selector(setBackgroundFilters:) withObject:nil];
        [unit performSelector:@selector(setBackgroundFilters:) withObject:filters];
        [filters release];
        std::fprintf(stderr, "probe: reapplied filters\n");
      }
    }
    if (std::getenv("LOKA_PROBE_REDISPLAY"))
    {
      [[scroll documentView] setNeedsDisplay:YES];
      [clip setNeedsDisplay:YES];
      [scroll setNeedsDisplay:YES];
      [[window contentView] setNeedsDisplay:YES];
    }
    std::fprintf(stderr, "probe: scrolled by %.0f copiesOnScroll=%d\n", dy, [clip copiesOnScroll] ? 1 : 0);
    return;
  }
  std::fprintf(stderr, "probe: scroll target not found\n");
}
- (void)capture:(NSTimer *)timer
{
  NSString *path = [timer userInfo];
#ifndef CARDSHOWCASE_PROBE_CAPTURE
  std::fprintf(stderr, "probe: snapshot unavailable with this SDK (%s)\n", [path UTF8String]);
  std::exit(5);
#else
  NSWindow *target = nil;
  NSArray *windows = [NSApp windows];
  for (unsigned int i = 0; i < (unsigned int)[windows count]; ++i)
    if ([[windows objectAtIndex:i] isVisible])
    {
      target = [windows objectAtIndex:i];
      break;
    }
  CardShowcaseWindowImageFn create = (CardShowcaseWindowImageFn)dlsym(RTLD_DEFAULT, "CGWindowListCreateImage");
  if (!target || !create)
  {
    std::fprintf(stderr, "probe: snapshot failed (window=%p create=%p)\n", (void *)target, (void *)create);
    std::exit(2);
  }
  CGImageRef image = create(CGRectNull, kCGWindowListOptionIncludingWindow, (CGWindowID)[target windowNumber],
                            kCGWindowImageBoundsIgnoreFraming);
  if (!image)
  {
    std::fprintf(stderr, "probe: snapshot refused\n");
    std::exit(3);
  }
  NSBitmapImageRep *rep = [[NSBitmapImageRep alloc] initWithCGImage:image];
  CGImageRelease(image);
  NSData *png = [rep representationUsingType:(NSBitmapImageFileType)4 /* PNG on every SDK */ properties:[NSDictionary dictionary]];
  BOOL ok = [png writeToFile:path atomically:YES];
  std::fprintf(stderr, "probe: snapshot %s %dx%d -> %s\n", ok ? "written" : "NOT written", (int)[rep pixelsWide],
               (int)[rep pixelsHigh], [path UTF8String]);
  [rep release];
  std::exit(ok ? 0 : 4);
#endif
}
@end

int main(int argc, char **argv)
{
  (void)argc;
  (void)argv;

  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  // PROBE ONLY: CardShowcase turns the blurred bar on by default so a double-click
  // shows it; LOKA_PROBE_NO_BLUR=1 keeps the opaque bar for comparison. The unit
  // picks the material from the running AppKit.
  if (!std::getenv("LOKA_PROBE_NO_BLUR"))
    setenv("LOKA_PROBE_EXP_BLUR", "1", 0);
  // 10.5-10.9 filters are fixed (RGB tint, always-masking backing layer), so the
  // sample turns them on too; LOKA_PROBE_NO_LEGACY_FILTERS=1 keeps the opaque bar there.
  if (!std::getenv("LOKA_PROBE_NO_BLUR") && !std::getenv("LOKA_PROBE_NO_LEGACY_FILTERS"))
    setenv("LOKA_PROBE_LEGACY_FILTERS", "1", 0);
  const char *benchmark = std::getenv("LOKA_PROBE_SCROLL_BENCH");
  if (benchmark)
  {
    char *end = 0;
    errno = 0;
    const long steps = std::strtol(benchmark, &end, 10);
    if (errno == ERANGE || end == benchmark || *end != '\0' || steps < 1 || steps > INT_MAX)
    {
      std::fprintf(stderr, "probe: bench requires steps in 1..INT_MAX\n");
      return 2;
    }
    CardShowcaseProbeSnapshot *probe = [[CardShowcaseProbeSnapshot alloc] init];
    [NSTimer scheduledTimerWithTimeInterval:1.5
                                     target:probe
                                   selector:@selector(benchmark:)
                                   userInfo:[NSNumber numberWithLong:steps]
                                    repeats:NO];
    [probe release]; // The timer retains its target until it fires.
  }
  const char *snapshot = std::getenv("LOKA_PROBE_SNAPSHOT");
  if (snapshot && !benchmark)
  {
    const char *scrollBy = std::getenv("LOKA_PROBE_SCROLL_BY");
    CardShowcaseProbeSnapshot *probe = [[CardShowcaseProbeSnapshot alloc] init];
    if (scrollBy)
      [NSTimer scheduledTimerWithTimeInterval:1.5
                                       target:probe
                                     selector:@selector(scrollBy:)
                                     userInfo:[NSNumber numberWithDouble:std::atof(scrollBy)]
                                      repeats:NO];
    [NSTimer scheduledTimerWithTimeInterval:(scrollBy ? 3.0 : 2.5)
                                     target:probe
                                   selector:@selector(capture:)
                                   userInfo:[NSString stringWithUTF8String:snapshot]
                                    repeats:NO];
  }
  // PROBE ONLY: LOKA_PROBE_DARK=1 asks for the dark appearance inside this process (10.14+).
  if (std::getenv("LOKA_PROBE_DARK"))
  {
    NSApplication *app = [NSApplication sharedApplication];
    Class appearanceClass = NSClassFromString(@"NSAppearance");
    id dark = appearanceClass && [appearanceClass respondsToSelector:@selector(appearanceNamed:)]
                  ? [appearanceClass performSelector:@selector(appearanceNamed:) withObject:@"NSAppearanceNameDarkAqua"]
                  : nil;
    if (dark && [app respondsToSelector:@selector(setAppearance:)])
      [app performSelector:@selector(setAppearance:) withObject:dark];
    std::fprintf(stderr, "probe: dark appearance %s\n", dark ? "requested" : "unavailable");
  }
  int result = loka::platform::RunApp<CardShowcaseAppConfig>();
  (void)pool;
  return result;
}
