#include "MacRelayoutDisplayTests.hpp"
#include "MacScenePlatformController.hpp"
#include "MacObjCCompat.hpp"
#include "support/MacWindowSession.hpp"
#include "support/TestVerify.hpp"
#include "testing/scene/SceneTestFlow.hpp"
#include "app/RectSurface.hpp"
#include "app/nodes/ImageView.hpp"
#include "app/nodes/Text.hpp"
#include "app/nodes/controls/Card.hpp"
#include "app/nodes/controls/Cell.hpp"
#include "app/nodes/controls/Ribbon.hpp"
#include "app/nodes/nestable/Box.hpp"
#include "app/nodes/nestable/RowColumn.hpp"
#include "app/nodes/nestable/ScrollView.hpp"
#include <cstdio>

namespace
{
  using namespace loka::app;
  using namespace loka::app::scene;

  /** Own the native host until the scene and controller have retired their views. */
  struct Host
  {
    NSAutoreleasePool *pool;
    NSWindow *window;
    Host()
        : pool([[NSAutoreleasePool alloc] init]),
          window(nil)
    {
      [NSApplication sharedApplication];
      this->window = [[NSWindow alloc] initWithContentRect:NSMakeRect(40, 40, 640, 480)
                                                 styleMask:LOKA_MAC_WINDOW_STYLE_TITLED
                                                   backing:NSBackingStoreBuffered
                                                     defer:NO];
      LOKA_VERIFY(this->window != nil);
      [this->window setReleasedWhenClosed:NO];
    }
    ~Host()
    {
      [this->window orderOut:nil];
      [this->window close];
      [this->window release];
      [this->pool drain];
    }
  };

  NSView *findView(NSView *root, Class type)
  {
    if ([root isKindOfClass:type])
      return root;
    NSArray *children = [root subviews];
    for (NSUInteger i = 0; i < [children count]; ++i)
      if (NSView *found = findView([children objectAtIndex:i], type))
        return found;
    return nil;
  }

  /** Collects the views of one class that are clean after a display; a view
      AppKit left dirty (the fixture's bottom card band stays dirty after
      -[NSWindow display] on 10.9) is reported and excluded, so the pin
      compares each view with its own earlier state instead of an absolute. */
  unsigned collectCleanViews(NSView *root, Class type, NSMutableArray *clean)
  {
    unsigned count = 0;
    if ([root isKindOfClass:type])
    {
      const NSRect f = [root frame];
      std::printf("quiescent relayout: %s needsDisplay=%d frame=(%g,%g %gx%g)\n",
                  [NSStringFromClass([root class]) UTF8String], static_cast<int>([root needsDisplay]), f.origin.x,
                  f.origin.y, f.size.width, f.size.height);
      if (![root needsDisplay])
      {
        [clean addObject:root];
        ++count;
      }
    }
    NSArray *children = [root subviews];
    for (NSUInteger i = 0; i < [children count]; ++i)
      count += collectCleanViews([children objectAtIndex:i], type, clean);
    return count;
  }

  void verifyRelayout(bool move)
  {
    Host host;
    if (loka::testing::MacConsoleScreenIsLocked())
    {
      std::printf("[skip] relayout display pins: console screen is locked\n");
      return;
    }
    loka::core::MutableState<int> spacerWidth(20);
    MacScenePlatformController controller([host.window contentView], RailMetrics());
    NodeDefinitionBase *definition =
        (ScrollView() << (Column() << (Card() << Text("Quiescent card") << ImageView().size(32, 32))
                                   << Cell(CellProps().text("Quiescent cell"))
                                   << (RibbonControl() << (RibbonGroup("Quiescent group") << RibbonItem("Item")))
                                   << (Row() << Box().width(&spacerWidth).height(30) << RectSurface().size(40, 30))))
            .clone();
    LOKA_VERIFY(definition);
    {
      Scene scene(definition);
      LOKA_VERIFY(scene.mount(&controller));
      loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);
      controller.relayout(640, 480);
      [host.window makeKeyAndOrderFront:nil];
      controller.relayout(640, 480);
      [host.window display];
      NSScrollView *scroll = (NSScrollView *)findView([host.window contentView], [NSScrollView class]);
      LOKA_VERIFY(scroll);
      NSView *document = [scroll documentView];
      // Each class pins the matching context's relayout, including all card
      // bands and ribbon edges; absence must fail rather than pass vacuously.
      NSString *classes[] = {@"LokaCardBandView",
                             @"NSTextField",
                             @"LokaImageView",
                             @"LokaCellView",
                             @"LokaRectSurfaceView",
                             @"LokaRibbonGroupEdgeView"};
      NSMutableArray *clean = [NSMutableArray array];
      for (unsigned i = 0; i < sizeof(classes) / sizeof(classes[0]); ++i)
        LOKA_VERIFY(collectCleanViews(document, NSClassFromString(classes[i]), clean) > 0);
      NSView *surface = findView(document, NSClassFromString(@"LokaRectSurfaceView"));
      LOKA_VERIFY(surface);
      const NSRect before = [surface frame];
      if (move)
      {
        spacerWidth.set(40);
        scene.flushInvalidation();
      }
      controller.relayout(640, 480);
      // Observe before any display/event pumping can consume the invalidation.
      if (move)
      {
        LOKA_VERIFY(!NSEqualPoints(before.origin, [surface frame].origin));
        LOKA_VERIFY(NSEqualSizes(before.size, [surface frame].size));
        LOKA_VERIFY([surface needsDisplay]);
      }
      else
      {
        // A quiescent pass must not dirty any view that the display left clean.
        for (NSUInteger i = 0; i < [clean count]; ++i)
        {
          NSView *view = [clean objectAtIndex:i];
          std::printf("quiescent relayout: %s still clean=%d\n", [NSStringFromClass([view class]) UTF8String],
                      static_cast<int>(![view needsDisplay]));
          LOKA_VERIFY(![view needsDisplay]);
        }
      }
      loka::dsl::testing::SceneTestAccess::unmount(scene);
    }
    controller.drainNativeRetirements();
  }
} // namespace

void testMacQuiescentRelayoutDoesNotInvalidate()
{
  verifyRelayout(false);
}

void testMacChangedFrameStillInvalidates()
{
  verifyRelayout(true);
}
