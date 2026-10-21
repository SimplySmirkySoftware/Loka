#include "MacZStackUnitTests.hpp"
#include "MacScenePlatformController.hpp"
#include "MacSceneLayout.hpp"
#include "MacGround.hpp"
#include "context/MacZStackContext.hpp"
#include "support/MacWindowCapture.hpp"
#include "support/MacWindowSession.hpp"
#include "support/MacRetirementProbe.hpp"
#include "support/TextEditorStateOwner.hpp"
#include "support/LifecycleFactTestAccess.hpp"
#include "support/MacNativeRefusal.hpp"
#include "support/TestVerify.hpp"
#include "testing/scene/SceneTestFlow.hpp"
#include "app/RectSurface.hpp"
#include "app/nodes/ImageView.hpp"
#include "app/nodes/Text.hpp"
#include "app/nodes/controls/Button.hpp"
#include "app/nodes/controls/EditText.hpp"
#include "app/nodes/nestable/Box.hpp"
#include "app/nodes/nestable/RowColumn.hpp"
#include "app/nodes/nestable/ScrollView.hpp"
#include "app/nodes/nestable/Show.hpp"
#include "app/nodes/nestable/ZStack.hpp"
#include <climits>
#include <cstdio>

namespace loka
{
  namespace testing
  {
    void failMacStackUnitViews(unsigned count);
#ifdef TEST_BUILD
    void failMacPhotoCaptures(unsigned count);
    NSBitmapImageRep *macStackPhoto(NSView *view);
    bool macStackPhotoDisabled(NSView *view);
    NSRect macStackPhotoRect(NSView *view);
#endif
  }
} // namespace loka

@interface LokaStackTestRoot : NSView
@end
@implementation LokaStackTestRoot
- (BOOL)isFlipped
{
  return YES;
}
@end

namespace
{
  using namespace loka::app;
  using namespace loka::app::scene;
  using loka::core::Frame;

  /** Scoped test-only observation of native insertions on the fixture root. */
  class UnitInsertionProbe
  {
  public:
    explicit UnitInsertionProbe(NSView *watched)
        : watched_(watched),
          original_(method_getImplementation(
              class_getInstanceMethod([LokaStackTestRoot class], @selector(addSubview:positioned:relativeTo:)))),
          insertions_(0),
          replacement_(@selector(addSubview:positioned:relativeTo:),
                       reinterpret_cast<IMP>(&UnitInsertionProbe::Insert),
                       [LokaStackTestRoot class])
    {
      LOKA_VERIFY(!active_);
      active_ = this;
    }
    ~UnitInsertionProbe()
    {
      active_ = 0;
    }
    unsigned insertions() const
    {
      return this->insertions_;
    }

  private:
    static void Insert(id parent, SEL selector, NSView *view, NSWindowOrderingMode position, NSView *relative)
    {
      UnitInsertionProbe *probe = active_;
      if (view == probe->watched_)
        ++probe->insertions_;
      typedef void (*InsertFn)(id, SEL, NSView *, NSWindowOrderingMode, NSView *);
      reinterpret_cast<InsertFn>(probe->original_)(parent, selector, view, position, relative);
    }
    NSView *const watched_;
    const IMP original_;
    unsigned insertions_;
    MacNativeRefusal replacement_;
    static UnitInsertionProbe *active_;
    UnitInsertionProbe(const UnitInsertionProbe &);
    UnitInsertionProbe &operator=(const UnitInsertionProbe &);
  };
  UnitInsertionProbe *UnitInsertionProbe::active_ = 0;

