#include "MacAttributedTextPressTests.hpp"
#include "support/MacAttributedTextTestAccess.hpp"
#include "support/MacNativeRefusal.hpp"
#include "support/MacWindowSession.hpp"
#include "support/LinkPressSessionAccess.hpp"
#include "support/LifecycleFactTestAccess.hpp"
#include "testing/MacLinkPressHooks.hpp"
#include "testing/core/StateTrackerTestAccess.hpp"
#include "MacScenePlatformController.hpp"
#include "context/LokaClipHostView.hpp"
#include "app/nodes/controls/Button.hpp"
#include "app/nodes/nestable/Box.hpp"
#include "app/nodes/nestable/ZStack.hpp"
#include "app/nodes/boundary/StdComposition.hpp"
#include "app/scene/Scene.hpp"
#include "MacApp.hpp"
#include "MacWindow.hpp"
#include "testing/MacWindowTestAccess.hpp"
#include "platform/null/NullPlatformContext.hpp"
#include <cmath>
#include <cstdio>

namespace loka
{
  namespace testing
  {
    void failMacTextPresentationStep(unsigned ordinal);
  }
} // namespace loka

#if MAC_OS_X_VERSION_MAX_ALLOWED >= 101200
#define PRESS_DOWN NSEventTypeLeftMouseDown
#define PRESS_UP NSEventTypeLeftMouseUp
#define PRESS_DRAG NSEventTypeLeftMouseDragged
#else
#define PRESS_DOWN NSLeftMouseDown
#define PRESS_UP NSLeftMouseUp
#define PRESS_DRAG NSLeftMouseDragged
#endif

@interface LokaPressRecorder : NSView
{
  unsigned downs_, drags_, ups_;
}
- (unsigned)downs;
- (unsigned)drags;
- (unsigned)ups;
@end
@implementation LokaPressRecorder
- (BOOL)isFlipped
{
  return YES;
}
- (void)mouseDown:(NSEvent *)event
{
  (void)event;
  ++downs_;
}
- (void)mouseDragged:(NSEvent *)event
{
  (void)event;
  ++drags_;
}
- (void)mouseUp:(NSEvent *)event
{
  (void)event;
  ++ups_;
}
- (unsigned)downs
{
  return downs_;
}
- (unsigned)drags
{
  return drags_;
}
- (unsigned)ups
{
  return ups_;
}
@end

namespace
{
  using namespace loka::app;
  using namespace loka::app::scene;
  using namespace loka::core;
  using namespace loka::testing;
  typedef loka::dsl::testing::MacAttributedTextTestAccess Access;

  struct Pool
  {
    NSAutoreleasePool *pool;
    Pool()
        : pool([[NSAutoreleasePool alloc] init])
    {
      [NSApplication sharedApplication];
    }
    ~Pool()
    {
      [pool drain];
    }
  };

  NSWindow *PressWindow()
  {
    NSWindow *window = [[[NSWindow alloc] initWithContentRect:NSMakeRect(80, 80, 320, 160)
                                                    styleMask:LOKA_MAC_WINDOW_STYLE_TITLED
                                                      backing:NSBackingStoreBuffered
                                                        defer:NO] autorelease];
    LOKA_VERIFY(window);
    [window setReleasedWhenClosed:NO];
    [window orderFront:nil];
    return window;
  }

  bool KeyWindow(NSWindow *window, const char *test)
  {
    if (MacRequestKeyWindow(window))
    {
      std::printf("[run] %s key window\n", test);
      return true;
    }
    std::printf("[skip] %s: %s\n", test, MacKeyRefusalReason());
    return false;
  }

  AttributedString PressLink(const char *s)
  {
    return Linked(s, TextStyle(), LinkTarget(String("target")));
  }
  AttributedString Original()
  {
    return PressLink("HHHH") + Styled("  ", TextStyle()) + PressLink("MMMM");
  }

  /** Always derive even the refusal points from the installed manager and container. */
  NSPoint Glyph(NSView *view, NSUInteger ch)
  {
    NSLayoutManager *m = Access::manager(view);
    NSUInteger count = 0;
    NSRectArray rects = [m rectArrayForCharacterRange:NSMakeRange(ch, 1)
                         withinSelectedCharacterRange:NSMakeRange(NSNotFound, 0)
                                      inTextContainer:Access::container(view)
                                            rectCount:&count];
    LOKA_VERIFY(count);
    const NSRect r = rects[0];
    NSRange glyph = [m glyphRangeForCharacterRange:NSMakeRange(ch, 1) actualCharacterRange:NULL];
    const NSRect visible =
        NSIntersectionRect(r, [m lineFragmentUsedRectForGlyphAtIndex:glyph.location effectiveRange:NULL]);
    LOKA_VERIFY(!NSIsEmptyRect(visible));
    return NSMakePoint(NSMidX(visible), NSMidY(visible));
  }

  NSEvent *Event(NSWindow *window,
                 NSEventType type,
                 NSPoint inWindow,
                 NSInteger number,
                 NSUInteger flags = 0,
                 NSInteger clicks = 1)
  {
    mach_timebase_info_data_t scale;
    LOKA_VERIFY(mach_timebase_info(&scale) == KERN_SUCCESS);
    NSTimeInterval timestamp = mach_absolute_time() * static_cast<double>(scale.numer) / scale.denom / 1.0e9;
    NSEvent *event = [NSEvent mouseEventWithType:type
                                        location:inWindow
                                   modifierFlags:flags
                                       timestamp:timestamp
                                    windowNumber:[window windowNumber]
                                         context:nil
                                     eventNumber:number
                                      clickCount:clicks
                                        pressure:type == PRESS_UP ? 0 : 1];
    LOKA_VERIFY(event && [event eventNumber] == number);
    return event;
  }
  void Display(NSView *v)
  {
    [v setNeedsDisplay:YES];
    [[v window] displayIfNeeded];
#if LOKA_MAC_HAS_CALAYER
    [CATransaction flush];
#endif
  }

