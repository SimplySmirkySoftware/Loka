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