  struct Host
  {
    NSAutoreleasePool *pool;
    NSView *root;
    NSWindow *window;
    Host()
        : pool([[NSAutoreleasePool alloc] init]),
          root(nil),
          window(nil)
    {
      [NSApplication sharedApplication];
      this->root = [[LokaStackTestRoot alloc] initWithFrame:NSMakeRect(0, 0, 300, 200)];
    }
    ~Host()
    {
      [this->window orderOut:nil];
      [this->window close];
      [this->window release];
      [this->root release];
      [this->pool drain];
    }
    bool show(bool layered = false)
    {
      if (loka::testing::MacConsoleScreenIsLocked())
      {
        std::printf("[skip] ZStack real window: console screen is locked\n");
        return false;
      }
      if (layered && ![this->root respondsToSelector:@selector(setWantsLayer:)])
      {
        std::printf("[skip] ZStack layered capture: setWantsLayer: unavailable\n");
        return false;
      }
      this->window = [[NSWindow alloc] initWithContentRect:NSMakeRect(40, 40, 300, 200)
                                                 styleMask:LOKA_MAC_WINDOW_STYLE_TITLED
                                                   backing:NSBackingStoreBuffered
                                                     defer:NO];
      if (!this->window)
      {
        std::printf("[skip] ZStack real window: creation refused\n");
        return false;
      }
      [this->window setReleasedWhenClosed:NO];
      [this->window setContentView:this->root];
      if (layered)
        [this->root setWantsLayer:YES];
      [this->window makeKeyAndOrderFront:nil];
      return true;
    }
  };
  struct Fixture
  {
    Host host;
    MacScenePlatformController controller;
    Node *tree;
    Fixture()
        : controller(this->host.root, RailMetrics()),
          tree(0)
    {
    }
    ~Fixture()
    {
      this->controller.onChange(0, NODE_DIRTY_NONE, false);
      delete this->tree;
      this->controller.drainNativeRetirements();
    }
    void layout(int width = 300, int height = 200)
    {
      this->controller.onChange(this->tree, NODE_DIRTY_NONE, false);
      this->controller.relayout(width, height);
    }
  };
  NSView *childOfClass(NSView *parent, Class type)
  {
    NSArray *children = [parent subviews];
    for (NSUInteger i = 0; i < [children count]; ++i)
      if ([[children objectAtIndex:i] isKindOfClass:type])
        return [children objectAtIndex:i];
    return nil;
  }
  NSView *unitUnder(NSView *parent)
  {
    return childOfClass(parent, NSClassFromString(@"LokaStackUnitView"));
  }
  NSScrollView *scrollUnder(NSView *parent)
  {
    return (NSScrollView *)childOfClass(parent, [NSScrollView class]);
  }
  StackNode *buttonColumn()
  {
    StackNode *column = new StackNode(StackProps(STACK_AXIS_COLUMN));
    column->addChild(new ButtonNode(ButtonProps().text("Upper")));
    return column;
  }
  ZStackNode *stackWithGround(Fixture &f)
  {
    ZStackNode *stack = new ZStackNode((ZStackProps()));
    stack->addChild(new RectSurfaceNode(RectSurfaceProps().size(200, 160)));
    f.tree = stack;
    return stack;
  }
  bool hitBelongsTo(NSView *parent, NSView *unit, NSPoint point)
  {
    // point is in parent coordinates. Each child's hitTest: takes its
    // superview's coordinates, even when parent is a detached flipped root.
    NSArray *children = [parent subviews];
    for (NSUInteger i = [children count]; i > 0; --i)
    {
      NSView *hit = [[children objectAtIndex:i - 1] hitTest:point];
      if (hit)
        return hit == unit || [hit isDescendantOf:unit];
    }
    return false;
  }
  void settle(NSView *root)
  {
    [root displayIfNeeded];
    [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    [[root window] flushWindow];
#pragma clang diagnostic pop
  }
  NSBitmapImageRep *capture(NSView *root)
  {
    settle(root);
    const char *reason = 0;
    NSBitmapImageRep *bitmap = loka::testing::ReadMacWindowServer(root, reason);
    if (!bitmap)
      std::printf("[skip] ZStack composite: %s\n", reason);
    return [bitmap autorelease];
  }
  // colorUsingColorSpaceName: is the Tiger-era spelling (deprecated in 10.14);
  // the same silence as MacGroundTests.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
  NSColor *pixel(NSBitmapImageRep *bitmap, NSView *root, CGFloat x, CGFloat y)
  {
    return [[bitmap colorAtX:static_cast<NSInteger>(x * [bitmap pixelsWide] / [root bounds].size.width)
                           y:static_cast<NSInteger>(y * [bitmap pixelsHigh] / [root bounds].size.height)]
        colorUsingColorSpaceName:NSDeviceRGBColorSpace];
  }
  // MacRectSurfaceContext paints text-role sprites source-over an opaque
  // document ground. Match MacGroundTests::checkRect, not raw labelColor.
  NSColor *spriteColorOverDocument(NSColor *text, NSColor *document)
  {
    text = [text colorUsingColorSpaceName:NSDeviceRGBColorSpace];
    document = [document colorUsingColorSpaceName:NSDeviceRGBColorSpace];
    LOKA_VERIFY(text && document && [document alphaComponent] == 1.0);
    const CGFloat alpha = [text alphaComponent];
    return [NSColor colorWithDeviceRed:alpha * [text redComponent] + (1.0 - alpha) * [document redComponent]
                                 green:alpha * [text greenComponent] + (1.0 - alpha) * [document greenComponent]
                                  blue:alpha * [text blueComponent] + (1.0 - alpha) * [document blueComponent]
                                 alpha:1.0];
  }
  bool sameColor(NSColor *a, NSColor *b)
  {
    a = [a colorUsingColorSpaceName:NSDeviceRGBColorSpace];
    b = [b colorUsingColorSpaceName:NSDeviceRGBColorSpace];
    return a && b && std::fabs([a redComponent] - [b redComponent]) < 0.04
           && std::fabs([a greenComponent] - [b greenComponent]) < 0.04
           && std::fabs([a blueComponent] - [b blueComponent]) < 0.04;
  }
#pragma clang diagnostic pop
} // namespace

void testMacZStackLaterChildGetsUnitView()
{
  Fixture f;
  ZStackNode *stack = stackWithGround(f);
  stack->addChild(new RectSurfaceNode(RectSurfaceProps().size(40, 80)));
  f.layout();
  NSView *unit = unitUnder(f.host.root);
  LOKA_VERIFY(unit && [unit superview] == f.host.root);
  LOKA_VERIFY([[f.host.root subviews] count] == 2 && [[unit subviews] count] == 1);
  const NSRect frame = [unit frame];
  LOKA_VERIFY(frame.origin.x == MacSceneLayout::kRootInset && frame.origin.y == MacSceneLayout::kRootInset);
  LOKA_VERIFY(frame.size.width == 300 - 2 * MacSceneLayout::kRootInset && frame.size.height == 80);
  const NSRect child = [[[unit subviews] objectAtIndex:0] frame];
  LOKA_VERIFY(child.origin.x == 0 && child.origin.y == 0);
  LOKA_VERIFY([unit isFlipped] && [unit isOpaque] && ![unit acceptsFirstResponder]);
}

void testMacZStackUnitsStackInCompositionOrder()
{
  Fixture f;
  ZStackNode *stack = stackWithGround(f);
  stack->addChild(new RectSurfaceNode(RectSurfaceProps().size(40, 80)));
  stack->addChild(new RectSurfaceNode(RectSurfaceProps().size(40, 60)));
  f.layout();
  NSArray *views = [f.host.root subviews];
  LOKA_VERIFY([views count] == 3);
  NSView *lower = [views objectAtIndex:1];
  NSView *upper = [views objectAtIndex:2];
  LOKA_VERIFY([lower isKindOfClass:NSClassFromString(@"LokaStackUnitView")]);
  LOKA_VERIFY([upper isKindOfClass:NSClassFromString(@"LokaStackUnitView")]);
  LOKA_VERIFY([lower frame].size.height == 80 && [upper frame].size.height == 60);
  const NSPoint point = NSMakePoint([upper frame].origin.x + 100, [upper frame].origin.y + 10);
  LOKA_VERIFY(hitBelongsTo(f.host.root, upper, point));
}

void testMacZStackUnitHitTestShadowsGround()
{
  Fixture f;
  ZStackNode *stack = new ZStackNode((ZStackProps()));
  f.tree = stack;
  stack->addChild(buttonColumn());
  stack->addChild(new BoxNode(BoxProps().setSize(200, 80)));
  f.layout();
  NSView *unit = unitUnder(f.host.root);
  NSView *button = childOfClass(f.host.root, [NSButton class]);
  LOKA_VERIFY(unit && button);
  const NSRect frame = [button frame];
  const NSPoint point = NSMakePoint(NSMidX(frame), NSMidY(frame));
  LOKA_VERIFY(hitBelongsTo(f.host.root, unit, point));
  [unit setHidden:YES];
  LOKA_VERIFY(hitBelongsTo(f.host.root, button, point));
}

#if MAC_OS_X_VERSION_MAX_ALLOWED >= 1050
namespace
{
  NSEvent *lineWheel(bool horizontal)
  {
    CGEventRef cg = horizontal ? CGEventCreateScrollWheelEvent(NULL, kCGScrollEventUnitLine, 2, 0, -3)
                               : CGEventCreateScrollWheelEvent(NULL, kCGScrollEventUnitLine, 1, -3);
    NSEvent *event = [NSEvent eventWithCGEvent:cg];
    CFRelease(cg);
    return event;
  }
  void scrollToMiddle(NSScrollView *scroll, NSView *root)
  {
    [[scroll contentView] scrollToPoint:NSMakePoint(100, 100)];
    [scroll reflectScrolledClipView:[scroll contentView]];
    settle(root);
  }
  CGFloat along(NSScrollView *scroll, bool horizontal)
  {
    const NSPoint origin = [[scroll contentView] bounds].origin;
    return horizontal ? origin.x : origin.y;
  }
} // namespace
#endif

/** A unit's view is the ground ScrollView's sibling, so a wheel that the unit's view
    receives climbs the unit's superviews and never reaches the ground; a ZStack inside a
    ScrollView passes it up to that ScrollView. Both axes (#1500). The wheel goes to the
    view a hit test selects, as AppKit does for a non-momentum event; AppKit's own target
    selection belongs to the OS and is not pinned. */
void testMacZStackUnitWheelLeavesGroundScrollView()
{
#if MAC_OS_X_VERSION_MAX_ALLOWED >= 1050
  for (int inside = 0; inside != 2; ++inside)
  {
    loka::core::MutableState<Frame> storage(Frame(0, 0, 0, 0));
    NodeState<Frame> origin(&storage, 0);
    Fixture f;
    ScrollViewNode *scroll = new ScrollViewNode(ScrollViewProps(origin));
    ZStackNode *stack = new ZStackNode((ZStackProps()));
    if (inside)
    {
      stack->addChild(new RectSurfaceNode(RectSurfaceProps().size(600, 600)));
      stack->addChild(new BoxNode(BoxProps().setSize(300, 40)));
      scroll->addChild(stack);
      f.tree = scroll;
    }
    else
    {
      scroll->addChild(new RectSurfaceNode(RectSurfaceProps().size(600, 600)));
      stack->addChild(scroll);
      stack->addChild(new BoxNode(BoxProps().setSize(300, 40)));
      f.tree = stack;
    }
    f.layout(300, 140);
    if (!f.host.show())
      return;
    f.layout(300, 140);
    NSView *root = f.host.root;
    settle(root);
    NSScrollView *native = scrollUnder(root);
    NSView *unit = inside ? unitUnder([native documentView]) : unitUnder(root);
    LOKA_VERIFY(native && unit);
    LOKA_VERIFY(inside ? [unit isDescendantOf:[native documentView]]
                       : [unit superview] == root && [native superview] == root);
    const NSRect bounds = [unit bounds];
    const NSPoint point = [unit convertPoint:NSMakePoint(NSMidX(bounds), NSMidY(bounds)) toView:root];
    NSView *hit = nil;
    NSArray *children = [root subviews];
    for (NSUInteger i = [children count]; i > 0 && !hit; --i)
      hit = [[children objectAtIndex:i - 1] hitTest:point];
    LOKA_VERIFY(hit && (hit == unit || [hit isDescendantOf:unit]));
    for (int horizontal = 0; horizontal != 2; ++horizontal)
    {
      // Positive control: the synthesized wheel must move the ScrollView it is sent to.
      scrollToMiddle(native, root);
      const CGFloat start = along(native, horizontal);
      [native scrollWheel:lineWheel(horizontal)];
      settle(root);
      if (along(native, horizontal) == start)
      {
        std::printf("[skip] testMacZStackUnitWheelLeavesGroundScrollView: a synthesized wheel does not scroll "
                    "an NSScrollView here, so the %s %s half did not run\n",
                    inside ? "inside" : "sibling",
                    horizontal ? "horizontal" : "vertical");
        continue;
      }
      scrollToMiddle(native, root);
      [hit scrollWheel:lineWheel(horizontal)];
      settle(root);
      LOKA_VERIFY(inside ? along(native, horizontal) != start : along(native, horizontal) == start);
    }
  }
#else
  std::printf(
      "[skip] testMacZStackUnitWheelLeavesGroundScrollView: CGEventCreateScrollWheelEvent needs the 10.5 SDK\n");
#endif
}

void testMacZStackSiblingAfterZStackDoesNotReorderUnits()
{
  loka::core::MutableState<loka::core::String> storage(loka::core::String("selection"));
  NodeState<loka::core::String> text(&storage, 0);
  Fixture f;
  StackNode *column = new StackNode(StackProps(STACK_AXIS_COLUMN));
  f.tree = column;
  ZStackNode *stack = new ZStackNode((ZStackProps()));
  stack->addChild(new BoxNode(BoxProps().setSize(200, 80)));
  stack->addChild(new EditTextNode(EditTextProps(text)));
  column->addChild(stack);
  column->addChild(new ButtonNode(ButtonProps().text("Later sibling")));
  f.layout();
  NSView *unit = unitUnder(f.host.root);
  NSTextField *field = (NSTextField *)childOfClass(unit, [NSTextField class]);
  LOKA_VERIFY(unit && field);
  NSArray *before = [[f.host.root subviews] copy];
  if (f.host.show())
  {
    if (![f.host.window makeFirstResponder:field] || ![field currentEditor])
      std::printf("[skip] ZStack selection: window could not acquire a field editor\n");
    else
    {
      NSText *editor = [field currentEditor];
      [editor setSelectedRange:NSMakeRange(1, 3)];
      for (int i = 0; i < 3; ++i)
        f.layout();
      LOKA_VERIFY([f.host.window firstResponder] == editor);
      LOKA_VERIFY(NSEqualRanges([editor selectedRange], NSMakeRange(1, 3)));
    }
  }
  for (int i = 0; i < 3; ++i)
    f.layout();
  LOKA_VERIFY([[f.host.root subviews] isEqualToArray:before]);
  [before release];
}

void testMacZStackGroundCreatedViewGoesBelowUnits()
{
  loka::core::MutableState<bool> visible(false);
  Fixture f;
  NodeDefinitionBase *definition =
      (ZStack() << (Column() << (Show(visible) << Button("New ground"))) << RectSurface().size(40, 80)).clone();
  LOKA_VERIFY(definition);
  Scene scene(definition);
  LOKA_VERIFY(scene.mount(&f.controller));
  loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);
  f.controller.relayout(300, 200);
  NSView *unit = unitUnder(f.host.root);
  LOKA_VERIFY(unit && !childOfClass(f.host.root, [NSButton class]));
  visible.set(true);
  scene.flushInvalidation();
  f.controller.relayout(300, 200);
  NSView *button = childOfClass(f.host.root, [NSButton class]);
  NSArray *views = [f.host.root subviews];
  LOKA_VERIFY(button && [views indexOfObjectIdenticalTo:button] < [views indexOfObjectIdenticalTo:unit]);
  loka::dsl::testing::SceneTestAccess::unmount(scene);
  f.controller.drainNativeRetirements();
}

