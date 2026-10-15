#include "MacGroundTests.hpp"
#include "MacGround.hpp"
#include "MacObjCCompat.hpp"
#include "MacScenePlatformController.hpp"
#include "app/RectSurface.hpp"
#include "app/nodes/controls/Cell.hpp"
#include "app/nodes/controls/Ribbon.hpp"
#include "app/nodes/ImageView.hpp"
#include "app/nodes/nestable/ScrollView.hpp"
#include "support/ContrastRatio.hpp"
#include "support/TestVerify.hpp"
#include <cmath>
#include <cstdio>

namespace
{
  NSColor *srgb(NSColor *color)
  {
    NSColor *result = [color colorUsingColorSpace:[NSColorSpace sRGBColorSpace]];
    LOKA_VERIFY(result != nil);
    return result;
  }

  double luminance(NSColor *color)
  {
    NSColor *rgb = srgb(color);
    return loka_test::RelativeLuminance([rgb redComponent], [rgb greenComponent], [rgb blueComponent]);
  }

  bool sameColor(NSColor *first, NSColor *second)
  {
    NSColor *a = srgb(first);
    NSColor *b = srgb(second);
    const double tolerance = 2.0 / 255.0;
    return std::fabs([a redComponent] - [b redComponent]) <= tolerance &&
           std::fabs([a greenComponent] - [b greenComponent]) <= tolerance &&
           std::fabs([a blueComponent] - [b blueComponent]) <= tolerance &&
           std::fabs([a alphaComponent] - [b alphaComponent]) <= tolerance;
  }

  /** Source-over of a possibly translucent color on an opaque ground. */
  NSColor *over(NSColor *top, NSColor *ground)
  {
    NSColor *t = srgb(top);
    NSColor *g = srgb(ground);
    const CGFloat a = [t alphaComponent];
    return [NSColor colorWithSRGBRed:a * [t redComponent] + (1.0 - a) * [g redComponent]
                               green:a * [t greenComponent] + (1.0 - a) * [g greenComponent]
                                blue:a * [t blueComponent] + (1.0 - a) * [g blueComponent]
                               alpha:a + (1.0 - a) * [g alphaComponent]];
  }

  typedef void (*AppearanceCheck)(bool dark);

  void inAppearances(AppearanceCheck check, bool legacyDrawing = false)
  {
    (void)legacyDrawing;
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
    [NSApplication sharedApplication];
#if MAC_OS_X_VERSION_MAX_ALLOWED >= 101400
    if ([NSAppearance respondsToSelector:@selector(appearanceNamed:)] &&
        [NSAppearance instancesRespondToSelector:@selector(bestMatchFromAppearancesWithNames:)])
    {
      NSString *names[] = {NSAppearanceNameAqua, NSAppearanceNameDarkAqua};
      for (int i = 0; i < 2; ++i)
      {
        NSAppearance *appearance = [NSAppearance appearanceNamed:names[i]];
        LOKA_VERIFY(appearance != nil);
#if MAC_OS_X_VERSION_MAX_ALLOWED >= 110000
        if (!legacyDrawing && [appearance respondsToSelector:@selector(performAsCurrentDrawingAppearance:)])
        {
          [appearance performAsCurrentDrawingAppearance:^{ check(i == 1); }];
        }
        else
#endif
        {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
          NSAppearance *previous = [[NSAppearance currentAppearance] retain];
          [NSAppearance setCurrentAppearance:appearance];
          check(i == 1);
          [NSAppearance setCurrentAppearance:previous];
          [previous release];
#pragma clang diagnostic pop
        }
      }
    }
    else
#endif
    {
      std::printf("[skip] dark appearance needs 10.14\n");
      check(false);
    }
    [pool drain];
  }

