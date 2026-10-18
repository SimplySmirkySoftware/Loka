#include "app/bootstrap/RunApp.hpp"
#include "MyAppConfig.hpp"
#include <Cocoa/Cocoa.h>
#include <cstdio>
#include <cstdlib>
#include <dlfcn.h>

// PROBE ONLY: LOKA_PROBE_SNAPSHOT=<png> captures this process's first visible
// window from the window server after a short delay, then exits.
typedef CGImageRef (*CardShowcaseWindowImageFn)(CGRect, CGWindowListOption, CGWindowID, CGWindowImageOption);

@interface CardShowcaseProbeSnapshot : NSObject
- (void)capture:(NSTimer *)timer;
@end
@implementation CardShowcaseProbeSnapshot
- (void)capture:(NSTimer *)timer
{
  NSString *path = [timer userInfo];
  NSWindow *target = nil;
  NSArray *windows = [NSApp windows];
  for (NSUInteger i = 0; i < [windows count]; ++i)
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
}
@end

int main(int argc, char **argv)
{
  (void)argc;
  (void)argv;

  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  const char *snapshot = std::getenv("LOKA_PROBE_SNAPSHOT");
  if (snapshot)
  {
    CardShowcaseProbeSnapshot *probe = [[CardShowcaseProbeSnapshot alloc] init];
    [NSTimer scheduledTimerWithTimeInterval:2.5
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
