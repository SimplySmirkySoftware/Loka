#include "app/bootstrap/RunApp.hpp"
#include "MyAppConfig.hpp"
#include <Cocoa/Cocoa.h>
#include <cstdio>
#include <cstdlib>
#include <dlfcn.h>

// PROBE ONLY: LOKA_PROBE_SNAPSHOT=<png> captures this process's first visible
// window from the window server after a short delay, then exits.
typedef CGImageRef (*ShowcaseWindowImageFn)(CGRect, CGWindowListOption, CGWindowID, CGWindowImageOption);

@interface ShowcaseProbeSnapshot : NSObject
- (void)capture:(NSTimer *)timer;
@end
@implementation ShowcaseProbeSnapshot
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
  ShowcaseWindowImageFn create = (ShowcaseWindowImageFn)dlsym(RTLD_DEFAULT, "CGWindowListCreateImage");
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
    ShowcaseProbeSnapshot *probe = [[ShowcaseProbeSnapshot alloc] init];
    [NSTimer scheduledTimerWithTimeInterval:2.5
                                     target:probe
                                   selector:@selector(capture:)
                                   userInfo:[NSString stringWithUTF8String:snapshot]
                                    repeats:NO];
  }
  int result = loka::platform::RunApp<ShowcaseAppConfig>();
  (void)pool;
  return result;
}