  // Render the real view's drawRect into a transparent offscreen surface.
  // Calling the drawing method avoids depending on a window's backing scale
  // or AppKit's cache-display treatment of transparent ancestor pixels.
  NSBitmapImageRep *render(NSView *view)
  {
    const NSRect bounds = [view bounds];
    NSBitmapImageRep *bitmap = [[[NSBitmapImageRep alloc]
        initWithBitmapDataPlanes:NULL pixelsWide:static_cast<NSInteger>(bounds.size.width)
        pixelsHigh:static_cast<NSInteger>(bounds.size.height) bitsPerSample:8 samplesPerPixel:4
        hasAlpha:YES isPlanar:NO colorSpaceName:NSDeviceRGBColorSpace bytesPerRow:0 bitsPerPixel:0] autorelease];
    LOKA_VERIFY(bitmap != nil);
    NSGraphicsContext *context = [NSGraphicsContext graphicsContextWithBitmapImageRep:bitmap];
    LOKA_VERIFY(context != nil);
    [NSGraphicsContext saveGraphicsState];
    [NSGraphicsContext setCurrentContext:context];
    [[NSColor clearColor] setFill];
#if MAC_OS_X_VERSION_MAX_ALLOWED >= 101200
    NSRectFillUsingOperation(bounds, NSCompositingOperationCopy);
#else
    NSRectFillUsingOperation(bounds, NSCompositeCopy);
#endif
    [view drawRect:bounds];
    [NSGraphicsContext restoreGraphicsState];
    return bitmap;
  }

  NSView *project(MacScenePlatformController &controller, NSView *parent,
                  loka::app::scene::Node &node)
  {
    controller.onChange(&node, loka::app::scene::NODE_DIRTY_NONE, false);
    controller.relayout(160, 100);
    LOKA_VERIFY([[parent subviews] count] == 1);
    NSView *view = [[parent subviews] objectAtIndex:0];
    // A fixed pixel extent makes samples independent of the layout policy.
    [view setFrame:NSMakeRect(0, 0, 120, 60)];
    return view;
  }

  void checkContrast(bool dark)
  {
    const loka::app::SurfaceGround grounds[] = {
        loka::app::SURFACE_GROUND_WINDOW, loka::app::SURFACE_GROUND_DOCUMENT,
        loka::app::SURFACE_GROUND_CONTROL};
    for (int i = 0; i < 3; ++i)
    {
      NSColor *ground = nil;
      LOKA_VERIFY(loka::macos::QueryMacGroundColor(grounds[i], ground));
      const double ratio = loka_test::ContrastRatio(
          luminance(loka::macos::MacTextRoleColor()), luminance(ground));
      std::printf("[contrast] %s ground=%d ratio=%.6f\n", dark ? "dark" : "light", grounds[i], ratio);
      LOKA_VERIFY(ratio >= 4.5);
    }
  }

  // controlShadowColor and colorUsingColorSpaceName: are the Tiger-era spellings
  // the edge view draws with; newer SDKs deprecate them.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
  void checkRibbonGroupEdge(bool dark)
  {
    (void)dark;
    using namespace loka::app;
    NSColor *ground = nil;
    LOKA_VERIFY(loka::macos::QueryMacGroundColor(SURFACE_GROUND_WINDOW, ground));
    const RibbonGroupEdge kinds[] = {RIBBON_GROUP_EDGE_LEFT, RIBBON_GROUP_EDGE_RIGHT, RIBBON_GROUP_EDGE_BOTTOM};
    NSView *parent = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 160, 100)];
    {
      MacScenePlatformController controller(parent, RailMetrics());
      for (int i = 0; i < 3; ++i)
      {
        RibbonGroupEdgeNode node((RibbonGroupEdgeProps(kinds[i])));
        NSView *view = project(controller, parent, node);
        LOKA_VERIFY([view isFlipped]);
        // A three-row bottom tells the solid bottom fill from a one-row blend.
        [view setFrame:i == 2 ? NSMakeRect(0, 0, 9, 3) : NSMakeRect(0, 0, 1, 9)];
        // render() calls drawRect directly into an unflipped bitmap context:
        // bitmap y=0 is the last drawn geometric row (the view's bottom).
        NSBitmapImageRep *bitmap = render(view);
        if (i == 2)
        {
          for (int y = 0; y < 3; ++y)
            for (int x = 0; x < 9; ++x)
              LOKA_VERIFY(sameColor([bitmap colorAtX:x y:y], [NSColor controlShadowColor]));
        }
        else
        {
          LOKA_VERIFY(sameColor([bitmap colorAtX:0 y:0], [NSColor controlShadowColor]));
          LOKA_VERIFY(sameColor([bitmap colorAtX:0 y:8], ground));
          NSColor *a = [[NSColor controlShadowColor] colorUsingColorSpaceName:NSCalibratedRGBColorSpace];
          NSColor *b = [ground colorUsingColorSpaceName:NSCalibratedRGBColorSpace];
          LOKA_VERIFY(a != nil && b != nil);
          NSColor *middle = [NSColor colorWithCalibratedRed:([a redComponent] + [b redComponent]) / 2
                                                      green:([a greenComponent] + [b greenComponent]) / 2
                                                       blue:([a blueComponent] + [b blueComponent]) / 2
                                                      alpha:([a alphaComponent] + [b alphaComponent]) / 2];
          LOKA_VERIFY(sameColor([bitmap colorAtX:0 y:4], middle));
          [view setFrame:NSMakeRect(0, 0, 1, 1)];
          LOKA_VERIFY(sameColor([render(view) colorAtX:0 y:0], [NSColor controlShadowColor]));
        }
        controller.onChange(0, scene::NODE_DIRTY_NONE, false);
      }
    }
    LOKA_VERIFY([[parent subviews] count] == 0);
    [parent release];
  }