void testMacZStackUnitCreationRefusalLaysOutNothing()
{
  Fixture f;
  ZStackNode *stack = stackWithGround(f);
  StackNode *column = buttonColumn();
  stack->addChild(column);
  loka::testing::failMacStackUnitViews(1);
  f.layout();
  loka::testing::failMacStackUnitViews(0);
  LOKA_VERIFY(!unitUnder(f.host.root) && !childOfClass(f.host.root, [NSButton class]));
  LOKA_VERIFY(!column->childrenHead()->getContext());
  f.layout();
  NSView *unit = unitUnder(f.host.root);
  LOKA_VERIFY(unit && childOfClass(unit, [NSButton class]));
  LOKA_VERIFY(!childOfClass(f.host.root, [NSButton class]));
}

void testMacZStackLateFirstUnitLandsBelowSecond()
{
  Fixture f;
  ZStackNode *stack = stackWithGround(f);
  stack->addChild(new BoxNode(BoxProps().setSize(40, 80)));
  stack->addChild(new BoxNode(BoxProps().setSize(40, 60)));
  loka::testing::failMacStackUnitViews(1);
  f.layout();
  loka::testing::failMacStackUnitViews(0);
  NSArray *before = [f.host.root subviews];
  NSView *unit2 = unitUnder(f.host.root);
  LOKA_VERIFY([before count] == 2 && unit2 && [unit2 frame].size.height == 60);
  NSView *ground = [before objectAtIndex:0];
  const NSUInteger groundBefore = [before indexOfObject:ground];
  const NSUInteger secondBefore = [before indexOfObject:unit2];
  LOKA_VERIFY(groundBefore < secondBefore);

  UnitInsertionProbe probe(unit2);
  f.layout();
  LOKA_VERIFY(probe.insertions() == 0);
  NSArray *after = [f.host.root subviews];
  NSView *unit1 = unitUnder(f.host.root);
  LOKA_VERIFY([after count] == 3 && unit1 != unit2 && [unit1 frame].size.height == 80);
  const NSUInteger groundAfter = [after indexOfObject:ground];
  const NSUInteger secondAfter = [after indexOfObject:unit2];
  LOKA_VERIFY(groundAfter == groundBefore && secondAfter == secondBefore + 1);
  LOKA_VERIFY(groundAfter < [after indexOfObject:unit1] && [after indexOfObject:unit1] < secondAfter);
  LOKA_VERIFY([unit2 superview] == f.host.root && [unit2 frame].size.height == 60);
  // Only the new unit inserted below unit2 changes its index; a settled pass
  // must preserve both units' indices relative to the same ground view.
  f.layout();
  LOKA_VERIFY([[f.host.root subviews] indexOfObject:ground] == groundAfter);
  LOKA_VERIFY([[f.host.root subviews] indexOfObject:unit2] == secondAfter);
  LOKA_VERIFY(probe.insertions() == 0);
}

void testMacScrollViewRefusedInsideUnit()
{
  Fixture f;
  ZStackNode *stack = stackWithGround(f);
  ScrollViewNode *scroll = new ScrollViewNode((ScrollViewProps()));
  scroll->addChild(buttonColumn());
  stack->addChild(scroll);
  f.layout();
  NSView *unit = unitUnder(f.host.root);
  LOKA_VERIFY(unit && [[unit subviews] count] == 0 && [unit frame].size.height == 0);
  LOKA_VERIFY(!scroll->getContext() && !scrollUnder(f.host.root));
}

void testMacZStackNestedZStackInUnitLaysOutNothing()
{
  Fixture f;
  ZStackNode *stack = stackWithGround(f);
  ZStackNode *nested = new ZStackNode((ZStackProps()));
  nested->addChild(buttonColumn());
  nested->addChild(buttonColumn());
  stack->addChild(nested);
  f.layout();
  NSView *unit = unitUnder(f.host.root);
  LOKA_VERIFY(unit && [[unit subviews] count] == 0 && [unit frame].size.height == 0);
  LOKA_VERIFY(!nested->getContext() && !nested->childrenHead()->getContext());
}

void testMacScrollViewHandlerChildRefusalBlocksCommit()
{
  loka::core::MutableState<Frame> storage(Frame(0, 1000, 0, 0));
  NodeState<Frame> origin(&storage, 0);
  Fixture f;
  ScrollViewNode *scroll = new ScrollViewNode(ScrollViewProps(origin));
  f.tree = scroll;
  StackNode *column = new StackNode(StackProps(STACK_AXIS_COLUMN));
  scroll->addChild(column);
  BoxNode *box = new BoxNode(BoxProps().setSize(180, 400));
  column->addChild(box);
  column->addChild(new TextNode(TextProps("After")));
  f.layout(300, 140);
  const int clamped = origin.get().y;
  LOKA_VERIFY(clamped > 0 && clamped < 1000);
  f.layout(300, 140);
  NSScrollView *native = scrollUnder(f.host.root);
  const NSRect before = [[native documentView] frame];
  const NSRect clip = [[native contentView] bounds];
  box->props.setSize(180, SHRT_MAX - MacSceneLayout::kRootInset - MacSceneLayout::kGap + 1);
  f.layout(300, 140);
  LOKA_VERIFY(NSEqualRects([[native documentView] frame], before));
  LOKA_VERIFY(NSEqualRects([[native contentView] bounds], clip) && origin.get().y == clamped);
}

void testMacZStackRootUnitChildRefusalKeepsPreviousRectangle()
{
  // Deliberate twin of the other native rail's root-unit refusal pin.
  Fixture f;
  ZStackNode *stack = stackWithGround(f);
  StackNode *column = new StackNode(StackProps(STACK_AXIS_COLUMN));
  ImageViewNode *leading = new ImageViewNode(ImageViewProps().size(180, 80));
  column->addChild(leading);
  column->addChild(new ButtonNode(ButtonProps().text("After")));
  stack->addChild(column);
  f.layout(300, 140);
  NSView *unit = unitUnder(f.host.root);
  LOKA_VERIFY(unit);
  const NSRect before = [unit frame];
  LOKA_VERIFY(before.size.height > 0);
  leading->props.size(180, static_cast<short>(SHRT_MAX - MacSceneLayout::kRootInset - MacSceneLayout::kGap + 1));
  f.layout(300, 140);
  LOKA_VERIFY(NSEqualRects([unit frame], before));
}

