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
  void verifyComposite(bool scrolling, bool layered)
  {
    std::printf("ZStack composite layer=%d scroll=%d\n", layered, scrolling);
    RectSurfaceModel ink;
    ink.rectCount = 1;
    ink.rects[0] = RectSprite(0, 0, 200, 400);
    loka::core::MutableState<RectSurfaceModel> model(ink);
    Fixture f;
    ZStackNode *stack = new ZStackNode((ZStackProps()));
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
  MacZStackContext context(&f.controller);
  MacZStackContext::Unit **cursor = context.unitsBegin();
  MacZStackContext::Unit *unit = context.unitFor(1, cursor);
  NSAutoreleasePool *inner = [[NSAutoreleasePool alloc] init];
  LOKA_VERIFY(unit && unit->ensure(f.host.root, f.controller.projection().projectFrame(Frame(10, 10, 80, 80)), 0, 0));
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