#pragma clang diagnostic pop

  void checkCell(bool dark)
  {
    NSView *parent = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 160, 100)];
    {
      MacScenePlatformController controller(parent, loka::app::RailMetrics());
      loka::app::CellNode node(loka::app::CellProps().text("MMMM"));
      NSView *view = project(controller, parent, node);
      NSBitmapImageRep *bitmap = render(view);
      LOKA_VERIFY(sameColor([bitmap colorAtX:4 y:4], [NSColor controlBackgroundColor]));
      // The one-pixel border on each edge must differ from the ground it
      // frames. NSFrameRect draws with the fill color, so a border color set
      // only as the stroke came out in the ground color (#1208).
      const NSInteger edges[][2] = {{0, 30}, {119, 30}, {60, 0}, {60, 59}};
      for (int i = 0; i < 4; ++i)
        LOKA_VERIFY(!sameColor([bitmap colorAtX:edges[i][0] y:edges[i][1]],
                               [NSColor controlBackgroundColor]));
      std::printf("[pixels] Cell %s border=%.6f ground=%.6f\n", dark ? "dark" : "light",
                  luminance([bitmap colorAtX:0 y:30]), luminance([NSColor controlBackgroundColor]));
      // In dark appearance, glyph interiors must follow the bright text role;
      // the former fixed black cannot produce any such pixel.
      int textPixels = 0;
      const double textLuminance = luminance(loka::macos::MacTextRoleColor());
      for (NSInteger y = 15; y < 45; ++y)
        for (NSInteger x = 35; x < 85; ++x)
        {
          const double pixel = luminance([bitmap colorAtX:x y:y]);
          if (dark ? pixel >= textLuminance * 0.5 : pixel <= textLuminance + 0.05)
            ++textPixels;
        }
      std::printf("[pixels] Cell %s text=%d\n", dark ? "dark" : "light", textPixels);
      LOKA_VERIFY(textPixels > 20);
      controller.onChange(0, loka::app::scene::NODE_DIRTY_NONE, false);
    }
    [parent release];
  }

  void checkRect(bool dark)
  {
    (void)dark;
    NSView *parent = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 160, 100)];
    {
      MacScenePlatformController controller(parent, loka::app::RailMetrics());
      loka::app::RectSurfaceModel model;
      model.rectCount = 1;
      model.rects[0] = loka::app::RectSprite(20, 20, 20, 20);
      loka::core::MutableState<loka::app::RectSurfaceModel> state(model);
      loka::app::RectSurfaceNode node(loka::app::RectSurfaceProps().model(&state));
      NSBitmapImageRep *bitmap = render(project(controller, parent, node));
      LOKA_VERIFY(sameColor([bitmap colorAtX:4 y:4], [NSColor textBackgroundColor]));
      // Sprites compose over the document ground: the surface stays opaque
      // even though the text role (labelColor) carries alpha.
      LOKA_VERIFY(sameColor([bitmap colorAtX:30 y:30],
                            over(loka::macos::MacTextRoleColor(), [NSColor textBackgroundColor])));
      LOKA_VERIFY([[bitmap colorAtX:30 y:30] alphaComponent] == 1.0);
      controller.onChange(0, loka::app::scene::NODE_DIRTY_NONE, false);
    }
    [parent release];
  }

  void checkImage(bool dark)
  {
    (void)dark;
    NSView *parent = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 160, 100)];
    {
      MacScenePlatformController controller(parent, loka::app::RailMetrics());
      loka::app::ImageViewNode node((loka::app::ImageViewProps()));
      NSView *view = project(controller, parent, node);
      LOKA_VERIFY(![view isOpaque]);
      NSBitmapImageRep *bitmap = render(view);
      LOKA_VERIFY([[bitmap colorAtX:30 y:30] alphaComponent] == 0.0);
      controller.onChange(0, loka::app::scene::NODE_DIRTY_NONE, false);
    }
    [parent release];
  }
}