void testMacZStackUnitInsideViewportPropagatesRefusal()
{
  loka::core::MutableState<Frame> storage(Frame(0, 1000, 0, 0));
  NodeState<Frame> origin(&storage, 0);
  Fixture f;
  ScrollViewNode *scroll = new ScrollViewNode(ScrollViewProps(origin));
  f.tree = scroll;
  ZStackNode *stack = new ZStackNode((ZStackProps()));
  scroll->addChild(stack);
  RectSurfaceNode *ground = new RectSurfaceNode(RectSurfaceProps().size(200, 400));
  stack->addChild(ground);
  StackNode *column = new StackNode(StackProps(STACK_AXIS_COLUMN));
  ImageViewNode *leading = new ImageViewNode(ImageViewProps().size(180, 80));
  column->addChild(leading);
  column->addChild(new ButtonNode(ButtonProps().text("After")));
  stack->addChild(column);
  f.layout(300, 140);
  const int clamped = origin.get().y;
  LOKA_VERIFY(clamped > 0 && clamped < 1000);
  f.layout(300, 140);
  NSScrollView *native = scrollUnder(f.host.root);
  NSView *unit = unitUnder([native documentView]);
  LOKA_VERIFY(unit);
  const NSRect before = [[native documentView] frame];
  const NSRect clip = [[native contentView] bounds];
  const NSRect unitBefore = [unit frame];
  ground->props.size(200, 10);
  leading->props.size(180, SHRT_MAX - MacSceneLayout::kRootInset - MacSceneLayout::kGap + 1);
  f.layout(300, 140);
  LOKA_VERIFY(NSEqualRects([[native documentView] frame], before));
  LOKA_VERIFY(NSEqualRects([[native contentView] bounds], clip) && origin.get().y == clamped);
  LOKA_VERIFY(NSEqualRects([unit frame], unitBefore));
}

void testMacZStackGroundRefusalStopsUpperChildren()
{
  Fixture f;
  ScrollViewNode *scroll = new ScrollViewNode((ScrollViewProps()));
  f.tree = scroll;
  ZStackNode *stack = new ZStackNode((ZStackProps()));
  scroll->addChild(stack);
  stack->addChild(new RectSurfaceNode(RectSurfaceProps().size(180, SHRT_MAX)));
  StackNode *column = buttonColumn();
  stack->addChild(column);
  f.layout(300, 140);
  NSScrollView *native = scrollUnder(f.host.root);
  LOKA_VERIFY(native && !unitUnder([native documentView]));
  LOKA_VERIFY(!column->childrenHead()->getContext());
}

void testMacZStackFirstPassRefusalKeepsUnitEmpty()
{
  Fixture f;
  ScrollViewNode *scroll = new ScrollViewNode((ScrollViewProps()));
  f.tree = scroll;
  ZStackNode *stack = new ZStackNode((ZStackProps()));
  scroll->addChild(stack);
  stack->addChild(new RectSurfaceNode(RectSurfaceProps().size(180, 80)));
  StackNode *column = new StackNode(StackProps(STACK_AXIS_COLUMN));
  column->addChild(new BoxNode(BoxProps().setSize(180, SHRT_MAX)));
  column->addChild(new TextNode(TextProps("Must not materialize")));
  stack->addChild(column);
  f.layout(300, 140);
  NSView *unit = unitUnder([scrollUnder(f.host.root) documentView]);
  LOKA_VERIFY(unit && [unit frame].size.height == 0 && [[unit subviews] count] == 0);
}

namespace
{
  void verifyComposite(bool scrolling, bool layered, ZStackUnitGround unitGround = ZSTACK_UNIT_GROUND_WINDOW)
  {
    std::printf("ZStack composite layer=%d scroll=%d\n", layered, scrolling);
    RectSurfaceModel ink;
    ink.rectCount = 1;
    ink.rects[0] = RectSprite(0, 0, 200, 400);
    loka::core::MutableState<RectSurfaceModel> model(ink);
    Fixture f;
    ZStackNode *stack = new ZStackNode((ZStackProps()));
    stack->props.unitGround = unitGround;
    f.tree = stack;
    RectSurfaceNode *ground = new RectSurfaceNode(RectSurfaceProps().model(&model).size(200, 400));
    if (scrolling)
    {
      ScrollViewNode *scroll = new ScrollViewNode((ScrollViewProps()));
      scroll->addChild(ground);
      stack->addChild(scroll);
    }
    else
      stack->addChild(ground);
    stack->addChild(new BoxNode(BoxProps().setSize(40, 80)));
    f.layout();
    NSView *unit = unitUnder(f.host.root);
    LOKA_VERIFY(unit && [unit isOpaque] && [unit frame].size.height == 80);
    NSScrollView *scroll = scrollUnder(f.host.root);
    LOKA_VERIFY(!scrolling || scroll);
    if (scrolling)
    {
      // NSAppKitVersionNumber11_0 = 2022; numeric like MacTextColorTests for old SDKs.
      if (NSAppKitVersionNumber < 2022)
      {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        LOKA_VERIFY(![[scroll contentView] copiesOnScroll]);
#pragma clang diagnostic pop
      }
      else
        std::printf("[skip] ZStack copiesOnScroll: no-op on 11.0+\n");
    }
    if (!f.host.show(layered))
      return;
    NSBitmapImageRep *before = capture(f.host.root);
    if (!before)
      return;
    if (layered && ![f.host.root layer])
    {
      std::printf("[skip] ZStack layered capture: root has no layer\n");
      return;
    }
    const CGFloat x = [unit frame].origin.x + 100;
    const CGFloat y = [unit frame].origin.y;
    NSColor *windowGround = nil;
    LOKA_VERIFY(loka::macos::QueryMacGroundColor(SURFACE_GROUND_WINDOW, windowGround));
    // On 10.5-10.13 the window ground is a pattern colour that has no RGB form;
    // the bare root margin shows that ground, so sample it as the reference.
    if (![windowGround colorUsingColorSpace:[NSColorSpace deviceRGBColorSpace]])
    {
      std::printf("[note] ZStack composite: window ground is a pattern; comparing against the margin pixel\n");
      windowGround = pixel(before, f.host.root, 2, 2);
    }
    LOKA_VERIFY(sameColor(pixel(before, f.host.root, x, y + 50), windowGround));
    // Positive control: actual composited sprite ink outside the opaque unit.
    // The context binds the model on construction; no Scene attach is needed.
    NSColor *documentGround = nil;
    LOKA_VERIFY(loka::macos::QueryMacGroundColor(SURFACE_GROUND_DOCUMENT, documentGround));
    NSColor *inkColor = spriteColorOverDocument(loka::macos::MacTextRoleColor(), documentGround);
    LOKA_VERIFY(!sameColor(inkColor, windowGround));
    LOKA_VERIFY(sameColor(pixel(before, f.host.root, x, y + 120), inkColor));
    // Prove capture liveness; a stale server image is not stacking evidence.
    [unit setHidden:YES];
    NSBitmapImageRep *uncovered = capture(f.host.root);
    [unit setHidden:NO];
    if (!uncovered)
      return;
    if (sameColor(pixel(uncovered, f.host.root, x, y + 50), windowGround))
    {
      std::printf("[skip] ZStack composite: server image did not observe hiding the unit\n");
      return;
    }
    LOKA_VERIFY(sameColor(pixel(uncovered, f.host.root, x, y + 50), inkColor));
    if (scrolling)
    {
      [[scroll contentView] scrollToPoint:NSMakePoint(0, 30)];
      [scroll reflectScrolledClipView:[scroll contentView]];
      [scroll setNeedsDisplay:YES];
      [[scroll documentView] setNeedsDisplay:YES];
    }
    else
      [[[f.host.root subviews] objectAtIndex:0] setNeedsDisplay:YES];
    NSBitmapImageRep *after = capture(f.host.root);
    if (!after)
      return;
    LOKA_VERIFY(sameColor(pixel(after, f.host.root, x, y + 50), windowGround));
  }
} // namespace

void testMacZStackUnitPaintsOpaqueGround()
{
  verifyComposite(false, false);
  verifyComposite(false, true);
}

void testMacZStackUnitOverScrollViewSurvivesGroundRedraw()
{
  verifyComposite(true, false);
  verifyComposite(true, true);
}