  struct Press
  {
    Pool pool;
    NSWindow *window;
    LokaPressRecorder *root;
    MacScenePlatformController controller;
    PushStateTracker tracker;
    MutableState<LinkTarget> result;
    MutableState<AttributedString> content;
    AttributedTextNode *node;
    MacAttributedTextContext *context;
    NSView *view;
    LayoutState seat;
    unsigned writes;
    bool observerDown;
    explicit Press(bool bound = true, bool parked = false, bool display = true)
        : window(PressWindow()),
          root([[[LokaPressRecorder alloc] initWithFrame:[[window contentView] bounds]] autorelease]),
          controller(root, RailMetrics()),
          result(LinkTarget()),
          content(Original()),
          node(0),
          context(0),
          view(nil),
          writes(0),
          observerDown(false)
    {
      [window setContentView:root];
      tracker.addState(&result);
      result.bind(&changed, this, false);
      AttributedTextProps props(&content);
      if (bound)
        props.activatedLink(NodeState<LinkTarget>(&result, &tracker));
      node = new AttributedTextNode(props);
      if (parked)
        NotifySubtreeNodeDetached(node);
      seat.width = 200;
      node->layoutProjected(&controller, seat);
      context = static_cast<MacAttributedTextContext *>(node->getContext());
      LOKA_VERIFY(context);
      view = [Access::view(*context) retain];
      LOKA_VERIFY([view isHidden] == parked);
      if (!parked && display)
        present();
    }
    ~Press()
    {
      delete node;
      controller.drainNativeRetirements();
      [view release];
      result.unbind(&changed, this);
      [window orderOut:nil];
    }
    void present()
    {
      Display(view);
      MacPresentedText fact;
      LOKA_VERIFY([view queryPresentedFact:&fact]);
      std::printf("[run] presentation %s\n", Access::presentedPath(view) == Access::LAYER ? "L" : "W");
    }
    NSPoint location(NSUInteger ch = 0) const
    {
      return [view convertPoint:Glyph(view, ch) toView:nil];
    }
    void down(NSInteger n = 1, NSUInteger flags = 0, NSInteger clicks = 1)
    {
      [view mouseDown:Event([view window], PRESS_DOWN, location(), n, flags, clicks)];
    }
    void up(NSInteger n = 1, NSUInteger ch = 0, NSInteger clicks = 1)
    {
      [view mouseUp:Event([view window], PRESS_UP, location(ch), n, 0, clicks)];
    }
    static void changed(void *data)
    {
      Press &f = *static_cast<Press *>(data);
      LOKA_VERIFY(f.controller.borrowPhase().open() && f.result.get() == LinkTarget(String("target")));
      ++f.writes;
      if (f.observerDown && f.writes == 1)
        f.down(9);
    }
    void layout(NodeDirtyFlags inputs)
    {
      seat.inputs = inputs;
      context->layout(&controller, seat);
    }
    void park()
    {
      NotifySubtreeNodeDetached(node);
      LifecycleFactTestAccess::DeliverFacts(node);
    }
    void resume()
    {
      NotifySubtreeNodeAttached(node);
      LifecycleFactTestAccess::DeliverFacts(node);
      present();
    }
    void retire()
    {
      LifecycleFactTestAccess::MarkSubtreeRetired(node);
      LifecycleFactTestAccess::DeliverFacts(node);
    }
  };

  struct Observe
  {
    Observe(MacLinkPressHook fn, void *data)
    {
      observeMacLinkPress(fn, data);
    }
    ~Observe()
    {
      observeMacLinkPress(0, 0);
    }
  };
  void Completed(const char *test)
  {
    std::printf("[complete] %s\n", test);
  }
} // namespace

void testMacLinkPressIdentity()
{
  Press f;
  f.down(1);
  // The missing A-UP and B-DOWN never enter this adapter.
  f.up(2);
  LOKA_VERIFY(f.writes == 0);
  f.down(3);
  f.up(3);
  LOKA_VERIFY(f.writes == 1);
  Completed(__func__);
}

void testMacLinkPressModifiers()
{
  Press f;
  f.down();
  f.down(1, LOKA_MAC_CONTROL_MODIFIER);
  f.up();
  LOKA_VERIFY(f.writes == 0);
  f.down();
  f.up();
  f.down(2, 0, 2);
  f.up(2, 0, 2);
  LOKA_VERIFY(f.writes == 2);
  Press unbound(false);
  unbound.down();
  unbound.up();
  LOKA_VERIFY(!unbound.writes && ![unbound.root downs] && ![unbound.root ups]);
  Completed(__func__);
}

void testMacLinkPressConsumption()
{
  Press f;
  NSEvent *drag = Event(f.window, PRESS_DRAG, f.location(), 1);
  f.down();
  [f.view mouseDragged:drag];
  f.up();
  LOKA_VERIFY(f.writes == 1);
  LOKA_VERIFY([f.root downs] == 0);
  LOKA_VERIFY([f.root drags] == 0);
  LOKA_VERIFY([f.root ups] == 0);
  Completed(__func__);
}

