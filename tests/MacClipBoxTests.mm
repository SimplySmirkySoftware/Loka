#include "MacClipBoxTests.hpp"
#include "MacScenePlatformController.hpp"
#include "MacSceneLayout.hpp"
#include "context/MacClipBoxContext.hpp"
#include "context/LokaClipHostView.hpp"
#include "testing/MacScenePlatformTestAccess.hpp"
#include "testing/scene/SceneTestFlow.hpp"
#include "support/TestVerify.hpp"
#include "support/MacNativeRefusal.hpp"
#include "support/MacRetirementProbe.hpp"
#include "support/MacWindowSession.hpp"
#include "support/LifecycleFactTestAccess.hpp"
#include "support/TextBufferFixture.hpp"
#include "support/TextEditorStateOwner.hpp"
#include "app/nodes/controls/Button.hpp"
#include "app/nodes/controls/Card.hpp"
#include "app/nodes/controls/EditText.hpp"
#include "app/nodes/nestable/Box.hpp"
#include "app/nodes/nestable/ScrollView.hpp"
#include "app/nodes/nestable/ZStack.hpp"
#include "app/nodes/nestable/RowColumn.hpp"
#include "app/nodes/nestable/Show.hpp"
#include "app/RectSurface.hpp"
#include "app/nodes/controls/PopupMenu.hpp"
#include "support/MacWindowCapture.hpp"
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <cstring>
#include <climits>
#include <cstdio>

@interface LokaClipTestRoot : NSView
{
  NSRect recorded_;
}
@property(assign) NSRect recorded;
@end
@implementation LokaClipTestRoot
@synthesize recorded = recorded_;
- (BOOL)isFlipped
{
  return YES;
}
- (void)setNeedsDisplayInRect:(NSRect)rect
{
  [self setRecorded:rect];
  [super setNeedsDisplayInRect:rect];
}
@end
@interface LokaClipTestInk : NSView
@end
@implementation LokaClipTestInk
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

namespace
{
  using namespace loka::app;
  using namespace loka::app::scene;
  using loka::core::Frame;
  typedef loka::dsl::testing::MacScenePlatformTestAccess Access;
  LokaClipTestRoot *makeRoot()
  {
    [NSApplication sharedApplication];
    return [[LokaClipTestRoot alloc] initWithFrame:NSMakeRect(0, 0, 300, 200)];
  }
  struct Host
  {
    NSAutoreleasePool *pool;
    LokaClipTestRoot *root;
    NSWindow *window;
    Host()
        : pool([[NSAutoreleasePool alloc] init]),
          root(makeRoot()),
          window(nil)
    {
    }
    ~Host()
    {
      [this->window orderOut:nil];
      [this->window close];
      [this->window release];
      [this->root release];
      [this->pool drain];
    }
    bool show()
    {
      if (loka::testing::MacConsoleScreenIsLocked())
      {
        std::printf("[skip] clip real window: console screen is locked\n");
        return false;
      }
      this->window = [[NSWindow alloc] initWithContentRect:NSMakeRect(40, 40, 300, 200)
                                                 styleMask:LOKA_MAC_WINDOW_STYLE_TITLED
                                                   backing:NSBackingStoreBuffered
                                                     defer:NO];
      LOKA_VERIFY(this->window);
      [this->window setReleasedWhenClosed:NO];
      [this->window setContentView:this->root];
      [this->window makeKeyAndOrderFront:nil];
      return true;
    }
  };
  struct Fixture
  {
    Host host;
    MacScenePlatformController controller;
    Node *tree;
    explicit Fixture(const RailMetrics &metrics = RailMetrics())
        : controller(this->host.root, metrics),
          tree(0)
    {
    }
    ~Fixture()
    {
      this->controller.onChange(0, NODE_DIRTY_NONE, false);
      delete this->tree;
      this->controller.drainNativeRetirements();
    }
    void layout()
    {
      this->controller.onChange(this->tree, NODE_DIRTY_NONE, false);
      this->controller.relayout(300, 200);
    }
    int at(int x, int y, int width, int height)
    {
      LayoutState seat;
      seat.x = x;
      seat.y = y;
      seat.width = width;
      seat.height = height;
      seat.spacing = MacSceneLayout::kGap;
      return Access::layoutNode(this->controller, this->tree, seat);
    }
  };
  BoxNode *box(int width = 100, int height = 40, bool clip = true)
  {
    BoxProps props;
    props.setSize(width, height);
    props.childClip_ = clip ? layout::CHILD_CLIP_OWN_RECT : layout::CHILD_CLIP_NONE;
    return new BoxNode(props);
  }
  NSView *child(NSView *parent, Class type)
  {
    NSArray *views = [parent subviews];
    for (NSUInteger i = 0; i < [views count]; ++i)
      if ([[views objectAtIndex:i] isKindOfClass:type])
        return [views objectAtIndex:i];
    return nil;
  }
  LokaClipHostView *host(Node *node)
  {
    IClipBoxContext *clip = node->getContext() ? node->getContext()->asClipBoxContext() : 0;
    return clip ? (LokaClipHostView *)static_cast<MacClipBoxContext *>(clip)->view() : nil;
  }
  NSView *ink(NSView *parent, NSRect rect)
  {
    NSView *view = [[LokaClipTestInk alloc] initWithFrame:rect];
    [parent addSubview:view];
    [view release];
    return view;
  }
  void verifyRedPixels(NSView *root, NSRect clip, bool clipped)
  {
    [root displayIfNeeded];
    NSBitmapImageRep *rep = [root bitmapImageRepForCachingDisplayInRect:[root bounds]];
    LOKA_VERIFY(rep);
    [root cacheDisplayInRect:[root bounds] toBitmapImageRep:rep];
    unsigned inside = 0, outside = 0;
    const CGFloat scale = [rep pixelsWide] / [root bounds].size.width;
    for (NSInteger y = 0; y < [rep pixelsHigh]; ++y)
      for (NSInteger x = 0; x < [rep pixelsWide]; ++x)
      {
        NSColor *color = [[rep colorAtX:x y:y] colorUsingColorSpace:[NSColorSpace genericRGBColorSpace]];
        if ([color redComponent] > 0.8 && [color greenComponent] < 0.3 && [color blueComponent] < 0.3)
        {
          if (NSPointInRect(NSMakePoint((x + 0.5) / scale, (y + 0.5) / scale), clip))
            ++inside;
          else
            ++outside;
        }
      }
    LOKA_VERIFY(inside > 0);
    LOKA_VERIFY(clipped ? outside == 0 : outside > 0);
  }
} // namespace

