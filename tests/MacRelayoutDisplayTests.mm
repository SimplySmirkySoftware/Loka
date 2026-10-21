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
#include <objc/runtime.h>

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
      this->window = [[NSWindow alloc] initWithContentRect:NSMakeRect(40, 40, 640, 900)
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

  /** Install each draw hook once; forward to AppKit and restore at process exit. */
  class DrawCounts
  {
  public:
    enum ViewKind
    {
      CARD,
      TEXT,
      IMAGE,
      CELL,
      SURFACE,
      EDGE,
      VIEW_COUNT
    };

    static DrawCounts &instance()
    {
      static DrawCounts counts;
      return counts;
    }
    void reset()
    {
      for (int i = 0; i < VIEW_COUNT; ++i)
        this->entries_[i].count = 0;
    }
    unsigned count(ViewKind kind) const
    {
      return this->entries_[kind].count;
    }
    Class viewClass(ViewKind kind) const
    {
      return this->entries_[kind].type;
    }

  private:
    struct Entry
    {
      Class type;
      Method method;
      IMP original;
      unsigned count;
    };
    Entry entries_[VIEW_COUNT];

    template <int Kind> static void draw(id view, SEL selector, NSRect rect)
    {
      Entry &entry = instance().entries_[Kind];
      ++entry.count;
      typedef void (*DrawFn)(id, SEL, NSRect);
      reinterpret_cast<DrawFn>(entry.original)(view, selector, rect);
    }
    DrawCounts()
    {
      NSString *names[] = {@"LokaCardBandView",
                           @"NSTextField",
                           @"LokaImageView",
                           @"LokaCellView",
                           @"LokaRectSurfaceView",
                           @"LokaRibbonGroupEdgeView"};
      const IMP replacements[] = {reinterpret_cast<IMP>(&draw<CARD>),
                                  reinterpret_cast<IMP>(&draw<TEXT>),
                                  reinterpret_cast<IMP>(&draw<IMAGE>),
                                  reinterpret_cast<IMP>(&draw<CELL>),
                                  reinterpret_cast<IMP>(&draw<SURFACE>),
                                  reinterpret_cast<IMP>(&draw<EDGE>)};
      for (int i = 0; i < VIEW_COUNT; ++i)
      {
        Entry &entry = this->entries_[i];
        entry.type = NSClassFromString(names[i]);
        LOKA_VERIFY(entry.type != Nil);
        Method inherited = class_getInstanceMethod(entry.type, @selector(drawRect:));
        LOKA_VERIFY(inherited);
        // Materialize inherited methods on the target class before replacing,
        // so a hook never accidentally changes NSView's implementation.
        class_addMethod(
            entry.type, @selector(drawRect:), method_getImplementation(inherited), method_getTypeEncoding(inherited));
        entry.method = class_getInstanceMethod(entry.type, @selector(drawRect:));
        entry.original = method_setImplementation(entry.method, replacements[i]);
        entry.count = 0;
      }
    }
    ~DrawCounts()
    {
      for (int i = 0; i < VIEW_COUNT; ++i)
        method_setImplementation(this->entries_[i].method, this->entries_[i].original);
    }
    DrawCounts(const DrawCounts &);
    DrawCounts &operator=(const DrawCounts &);
  };

  void verifyRelayout(bool move)
  {
    Host host;
    if (loka::testing::MacConsoleScreenIsLocked())
    {
      std::printf("[skip] relayout display pins: console screen is locked\n");
      return;
    }
    DrawCounts &draws = DrawCounts::instance();
    draws.reset();
    loka::core::MutableState<int> spacerWidth(20);
    MacScenePlatformController controller([host.window contentView], RailMetrics());
    NodeDefinitionBase *definition =
        // The Cell fills the remaining height, so it comes last to keep every
        // sampled view inside the window for the first display.
        (ScrollView() << (Column() << (Card() << Text("Quiescent card") << ImageView().size(32, 32))
                                   << (Row() << Box().width(&spacerWidth).height(30) << RectSurface().size(40, 30))
                                   << (RibbonControl() << (RibbonGroup("Quiescent group") << RibbonItem("Item")))
                                   << Cell(CellProps().text("Quiescent cell"))))
            .clone();
    LOKA_VERIFY(definition);
    {
      Scene scene(definition);
      LOKA_VERIFY(scene.mount(&controller));
      loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);
      controller.relayout(640, 900);
      [host.window makeKeyAndOrderFront:nil];
      controller.relayout(640, 900);
      [host.window display];
      NSScrollView *scroll = (NSScrollView *)findView([host.window contentView], [NSScrollView class]);
      LOKA_VERIFY(scroll);
      NSView *document = [scroll documentView];
      for (int i = 0; i < DrawCounts::VIEW_COUNT; ++i)
      {
        const DrawCounts::ViewKind kind = static_cast<DrawCounts::ViewKind>(i);
        NSView *sample = findView(document, draws.viewClass(kind));
        const NSRect f = sample ? [sample frame] : NSZeroRect;
        std::printf("relayout display: %s draws after first display=%u frame=(%g,%g %gx%g) hidden=%d\n",
                    [NSStringFromClass(draws.viewClass(kind)) UTF8String], draws.count(kind), f.origin.x, f.origin.y,
                    f.size.width, f.size.height, sample ? static_cast<int>([sample isHidden]) : -1);
      }
      for (int i = 0; i < DrawCounts::VIEW_COUNT; ++i)
      {
        const DrawCounts::ViewKind kind = static_cast<DrawCounts::ViewKind>(i);
        LOKA_VERIFY(findView(document, draws.viewClass(kind)));
        LOKA_VERIFY(draws.count(kind) > 0);
      }
      draws.reset();
      NSView *surface = findView(document, NSClassFromString(@"LokaRectSurfaceView"));
      LOKA_VERIFY(surface);
      const NSRect before = [surface frame];
      if (move)
      {
        spacerWidth.set(40);
        scene.flushInvalidation();
      }
      controller.relayout(640, 900);
      [host.window displayIfNeeded];
      if (move)
      {
        LOKA_VERIFY(!NSEqualPoints(before.origin, [surface frame].origin));
        LOKA_VERIFY(NSEqualSizes(before.size, [surface frame].size));
        LOKA_VERIFY(draws.count(DrawCounts::SURFACE) >= 1);
      }
      else
      {
        for (int i = 0; i < DrawCounts::VIEW_COUNT; ++i)
        {
          const DrawCounts::ViewKind kind = static_cast<DrawCounts::ViewKind>(i);
          std::printf("quiescent relayout: %s drawRect calls=%u\n",
                      [NSStringFromClass(draws.viewClass(kind)) UTF8String],
                      draws.count(kind));
          LOKA_VERIFY(draws.count(kind) == 0);
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