void testMacGroundRoles()
{
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  NSColor *out = [NSColor magentaColor];
  LOKA_VERIFY(!loka::macos::QueryMacGroundColor(loka::app::SURFACE_GROUND_TRANSPARENT, out));
  LOKA_VERIFY(out == [NSColor magentaColor]);
  LOKA_VERIFY(!loka::macos::QueryMacGroundColor(loka::app::SURFACE_GROUND_NATIVE, out));
  LOKA_VERIFY(out == [NSColor magentaColor]);
  LOKA_VERIFY(loka::macos::QueryMacGroundColor(loka::app::SURFACE_GROUND_WINDOW, out));
  LOKA_VERIFY(out == [NSColor windowBackgroundColor]);
  LOKA_VERIFY(loka::macos::QueryMacGroundColor(loka::app::SURFACE_GROUND_DOCUMENT, out));
  LOKA_VERIFY(out == [NSColor textBackgroundColor]);
  LOKA_VERIFY(loka::macos::QueryMacGroundColor(loka::app::SURFACE_GROUND_CONTROL, out));
  LOKA_VERIFY(out == [NSColor controlBackgroundColor]);
  [pool drain];
}

void testMacGroundLegibility() { inAppearances(checkContrast); }
void testMacGroundLegacyDrawingAppearance()
{
  // Exercise the 10.14-10.15 drawing route even on a newer host.
  inAppearances(checkContrast, true);
}
void testMacCellGroundAndTextPixels() { inAppearances(checkCell); }
void testMacRectSurfaceGroundAndSpritePixels() { inAppearances(checkRect); }
void testMacImageViewTransparentPixels() { inAppearances(checkImage); }

void testMacScrollViewTransparentBackground()
{
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  [NSApplication sharedApplication];
  NSView *parent = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 160, 100)];
  {
    MacScenePlatformController controller(parent, loka::app::RailMetrics());
    loka::core::MutableState<loka::core::Frame> storage(loka::core::Frame(0, 0, 0, 0));
    loka::core::PushStateTracker tracker;
    tracker.addState(&storage);
    loka::app::scene::NodeState<loka::core::Frame> offset(&storage, &tracker);
    loka::app::ScrollViewNode node((loka::app::ScrollViewProps(offset)));
    NSScrollView *view = (NSScrollView *)project(controller, parent, node);
    LOKA_VERIFY([view isKindOfClass:[NSScrollView class]]);
    LOKA_VERIFY(![view drawsBackground]);
    LOKA_VERIFY(![[view contentView] drawsBackground]);
    LOKA_VERIFY(![[view documentView] isOpaque]);
    [[view documentView] setFrame:NSMakeRect(0, 0, 120, 60)];
    NSBitmapImageRep *bitmap = render([view documentView]);
    LOKA_VERIFY([[bitmap colorAtX:1 y:1] alphaComponent] == 0.0);
    controller.onChange(0, loka::app::scene::NODE_DIRTY_NONE, false);
  }
  [parent release];
  [pool drain];
}

void testMacRibbonGroupEdgePixels()
{
  inAppearances(checkRibbonGroupEdge);
}