void testMacClipBoxGeometryAndTwin()
{
  Fixture clipped, flat;
  BoxNode *a = box(), *b = box(100, 40, false);
  clipped.tree = a;
  flat.tree = b;
  a->addChild(new RectSurfaceNode(RectSurfaceProps().size(100, 100)));
  b->addChild(new RectSurfaceNode(RectSurfaceProps().size(100, 100)));
  LOKA_VERIFY(clipped.at(20, 30, 180, 160) == flat.at(20, 30, 180, 160));
  NSView *h = host(a);
  LOKA_VERIFY(h && NSEqualRects([h frame], NSMakeRect(20, 30, 100, 40)));
  LOKA_VERIFY(NSEqualPoints([h bounds].origin, [h frame].origin));
  NSView *av = child(h, NSClassFromString(@"LokaRectSurfaceView"));
  NSView *bv = child(flat.host.root, NSClassFromString(@"LokaRectSurfaceView"));
  LOKA_VERIFY(av && bv && NSEqualRects([av frame], [bv frame]));
  LOKA_VERIFY([av superview] == h && !b->getContext());
}

void testMacClipBoxHits()
{
  Fixture f;
  LOKA_VERIFY(![f.host.root window]);
  f.tree = box(100, 60);
  f.at(60, 50, 100, 60);
  NSView *parent = host(f.tree);
  LOKA_VERIFY(parent);
  NSView *paint = ink(parent, NSMakeRect(40, 30, 80, 120));
  NSView *below = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 300, 200)];
  [f.host.root addSubview:below positioned:NSWindowBelow relativeTo:parent];
  LOKA_VERIFY([parent hitTest:NSMakePoint(70, 60)] == paint);
  LOKA_VERIFY([parent hitTest:NSMakePoint(50, 60)] == nil);
  LOKA_VERIFY([parent hitTest:NSMakePoint(70, 120)] == nil);
  LOKA_VERIFY([parent hitTest:NSMakePoint(150, 60)] == nil);
  // Deliberate twin of MacZStackUnitTests.mm's hitBelongsTo: hit each child
  // in root coordinates, avoiding a detached flipped root's superview space.
  NSArray *children = [f.host.root subviews];
  NSView *hit = nil;
  for (NSUInteger i = [children count]; i > 0 && !hit; --i)
    hit = [[children objectAtIndex:i - 1] hitTest:NSMakePoint(150, 60)];
  LOKA_VERIFY(hit == below);
  [below release];
}

void testMacClipBoxPixels()
{
  for (int clipped = 0; clipped != 2; ++clipped)
  {
    Fixture f;
    if (!f.host.show())
      return;
    f.tree = box(100, 60, clipped != 0);
    f.at(60, 50, 100, 60);
    NSView *parent = clipped ? host(f.tree) : f.host.root;
    LOKA_VERIFY(parent);
    ink(parent, NSMakeRect(40, 30, 80, 120));
    verifyRedPixels(f.host.root, NSMakeRect(60, 50, 100, 60), clipped != 0);
  }
}

void testMacClipBoxNestedHosts()
{
  Fixture f;
  BoxNode *outer = box(160, 100), *inner = box();
  f.tree = outer;
  outer->addChild(inner);
  inner->addChild(new ButtonNode(ButtonProps().text("Nested")));
  f.layout();
  LOKA_VERIFY(host(outer) && host(inner));
  LOKA_VERIFY([host(inner) superview] == host(outer));
  LOKA_VERIFY(child(host(inner), [NSButton class]));
}

void testMacClipBoxScrollMixes()
{
  for (int inside = 0; inside != 2; ++inside)
  {
    Fixture f;
    BoxNode *clip = box(180, 100);
    ScrollViewNode *scroll = new ScrollViewNode((ScrollViewProps()));
    if (inside)
    {
      f.tree = clip;
      clip->addChild(scroll);
      scroll->addChild(new ButtonNode((ButtonProps())));
    }
    else
    {
      f.tree = scroll;
      scroll->addChild(clip);
      clip->addChild(new ButtonNode((ButtonProps())));
    }
    f.layout();
    NSScrollView *native = (NSScrollView *)child(inside ? host(clip) : f.host.root, [NSScrollView class]);
    LOKA_VERIFY(native && host(clip));
    if (inside)
      LOKA_VERIFY([native superview] == host(clip));
    else
      LOKA_VERIFY([host(clip) superview] == [native documentView]);
  }
}

void testMacClipBoxZStackMixes()
{
  for (int inside = 0; inside != 2; ++inside)
  {
    Fixture f;
    BoxNode *clip = box(180, 100);
    ZStackNode *stack = new ZStackNode((ZStackProps()));
    stack->addChild(new BoxNode(BoxProps().setSize(200, 100)));
    if (inside)
    {
      f.tree = clip;
      clip->addChild(stack);
      stack->addChild(new ButtonNode((ButtonProps())));
    }
    else
    {
      f.tree = stack;
      stack->addChild(clip);
      clip->addChild(new ButtonNode((ButtonProps())));
    }
    f.layout();
    NSView *unit = child(inside ? host(clip) : f.host.root, NSClassFromString(@"LokaStackUnitView"));
    LOKA_VERIFY(unit && host(clip));
    if (inside)
      LOKA_VERIFY([unit superview] == host(clip));
    else
      LOKA_VERIFY([host(clip) superview] == unit);
  }
}

void testMacClipCardBandsAndUnflagged()
{
  for (int clipped = 0; clipped != 2; ++clipped)
  {
    Fixture f;
    NodeDefinitionBase *definition =
        clipped ? (Card().clipChildren() << Button("Card")).clone() : (Card() << Button("Card")).clone();
    Scene scene(definition);
    LOKA_VERIFY(scene.mount(&f.controller));
    loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);
    f.controller.relayout(300, 200);
    NSView *h = child(f.host.root, [LokaClipHostView class]);
    LOKA_VERIFY(clipped ? h != nil : h == nil);
    NSView *parent = clipped ? h : f.host.root;
    LOKA_VERIFY(child(parent, NSClassFromString(@"LokaCardBandView")));
    LOKA_VERIFY(child(parent, [NSButton class]));
    loka::dsl::testing::SceneTestAccess::unmount(scene);
    f.controller.drainNativeRetirements();
  }
  Fixture f;
  CardNode *card = new CardNode((CardProps()));
  f.tree = card;
  f.layout();
  LOKA_VERIFY(!card->getContext() && [[f.host.root subviews] count] == 0);
}