void testMacZStackRetirementDetachesUnitBeforeDrain()
{
  int released = 0;
  Fixture f;
  MacZStackContext context(&f.controller, ZSTACK_UNIT_GROUND_WINDOW);
  MacZStackContext::Unit **cursor = context.unitsBegin();
  MacZStackContext::Unit *unit = context.unitFor(1, cursor);
  NSAutoreleasePool *inner = [[NSAutoreleasePool alloc] init];
  LOKA_VERIFY(
      unit
      && context.ensureUnit(*unit, f.host.root, f.controller.projection().projectFrame(Frame(10, 10, 80, 80)), 0, 0));
  NSView *view = (NSView *)unit->view();
  observeMacNativeDeallocation(view, released);
  context.onFactChanged(NODE_FACT_ATTACHED, NODE_FACT_DETACHED_RETAINED);
  LOKA_VERIFY([view isHidden] && [view superview] == f.host.root);
  context.onFactChanged(NODE_FACT_DETACHED_RETAINED, NODE_FACT_ATTACHED);
  LOKA_VERIFY(![view isHidden]);
  context.onFactChanged(NODE_FACT_ATTACHED, NODE_FACT_RETIRED);
  LOKA_VERIFY(![view superview] && [view isHidden] && released == 0 && !unit->view());
  // removeFromSuperview may retain/autorelease the view. Clear those native
  // temporaries first: only the safe-point queue may keep it alive now.
  [inner drain];
  LOKA_VERIFY(released == 0);
  inner = [[NSAutoreleasePool alloc] init];
  f.controller.drainNativeRetirements();
  [inner drain];
  LOKA_VERIFY(released == 1);
}

void testMacZStackReattachedGroundStaysBelowUnits()
{
  loka::app::testing::TextEditorStateOwner owner;
  loka::core::ObservableList<loka::core::String> lines;
  LOKA_VERIFY(lines.attach(&owner.tracker, 4) == loka::core::ATTACH_OK);
  LOKA_VERIFY(lines.insert(0, loka::core::String("Ground editor")) == loka::core::EDIT_OK);
  Fixture f;
  ZStackNode *stack = new ZStackNode((ZStackProps()));
  f.tree = stack;
  TextEditorNode *editor = new TextEditorNode(TextEditorProps(lines, owner.cursor));
  stack->addChild(editor);
  stack->addChild(new BoxNode(BoxProps().setSize(40, 80)));
  f.layout();
  NSScrollView *scroll = scrollUnder(f.host.root);
  NSView *unit = unitUnder(f.host.root);
  LOKA_VERIFY(scroll && unit);
  LOKA_VERIFY([[f.host.root subviews] indexOfObject:scroll] < [[f.host.root subviews] indexOfObject:unit]);
  NotifySubtreeNodeDetached(editor);
  LifecycleFactTestAccess::DeliverFacts(editor);
  NotifySubtreeNodeAttached(editor);
  LifecycleFactTestAccess::DeliverFacts(editor);
  f.layout();
  LOKA_VERIFY([[f.host.root subviews] indexOfObject:scroll] < [[f.host.root subviews] indexOfObject:unit]);
}

void testMacZStackGroundSuffixPreservesOrder()
{
  Fixture f;
  ZStackNode *stack = stackWithGround(f);
  stack->addChild(new BoxNode(BoxProps().setSize(40, 80)));
  f.layout();
  NSView *unit = unitUnder(f.host.root);
  LOKA_VERIFY(unit);
  NSView *previousLast = [[f.host.root subviews] lastObject];
  NSView *first = [[NSView alloc] initWithFrame:NSZeroRect];
  NSView *second = [[NSView alloc] initWithFrame:NSZeroRect];
  [f.host.root addSubview:first];
  [f.host.root addSubview:second];
  UnitInsertionProbe probe(unit);
  static_cast<MacZStackContext *>(stack->getContext())->lowerNewGroundViews(f.host.root, previousLast);
  NSArray *views = [f.host.root subviews];
  LOKA_VERIFY([views indexOfObject:first] < [views indexOfObject:second]);
  LOKA_VERIFY([views indexOfObject:second] < [views indexOfObject:unit]);
  LOKA_VERIFY(probe.insertions() == 0);
  [first release];
  [second release];
}

// Effect enums are NSInteger at the runtime boundary, including with old SDKs.
@interface NSView (LokaStackEffectTest)
- (NSInteger)blendingMode;
- (NSInteger)material;
- (NSInteger)state;
- (void)viewDidChangeBackingProperties;
@end

void testMacZStackBlurNativeEffect()
{
  Fixture f;
  ZStackNode *stack = stackWithGround(f);
  stack->addChild(new RectSurfaceNode(RectSurfaceProps().size(40, 80)));
  f.layout();
  NSView *windowUnit = unitUnder(f.host.root);
  Class effectClass = NSClassFromString(@"NSVisualEffectView");
  LOKA_VERIFY(windowUnit && [windowUnit isOpaque]);
  LOKA_VERIFY(!effectClass || !childOfClass(windowUnit, effectClass));
  if (!effectClass)
  {
    std::printf("[skip] ZStack native blur: NSVisualEffectView unavailable\n");
    return;
  }
  Fixture blurred;
  ZStackNode *blur = stackWithGround(blurred);
  blur->props.unitGround = ZSTACK_UNIT_GROUND_EXP_BLUR;
  blur->addChild(new RectSurfaceNode(RectSurfaceProps().size(40, 80)));
  blurred.layout();
  NSView *unit = unitUnder(blurred.host.root);
  LOKA_VERIFY(unit && ![unit isOpaque] && [unit isFlipped] && ![unit acceptsFirstResponder]);
  NSArray *children = [unit subviews];
  LOKA_VERIFY([children count] == 2);
  NSView *effect = [children objectAtIndex:0];
  LOKA_VERIFY([effect isKindOfClass:effectClass]);
  LOKA_VERIFY(![[children objectAtIndex:1] isKindOfClass:effectClass]);
  LOKA_VERIFY([effect blendingMode] == 1 && [effect state] == 0);
  LOKA_VERIFY([effect material] == (NSAppKitVersionNumber >= 1671 ? 13 : 3));
  LOKA_VERIFY(NSEqualRects([effect frame], [unit bounds]));
  LOKA_VERIFY([effect autoresizingMask] == (NSViewWidthSizable | NSViewHeightSizable));
  blurred.layout(260, 200);
  LOKA_VERIFY(unitUnder(blurred.host.root) == unit && NSEqualRects([effect frame], [unit bounds]));
}

void testMacZStackBlurFirstUnitOnly()
{
  Class effectClass = NSClassFromString(@"NSVisualEffectView");
  if (!effectClass)
  {
    std::printf("[skip] ZStack first-unit blur: NSVisualEffectView unavailable\n");
    return;
  }
  Fixture f;
  ZStackNode *stack = stackWithGround(f);
  stack->props.unitGround = ZSTACK_UNIT_GROUND_EXP_BLUR;
  stack->addChild(new RectSurfaceNode(RectSurfaceProps().size(40, 80)));
  stack->addChild(new RectSurfaceNode(RectSurfaceProps().size(60, 100)));
  f.layout();
  NSView *first = unitUnder(f.host.root);
  NSArray *views = [f.host.root subviews];
  LOKA_VERIFY([views count] == 3 && [views objectAtIndex:1] == first);
  NSView *second = [views objectAtIndex:2];
  LOKA_VERIFY(first && ![first isOpaque]);
  NSArray *children = [first subviews];
  LOKA_VERIFY([children count] == 2);
  LOKA_VERIFY([[children objectAtIndex:0] isKindOfClass:effectClass]);
  LOKA_VERIFY(![[children objectAtIndex:1] isKindOfClass:effectClass]);
  LOKA_VERIFY(second != first && [second class] == NSClassFromString(@"LokaStackUnitView"));
  LOKA_VERIFY([second isOpaque] && [second isFlipped] && ![second acceptsFirstResponder]);
  LOKA_VERIFY(!childOfClass(second, effectClass));
  LOKA_VERIFY([[second subviews] count] == 1);
}