void testMacLinkPressRun()
{
  Press f;
  f.down();
  f.up(1, 6); // Same target, distinct maximal run.
  LOKA_VERIFY(f.writes == 0);
  f.down();
  f.content.set(PressLink("different"));
  f.content.set(Original());
  f.up();
  LOKA_VERIFY(f.writes == 1);
  f.down();
  f.content.set(PressLink("different"));
  f.up();
  LOKA_VERIFY(f.writes == 1);
  f.content.set(Original());
  f.down();
  f.content.set(PressLink("different"));
  f.content.set(PressLink("HH") + PressLink("HH") + Styled("  ", TextStyle()) + PressLink("MMMM"));
  f.up();
  LOKA_VERIFY(f.writes == 1);
  Completed(__func__);
}

void testMacLinkPressFalseQuery()
{
  Press f;
  f.down();
  f.content.set(PressLink("different"));
  f.up(); // First run starts at zero on both values; only fresh evidence can authorize a write.
  LOKA_VERIFY(f.writes == 0);
  Completed(__func__);
}

void testMacLinkPressObserver()
{
  Press f;
  f.observerDown = true;
  f.down();
  f.up();
  f.up(9);
  LOKA_VERIFY(f.writes == 2);
  Completed(__func__);
}

namespace
{
  struct Nested
  {
    Press &f;
    bool down;
    unsigned prepared, entries;
    static void visit(MacLinkPressVisit v, void *data)
    {
      Nested &n = *static_cast<Nested *>(data);
      if (v == MAC_LINK_DOWN_ENTRY || v == MAC_LINK_UP_ENTRY)
        ++n.entries;
      if (v == MAC_LINK_PREPARE && ++n.prepared == 1)
      {
        if (n.down)
          n.f.down(2);
        else
          n.f.up(2);
      }
    }
  };
} // namespace

void testMacLinkPressNested()
{
  for (int outerDown = 0; outerDown <= 1; ++outerDown)
    for (int innerDown = 0; innerDown <= 1; ++innerDown)
    {
      Press f;
      if (!outerDown)
        f.down();
      Nested n = {f, innerDown != 0, 0, 0};
      {
        Observe trace(&Nested::visit, &n);
        if (outerDown)
          f.down();
        else
          f.up();
      }
      LOKA_VERIFY(n.prepared == 1 && n.entries == 2);
      f.up();
      LOKA_VERIFY(f.writes == 0);
      LOKA_VERIFY([f.root downs] == 0 && [f.root ups] == 0);
    }
  Completed(__func__);
}

namespace
{
  struct FailedInstall
  {
    Press &press;
    MacPressedLink saved;
    unsigned installs;
    static void seed(MacLinkPressVisit v, void *data)
    {
      if (v != MAC_LINK_INSTALL)
        return;
      FailedInstall &f = *static_cast<FailedInstall *>(data);
      ++f.installs;
      // Isolate the failure clear from the earlier rebuild clear, which has already spent the real DOWN.
      loka::app::internal::LinkPressProcedure<MacPressedLink> p(Access::session(*f.press.context));
      LOKA_VERIFY(p.admitted());
      p.placeCandidate(f.saved);
    }
  };
  struct ProjectionVisits
  {
    unsigned clears, installs;
    static void count(MacLinkPressVisit v, void *data)
    {
      ProjectionVisits &n = *static_cast<ProjectionVisits *>(data);
      if (v == MAC_LINK_CLEAR)
        ++n.clears;
      if (v == MAC_LINK_INSTALL)
        ++n.installs;
    }
  };
} // namespace
void testMacLinkPressFailedClear()
{
  Press f;
  f.down();
  FailedInstall probe = {f, MacPressedLink(), 0};
  {
    loka::app::internal::LinkPressProcedure<MacPressedLink> p(Access::session(*f.context));
    LOKA_VERIFY(p.takeCandidate(probe.saved));
  }
  Observe trace(&FailedInstall::seed, &probe);
  failMacTextPresentationStep(1);
  f.layout(NODE_DIRTY_PROPS);
  LOKA_VERIFY(probe.installs == 1);
  LinkPressSessionAccess::expectInvalidated(Access::session(*f.context), false);
  Completed(__func__);
}

void testMacLinkPressRebuild()
{
  Press f;
  ProjectionVisits n = {0, 0};
  Observe trace(&ProjectionVisits::count, &n);
  f.down();
  f.layout(NODE_DIRTY_PROPS);
  f.present();
  LOKA_VERIFY(n.clears == 1 && n.installs == 1);
  f.up();
  LOKA_VERIFY(f.writes == 0);
  f.down();
  f.layout(NODE_DIRTY_NONE);
  f.present();
  f.up();
  LOKA_VERIFY(f.writes == 1 && n.installs == 1);
  f.down();
  f.context->onPropsApplied();
  LinkPressSessionAccess::expectInvalidated(Access::session(*f.context), false);
  LOKA_VERIFY(!Access::complete(*f.context) && n.installs == 1);
  f.layout(NODE_DIRTY_NONE);
  f.present();
  f.down();
  failMacTextPresentationStep(1);
  f.layout(NODE_DIRTY_PROPS);
  LOKA_VERIFY(!Access::complete(*f.context) && n.clears == 5 && n.installs == 3);
  LinkPressSessionAccess::expectInvalidated(Access::session(*f.context), false);
  Completed(__func__);
}