void testMacClipBoxTextEditorParent()
{
  loka::app::testing::TextEditorStateOwner owner;
  loka::testing::TextBufferFixture lines;
  LOKA_VERIFY(lines.attach(&owner.tracker, 4) == loka::core::ATTACH_OK);
  Fixture f;
  BoxNode *clip = box(180, 100);
  f.tree = clip;
  TextEditorNode *editor = new TextEditorNode(TextEditorProps(lines, owner.cursor));
  clip->addChild(editor);
  f.layout();
  NSView *native = child(host(clip), [NSScrollView class]);
  LOKA_VERIFY(native);
  NotifySubtreeNodeDetached(editor);
  LifecycleFactTestAccess::DeliverFacts(editor);
  NotifySubtreeNodeAttached(editor);
  LifecycleFactTestAccess::DeliverFacts(editor);
  LOKA_VERIFY([native superview] == host(clip));
}

void testMacClipBoxContextFailure()
{
  Fixture f;
  BoxNode *clip = box();
  f.tree = clip;
  ButtonNode *button = new ButtonNode((ButtonProps()));
  clip->addChild(button);
  loka::testing::failMacClipBoxContexts(1);
  f.layout();
  LOKA_VERIFY(!clip->getContext() && !button->getContext() && [[f.host.root subviews] count] == 0);
  f.layout();
  LOKA_VERIFY(host(clip) && child(host(clip), [NSButton class]));
}

void testMacClipBoxHostFailureKeepsContext()
{
  Fixture f;
  BoxNode *clip = box();
  f.tree = clip;
  ButtonNode *button = new ButtonNode((ButtonProps()));
  clip->addChild(button);
  loka::testing::failMacClipHostViews(1);
  f.layout();
  NodeContext *context = clip->getContext();
  LOKA_VERIFY(context && !host(clip) && !button->getContext() && [[f.host.root subviews] count] == 0);
  loka::testing::failMacClipBoxContexts(1);
  f.layout();
  loka::testing::failMacClipBoxContexts(0);
  LOKA_VERIFY(clip->getContext() == context && host(clip) && child(host(clip), [NSButton class]));
}

void testMacClipBoxDepthRefusalIsStable()
{
  Fixture f;
  BoxNode *boxes[7];
  for (int i = 0; i < 7; ++i)
  {
    boxes[i] = box(180, 100);
    if (i)
      boxes[i - 1]->addChild(boxes[i]);
    else
      f.tree = boxes[i];
  }
  boxes[6]->addChild(new ButtonNode((ButtonProps())));
  for (int pass = 0; pass < 3; ++pass)
  {
    f.layout();
    for (int i = 0; i < 6; ++i)
      LOKA_VERIFY(host(boxes[i]));
    LOKA_VERIFY(!boxes[6]->getContext());
    LOKA_VERIFY(child(host(boxes[5]), [NSButton class]));
  }
}

void testMacClipBoxForeignContextRefused()
{
#ifdef LOKA_LIFECYCLE_AUDIT
  std::printf("[skip] foreign clip context: lifecycle audit intentionally asserts; run without audit\n");
#else
  struct Foreign : NodeContext
  {
  };
  Fixture f;
  BoxNode *clip = box();
  f.tree = clip;
  ButtonNode *button = new ButtonNode((ButtonProps()));
  clip->addChild(button);
  NodeContext *foreign = new Foreign;
  clip->setContext(foreign);
  for (int pass = 0; pass < 2; ++pass)
  {
    f.layout();
    LOKA_VERIFY(clip->getContext() == foreign && !button->getContext() && [[f.host.root subviews] count] == 0);
  }
#endif
}

void testMacClipBoxUnresolvedWidthProjection()
{
  Fixture f(RailMetrics(Ratio(), Ratio(3, 2)));
  f.tree = box(0, 40);
  ProjectionParentScopeStack &scopes = Access::projectionScopes(f.controller);
  ProjectionParentScope translated(f.host.root, -90, -20, Frame(0, 0, 300, 200));
  ProjectionViewportScopeGuard viewport(scopes, translated);
  LOKA_VERIFY(viewport.isActive());
  f.at(20, 30, 0, 100);
  NSView *h = host(f.tree);
  LOKA_VERIFY(h);
  LOKA_VERIFY(NSMaxX([h frame]) == f.controller.projection().projectEdge(layout::kClipFarRightLu - 90).pt);
  LOKA_VERIFY(NSMinX([h frame]) == f.controller.projection().projectEdge(-70).pt);
  LOKA_VERIFY(NSEqualPoints([h bounds].origin, [h frame].origin));
}

void testMacClipBoxUnresolvedWidthPixelsAndLayers()
{
  for (int layered = 0; layered != 2; ++layered)
  {
    Fixture f;
    if (!f.host.show())
      return;
    if (layered && (NSAppKitVersionNumber < 1187.0 || ![f.host.root respondsToSelector:@selector(setWantsLayer:)]))
    {
      std::printf("[skip] clip layer contents: requires AppKit 10.8 wantsUpdateLayer path\n");
      continue;
    }
    if (layered)
      [f.host.root setWantsLayer:YES];
    f.tree = box(0, 60);
    f.at(60, 50, 0, 100);
    NSView *h = host(f.tree);
    LOKA_VERIFY(h);
    NSView *paint = ink(h, NSMakeRect(40, 30, 250, 120));
    verifyRedPixels(f.host.root, NSMakeRect(60, 50, 240, 60), true);
    LOKA_VERIFY([h hitTest:NSMakePoint(280, 60)] == paint);
    LOKA_VERIFY([h hitTest:NSMakePoint(50, 60)] == nil);
    if (layered)
    {
      // KVC keeps the test compatible with SDKs without a public CALayer header.
      id layer = [h layer];
      LOKA_VERIFY(layer && [layer valueForKey:@"contents"] == nil);
    }
  }
}