void testMacZStackBlurGroundIsCreationTimeConstant()
{
#ifdef LOKA_LIFECYCLE_AUDIT
  // The mismatching applyPropsToNode below fires MacZStackContext::onPropsApplied's
  // lifecycle-audit assertion. Exercise the ignore contract in a non-audit build.
  std::printf("[skip] ZStack retained ground: requires LOKA_LIFECYCLE_AUDIT=OFF; mismatch asserts in onPropsApplied\n");
#else
  if (!NSClassFromString(@"NSVisualEffectView"))
  {
    std::printf("[skip] ZStack retained ground: requires NSVisualEffectView to distinguish the grounds\n");
    return;
  }
  for (int initiallyBlurred = 0; initiallyBlurred != 2; ++initiallyBlurred)
  {
    Fixture f;
    ZStackNode *stack = stackWithGround(f);
    stack->setPropsTypeId(ZStackProps::staticTypeId());
    stack->props.unitGround = initiallyBlurred ? ZSTACK_UNIT_GROUND_EXP_BLUR : ZSTACK_UNIT_GROUND_WINDOW;
    // First projection has only ground: the context must already own the choice.
    f.layout();
    NodeContext *context = stack->getContext();
    LOKA_VERIFY(context);
    ZStackProps changed;
    changed.unitGround = initiallyBlurred ? ZSTACK_UNIT_GROUND_WINDOW : ZSTACK_UNIT_GROUND_EXP_BLUR;
    LOKA_VERIFY(ZStack(changed).applyPropsToNode(stack));
    stack->addChild(new BoxNode(BoxProps().setSize(200, 80)));
    f.layout();
    NSView *first = unitUnder(f.host.root);
    LOKA_VERIFY(first && [first isOpaque] == !initiallyBlurred);
    LOKA_VERIFY(stack->getContext() == context);
    // A further retained apply must neither reinsert the first view nor change
    // window ground for another unit admitted afterwards.
    UnitInsertionProbe probe(first);
    LOKA_VERIFY(ZStack(changed).applyPropsToNode(stack));
    stack->addChild(new BoxNode(BoxProps().setSize(200, 40)));
    f.layout();
    NSArray *views = [f.host.root subviews];
    LOKA_VERIFY([views count] == 3 && [views objectAtIndex:1] == first);
    NSView *second = [views objectAtIndex:2];
    LOKA_VERIFY(second != first && [second isOpaque]);
    LOKA_VERIFY(stack->getContext() == context && probe.insertions() == 0);
  }
#endif
}

namespace
{
  NSBitmapImageRep *captureUnit(NSView *unit)
  {
    NSBitmapImageRep *bitmap = [unit bitmapImageRepForCachingDisplayInRect:[unit bounds]];
    LOKA_VERIFY(bitmap != nil);
    [unit cacheDisplayInRect:[unit bounds] toBitmapImageRep:bitmap];
    return bitmap;
  }

  unsigned long photoFingerprint(NSView *unit)
  {
    NSBitmapImageRep *photo = loka::testing::macStackPhoto(unit);
    LOKA_VERIFY(photo && [photo bitmapData]);
    LOKA_VERIFY([photo bytesPerRow] == 4 * [photo pixelsWide]);
    unsigned long hash = 5381;
    const size_t size = static_cast<size_t>([photo bytesPerRow]) * [photo pixelsHigh];
    for (size_t i = 0; i < size; ++i)
      hash = (hash * 33) ^ [photo bitmapData][i];
    return hash;
  }

  void drawRedSurface(id view, SEL, NSRect)
  {
    [[NSColor colorWithDeviceRed:1 green:0 blue:0 alpha:1] setFill];
    NSRectFill([view bounds]);
  }
  void drawBlueSurface(id view, SEL, NSRect)
  {
    [[NSColor colorWithDeviceRed:0 green:0 blue:1 alpha:1] setFill];
    NSRectFill([view bounds]);
  }
  void drawSplitSurface(id view, SEL, NSRect)
  {
    const NSRect bounds = [view bounds];
    [[NSColor redColor] setFill];
    NSRectFill(bounds);
    [[NSColor blueColor] setFill];
    NSRectFill(NSMakeRect(NSMinX(bounds), NSMidY(bounds), NSWidth(bounds), NSHeight(bounds) / 2));
  }

  /** Synthetic dirty lists exercise the real draw entry in an on-screen pass.
      The normal full-list case is also tested without this replacement. */
  void partialPhotoRects(id view, SEL, const NSRect **rects, NSInteger *count)
  {
    static NSRect part;
    part = NSInsetRect([view visibleRect], 2, 2);
    *rects = &part;
    *count = 1;
  }
  void splitPhotoRects(id view, SEL, const NSRect **rects, NSInteger *count)
  {
    static NSRect parts[2];
    const NSRect visible = [view visibleRect];
    parts[0] = NSMakeRect(NSMinX(visible), NSMinY(visible), NSWidth(visible) / 2, NSHeight(visible));
    parts[1] = NSMakeRect(NSMidX(visible), NSMinY(visible), NSWidth(visible) / 2, NSHeight(visible));
    *rects = parts;
    *count = 2;
  }
  NSRect emptyPhotoRect(id, SEL)
  {
    return NSZeroRect;
  }
  NSRect changedPhotoRect(id view, SEL)
  {
    return NSInsetRect([view bounds], 3, 4);
  }
  BOOL refusePhotoReplay(id, SEL, NSRect)
  {
    return NO;
  }
  id ownPhotoLayer(id, SEL);
  NSDictionary *noTransparency(id, SEL, NSString *)
  {
    return [NSDictionary dictionaryWithObject:[NSNumber numberWithBool:NO] forKey:@"AppleEnableMenuBarTransparency"];
  }

  /** Observes requests made inside draw, even if AppKit later consumes them. */
  class PhotoDrawProbe
  {
  public:
    PhotoDrawProbe()
        : originalDraw_(method_getImplementation(class_getInstanceMethod(photoClass(), @selector(drawRect:)))),
          originalDamage_(method_getImplementation(class_getInstanceMethod(photoClass(), @selector(setNeedsDisplay:)))),
          originalRectDamage_(
              method_getImplementation(class_getInstanceMethod(photoClass(), @selector(setNeedsDisplayInRect:)))),
          depth_(0),
          draws_(0),
          requests_(0),
          draw_(@selector(drawRect:), reinterpret_cast<IMP>(&Draw), photoClass()),
          damage_(@selector(setNeedsDisplay:), reinterpret_cast<IMP>(&Damage), photoClass()),
          rectDamage_(@selector(setNeedsDisplayInRect:), reinterpret_cast<IMP>(&RectDamage), photoClass())
    {
      LOKA_VERIFY(!active_);
      active_ = this;
    }
    ~PhotoDrawProbe()
    {
      active_ = 0;
    }
    unsigned draws() const
    {
      return this->draws_;
    }
    unsigned requests() const
    {
      return this->requests_;
    }
    static bool drawing()
    {
      return active_ && active_->depth_ != 0;
    }
    static Class photoClass()
    {
      return NSClassFromString(@"LokaStackPhotoUnitView");
    }

  private:
    static void Draw(id view, SEL selector, NSRect rect)
    {
      PhotoDrawProbe &p = *active_;
      ++p.depth_;
      ++p.draws_;
      reinterpret_cast<void (*)(id, SEL, NSRect)>(p.originalDraw_)(view, selector, rect);
      --p.depth_;
    }
    static void Damage(id view, SEL selector, BOOL value)
    {
      PhotoDrawProbe &p = *active_;
      if (p.depth_ && value)
        ++p.requests_;
      reinterpret_cast<void (*)(id, SEL, BOOL)>(p.originalDamage_)(view, selector, value);
    }
    static void RectDamage(id view, SEL selector, NSRect rect)
    {
      PhotoDrawProbe &p = *active_;
      if (p.depth_)
        ++p.requests_;
      reinterpret_cast<void (*)(id, SEL, NSRect)>(p.originalRectDamage_)(view, selector, rect);
    }
    IMP originalDraw_, originalDamage_, originalRectDamage_;
    unsigned depth_, draws_, requests_;
    MacNativeRefusal draw_, damage_, rectDamage_;
    static PhotoDrawProbe *active_;
    PhotoDrawProbe(const PhotoDrawProbe &);
    PhotoDrawProbe &operator=(const PhotoDrawProbe &);
  };
  PhotoDrawProbe *PhotoDrawProbe::active_ = 0;
  id ownPhotoLayer(id, SEL)
  {
    // Present capability loss only at the production draw gate, without
    // asking AppKit to replace this fixture's backing or display path.
    return PhotoDrawProbe::drawing() ? [[[NSClassFromString(@"CALayer") alloc] init] autorelease] : nil;
  }

  bool legacyPhotosAvailable()
  {
    if (NSClassFromString(@"NSVisualEffectView"))
    {
      std::printf("[skip] ZStack legacy photos: NSVisualEffectView factory branch; run on Mavericks\n");
      return false;
    }
    if (![NSScreen mainScreen] || loka::testing::MacConsoleScreenIsLocked())
    {
      std::printf("[skip] ZStack legacy photos: no unlocked GUI screen\n");
      return false;
    }
    NSDictionary *global = [[NSUserDefaults standardUserDefaults] persistentDomainForName:NSGlobalDomain];
    id transparency = [global objectForKey:@"AppleEnableMenuBarTransparency"];
    if (transparency && [transparency respondsToSelector:@selector(boolValue)] && ![transparency boolValue])
    {
      std::printf("[skip] ZStack legacy photos: transparency disabled; enable for positive capture control\n");
      return false;
    }
    return true;
  }

