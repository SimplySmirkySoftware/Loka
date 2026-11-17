// #1828 runtime probe for macOS 10.4-10.8: a clip box has no setClipsToBounds:
// there, so LokaClipHostView relies on AppKit's default subview clipping.
// The probe lays out a clipped Box and its unclipped twin with the production
// MacScenePlatformController, puts a red ink view across the box edge, and
// checks pixels (view cache and window backing store) and hit tests.
// Usage: ClipProbe <result-file>. Exit 0 = every check passed.
#include "MacScenePlatformController.hpp"
#include "app/nodes/nestable/Box.hpp"
#include "app/RectSurface.hpp"
#import <Cocoa/Cocoa.h>
#include <cstdio>

using namespace loka::app;
using namespace loka::app::scene;

@interface ProbeRoot : NSView
@end
@implementation ProbeRoot
- (BOOL)isFlipped
{
  return YES;
}
@end
@interface ProbeInk : NSView
@end
@implementation ProbeInk
- (BOOL)isFlipped
{
  return YES;
}
- (void)drawRect:(NSRect)dirty
{
  (void)dirty;
  [[NSColor redColor] set];
  NSRectFill([self bounds]);
}
@end

static FILE *out = 0;
static int failures = 0;
static void probeCheck(bool ok, const char *what)
{
  std::fprintf(out, "%s %s\n", ok ? "PASS" : "FAIL", what);
  std::fflush(out);
  if (!ok)
    ++failures;
}

static bool isRed(NSColor *c)
{
  c = [c colorUsingColorSpaceName:NSCalibratedRGBColorSpace];
  return c && [c redComponent] > 0.8 && [c greenComponent] < 0.3 && [c blueComponent] < 0.3;
}

static void countRed(NSBitmapImageRep *rep, NSRect space, NSRect box, unsigned &inside, unsigned &outside, bool flippedRows)
{
  inside = outside = 0;
  const float scale = [rep pixelsWide] / space.size.width;
  for (int y = 0; y < [rep pixelsHigh]; ++y)
    for (int x = 0; x < [rep pixelsWide]; ++x)
      if (isRed([rep colorAtX:x y:y]))
      {
        // Rows of both reps start at the top; root is flipped, so row y maps to root y.
        float ry = (y + 0.5f) / scale;
        if (!flippedRows)
          ry = space.size.height - ry;
        if (NSPointInRect(NSMakePoint(space.origin.x + (x + 0.5f) / scale, space.origin.y + ry), box))
          ++inside;
        else
          ++outside;
      }
}

static NSView *findClass(NSView *parent, NSString *name)
{
  Class type = NSClassFromString(name);
  NSArray *views = [parent subviews];
  for (unsigned i = 0; i < [views count]; ++i)
  {
    NSView *v = [views objectAtIndex:i];
    if (type && [v isKindOfClass:type])
      return v;
    NSView *deeper = findClass(v, name);
    if (deeper)
      return deeper;
  }
  return nil;
}