namespace
{
  class UnresolvedOffer : public BoxNode
  {
  public:
    UnresolvedOffer()
        : BoxNode(BoxProps())
    {
    }
    virtual const void *nodeTypeKey() const
    {
      return NodeTypeToken<UnresolvedOffer>();
    }
  };
  /** Offers zero width during the actual content pass, before document commit. */
  class UnresolvedOfferHandler : public IPlatformLayoutHandler
  {
  public:
    virtual const void *nodeTypeKey() const
    {
      return NodeTypeToken<UnresolvedOffer>();
    }
    virtual int layoutNode(Node *node,
                           const LayoutState &state,
                           IPlatformLayoutTraversal *traversal,
                           const layout::LazyWindow * = 0,
                           layout::StackSpans * = 0)
    {
      LayoutState offer = state;
      offer.width = 0;
      return traversal->layoutChild(node->asNestable()->childrenHead(), offer);
    }
  };
} // namespace
void testMacClipBoxUnresolvedWidthDoesNotGrowDocument()
{
  NSSize documentSizes[2];
  for (int clipped = 0; clipped != 2; ++clipped)
  {
    Fixture f;
    ScrollViewNode *scroll = new ScrollViewNode((ScrollViewProps()));
    LOKA_VERIFY(Access::registerLayoutHandler(f.controller, new UnresolvedOfferHandler));
    UnresolvedOffer *offer = new UnresolvedOffer;
    BoxNode *clip = box(0, 40, clipped != 0);
    f.tree = scroll;
    scroll->addChild(offer);
    offer->addChild(clip);
    clip->addChild(new RectSurfaceNode(RectSurfaceProps().size(80, 60)));
    f.layout();
    NSScrollView *native = (NSScrollView *)child(f.host.root, [NSScrollView class]);
    LOKA_VERIFY(native);
    documentSizes[clipped] = [[native documentView] frame].size;
    if (clipped)
    {
      LOKA_VERIFY([host(clip) superview] == [native documentView]);
      LOKA_VERIFY(NSMaxX([host(clip) frame])
                  == f.controller.projection().projectEdge(layout::kClipFarRightLu - MacSceneLayout::kRootInset).pt);
    }
    f.layout();
    LOKA_VERIFY(NSEqualSizes([[native documentView] frame].size, documentSizes[clipped]));
  }
  LOKA_VERIFY(NSEqualSizes(documentSizes[0], documentSizes[1]));
}

void testMacClipBoxParkedShowReturns()
{
  Fixture f;
  loka::core::MutableState<bool> visible(true);
  Scene scene((Column() << (Show(visible) << (Box().clipChildren().height(60) << Button("Parked")))).clone());
  LOKA_VERIFY(scene.mount(&f.controller));
  loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);
  f.controller.relayout(300, 200);
  NSView *h = child(f.host.root, [LokaClipHostView class]);
  NSView *button = child(h, [NSButton class]);
  LOKA_VERIFY(h && button && ![h isHidden]);
  visible.set(false);
  scene.flushInvalidation();
  LOKA_VERIFY([h isHidden] && [button superview] == h);
  visible.set(true);
  scene.flushInvalidation();
  f.controller.relayout(300, 200);
  LOKA_VERIFY(![h isHidden] && child(f.host.root, [LokaClipHostView class]) == h && [button superview] == h);
  loka::dsl::testing::SceneTestAccess::unmount(scene);
}

void testMacClipBoxReplaceInvalidatesOldRect()
{
  Fixture f;
  f.tree = box();
  f.at(20, 30, 180, 100);
  LokaClipHostView *h = host(f.tree);
  LOKA_VERIFY(h);
  NSRect before = [h frame];
  [f.host.root setRecorded:NSZeroRect];
  f.at(50, 70, 180, 100);
  LOKA_VERIFY(NSEqualRects([f.host.root recorded], before));
  LOKA_VERIFY(NSEqualPoints([h bounds].origin, NSMakePoint(50, 70)));
}

namespace
{
  IMP originalRemove = 0;
  unsigned removedWithChildren = 0;
  void RecordHostRemoval(id view, SEL selector)
  {
    if ([[(NSView *)view subviews] count])
      ++removedWithChildren;
    typedef void (*Remove)(id, SEL);
    reinterpret_cast<Remove>(originalRemove)(view, selector);
  }
} // namespace
void testMacClipBoxRetiresChildrenBeforeHost()
{
  int released = 0;
  Fixture f;
  f.tree = box();
  f.tree->asNestable()->addChild(new ButtonNode((ButtonProps())));
  f.layout();
  NSView *h = host(f.tree);
  LOKA_VERIFY(h && child(h, [NSButton class]));
  observeMacNativeDeallocation(h, released);
  removedWithChildren = 0;
  originalRemove =
      method_getImplementation(class_getInstanceMethod([LokaClipHostView class], @selector(removeFromSuperview)));
  MacNativeRefusal record(
      @selector(removeFromSuperview), reinterpret_cast<IMP>(&RecordHostRemoval), [LokaClipHostView class]);
  LifecycleFactTestAccess::MarkSubtreeRetired(f.tree);
  f.controller.releaseNodeContexts(f.tree);
  f.controller.onChange(0, NODE_DIRTY_NONE, false);
  LOKA_VERIFY(removedWithChildren == 0 && ![h superview]);
  f.controller.drainNativeRetirements();
  // The enclosing pool may still own AppKit temporaries; fixture drain verifies
  // removal, while the separate context pin below isolates queue reclamation.
}

void testMacClipBoxRetirementQueueAndPark()
{
  int released = 0;
  Fixture f;
  MacClipBoxContext context(&f.controller);
  NSAutoreleasePool *inner = [[NSAutoreleasePool alloc] init];
  LOKA_VERIFY(context.ensureHost(f.host.root, f.controller.projection().projectFrame(Frame(10, 10, 80, 80))));
  NSView *h = (NSView *)context.view();
  observeMacNativeDeallocation(h, released);
  context.onFactChanged(NODE_FACT_ATTACHED, NODE_FACT_DETACHED_RETAINED);
  LOKA_VERIFY([h isHidden] && [h superview] == f.host.root);
  context.onFactChanged(NODE_FACT_DETACHED_RETAINED, NODE_FACT_ATTACHED);
  LOKA_VERIFY(![h isHidden]);
  context.onFactChanged(NODE_FACT_ATTACHED, NODE_FACT_RETIRED);
  LOKA_VERIFY(!context.view() && ![h superview] && released == 0);
  [inner drain];
  LOKA_VERIFY(released == 0);
  inner = [[NSAutoreleasePool alloc] init];
  f.controller.drainNativeRetirements();
  [inner drain];
  LOKA_VERIFY(released == 1);
}

void testMacClipBoxNodeHandlerWall()
{
  Fixture f;
  RefusedNodeHandler boxHandler(NodeTypeToken<BoxNode>()), cardHandler(NodeTypeToken<CardNode>());
  LOKA_VERIFY(!f.controller.registerNodeHandler(&boxHandler));
  LOKA_VERIFY(!f.controller.registerNodeHandler(&cardHandler));
}

