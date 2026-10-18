#include "app/bootstrap/RunApp.hpp"
#include "MyAppConfig.hpp"
// PROBE ONLY: native timers, window-server capture and monotonic benchmark.
#include <AvailabilityMacros.h>
#include <Cocoa/Cocoa.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <cerrno>
#include <climits>
#include <mach/mach_time.h>
// PROBE ONLY: LOKA_PROBE_SNAPSHOT=<png> captures this process's first visible
// window from the window server after a short delay, then exits. The window
// list API arrived in 10.5, so a 10.4 SDK build reports the capture as unavailable.
#if defined(MAC_OS_X_VERSION_10_5) && MAC_OS_X_VERSION_MAX_ALLOWED >= MAC_OS_X_VERSION_10_5
#define CARDSHOWCASE_PROBE_CAPTURE 1
typedef CGImageRef (*CardShowcaseWindowImageFn)(CGRect, CGWindowListOption, CGWindowID, CGWindowImageOption);
#endif

// PROBE ONLY: timer-owned instrumentation; no persistent application state.
@interface CardShowcaseProbeSnapshot : NSObject
- (void)focusCaret:(NSTimer *)timer;
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
// PROBE ONLY: one depth-first view walk at the one-second timer, in the key window.
static NSTextField *FindTextField(NSView *view)
{
  if ([view isKindOfClass:[NSTextField class]] && [(NSTextField *)view isEditable])
    return (NSTextField *)view;
  NSArray *subviews = [view subviews];
  for (unsigned int i = 0; i < (unsigned int)[subviews count]; ++i)
  {
    NSTextField *found = FindTextField([subviews objectAtIndex:i]);
    if (found)
      return found;
  }
  return nil;
}
- (void)focusCaret:(NSTimer *)timer
{
  (void)timer;
  // PROBE ONLY: an ssh-launched app may not be frontmost; activate so the caret blinks,
  // and report the focus outcome so a silent log is not read as "no unit draws".
  [NSApp activateIgnoringOtherApps:YES];
  NSWindow *window = [NSApp keyWindow];
  if (!window && [[NSApp windows] count] > 0)
  {
    window = [[NSApp windows] objectAtIndex:0];
    [window makeKeyAndOrderFront:nil];
  }
  NSTextField *field = FindTextField([window contentView]);
  const bool focused = field && [window makeFirstResponder:field];
  if (focused)
  {
    NSText *editor = [field currentEditor];
    [editor setSelectedRange:NSMakeRange([[editor string] length], 0)];
  }
  std::fprintf(stderr, "probe: caret active=%d key=%d field=%p focused=%d\n", [NSApp isActive] ? 1 : 0,
               [window isKeyWindow] ? 1 : 0, (void *)field, focused ? 1 : 0);
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
    NSPoint origin = [clip bounds].origin;
    origin.y += dy;
    [clip scrollToPoint:origin];
    [scroll reflectScrolledClipView:clip];
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
  // PROBE ONLY: one-shot benchmark, scroll, snapshot and caret timers.
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
  const char *scrollBy = std::getenv("LOKA_PROBE_SCROLL_BY");
  if ((snapshot || scrollBy) && !benchmark)
  {
    CardShowcaseProbeSnapshot *probe = [[CardShowcaseProbeSnapshot alloc] init];
    if (scrollBy)
      [NSTimer scheduledTimerWithTimeInterval:1.5
                                       target:probe
                                     selector:@selector(scrollBy:)
                                     userInfo:[NSNumber numberWithDouble:std::atof(scrollBy)]
                                      repeats:NO];
    if (snapshot)
      [NSTimer scheduledTimerWithTimeInterval:(scrollBy ? 3.0 : 2.5)
                                     target:probe
                                   selector:@selector(capture:)
                                   userInfo:[NSString stringWithUTF8String:snapshot]
                                    repeats:NO];
    [probe release];
  }
  const char *caret = std::getenv("LOKA_PROBE_CARET");
  if (caret && std::strcmp(caret, "1") == 0)
  {
    CardShowcaseProbeSnapshot *probe = [[CardShowcaseProbeSnapshot alloc] init];
    [NSTimer scheduledTimerWithTimeInterval:1.0 target:probe selector:@selector(focusCaret:)
                                  userInfo:nil repeats:NO];
    [probe release];
  }
  int result = loka::platform::RunApp<CardShowcaseAppConfig>();
  // Leopard/PPC may crash while popping the outermost process-lifetime pool
  // during shutdown. The process is exiting here anyway, so let the OS reclaim
  // it instead of releasing the pool explicitly.
  (void)pool;
  return result;
}