namespace
{
  struct Placement
  {
    Press &f;
    bool park, active;
    unsigned observations;
    static void visit(MacLinkPressVisit v, void *data)
    {
      Placement &p = *static_cast<Placement *>(data);
      if (v == (p.park ? MAC_LINK_DROP_WINDOW : MAC_LINK_CLEAR))
      {
        ++p.observations;
        LinkPressSessionAccess::expectInvalidated(Access::session(*p.f.context), p.active);
      }
    }
  };
} // namespace

void testMacLinkPressPlacement()
{
  for (int park = 0; park <= 1; ++park)
    for (int active = 0; active <= 1; ++active)
    {
      Press f;
      f.down();
      // Keep a placed candidate AND, on the second leg, an active fresh mark before native work.
      loka::app::internal::LinkPressProcedure<MacPressedLink> p(Access::session(*f.context));
      if (!active)
        p.close();
      Placement probe = {f, park != 0, active != 0, 0};
      Observe trace(&Placement::visit, &probe);
      if (park)
        f.park();
      else
        f.context->onPropsApplied();
      LOKA_VERIFY(probe.observations == 1);
    }
  Completed(__func__);
}

void testMacLinkPressDetach()
{
  Press f;
  f.down();
  f.park();
  f.resume();
  f.up();
  LOKA_VERIFY(f.writes == 0);
  f.down();
  f.up();
  LOKA_VERIFY(f.writes == 1);
  Completed(__func__);
}

namespace
{
  void countEntries(MacLinkPressVisit v, void *data)
  {
    if (v == MAC_LINK_DOWN_ENTRY || v == MAC_LINK_UP_ENTRY)
      ++*static_cast<unsigned *>(data);
  }
  struct RetiringView
  {
    NSView *view;
    NSEvent *up;
    unsigned entries, drops;
    static void visit(MacLinkPressVisit v, void *data)
    {
      RetiringView &r = *static_cast<RetiringView *>(data);
      countEntries(v, &r.entries);
      if (v == MAC_LINK_DROP_WINDOW)
      {
        ++r.drops;
        [r.view mouseUp:r.up]; // The first terminal native call must already see the revocation.
      }
    }
  };
  void retireOnClose(void *data)
  {
    Press &f = *static_cast<Press *>(data);
    LOKA_VERIFY(Operation::isSettling() && !f.controller.borrowPhase().open());
    delete f.node;
    f.node = 0;
    f.controller.drainNativeRetirements();
  }
  void scheduleRetire(MacLinkPressVisit v, void *data)
  {
    if (v == MAC_LINK_PREPARE)
    {
      Press &f = *static_cast<Press *>(data);
      LOKA_VERIFY(Operation::openActive(&f.tracker) == OPEN_OK);
      loka::core::testing::PushStateTrackerTestAccess::defer(f.tracker, &retireOnClose, &f);
    }
  }
} // namespace
void testMacLinkPressLifetime()
{
  Press f(true, true);
  NSEvent *down = Event(f.window, PRESS_DOWN, f.location(), 1);
  NSEvent *up = Event(f.window, PRESS_UP, f.location(), 1);
  RetiringView r = {f.view, up, 0, 0};
  Observe trace(&RetiringView::visit, &r);
  [f.view mouseUp:up];
  LOKA_VERIFY(r.entries == 1);
  f.retire();
  [f.view mouseUp:up];
  LOKA_VERIFY(r.entries == 1 && r.drops == 1 && ![f.view superview]);
  delete f.node;
  f.node = 0;
  f.controller.drainNativeRetirements();
  [f.view mouseUp:up];
  [f.view mouseDown:down];
  LOKA_VERIFY(r.entries == 1 && !f.writes);
  Completed(__func__);
}

void testMacLinkPressTurnClose()
{
  Press f;
  NSEvent *up = Event(f.window, PRESS_UP, f.location(), 1);
  {
    Observe trace(&scheduleRetire, &f);
    f.down();
  }
  LOKA_VERIFY(!f.node && ![f.view superview]);
  [f.view mouseUp:up];
  LOKA_VERIFY(f.writes == 0);
  Completed(__func__);
}

namespace
{
  BOOL invisible(id, SEL)
  {
    return NO;
  }

  void WindowObservation(Press &f, NSEvent *up, const char *phase)
  {
    NSView *v = f.view;
    NSRect clip = [v visibleRect];
    AttributedString::LinkRun run;
    NSPoint point = [v convertPoint:[up locationInWindow] fromView:nil];
    bool eligible = Access::queryPressCandidate(*f.context, point, run);
    if (!eligible)
      std::puts("[observe] redundant window guard on this native path");
    // Observe the slot by taking and replacing its complete payload before dispatch.
    loka::app::internal::LinkPressProcedure<MacPressedLink> p(Access::session(*f.context));
    MacPressedLink saved;
    bool held = p.takeCandidate(saved);
    if (held)
      p.placeCandidate(saved);
    std::printf("[observe] %s visible=%d same-window=%d held=%d clip=(%g,%g %gx%g) eligible=%d\n",
                phase,
                int([[v window] isVisible]),
                int([v window] == [up window]),
                int(held),
                double(NSMinX(clip)),
                double(NSMinY(clip)),
                double(NSWidth(clip)),
                double(NSHeight(clip)),
                int(eligible));
  }
} // namespace