void testMacClipBoxSkippedViewportKeepsControlsFocusAndUnits()
{
  loka::core::MutableState<loka::core::String> storage(loka::core::String("selection"));
  NodeState<loka::core::String> text(&storage, 0);
  Fixture f;
  f.tree = box(180, 100);
  ZStackNode *stack = new ZStackNode((ZStackProps()));
  stack->addChild(new BoxNode(BoxProps().setSize(180, 100)));
  stack->addChild(new EditTextNode(EditTextProps(text)));
  f.tree->asNestable()->addChild(stack);
  ProjectionParentScopeStack &scopes = Access::projectionScopes(f.controller);
  ProjectionViewportScopeGuard viewport(scopes, scopes.current());
  LOKA_VERIFY(viewport.isActive());
  f.at(0, 0, 180, 100);
  NSView *h = host(f.tree);
  NSView *unit = child(h, NSClassFromString(@"LokaStackUnitView"));
  NSTextField *field = (NSTextField *)child(unit, [NSTextField class]);
  LOKA_VERIFY(h && unit && field);
  NSResponder *focused = nil;
  if (f.host.show())
  {
    if ([f.host.window makeFirstResponder:field] && [field currentEditor])
      focused = [f.host.window firstResponder];
    else
      std::printf("[skip] clip skipped viewport focus: field editor unavailable\n");
  }
  Access::refuseScrollViewShortRange(f.controller);
  LOKA_VERIFY(f.at(0, 0, 180, 100) == 0);
  LOKA_VERIFY(host(f.tree) == h && [unit superview] == h && [field superview] == unit);
  if (focused)
    LOKA_VERIFY([f.host.window firstResponder] == focused);
}

void testMacClipBoxFailureKeepsSiblingProjection()
{
  Fixture f;
  StackNode *column = new StackNode(StackProps(STACK_AXIS_COLUMN));
  f.tree = column;
  BoxNode *clip = box();
  column->addChild(new ButtonNode(ButtonProps().text("Sibling")));
  column->addChild(clip);
  clip->addChild(new ButtonNode(ButtonProps().text("Withheld")));
  loka::testing::failMacClipHostViews(2);
  f.layout();
  NSView *sibling = child(f.host.root, [NSButton class]);
  LOKA_VERIFY(sibling && !host(clip));
  const NSRect previous = [sibling frame];
  f.layout();
  LOKA_VERIFY(child(f.host.root, [NSButton class]) == sibling && NSEqualRects([sibling frame], previous));
  LOKA_VERIFY(!host(clip) && !clip->childrenHead()->getContext());
  f.layout();
  LOKA_VERIFY(host(clip) && child(host(clip), [NSButton class]));
}

void testMacClipBoxFocusedEditRetirement()
{
  loka::core::MutableState<loka::core::String> storage(loka::core::String("focus"));
  NodeState<loka::core::String> text(&storage, 0);
  Fixture f;
  f.tree = box(180, 100);
  f.tree->asNestable()->addChild(new EditTextNode(EditTextProps(text)));
  f.layout();
  NSView *h = host(f.tree);
  NSTextField *field = (NSTextField *)child(h, [NSTextField class]);
  LOKA_VERIFY(h && field);
  if (!f.host.show())
    return;
  if (![f.host.window makeFirstResponder:field] || ![field currentEditor])
  {
    std::printf("[skip] clip focused retirement: field editor unavailable\n");
    return;
  }
  LifecycleFactTestAccess::MarkSubtreeRetired(f.tree);
  f.controller.releaseNodeContexts(f.tree);
  f.controller.onChange(0, NODE_DIRTY_NONE, false);
  LOKA_VERIFY(![h superview] && ![field superview]);
  f.controller.drainNativeRetirements();
  LOKA_VERIFY([[f.host.root subviews] count] == 0);
}

namespace
{
  /** No registered handler: exercises the controller's existing fallback. */
  class FallbackBox : public BoxNode
  {
  public:
    explicit FallbackBox(bool clip)
        : BoxNode(BoxProps().setSize(100, 40))
    {
      this->props.childClip_ = clip ? layout::CHILD_CLIP_OWN_RECT : layout::CHILD_CLIP_NONE;
    }
    virtual const void *nodeTypeKey() const
    {
      return NodeTypeToken<FallbackBox>();
    }
  };
} // namespace
void testMacClipBoxLargeOfferFixedSizeTwin()
{
  Fixture clipped, flat;
  clipped.tree = new FallbackBox(true);
  flat.tree = new FallbackBox(false);
  clipped.tree->asNestable()->addChild(new ButtonNode((ButtonProps())));
  flat.tree->asNestable()->addChild(new ButtonNode((ButtonProps())));
  clipped.controller.onChange(clipped.tree, NODE_DIRTY_NONE, false);
  flat.controller.onChange(flat.tree, NODE_DIRTY_NONE, false);
  clipped.controller.relayout(40000, 40000);
  flat.controller.relayout(40000, 40000);
  NSView *h = host(clipped.tree);
  NSView *a = child(h, [NSButton class]);
  NSView *b = child(flat.host.root, [NSButton class]);
  LOKA_VERIFY(h && a && b && NSEqualRects([a frame], [b frame]));
  LOKA_VERIFY([h frame].size.width == 100 && [h frame].size.height == 40);
}