  NSView *preparePhoto(Fixture &f, ZStackUnitGround ground)
  {
    ZStackNode *stack = stackWithGround(f);
    stack->props.unitGround = ground;
    stack->addChild(new BoxNode(BoxProps().setSize(200, 80)));
    stack->addChild(new BoxNode(BoxProps().setSize(200, 60)));
    f.layout();
    NSView *unit = unitUnder(f.host.root);
    LOKA_VERIFY(unit && ![unit isOpaque] && [unit isFlipped] && ![unit acceptsFirstResponder]);
    LOKA_VERIFY([unit class]
                == NSClassFromString(ground == ZSTACK_UNIT_GROUND_EXP_BLUR ? @"LokaStackBandUnitView"
                                                                           : @"LokaStackStaticUnitView"));
    LOKA_VERIFY([[[f.host.root subviews] objectAtIndex:2] class] == NSClassFromString(@"LokaStackUnitView"));
    return unit;
  }
  bool showPhoto(Fixture &f)
  {
    if (!f.host.show())
      return false;
    if (![f.host.window isVisible] || ![f.host.window screen])
    {
      std::printf("[skip] ZStack legacy photos: ordered window is not on screen\n");
      return false;
    }
    return true;
  }
  void fullPhotoDraw(Fixture &f)
  {
    [f.host.root setNeedsDisplay:YES];
    [f.host.window display];
  }
  void verifyEmptyPhoto(NSView *unit)
  {
    LOKA_VERIFY(!loka::testing::macStackPhoto(unit));
    LOKA_VERIFY(!loka::testing::macStackPhotoDisabled(unit));
  }
} // namespace

void testMacZStackPhotoCapturePolicy()
{
  if (!legacyPhotosAvailable())
    return;
  for (unsigned policy = 0; policy < 2; ++policy)
  {
    Fixture f;
    NSView *unit = preparePhoto(f, policy ? ZSTACK_UNIT_GROUND_EXP_STATIC_BLUR : ZSTACK_UNIT_GROUND_EXP_BLUR);
    MacNativeRefusal red(
        @selector(drawRect:), reinterpret_cast<IMP>(&drawRedSurface), NSClassFromString(@"LokaRectSurfaceView"));
    if (!showPhoto(f))
      return;
    PhotoDrawProbe probe;
    fullPhotoDraw(f);
    const unsigned long first = photoFingerprint(unit);
    LOKA_VERIFY(probe.draws() > 0 && probe.requests() == 0);
    // Full native dirty list is the positive control. Ground changes below
    // now must not enter a partial or fragmented capture.
    MacNativeRefusal blue(
        @selector(drawRect:), reinterpret_cast<IMP>(&drawBlueSurface), NSClassFromString(@"LokaRectSurfaceView"));
    {
      MacNativeRefusal partial(@selector(getRectsBeingDrawn:count:),
                               reinterpret_cast<IMP>(&partialPhotoRects),
                               PhotoDrawProbe::photoClass());
      fullPhotoDraw(f);
      LOKA_VERIFY(photoFingerprint(unit) == first);
    }
    {
      MacNativeRefusal split(
          @selector(getRectsBeingDrawn:count:), reinterpret_cast<IMP>(&splitPhotoRects), PhotoDrawProbe::photoClass());
      fullPhotoDraw(f);
      LOKA_VERIFY(photoFingerprint(unit) == first);
    }
    fullPhotoDraw(f);
    if (policy)
      LOKA_VERIFY(photoFingerprint(unit) == first);
    else
    {
      LOKA_VERIFY(photoFingerprint(unit) != first);
      // Failed retake must discard an existing photo, not just disable an
      // initially empty unit. The same key must then refuse another attempt.
      loka::testing::failMacPhotoCaptures(1);
      fullPhotoDraw(f);
      LOKA_VERIFY(!loka::testing::macStackPhoto(unit) && loka::testing::macStackPhotoDisabled(unit));
      fullPhotoDraw(f);
      LOKA_VERIFY(!loka::testing::macStackPhoto(unit) && loka::testing::macStackPhotoDisabled(unit));
      loka::testing::failMacPhotoCaptures(0);
    }
    LOKA_VERIFY(probe.requests() == 0);
  }
}

void testMacZStackPhotoLifetime()
{
  if (!legacyPhotosAvailable())
    return;
  for (unsigned policy = 0; policy < 2; ++policy)
  {
    Fixture f;
    NSView *unit = preparePhoto(f, policy ? ZSTACK_UNIT_GROUND_EXP_STATIC_BLUR : ZSTACK_UNIT_GROUND_EXP_BLUR);
    if (!showPhoto(f))
      return;
    fullPhotoDraw(f);
    LOKA_VERIFY(loka::testing::macStackPhoto(unit));
    MacZStackContext *context = static_cast<MacZStackContext *>(f.tree->getContext());
    context->onFactChanged(NODE_FACT_ATTACHED, NODE_FACT_DETACHED_RETAINED);
    verifyEmptyPhoto(unit);
    context->onFactChanged(NODE_FACT_DETACHED_RETAINED, NODE_FACT_ATTACHED);
    verifyEmptyPhoto(unit);
    fullPhotoDraw(f);
    LOKA_VERIFY(loka::testing::macStackPhoto(unit));
    MacZStackContext::Unit *seat = *context->unitsBegin();
    const unsigned long beforeEmpty = photoFingerprint(unit);
    seat->place(f.controller.projection().projectFrame(Frame(10, 10, 180, 0)));
    LOKA_VERIFY(photoFingerprint(unit) == beforeEmpty);
    seat->place(f.controller.projection().projectFrame(Frame(10, 10, 180, 90)));
    verifyEmptyPhoto(unit);
    // A failed attempt consumes the hook once and disables both policies.
    loka::testing::failMacPhotoCaptures(1);
    fullPhotoDraw(f);
    LOKA_VERIFY(!loka::testing::macStackPhoto(unit) && loka::testing::macStackPhotoDisabled(unit));
    fullPhotoDraw(f);
    LOKA_VERIFY(!loka::testing::macStackPhoto(unit) && loka::testing::macStackPhotoDisabled(unit));
    seat->place(f.controller.projection().projectFrame(Frame(10, 10, 180, 0)));
    LOKA_VERIFY(loka::testing::macStackPhotoDisabled(unit));
    seat->place(f.controller.projection().projectFrame(Frame(10, 10, 180, 100)));
    verifyEmptyPhoto(unit);
    fullPhotoDraw(f);
    LOKA_VERIFY(loka::testing::macStackPhoto(unit));
    // Real ancestor scrolling: same window viewport, different local slice.
    NSClipView *clip = [[NSClipView alloc] initWithFrame:[unit frame]];
    [clip setDrawsBackground:NO];
    [f.host.root addSubview:clip];
    [unit retain];
    [unit removeFromSuperview];
    [unit setFrame:NSMakeRect(0, 0, 180, 1000)];
    [clip setDocumentView:unit];
    [unit release];
    fullPhotoDraw(f);
    LOKA_VERIFY(loka::testing::macStackPhoto(unit));
    const NSRect before = loka::testing::macStackPhotoRect(unit);
    [clip scrollToPoint:NSMakePoint(0, 20)];
    {
      MacNativeRefusal partial(@selector(getRectsBeingDrawn:count:),
                               reinterpret_cast<IMP>(&partialPhotoRects),
                               PhotoDrawProbe::photoClass());
      fullPhotoDraw(f);
      verifyEmptyPhoto(unit);
      LOKA_VERIFY(!NSEqualRects(before, loka::testing::macStackPhotoRect(unit)));
    }
    fullPhotoDraw(f);
    LOKA_VERIFY(loka::testing::macStackPhoto(unit));
    context->onFactChanged(NODE_FACT_ATTACHED, NODE_FACT_DETACHED_RETAINED);
    context->onFactChanged(NODE_FACT_DETACHED_RETAINED, NODE_FACT_ATTACHED);
    loka::testing::failMacPhotoCaptures(1);
    fullPhotoDraw(f);
    LOKA_VERIFY(loka::testing::macStackPhotoDisabled(unit));
    [clip scrollToPoint:NSMakePoint(0, 40)];
    {
      MacNativeRefusal partial(@selector(getRectsBeingDrawn:count:),
                               reinterpret_cast<IMP>(&partialPhotoRects),
                               PhotoDrawProbe::photoClass());
      fullPhotoDraw(f);
      verifyEmptyPhoto(unit);
    }
    fullPhotoDraw(f);
    LOKA_VERIFY(loka::testing::macStackPhoto(unit));
    [unit viewDidChangeBackingProperties];
    verifyEmptyPhoto(unit);
    loka::testing::failMacPhotoCaptures(1);
    fullPhotoDraw(f);
    LOKA_VERIFY(loka::testing::macStackPhotoDisabled(unit));
    // A second window is a separate lifetime, even with matching local bounds.
    Host other;
    if (other.show())
    {
      [unit retain];
      [clip setDocumentView:nil];
      [other.root addSubview:unit];
      verifyEmptyPhoto(unit);
      [unit removeFromSuperview];
      [clip setDocumentView:unit];
      [unit release];
      fullPhotoDraw(f);
      LOKA_VERIFY(loka::testing::macStackPhoto(unit));
    }
    loka::testing::failMacPhotoCaptures(0);
    [clip release];
  }
}