void testMacLinkPressWindow()
{
  Press f;
  NSWindow *other = PressWindow();
  f.down();
  NSEvent *wrong = Event(other, PRESS_UP, f.location(), 1);
  AttributedString::LinkRun run;
  LOKA_VERIFY([wrong window] == other && other != [f.view window]);
  LOKA_VERIFY(
      Access::queryPressCandidate(*f.context, [f.view convertPoint:[wrong locationInWindow] fromView:nil], run));
  [f.view mouseUp:wrong];
  LOKA_VERIFY(f.writes == 0);
  f.up();
  LOKA_VERIFY(f.writes == 0);
  [f.view mouseDown:Event(other, PRESS_DOWN, f.location(), 2)];
  f.up(2);
  LOKA_VERIFY(f.writes == 0);
  [other orderOut:nil];
  Completed(__func__);
}

void testMacLinkPressVisibility()
{
  Press f;
  f.down();
  NSEvent *up = Event(f.window, PRESS_UP, f.location(), 1);
  {
    // Control only the visible answer; ordering out can independently remove native presentation.
    MacNativeRefusal hidden(@selector(isVisible), (IMP)invisible, [f.window class]);
    AttributedString::LinkRun run;
    LOKA_VERIFY([up window] == f.window && ![f.window isVisible]);
    LOKA_VERIFY(Access::queryPressCandidate(*f.context, Glyph(f.view, 0), run));
    [f.view mouseUp:up];
    LOKA_VERIFY(f.writes == 0);
    f.down(2);
  }
  f.up(2);
  LOKA_VERIFY(f.writes == 0);
  f.down(3);
  NSEvent *orderedOut = Event(f.window, PRESS_UP, f.location(), 3);
  [f.window orderOut:nil];
  WindowObservation(f, orderedOut, "ordered out before release");
  [f.view mouseUp:orderedOut];
  LOKA_VERIFY(f.writes == 0);
  Completed(__func__);
}

void testMacLinkPressTransfer()
{
  Press f;
  NSWindow *next = PressWindow();
  NSEvent *old = Event(f.window, PRESS_UP, f.location(), 7);
  f.down(7);
  [next orderFront:nil];
  [[next contentView] addSubview:f.view];
  f.present();
  WindowObservation(f, old, "transfer before teardown, controlled old-window UP");
  [f.view mouseUp:old];
  LOKA_VERIFY(f.writes == 0);
  [f.window orderFront:nil];
  [f.root addSubview:f.view];
  f.present();
  f.down(0);
  [next orderFront:nil];
  [[next contentView] addSubview:f.view];
  f.present();
  // Stated limit, Gate 1 §8.1: lost UP, consumed DOWN, colliding orphan UP in the new release window.
  f.up(0);
  LOKA_VERIFY(f.writes == 1);
  [next orderOut:nil];
  Completed(__func__);
}

void testMacLinkPressNativeClose()
{
  Press f;
  NSEvent *up = Event(f.window, PRESS_UP, f.location(), 1);
  f.down();
  [f.window close];
  LOKA_VERIFY(![f.window isVisible]);
  WindowObservation(f, up, "after native close, before logical teardown, controlled UP");
  [f.view mouseUp:up];
  LOKA_VERIFY(f.writes == 0);
  Completed(__func__);
}

namespace
{
  NSView *Hit(Press &f, NSView *root)
  {
    return [root hitTest:[[root superview] convertPoint:f.location() fromView:nil]];
  }
  NSClipView *Clip(Press &f, NSRect frame)
  {
    NSClipView *clip = [[[NSClipView alloc] initWithFrame:frame] autorelease];
    [f.root addSubview:clip];
    [clip setDocumentView:f.view];
    return clip;
  }
} // namespace
void testMacLinkPressPoints()
{
  Press f;
  NSPoint first = Glyph(f.view, 1);
  first.x = std::floor(first.x);
  NSRect frame = [f.view frame];
  NSClipView *clip = Clip(f, frame);
  f.present();
  f.down();
  // Scroll the document so the next glyph center lies exactly on the excluded maximum x.
  const CGFloat offset = 1;
  [clip setFrameSize:NSMakeSize(first.x - offset, NSHeight(frame))];
  [clip scrollToPoint:NSMakePoint(offset, 0)];
  f.present();
  LOKA_VERIFY(NSMaxX([f.view visibleRect]) == first.x);
  AttributedString::LinkRun run;
  LOKA_VERIFY(Access::queryPressCandidate(*f.context, first, run));
  [f.view mouseUp:Event(f.window, PRESS_UP, [f.view convertPoint:first toView:nil], 1)];
  LOKA_VERIFY(f.writes == 0);
  [clip scrollToPoint:NSZeroPoint];
  [clip setFrameSize:frame.size];
  f.present();
  f.down();
  NSPoint before = f.location();
  [clip setFrameOrigin:NSMakePoint(NSMinX(frame) + first.x, NSMinY(frame) + first.y)];
  f.present();
  LOKA_VERIFY(!NSEqualPoints(before, f.location()));
  f.up();
  LOKA_VERIFY(f.writes == 1);
  Completed(__func__);
}