int main(int argc, char **argv)
{
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  out = std::fopen(argc > 1 ? argv[1] : "/tmp/clipprobe-result.txt", "w");
  if (!out)
    return 2;
  [NSApplication sharedApplication];
  std::fprintf(out, "AppKit %.2f\n", NSAppKitVersionNumber);
  probeCheck(![[[[NSView alloc] init] autorelease] respondsToSelector:@selector(setClipsToBounds:)],
        "no setClipsToBounds: (the default-clipping branch is the one under test)");
  NSRect boxRect = NSZeroRect;
  for (int clipped = 1; clipped >= 0; --clipped)
  {
    NSAutoreleasePool *inner = [[NSAutoreleasePool alloc] init];
    ProbeRoot *root = [[ProbeRoot alloc] initWithFrame:NSMakeRect(0, 0, 300, 200)];
    NSWindow *window = [[NSWindow alloc] initWithContentRect:NSMakeRect(80, 80, 300, 200)
                                                   styleMask:NSTitledWindowMask
                                                     backing:NSBackingStoreBuffered
                                                       defer:NO];
    [window setReleasedWhenClosed:NO];
    [window setContentView:root];
    [window orderFrontRegardless];
    {
      MacScenePlatformController controller(root, RailMetrics());
      BoxProps props;
      props.setSize(100, 60);
      props.childClip_ = clipped ? layout::CHILD_CLIP_OWN_RECT : layout::CHILD_CLIP_NONE;
      BoxNode *box = new BoxNode(props);
      box->addChild(new RectSurfaceNode(RectSurfaceProps().size(100, 100)));
      controller.onChange(box, NODE_DIRTY_NONE, false);
      controller.relayout(300, 200);
      NSView *host = findClass(root, @"LokaClipHostView");
      NSView *parent = root;
      if (clipped)
      {
        probeCheck(host != nil, "clipped: a LokaClipHostView exists");
        if (host)
        {
          boxRect = [host frame];
          std::fprintf(out, "host frame (%.0f,%.0f %.0fx%.0f) bounds origin (%.0f,%.0f)\n", boxRect.origin.x,
                       boxRect.origin.y, boxRect.size.width, boxRect.size.height, [host bounds].origin.x,
                       [host bounds].origin.y);
          probeCheck(boxRect.size.width == 100 && boxRect.size.height == 60, "clipped: host is the box's 100x60");
          probeCheck(NSEqualPoints([host bounds].origin, boxRect.origin), "clipped: bounds origin equals frame origin");
          parent = host;
        }
      }
      else
        probeCheck(host == nil, "twin: no clip host");
      // The ink crosses the box's right and bottom edges; coordinates are root
      // coordinates in both cases because the host's bounds origin is its frame origin.
      NSRect inkRect = NSMakeRect(boxRect.origin.x + 40, boxRect.origin.y + 30, 80, 120);
      ProbeInk *ink = [[ProbeInk alloc] initWithFrame:inkRect];
      [parent addSubview:ink];
      [ink release];
      [window display];
      [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.3]];
      [window display];

      unsigned in1 = 0, out1 = 0, in2 = 0, out2 = 0;
      NSBitmapImageRep *cache = [root bitmapImageRepForCachingDisplayInRect:[root bounds]];
      [root cacheDisplayInRect:[root bounds] toBitmapImageRep:cache];
      countRed(cache, [root bounds], boxRect, in1, out1, true);
      [root lockFocus];
      NSBitmapImageRep *backing = [[[NSBitmapImageRep alloc] initWithFocusedViewRect:[root bounds]] autorelease];
      [root unlockFocus];
      countRed(backing, [root bounds], boxRect, in2, out2, true);
      std::fprintf(out, "%s: view cache red in=%u out=%u; backing store red in=%u out=%u\n",
                   clipped ? "clipped" : "twin", in1, out1, in2, out2);
      probeCheck(in1 > 0 && in2 > 0, clipped ? "clipped: ink drawn inside the box" : "twin: ink drawn inside the box");
      if (clipped)
        probeCheck(out1 == 0 && out2 == 0, "clipped: no ink outside the box (cache and backing store)");
      else
        probeCheck(out1 > 0 && out2 > 0, "twin: ink outside the box (the sample can see a missing clip)");

      // Hit tests in the content view's superview space.
      NSView *frameView = [root superview];
      NSPoint insidePt = [frameView convertPoint:NSMakePoint(boxRect.origin.x + 60, boxRect.origin.y + 45)
                                        fromView:root];
      NSPoint outsidePt = [frameView convertPoint:NSMakePoint(boxRect.origin.x + 60, boxRect.origin.y + 120)
                                         fromView:root];
      NSView *hitIn = [root hitTest:insidePt];
      NSView *hitOut = [root hitTest:outsidePt];
      probeCheck(hitIn == ink, clipped ? "clipped: inside point hits the ink" : "twin: inside point hits the ink");
      if (clipped)
        probeCheck(hitOut != ink, "clipped: point below the box does not hit the ink");
      else
        probeCheck(hitOut == ink, "twin: point below the box hits the ink");

      controller.onChange(0, NODE_DIRTY_NONE, false);
      delete box;
      controller.drainNativeRetirements();
    }
    [window orderOut:nil];
    [window close];
    [window release];
    [root release];
    [inner drain];
  }
  std::fprintf(out, "RESULT %s failures=%d\n", failures ? "FAIL" : "PASS", failures);
  std::fclose(out);
  [pool drain];
  return failures ? 1 : 0;
}