void testMacZStackPhotoReplayAndRefusal()
{
  if (!legacyPhotosAvailable())
    return;
  Fixture f;
  PhotoDrawProbe probe;
  NSView *unit = preparePhoto(f, ZSTACK_UNIT_GROUND_EXP_BLUR);
  if (!showPhoto(f))
    return;
  // Settle first: ordering the window front delivers its own display and, on
  // some hosts, a backing-properties notification that resets the lifetime.
  fullPhotoDraw(f);
  LOKA_VERIFY(loka::testing::macStackPhoto(unit));
  // An offscreen draw with a pending failure neither captures nor enters
  // DISABLED: the photo stays. The next on-screen full draw consumes it.
  loka::testing::failMacPhotoCaptures(1);
  captureUnit(unit);
  LOKA_VERIFY(loka::testing::macStackPhoto(unit) && !loka::testing::macStackPhotoDisabled(unit));
  fullPhotoDraw(f);
  LOKA_VERIFY(loka::testing::macStackPhotoDisabled(unit));
  {
    MacNativeRefusal changed(
        @selector(visibleRect), reinterpret_cast<IMP>(&changedPhotoRect), PhotoDrawProbe::photoClass());
    captureUnit(unit);
    LOKA_VERIFY(loka::testing::macStackPhotoDisabled(unit));
  }
  fullPhotoDraw(f);
  LOKA_VERIFY(loka::testing::macStackPhotoDisabled(unit));
  MacZStackContext *context = static_cast<MacZStackContext *>(f.tree->getContext());
  context->onFactChanged(NODE_FACT_ATTACHED, NODE_FACT_DETACHED_RETAINED);
  context->onFactChanged(NODE_FACT_DETACHED_RETAINED, NODE_FACT_ATTACHED);
  NSBitmapImageRep *frost = captureUnit(unit);
  MacNativeRefusal red(
      @selector(drawRect:), reinterpret_cast<IMP>(&drawRedSurface), NSClassFromString(@"LokaRectSurfaceView"));
  fullPhotoDraw(f);
  const unsigned long first = photoFingerprint(unit);
  const NSRect geometry = loka::testing::macStackPhotoRect(unit);
  {
    MacNativeRefusal changed(
        @selector(visibleRect), reinterpret_cast<IMP>(&changedPhotoRect), PhotoDrawProbe::photoClass());
    captureUnit(unit);
    LOKA_VERIFY(photoFingerprint(unit) == first);
    LOKA_VERIFY(NSEqualRects(loka::testing::macStackPhotoRect(unit), geometry));
  }
  {
    MacNativeRefusal empty(
        @selector(visibleRect), reinterpret_cast<IMP>(&emptyPhotoRect), PhotoDrawProbe::photoClass());
    fullPhotoDraw(f);
    LOKA_VERIFY(photoFingerprint(unit) == first);
    LOKA_VERIFY(NSEqualRects(loka::testing::macStackPhotoRect(unit), geometry));
  }
  NSBitmapImageRep *replayed = captureUnit(unit);
  NSColor *redPixel = pixel(replayed, unit, 100, 40);
  LOKA_VERIFY([redPixel redComponent] > [redPixel blueComponent] + 0.15);
  {
    MacNativeRefusal fail(@selector(drawInRect:), reinterpret_cast<IMP>(&refusePhotoReplay), [NSBitmapImageRep class]);
    NSBitmapImageRep *fallback = captureUnit(unit);
    LOKA_VERIFY(sameColor(pixel(fallback, unit, 100, 40), pixel(frost, unit, 100, 40)));
    LOKA_VERIFY(photoFingerprint(unit) == first);
  }
  MacNativeRefusal blue(
      @selector(drawRect:), reinterpret_cast<IMP>(&drawBlueSurface), NSClassFromString(@"LokaRectSurfaceView"));
  {
    MacNativeRefusal transparency(
        @selector(persistentDomainForName:), reinterpret_cast<IMP>(&noTransparency), [NSUserDefaults class]);
    fullPhotoDraw(f);
    LOKA_VERIFY(photoFingerprint(unit) == first);
    LOKA_VERIFY(sameColor(pixel(captureUnit(unit), unit, 100, 40), redPixel));
  }
  if ([unit respondsToSelector:@selector(layer)])
  {
    // Replace only the layer query: AppKit's actual layer allocation would
    // itself change backing and reset the photo before exercising refusal.
    MacNativeRefusal layer(@selector(layer), reinterpret_cast<IMP>(&ownPhotoLayer), PhotoDrawProbe::photoClass());
    captureUnit(unit);
    // Invoke the ordinary display while the own-layer gate refuses capture.
    const unsigned draws = probe.draws();
    fullPhotoDraw(f);
    LOKA_VERIFY(probe.draws() > draws);
    LOKA_VERIFY(photoFingerprint(unit) == first);
    LOKA_VERIFY(sameColor(pixel(captureUnit(unit), unit, 100, 40), redPixel));
  }
  else
    std::printf("[skip] ZStack own layer refusal: layer selector unavailable on Tiger\n");
  fullPhotoDraw(f);
  LOKA_VERIFY(photoFingerprint(unit) != first);
}

void testMacZStackPhotoOrientation()
{
  if (!legacyPhotosAvailable())
    return;
  Fixture f;
  PhotoDrawProbe probe;
  NSView *unit = preparePhoto(f, ZSTACK_UNIT_GROUND_EXP_BLUR);
  MacNativeRefusal split(
      @selector(drawRect:), reinterpret_cast<IMP>(&drawSplitSurface), NSClassFromString(@"LokaRectSurfaceView"));
  if (!showPhoto(f))
    return;
  // Orientation: the unit covers the ground exactly, so the photo's first rows
  // are the ground's red top and its last rows the blue bottom.
  NSView *ground = [[f.host.root subviews] objectAtIndex:0];
  [unit setFrame:[ground frame]];
  fullPhotoDraw(f);
  NSBitmapImageRep *photo = loka::testing::macStackPhoto(unit);
  LOKA_VERIFY(photo);
  NSColor *top = [photo colorAtX:[photo pixelsWide] / 2 y:12];
  NSColor *bottom = [photo colorAtX:[photo pixelsWide] / 2 y:[photo pixelsHigh] - 13];
  LOKA_VERIFY([top redComponent] > [top blueComponent] + 0.5);
  LOKA_VERIFY([bottom blueComponent] > [bottom redComponent] + 0.5);
  // Placement: clip the unit at a nonzero local origin. The replay must put
  // each photo row back at the visible rectangle it was taken from, which a
  // bounds-centred reflection would shift; compare replay against the photo
  // rather than against an assumed ground layout.
  NSClipView *clip = [[NSClipView alloc] initWithFrame:NSInsetRect([unit frame], 0, 20)];
  [clip setDrawsBackground:NO];
  [f.host.root addSubview:clip];
  [unit retain];
  [unit removeFromSuperview];
  [unit setFrame:NSMakeRect(0, 0, NSWidth([ground frame]), NSHeight([ground frame]))];
  [clip setDocumentView:unit];
  [unit release];
  [clip scrollToPoint:NSMakePoint(0, 20)];
  fullPhotoDraw(f);
  photo = loka::testing::macStackPhoto(unit);
  LOKA_VERIFY(photo);
  const NSRect visible = loka::testing::macStackPhotoRect(unit);
  LOKA_VERIFY(NSMinY(visible) > 0);
  NSBitmapImageRep *replay = captureUnit(unit);
  const double scale = [photo pixelsHigh] / NSHeight(visible);
  int compared = 0;
  for (int row = 12; row + 12 < [photo pixelsHigh]; row += [photo pixelsHigh] / 6)
  {
    NSColor *expected = [photo colorAtX:[photo pixelsWide] / 2 y:row];
    const double lean = [expected redComponent] - [expected blueComponent];
    if (lean > -0.5 && lean < 0.5)
      continue; // a blurred transition row does not discriminate
    NSColor *actual = pixel(replay, unit, NSMidX(visible), NSMinY(visible) + row / scale);
    const double actualLean = [actual redComponent] - [actual blueComponent];
    LOKA_VERIFY(lean > 0 ? actualLean > 0.15 : actualLean < -0.15);
    ++compared;
  }
  LOKA_VERIFY(compared >= 2);
  [clip release];
}