void testMacLinkPressScroll()
{
  Press f;
  NSPoint a = Glyph(f.view, 0), same = Glyph(f.view, 1), other = Glyph(f.view, 6);
  NSRect frame = [f.view frame];
  frame.size.width = other.x + a.x;
  NSClipView *clip = Clip(f, frame);
  for (int pass = 0; pass < 2; ++pass)
  {
    [clip scrollToPoint:NSZeroPoint];
    f.present();
    NSPoint held = f.location();
    f.down();
    NSPoint to = pass ? other : same;
    [clip scrollToPoint:NSMakePoint(to.x - a.x, 0)];
    f.present();
    AttributedString::LinkRun run;
    LOKA_VERIFY(Access::queryPressCandidate(*f.context, [f.view convertPoint:held fromView:nil], run));
    LOKA_VERIFY(run.first == (pass ? 2u : 0u));
    [f.view mouseUp:Event(f.window, PRESS_UP, held, 1)];
    LOKA_VERIFY(f.writes == 1);
  }
  Completed(__func__);
}

void testMacLinkPressClip()
{
  Press f;
  NSRect frame = [f.view frame];
  LokaClipHostView *host = [[[LokaClipHostView alloc] initWithFrame:frame] autorelease];
  [f.root addSubview:host];
  LOKA_VERIFY(Hit(f, f.root) == f.view);
  if (!KeyWindow(f.window, __func__))
  {
    Completed(__func__);
    return;
  }
  [f.window sendEvent:Event(f.window, PRESS_DOWN, f.location(), 1)];
  [f.window sendEvent:Event(f.window, PRESS_UP, f.location(), 1)];
  LOKA_VERIFY(f.writes == 1);
  LokaPressRecorder *child = [[[LokaPressRecorder alloc] initWithFrame:[host bounds]] autorelease];
  [host addSubview:child];
  LOKA_VERIFY(Hit(f, f.root) == child);
  [f.window sendEvent:Event(f.window, PRESS_DOWN, f.location(), 2)];
  [f.window sendEvent:Event(f.window, PRESS_UP, f.location(), 2)];
  LOKA_VERIFY([child downs] == 1);
  [child removeFromSuperview];
  [host addSubview:f.view];
  [host setBoundsOrigin:Glyph(f.view, 1)];
  [f.view setFrameOrigin:[host bounds].origin];
  f.present();
  LOKA_VERIFY(Hit(f, f.root) == f.view);
  f.down();
  f.up();
  LOKA_VERIFY(f.writes == 2);
  [host setHidden:YES];
  NSView *receiver = Hit(f, f.root);
  LOKA_VERIFY(receiver && receiver != f.view);
  unsigned entries = 0;
  Observe trace(&countEntries, &entries);
  [f.window sendEvent:Event(f.window, PRESS_DOWN, f.location(), 2)];
  [f.window sendEvent:Event(f.window, PRESS_UP, f.location(), 2)];
  LOKA_VERIFY(!entries);
  Completed(__func__);
}

namespace
{
  IMP originalDraw;
  unsigned partialDraws;
  void recordPartial(NSView *view, SEL cmd, NSRect dirty)
  {
    const NSRect *rects = 0;
    NSInteger count = 0;
    [view getRectsBeingDrawn:&rects count:&count];
    if (count && !MacTestRectsCover(rects, count, NSIntersectionRect([view visibleRect], [view bounds])))
      ++partialDraws;
    ((void (*)(id, SEL, NSRect))originalDraw)(view, cmd, dirty);
  }
  IMP originalCapability;
  BOOL withoutUpdateLayer(id receiver, SEL cmd, SEL selector)
  {
    return selector == @selector(wantsUpdateLayer)
               ? NO
               : ((BOOL(*)(id, SEL, SEL))originalCapability)(receiver, cmd, selector);
  }
} // namespace
void testMacLinkPressRepaint()
{
  Pool pool;
  Class meta = object_getClass((id)[NSView class]);
  originalCapability = class_getMethodImplementation(meta, @selector(instancesRespondToSelector:));
  MacNativeRefusal noL(@selector(instancesRespondToSelector:), (IMP)withoutUpdateLayer, meta);
  Press f(true, false, false);
  Display(f.view);
  if (MacLateBoundLayer(f.view))
  {
    std::puts("[skip] layered host; partial window repaint requires W");
    Completed(__func__);
    return;
  }
  LOKA_VERIFY(Access::presentedPath(f.view) == Access::WINDOW);
  std::puts("[run] unlayered window repaint");
  originalDraw = class_getMethodImplementation([f.view class], @selector(drawRect:));
  MacNativeRefusal record(@selector(drawRect:), (IMP)recordPartial, [f.view class]);
  partialDraws = 0;
  NSRect original = [f.view frame];
  NSPoint p = Glyph(f.view, 0);
  NSRect partial = NSMakeRect(p.x, p.y, 1, 1);
  f.down();
  [f.view displayRect:partial];
  LOKA_VERIFY(partialDraws == 1);
  f.up();
  LOKA_VERIFY(f.writes == 1);
  for (int resize = 0; resize <= 1; ++resize)
  {
    f.down();
    if (resize)
      // Changing bounds size avoids the full damage a frame resize schedules.
      [f.view setBoundsSize:NSMakeSize(original.size.width + p.x, original.size.height)];
    else
      [f.view dropWindowPresentation];
    partialDraws = 0;
    [f.view displayRect:partial];
    LOKA_VERIFY(partialDraws == 1);
    LOKA_VERIFY(Access::presentedPath(f.view) == Access::NONE);
    f.up();
    [f.view setBoundsSize:original.size];
    f.present();
    f.up();
    LOKA_VERIFY(f.writes == 1u + resize);
    f.down();
    f.up();
    LOKA_VERIFY(f.writes == 2u + resize);
  }
  f.down();
  [f.view dropWindowPresentation];
  f.present();
  f.up();
  LOKA_VERIFY(f.writes == 4);
  Completed(__func__);
}