void testMacClipBoxChannelRefusalKeepsHostRect()
{
  Fixture f;
  BoxNode *clip = box(180, 0);
  BoxNode *leading = box(100, 40, false);
  clip->addChild(leading);
  clip->addChild(new ButtonNode(ButtonProps().text("Following")));
  f.tree = clip;
  ProjectionParentScopeStack &scopes = Access::projectionScopes(f.controller);
  ProjectionViewportScopeGuard viewport(scopes, scopes.current());
  LOKA_VERIFY(viewport.isActive());
  const int firstBottom = f.at(0, 7, 180, 100);
  LOKA_VERIFY(!scopes.current().hasShortRangeRefusal());
  NSView *h = host(clip);
  NSView *button = child(h, [NSButton class]);
  LOKA_VERIFY(h && button && [button superview] == h);
  const NSRect hostBefore = [h frame];
  const NSRect childBefore = [button frame];
  LOKA_VERIFY(hostBefore.size.height > 40);

  // Twin of MacScrollViewGappedOriginRefuses: grow a leading Box past the
  // short range. Here its bottom already overflows at the nonzero origin.
  leading->props.setSize(100, SHRT_MAX);
  const int refusedBottom = f.at(0, 7, 180, 100);
  LOKA_VERIFY(scopes.current().hasShortRangeRefusal());
  LOKA_VERIFY(refusedBottom < firstBottom);
  LOKA_VERIFY(host(clip) == h && NSEqualRects([h frame], hostBefore));
  LOKA_VERIFY([button superview] == h && NSEqualRects([button frame], childBefore));

  // A host created on a refused visit is not placed on that visit, so its
  // creation alone must align the bounds origin with the frame origin.
  Fixture g;
  BoxNode *fresh = box(180, 0);
  BoxNode *tall = box(100, SHRT_MAX, false);
  fresh->addChild(tall);
  g.tree = fresh;
  ProjectionParentScopeStack &freshScopes = Access::projectionScopes(g.controller);
  ProjectionViewportScopeGuard freshViewport(freshScopes, freshScopes.current());
  LOKA_VERIFY(freshViewport.isActive());
  g.at(0, 7, 180, 100);
  LOKA_VERIFY(freshScopes.current().hasShortRangeRefusal());
  NSView *fh = host(fresh);
  LOKA_VERIFY(fh && [fh frame].origin.y > 0 && NSEqualPoints([fh bounds].origin, [fh frame].origin));
}

// Runtime declarations preserve the 10.6 SDK build.
@interface NSWindow (LokaClipTestScreenConversion)
- (NSRect)convertRectToScreen:(NSRect)rect;
@end
namespace loka
{
  namespace testing
  {
    NSBitmapImageRep *macStackPhoto(NSView *view);
  }
} // namespace loka

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
namespace
{
  NSEvent *clipMouse(NSWindow *window, NSEventType type, NSPoint point, NSInteger number)
  {
    mach_timebase_info_data_t scale;
    LOKA_VERIFY(mach_timebase_info(&scale) == KERN_SUCCESS);
    const NSTimeInterval timestamp = mach_absolute_time() * (double)scale.numer / scale.denom / 1.0e9;
    return [NSEvent mouseEventWithType:type
                              location:point
                         modifierFlags:0
                             timestamp:timestamp
                          windowNumber:[window windowNumber]
                               context:nil
                           eventNumber:number
                            clickCount:1
                              pressure:type == NSLeftMouseUp ? 0 : 1];
  }
} // namespace

/** Owns the synchronous test gesture's callbacks; timers never outlive sendDown. */
@interface LokaClipTestGesture : NSObject
{
  BOOL timedOut_;
}
- (BOOL)sendDown:(NSEvent *)down window:(NSWindow *)window following:(NSArray *)events;
- (void)feed:(NSTimer *)timer;
- (void)expire:(NSTimer *)timer;
@end
@implementation LokaClipTestGesture
- (void)feed:(NSTimer *)timer
{
  NSMutableArray *events = [timer userInfo];
  if ([events count])
  {
    NSEvent *event = [events objectAtIndex:0];
    [NSApp postEvent:clipMouse([event window], [event type], [event locationInWindow], [event eventNumber])
             atStart:YES];
    [events removeObjectAtIndex:0];
  }
  if (![events count])
    [timer invalidate];
}
- (void)expire:(NSTimer *)timer
{
  timedOut_ = YES;
  NSEvent *event = [timer userInfo];
  [NSApp postEvent:clipMouse([event window], NSLeftMouseUp, [event locationInWindow], [event eventNumber]) atStart:YES];
}
- (BOOL)sendDown:(NSEvent *)down window:(NSWindow *)window following:(NSArray *)events
{
  timedOut_ = NO;
  LOKA_VERIFY([events count] && [(NSEvent *)[events lastObject] type] == NSLeftMouseUp);
  NSMutableArray *pending = [events mutableCopy];
  NSTimer *feed = [NSTimer timerWithTimeInterval:0.03
                                          target:self
                                        selector:@selector(feed:)
                                        userInfo:pending
                                         repeats:YES];
  NSTimer *watchdog = [NSTimer timerWithTimeInterval:2.0
                                              target:self
                                            selector:@selector(expire:)
                                            userInfo:[events lastObject]
                                             repeats:NO];
  [pending release];
  NSRunLoop *loop = [NSRunLoop currentRunLoop];
  [loop addTimer:feed forMode:NSEventTrackingRunLoopMode];
  [loop addTimer:feed forMode:NSDefaultRunLoopMode];
  [loop addTimer:watchdog forMode:NSEventTrackingRunLoopMode];
  [loop addTimer:watchdog forMode:NSDefaultRunLoopMode];
  // Enter tracking before feeding any continuation. No key-window pump may
  // consume gesture events. Even a swallowed down cancels both timers here.
  [window sendEvent:down];
  [feed invalidate];
  [watchdog invalidate];
  return !timedOut_;
}
@end

@interface LokaClipTestClickTarget : NSObject
{
  unsigned count_;
}
- (void)clicked:(id)sender;
- (unsigned)count;
@end
@implementation LokaClipTestClickTarget
- (void)clicked:(id)sender
{
  (void)sender;
  ++count_;
}
- (unsigned)count
{
  return count_;
}
@end
/** A push button that takes the classic mouseDown: path. On macOS 26 a stock
    NSButton sends no action for events posted from this process (both twins
    counted 0); this subclass sends it on 10.9 and 26 alike. */
@interface LokaClipTestButton : NSButton
@end
@implementation LokaClipTestButton
- (void)mouseDown:(NSEvent *)event
{
  [super mouseDown:event];
}
@end