/** A real tracking control may dequeue UP itself. Otherwise send the UP through the window after DOWN returns. */
@interface LokaPressTracking : NSObject
{
  BOOL tracked_;
  unsigned actions_;
}
- (void)releaseButton:(NSTimer *)timer;
- (void)action:(id)sender;
- (unsigned)actions;
- (void)send:(NSEvent *)down up:(NSEvent *)up cover:(NSView *)cover;
@end
@implementation LokaPressTracking
- (void)releaseButton:(NSTimer *)timer
{
  NSArray *items = [timer userInfo];
  NSEvent *up = [items objectAtIndex:0];
  if ([items count] > 1)
    [[[up window] contentView] addSubview:[items objectAtIndex:1]];
  tracked_ = YES;
  [NSApp postEvent:up atStart:YES];
}
- (void)action:(id)sender
{
  (void)sender;
  ++actions_;
}
- (unsigned)actions
{
  return actions_;
}
- (void)send:(NSEvent *)down up:(NSEvent *)up cover:(NSView *)cover
{
  tracked_ = NO;
  NSArray *items = [NSArray arrayWithObjects:up, cover, nil];
  NSTimer *timer = [NSTimer timerWithTimeInterval:0.03
                                           target:self
                                         selector:@selector(releaseButton:)
                                         userInfo:items
                                          repeats:NO];
  [[NSRunLoop currentRunLoop] addTimer:timer forMode:NSEventTrackingRunLoopMode];
  [[NSRunLoop currentRunLoop] addTimer:timer forMode:NSDefaultRunLoopMode];
  [[down window] sendEvent:down];
  [timer invalidate];
  if (!tracked_)
  {
    if (cover)
      [[[up window] contentView] addSubview:cover];
    [[up window] sendEvent:up];
  }
  std::printf("[observe] UP via %s\n", tracked_ ? "native tracking" : "window dispatch");
}
@end

namespace
{
  IMP buttonDown, buttonUp;
  unsigned buttonDowns, buttonUps;
  void recordButtonDown(id button, SEL cmd, NSEvent *event)
  {
    ++buttonDowns;
    ((void (*)(id, SEL, NSEvent *))buttonDown)(button, cmd, event);
  }
  void recordButtonUp(id button, SEL cmd, NSEvent *event)
  {
    ++buttonUps;
    ((void (*)(id, SEL, NSEvent *))buttonUp)(button, cmd, event);
  }
  struct LeafReceivers
  {
    unsigned downs, ups;
    static void visit(MacLinkPressVisit v, void *data)
    {
      LeafReceivers &r = *static_cast<LeafReceivers *>(data);
      if (v == MAC_LINK_DOWN_ENTRY)
        ++r.downs;
      if (v == MAC_LINK_UP_ENTRY)
        ++r.ups;
    }
  };
} // namespace

void testMacLinkPressOverlays()
{
  for (int condition = 0; condition < 3; ++condition)
  {
    Pool pool;
    NSWindow *window = PressWindow();
    NSView *root = [[[LokaPressRecorder alloc] initWithFrame:[[window contentView] bounds]] autorelease];
    [window setContentView:root];
    if (!KeyWindow(window, __func__))
    {
      Completed(__func__);
      return;
    }
    MacScenePlatformController controller(root, RailMetrics());
    MutableState<LinkTarget> result;
    AttributedTextNode *node =
        new AttributedTextNode(AttributedTextProps(Original()).activatedLink(NodeState<LinkTarget>(&result, 0)));
    ZStackNode stack((ZStackProps()));
    stack.addChild(node);
    if (condition == 2)
      stack.addChild(new BoxNode(BoxProps().setSize(200, 64)));
    else
      stack.addChild(new ButtonNode(ButtonProps().text("Cover")));
    controller.onChange(&stack, NODE_DIRTY_NONE, false);
    controller.relayout(320, 160);
    NSView *leaf = Access::view(*static_cast<MacAttributedTextContext *>(node->getContext()));
    Display(leaf);
    // Choose an installed glyph under the native overlay. Button placement depends on host metrics.
    NSView *unit = [[root subviews] lastObject];
    NSButton *button = condition == 2 ? nil : (NSButton *)[[unit subviews] lastObject];
    LOKA_VERIFY(condition == 2 || [button isKindOfClass:[NSButton class]]);
    const NSUInteger character = 0;
    NSView *expected = button ? (NSView *)button : unit;
    // Center the actual button on a measured glyph while keeping its real ZStack unit and native receiver.
    if (button)
    {
      NSPoint glyph = [unit convertPoint:Glyph(leaf, character) fromView:leaf];
      NSRect frame = [button frame];
      [button setFrameOrigin:NSMakePoint(glyph.x - NSWidth(frame) / 2, glyph.y - NSHeight(frame) / 2)];
    }
    NSPoint location = [leaf convertPoint:Glyph(leaf, character) toView:nil];
    NSView *enabledHit = [[root superview] hitTest:location];
    LOKA_VERIFY(enabledHit == expected);
    if (condition == 1)
      [button setEnabled:NO];
    NSView *hit = [[root superview] hitTest:location];
    Class cls = button ? [button class] : [unit class];
    buttonDown = class_getMethodImplementation(cls, @selector(mouseDown:));
    buttonUp = class_getMethodImplementation(cls, @selector(mouseUp:));
    MacNativeRefusal down(@selector(mouseDown:), (IMP)recordButtonDown, cls);
    MacNativeRefusal up(@selector(mouseUp:), (IMP)recordButtonUp, cls);
    buttonDowns = buttonUps = 0;
    LeafReceivers receivers = {0, 0};
    Observe observation(&LeafReceivers::visit, &receivers);
    LokaPressTracking *gesture = [[[LokaPressTracking alloc] init] autorelease];
    [gesture send:Event(window, PRESS_DOWN, location, 4) up:Event(window, PRESS_UP, location, 4) cover:nil];
    std::printf("[observe] %s hit=%s overlay DOWN/UP=%u/%u leaf DOWN/UP=%u/%u activated=%d\n",
                condition == 2 ? "ground alone"
                : condition    ? "disabled button"
                               : "button",
                hit ? class_getName([hit class]) : "nil",
                buttonDowns,
                buttonUps,
                receivers.downs,
                receivers.ups,
                int(!result.get().isNone()));
    if (condition != 1)
      LOKA_VERIFY(buttonDowns && !receivers.downs && result.get().isNone());
    controller.onChange(0, NODE_DIRTY_NONE, false);
    [window orderOut:nil];
  }
  Completed(__func__);
}

void testMacLinkPressButtonCover()
{
  Press f;
  NSView *cover = [[[NSView alloc] initWithFrame:[f.view frame]] autorelease];
  f.down();
  [f.root addSubview:cover];
  LOKA_VERIFY(Hit(f, f.root) == cover);
  f.up();
  LOKA_VERIFY(f.writes == 1);
  [cover removeFromSuperview];
  NSButton *button = [[[NSButton alloc] initWithFrame:[f.view frame]] autorelease];
  [f.root addSubview:button];
  LokaPressTracking *gesture = [[[LokaPressTracking alloc] init] autorelease];
  [button setTarget:gesture];
  [button setAction:@selector(action:)];
  if (!KeyWindow(f.window, __func__))
  {
    Completed(__func__);
    return;
  }
  NSPoint point = f.location();
  [gesture send:Event(f.window, PRESS_DOWN, point, 4) up:Event(f.window, PRESS_UP, point, 4) cover:nil];
  unsigned bare = [gesture actions];
  [gesture send:Event(f.window, PRESS_DOWN, point, 5) up:Event(f.window, PRESS_UP, point, 5) cover:cover];
  std::printf("[observe] same cover, leaf activations=%u button actions without/with cover=%u/%u\n",
              f.writes,
              bare,
              [gesture actions] - bare);
  [button setTarget:nil];
  Completed(__func__);
}

namespace
{
  class CloseRoot;
  struct CloseProps : NodePropsBase<CloseProps>
  {
    typedef CloseRoot NodeType;
    struct TypeTag
    {
    };
    NodeState<LinkTarget> result;
    explicit CloseProps(const NodeState<LinkTarget> &box)
        : result(box)
    {
    }
    bool operator<(const PropsBase &rhs) const
    {
      return result.state() < static_cast<const CloseProps &>(rhs).result.state();
    }
  };
  class CloseRoot : public StdCompositionBoundaryNodeBase<CloseProps>
  {
  public:
    explicit CloseRoot(const CloseProps &props)
        : StdCompositionBoundaryNodeBase<CloseProps>(props)
    {
    }
    virtual void composeNode(NodeComposition &c)
    {
      c.declare(AttributedText(Original()).activatedLink(this->props.result));
    }
  };
  class CloseApp : public MacApp
  {
  public:
    explicit CloseApp(MacWindow *window)
        : MacApp(0)
    {
      this->group_ = new AppComponentGroup(std::vector<AppComponent *>(1, window));
      window->setApp(this);
    }
    virtual void quit() {}
  };
} // namespace

void testMacLinkPressAppClose()
{
  Pool pool;
  NullPlatformContext platform;
  MutableState<LinkTarget> result;
  CloseProps props(NodeState<LinkTarget>(&result, 0));
  MacWindow *window = new MacWindow(
      &platform, WindowProps().frame(50, 50, 320, 160).visible(true).scene(new Scene(Boundary<CloseRoot>(props))));
  CloseApp app(window);
  app.flushInvalidationsTick();
  NSView *root = (NSView *)loka::dsl::testing::MacWindowTestAccess::contentView(*window);
  NSView *leaf = [Access::leaf(root) retain];
  Display(leaf);
  NSPoint p = [leaf convertPoint:Glyph(leaf, 0) toView:nil];
  NSEvent *up = Event([leaf window], PRESS_UP, p, 7);
  [leaf mouseDown:Event([leaf window], PRESS_DOWN, p, 6)];
  [leaf mouseUp:Event([leaf window], PRESS_UP, p, 6)];
  LOKA_VERIFY(result.get() == LinkTarget(String("target")));
  result.set(LinkTarget());
  [leaf mouseDown:Event([leaf window], PRESS_DOWN, p, 7)];
  unsigned entries = 0;
  {
    Observe trace(&countEntries, &entries);
    app.requestWindowClose(window);
    app.flushInvalidationsTick();
    LOKA_VERIFY(![leaf window]);
    [leaf mouseUp:up];
    LOKA_VERIFY(!entries && result.get().isNone());
  }
  [leaf release];
  Completed(__func__);
}