// Runtime acceptance pins: all event traffic stays in this process.
namespace
{
  void settleClipWindow(Host &h)
  {
    for (int i = 0; i != 4; ++i)
    {
      [h.window display];
      [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
    }
  }
  NSBitmapImageRep *clipWindowImage(Host &h)
  {
    settleClipWindow(h);
    const char *reason = 0;
    NSBitmapImageRep *rep = loka::testing::ReadMacWindowServer(h.root, reason);
    if (!rep)
      std::printf("[skip] clip window capture: %s\n", reason);
    return [rep autorelease];
  }
  NSView *descendant(NSView *parent, Class type)
  {
    if ([parent isKindOfClass:type])
      return parent;
    NSArray *views = [parent subviews];
    for (NSUInteger i = 0; i < [views count]; ++i)
      if (NSView *found = descendant([views objectAtIndex:i], type))
        return found;
    return nil;
  }
  bool keyClipWindow(Host &h)
  {
    if ([NSApp respondsToSelector:@selector(setActivationPolicy:)])
      [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    [NSApp activateIgnoringOtherApps:YES];
    [h.window makeKeyAndOrderFront:nil];
    NSDate *limit = [NSDate dateWithTimeIntervalSinceNow:2.0];
    while (![h.window isKeyWindow] && [limit timeIntervalSinceNow] > 0)
    {
      NSEvent *event = [NSApp nextEventMatchingMask:NSAnyEventMask
                                          untilDate:[NSDate dateWithTimeIntervalSinceNow:0.05]
                                             inMode:NSDefaultRunLoopMode
                                            dequeue:YES];
      if (event)
        [NSApp sendEvent:event];
    }
    if (![h.window isKeyWindow])
      std::printf("[skip] clip input/focus: window could not become key\n");
    return [h.window isKeyWindow] != NO;
  }
  unsigned long long clipResidentBytes()
  {
#ifdef MACH_TASK_BASIC_INFO
    mach_task_basic_info_data_t info;
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    LOKA_VERIFY(task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info), &count)
                == KERN_SUCCESS);
#else
    task_basic_info_data_t info;
    mach_msg_type_number_t count = TASK_BASIC_INFO_COUNT;
    LOKA_VERIFY(task_info(mach_task_self(), TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info), &count)
                == KERN_SUCCESS);
#endif
    return info.resident_size;
  }
} // namespace

void testMacClipBoxClickReachesSiblingBelow()
{
  // Expected reds: mutation-c-hit-self and mutation-m-host-swallows-children.
  unsigned clicks[2];
  for (int clipped = 0; clipped != 2; ++clipped)
  {
    Fixture f;
    if (!f.host.show() || !keyClipWindow(f.host))
      return;
    f.tree = box(0, 80, clipped != 0);
    f.at(40, 40, 0, 80);
    NSView *h = host(f.tree);
    LOKA_VERIFY(clipped ? h != nil : h == nil);
    NSView *parent = clipped ? [h superview] : f.host.root;
    LOKA_VERIFY(parent == f.host.root);
    NSButton *button = [[[LokaClipTestButton alloc] initWithFrame:NSMakeRect(80, 55, 100, 28)] autorelease];
    LokaClipTestClickTarget *target = [[[LokaClipTestClickTarget alloc] init] autorelease];
    [button setTitle:@"Sibling"];
    [button setTarget:target];
    [button setAction:@selector(clicked:)];
    // Reproduce a late-created, unresolved-width host overlapping an earlier
    // sibling. The Box has no child that could receive this click.
    if (clipped)
      [parent addSubview:button positioned:NSWindowBelow relativeTo:h];
    else
      [parent addSubview:button];
    const NSPoint local = NSMakePoint(NSMidX([button frame]), NSMidY([button frame]));
    if (clipped)
      LOKA_VERIFY(NSPointInRect(local, [h frame]));
    settleClipWindow(f.host);
    const NSPoint point = [parent convertPoint:local toView:nil];
    // The window's hit decides the target. AppKit 10.9 forwards a mouseDown
    // the host does not handle to the control below, so the count alone
    // cannot tell a swallowing host from a transparent one there.
    LOKA_VERIFY([[f.host.root superview] hitTest:point] == button);
    // Like MacFocusTests, send down through the window; native tracking
    // receives up from the application queue, with a watchdog as a backstop.
    LokaClipTestGesture *gesture = [[[LokaClipTestGesture alloc] init] autorelease];
    LOKA_VERIFY([gesture sendDown:clipMouse(f.host.window, NSLeftMouseDown, point, 0)
                           window:f.host.window
                        following:[NSArray arrayWithObject:clipMouse(f.host.window, NSLeftMouseUp, point, 1)]]);
    clicks[clipped] = [target count];
    [button setTarget:nil];
    [button removeFromSuperview];
  }
  std::printf("[pin] clip sibling clicks: unflagged=%u flagged=%u (expected 1 each)\n", clicks[0], clicks[1]);
  LOKA_VERIFY(clicks[0] == 1 && clicks[1] == 1);
}

void testMacClipBoxDragTracksLikeTwin()
{
  NSRange selections[2];
  for (int clipped = 0; clipped != 2; ++clipped)
  {
    loka::app::testing::TextEditorStateOwner owner;
    loka::testing::TextBufferFixture lines;
    LOKA_VERIFY(lines.attach(&owner.tracker, 4) == loka::core::ATTACH_OK);
    LOKA_VERIFY(lines.replaceWithOne(loka::core::String("abcdefghijklmnopqrstuvwxyz")) == loka::core::EDIT_OK);
    Fixture f;
    if (!f.host.show() || !keyClipWindow(f.host))
      return;
    f.tree = box(220, 120, clipped != 0);
    f.tree->asNestable()->addChild(new TextEditorNode(TextEditorProps(lines, owner.cursor)));
    f.at(30, 30, 220, 120);
    NSTextView *editor = (NSTextView *)descendant(f.host.root, [NSTextView class]);
    LOKA_VERIFY(editor);
    settleClipWindow(f.host);
    [f.host.window makeFirstResponder:nil];
    [editor setSelectedRange:NSMakeRange(0, 0)];
    NSPoint start = [editor convertPoint:NSMakePoint(12, 10) toView:nil];
    // First click must resolve to the editor before it has focus.
    LOKA_VERIFY([f.host.root hitTest:start] == editor);
    NSMutableArray *events = [NSMutableArray array];
    for (int i = 1; i != 6; ++i)
    {
      const NSEventType type = i == 5 ? NSLeftMouseUp : NSLeftMouseDragged;
      [events addObject:clipMouse(f.host.window, type, NSMakePoint(start.x + i * 16, start.y), i)];
    }
    LokaClipTestGesture *gesture = [[[LokaClipTestGesture alloc] init] autorelease];
    LOKA_VERIFY([gesture sendDown:clipMouse(f.host.window, NSLeftMouseDown, start, 0)
                           window:f.host.window
                        following:events]);
    selections[clipped] = [editor selectedRange];
    LOKA_VERIFY(selections[clipped].length > 0);
    LOKA_VERIFY([f.host.window firstResponder] == editor);
  }
  LOKA_VERIFY(NSEqualRanges(selections[0], selections[1]));
  // Expected red: mutation-m-host-swallows-children; c preserves child hits.
}

void testMacClipBoxPopupAnchorMatchesTwin()
{
  // Expected red: mutation-b-no-bounds-origin.patch.
  NSRect windowRects[2], screenRects[2];
  for (int clipped = 0; clipped != 2; ++clipped)
  {
    Fixture f;
    if (!f.host.show())
      return;
    const char *items[] = {"One", "Two"};
    f.tree = box(180, 80, clipped != 0);
    f.tree->asNestable()->addChild(new PopupMenuNode(PopupMenuProps().items(items, 2)));
    f.at(60, 50, 180, 80);
    NSPopUpButton *popup = (NSPopUpButton *)descendant(f.host.root, [NSPopUpButton class]);
    LOKA_VERIFY(popup);
    windowRects[clipped] = [popup convertRect:[popup bounds] toView:nil];
    if ([f.host.window respondsToSelector:@selector(convertRectToScreen:)])
      screenRects[clipped] = [f.host.window convertRectToScreen:windowRects[clipped]];
    else
    {
      screenRects[clipped] = windowRects[clipped];
      screenRects[clipped].origin = [f.host.window convertBaseToScreen:windowRects[clipped].origin];
    }
  }
  LOKA_VERIFY(NSEqualRects(windowRects[0], windowRects[1]));
  LOKA_VERIFY(NSEqualRects(screenRects[0], screenRects[1]));
}

void testMacClipBoxUnresolvedWidthLayerBackedRssIsFlat()
{
  // Expected red: mutation-l-host-draws.patch (contents or RSS).
  Fixture f;
  if (!f.host.show())
    return;
  if (NSAppKitVersionNumber < 1187.0 || ![f.host.root respondsToSelector:@selector(setWantsLayer:)])
  {
    std::printf("[skip] clip RSS: requires AppKit 10.8 wantsUpdateLayer path\n");
    return;
  }
  [f.host.root setWantsLayer:YES];
  BoxNode *clip = box(0, 60);
  f.tree = clip;
  unsigned long long baseline = 0;
  for (int visit = 0; visit != 500; ++visit)
  {
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
    clip->props.setSize(0, visit % 2 ? 70 : 60);
    f.at(visit % 2 ? 50 : 60, visit % 2 ? 40 : 50, 0, 100);
    [f.host.window display];
    [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.002]];
    NSView *h = host(clip);
    LOKA_VERIFY(h && [h layer]);
    LOKA_VERIFY([[h layer] valueForKey:@"contents"] == nil);
    [pool drain];
    if (visit == 49)
      baseline = clipResidentBytes();
  }
  const unsigned long long finalSize = clipResidentBytes();
  std::printf("[pin] clip RSS after 50: %llu bytes; after 500: %llu bytes\n", baseline, finalSize);
  LOKA_VERIFY(finalSize < baseline + 8ULL * 1024 * 1024);
}

namespace
{
  double clipRedExcess(NSBitmapImageRep *rep, NSView *space, CGFloat x, CGFloat y)
  {
    NSColor *color = [[rep colorAtX:static_cast<NSInteger>(x * [rep pixelsWide] / NSWidth([space bounds]))
                                  y:static_cast<NSInteger>(y * [rep pixelsHigh] / NSHeight([space bounds]))]
        colorUsingColorSpaceName:NSDeviceRGBColorSpace];
    LOKA_VERIFY(color);
    return [color redComponent] - [color blueComponent];
  }
} // namespace
void testMacClipBoxGroundCapturesInBothBlurPaths()
{
  // Expected red: mutation-z-no-routing.patch; ink leaks to the outside sample.
  const bool effect = NSClassFromString(@"NSVisualEffectView") != nil;
  std::printf(effect ? "[skip] clip legacy blur: NSVisualEffectView selects effect path; run on Mavericks\n"
                     : "[skip] clip effect blur: NSVisualEffectView unavailable; running legacy capture\n");
  if (!effect)
  {
    id transparency = [[[NSUserDefaults standardUserDefaults] persistentDomainForName:NSGlobalDomain]
        objectForKey:@"AppleEnableMenuBarTransparency"];
    if (transparency && [transparency respondsToSelector:@selector(boolValue)] && ![transparency boolValue])
    {
      std::printf("[skip] clip legacy blur: menu-bar transparency disabled\n");
      return;
    }
  }
  double outside[2];
  for (int clipped = 0; clipped != 2; ++clipped)
  {
    Fixture f;
    if (!f.host.show() || !keyClipWindow(f.host))
      return;
    ZStackNode *stack = new ZStackNode((ZStackProps()));
    stack->props.unitGround = ZSTACK_UNIT_GROUND_EXP_BLUR;
    f.tree = stack;
    BoxNode *ground = box(100, 100, clipped != 0);
    stack->addChild(ground);
    stack->addChild(box(260, 180, false));
    f.at(60, 50, 260, 180);
    NSView *unit = child(f.host.root, NSClassFromString(@"LokaStackUnitView"));
    LOKA_VERIFY(unit);
    NSView *groundParent = host(ground) ? host(ground) : f.host.root;
    NSView *paint = ink(groundParent, NSMakeRect(60, 50, 230, 100));
    if (groundParent == f.host.root)
      [f.host.root addSubview:paint positioned:NSWindowBelow relativeTo:unit];
    [unit setFrame:[f.host.root bounds]];
    NSBitmapImageRep *rep = nil;
    if (effect)
    {
      NSView *effectView = child(unit, NSClassFromString(@"NSVisualEffectView"));
      LOKA_VERIFY(effectView && NSEqualRects([effectView frame], [unit bounds]));
      rep = clipWindowImage(f.host);
      if (!rep)
        return;
    }
    else
    {
      // Mirror fullPhotoDraw: the legacy source is captured only on screen.
      [f.host.root setNeedsDisplay:YES];
      [f.host.window display];
      settleClipWindow(f.host);
      rep = loka::testing::macStackPhoto(unit);
      LOKA_VERIFY(rep);
    }
    const double inside = clipRedExcess(rep, f.host.root, 110, 100);
    outside[clipped] = clipRedExcess(rep, f.host.root, 230, 100);
    // Samples are > 40 points from the cut, beyond the legacy blur radius.
    LOKA_VERIFY(inside > 0.02);
    if (clipped)
    {
      LOKA_VERIFY(outside[clipped] < 0.02);
      LOKA_VERIFY(inside > outside[clipped] + 0.02);
    }
    else
      LOKA_VERIFY(outside[clipped] > 0.02);
  }
  LOKA_VERIFY(outside[0] > outside[1] + 0.02);
}
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
