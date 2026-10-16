#include "MacScrollViewTests.hpp"
#include "app/layout/FallbackControlMetrics.hpp"

#include "MacSceneLayout.hpp"
#include "MacObjCCompat.hpp"
#include "support/TestVerify.hpp"

#include <AppKit/AppKit.h>
#include <cassert>
#include <cstdio>

#include "MacScenePlatformController.hpp"
#include "app/nodes/controls/Button.hpp"
#include "app/nodes/ImageView.hpp"
#include "app/RectSurface.hpp"
#include "context/MacScrollViewContext.hpp"
#include "app/nodes/nestable/RowColumn.hpp"
#include "app/nodes/nestable/ScrollView.hpp"
#include "app/scene/state/NodeState.hpp"
#include "core/State.hpp"
#include "core/StateTracker.hpp"

namespace
{
  class ViewportFact
  {
  public:
    explicit ViewportFact(int initial)
        : storage_(loka::core::Frame(0, initial, 0, 0)),
          tracker_(),
          state_(&this->storage_, &this->tracker_),
          changeCount_(0)
    {
      this->tracker_.addState(&this->storage_);
      this->storage_.bind(&ViewportFact::ChangedThunk, this, false);
    }

    ~ViewportFact()
    {
      this->storage_.unbind(&ViewportFact::ChangedThunk, this);
    }

    loka::app::scene::NodeState<loka::core::Frame> &state()
    {
      return this->state_;
    }

    int changeCount() const
    {
      return this->changeCount_;
    }

    void resetChangeCount()
    {
      this->changeCount_ = 0;
    }

  private:
    static void ChangedThunk(void *userData)
    {
      ViewportFact *self = static_cast<ViewportFact *>(userData);
      if (self)
      {
        ++self->changeCount_;
      }
    }

    loka::core::MutableState<loka::core::Frame> storage_;
    loka::core::PushStateTracker tracker_;
    loka::app::scene::NodeState<loka::core::Frame> state_;
    int changeCount_;

    ViewportFact(const ViewportFact &);
    ViewportFact &operator=(const ViewportFact &);
  };

  void CountClickThunk(void *userData)
  {
    ++*static_cast<int *>(userData);
  }

  loka::app::StackNode *
  addButtonColumn(loka::app::ScrollViewNode &scrollView, int buttonCount, loka::core::EmitterState *firstOnClick = 0)
  {
    loka::app::StackNode *column =
        new loka::app::StackNode((loka::app::StackProps(loka::app::STACK_AXIS_COLUMN)));
    for (int i = 0; i < buttonCount; ++i)
    {
      loka::app::ButtonProps props;
      if (i == 0 && firstOnClick)
      {
        props.onClick(firstOnClick);
      }
      column->addChild(new loka::app::ButtonNode(props));
    }
    scrollView.addChild(column);
    return column;
  }

  void establishLayout(MacScenePlatformController &controller, loka::app::scene::Node *root, int width, int height)
  {
    controller.onChange(root, loka::app::scene::NODE_DIRTY_NONE, false);
    controller.relayout(width, height);
  }

  NSScrollView *scrollViewUnderRoot(NSView *root)
  {
    NSArray *children = [root subviews];
    for (NSUInteger i = 0; i < [children count]; ++i)
    {
      NSView *child = [children objectAtIndex:i];
      if ([child isKindOfClass:[NSScrollView class]])
      {
        return (NSScrollView *)child;
      }
    }
    return nil;
  }

  NSArray *buttonsInDocument(NSScrollView *scrollView)
  {
    return scrollView ? [[scrollView documentView] subviews] : nil;
  }

  bool frameEquals(NSView *view, CGFloat x, CGFloat y, CGFloat width, CGFloat height)
  {
    const NSRect frame = [view frame];
    return frame.origin.x == x && frame.origin.y == y && frame.size.width == width && frame.size.height == height;
  }

  int clipOffset(NSScrollView *scrollView)
  {
    return static_cast<int>([[scrollView contentView] bounds].origin.y);
  }
} // namespace

void testMacScrollViewParentsChildrenToFlippedDocumentView()
{
  std::printf("\n==== [testMacScrollViewParentsChildrenToFlippedDocumentView] start ====\n");
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  [NSApplication sharedApplication];
  NSView *root = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 320, 240)];
  LOKA_VERIFY(root != nil);
  {
    MacScenePlatformController controller((void *)root, loka::app::RailMetrics());
    // Nodes are declared after the controller so they leave scope first:
    // ~Node remains the terminal retire door while the native queue owner is
    // alive. Releasing rootNode_ below also prevents clearNodeContexts from
    // replacing a live context without terminal fact delivery.
    ViewportFact offset(10);
    loka::app::ScrollViewNode scrollView((loka::app::ScrollViewProps(offset.state())));
    addButtonColumn(scrollView, 6);
    establishLayout(controller, &scrollView, 300, 140);

    NSScrollView *nativeScrollView = scrollViewUnderRoot(root);
    LOKA_VERIFY(nativeScrollView != nil);
    assert([nativeScrollView superview] == root);
    assert([nativeScrollView hasVerticalScroller]);
    assert(![nativeScrollView autohidesScrollers]);
    LOKA_VERIFY(frameEquals(nativeScrollView,
                            MacSceneLayout::kRootInset,
                            MacSceneLayout::kRootInset,
                            300 - 2 * MacSceneLayout::kRootInset,
                            140 - 2 * MacSceneLayout::kRootInset));

    NSView *documentView = [nativeScrollView documentView];
    LOKA_VERIFY(documentView != nil);
    assert([documentView isFlipped]);
    NSArray *buttons = buttonsInDocument(nativeScrollView);
    assert([buttons count] == 6);
    for (NSUInteger i = 0; i < [buttons count]; ++i)
    {
      assert([[buttons objectAtIndex:i] superview] == documentView);
    }
    const int buttonHeight = controller.rowLayoutMetrics().buttonHeight;
    const int advance = buttonHeight + MacSceneLayout::kGap;
    LOKA_VERIFY(frameEquals([buttons objectAtIndex:0], 0, 0, [nativeScrollView contentSize].width, buttonHeight));
    assert([(NSView *)[buttons objectAtIndex:1] frame].origin.y == advance);
    assert([documentView frame].size.height == 6 * advance - MacSceneLayout::kGap);
    assert(clipOffset(nativeScrollView) == 10);

    controller.onChange(0, loka::app::scene::NODE_DIRTY_NONE, false);
  }
  LOKA_VERIFY([[root subviews] count] == 0);
  [root release];
  [pool drain];
  std::printf("==== [testMacScrollViewParentsChildrenToFlippedDocumentView] PASSED ====\n");
}

void testMacScrollViewOffsetIsRelayoutInput()
{
  std::printf("\n==== [testMacScrollViewOffsetIsRelayoutInput] start ====\n");
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  [NSApplication sharedApplication];
  NSView *root = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 320, 240)];
  LOKA_VERIFY(root != nil);
  {
    MacScenePlatformController controller((void *)root, loka::app::RailMetrics());
    ViewportFact offset(0);
    loka::app::ScrollViewNode scrollView((loka::app::ScrollViewProps(offset.state())));
    addButtonColumn(scrollView, 6);
    establishLayout(controller, &scrollView, 300, 140);
    NSScrollView *nativeScrollView = scrollViewUnderRoot(root);
    LOKA_VERIFY(nativeScrollView != nil);
    NSView *documentView = [nativeScrollView documentView];
    NSArray *buttons = buttonsInDocument(nativeScrollView);
    assert([buttons count] == 6);
    const NSRect buttonAtZero = [[buttons objectAtIndex:0] frame];

    offset.state().set(loka::core::Frame(0, 7, 0, 0));
    controller.relayout(300, 140);
    assert(clipOffset(nativeScrollView) == 7);
    assert(NSEqualRects([[buttons objectAtIndex:0] frame], buttonAtZero)
           && "AppKit scrolls the document; child content coordinates stay fixed");
    const NSRect settledScrollFrame = [nativeScrollView frame];
    const NSRect settledDocumentFrame = [documentView frame];
    const NSRect settledButtonFrame = [[buttons objectAtIndex:0] frame];

    controller.relayout(300, 140);
    assert(clipOffset(nativeScrollView) == 7);
    assert(NSEqualRects([nativeScrollView frame], settledScrollFrame));
    assert(NSEqualRects([documentView frame], settledDocumentFrame));
    assert(NSEqualRects([[buttons objectAtIndex:0] frame], settledButtonFrame));

    controller.onChange(0, loka::app::scene::NODE_DIRTY_NONE, false);
  }
  LOKA_VERIFY([[root subviews] count] == 0);
  [root release];
  [pool drain];
  std::printf("==== [testMacScrollViewOffsetIsRelayoutInput] PASSED ====\n");
}

void testMacScrollViewBoundsObservationPublishesOffsetFact()
{
  std::printf("\n==== [testMacScrollViewBoundsObservationPublishesOffsetFact] start ====\n");
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  [NSApplication sharedApplication];
  NSView *root = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 320, 240)];
  LOKA_VERIFY(root != nil);
  {
    MacScenePlatformController controller((void *)root, loka::app::RailMetrics());
    ViewportFact offset(0);
    loka::app::ScrollViewNode scrollView((loka::app::ScrollViewProps(offset.state())));
    addButtonColumn(scrollView, 6);
    establishLayout(controller, &scrollView, 300, 140);
    NSScrollView *nativeScrollView = scrollViewUnderRoot(root);
    LOKA_VERIFY(nativeScrollView != nil);
    NSClipView *clipView = [nativeScrollView contentView];

    offset.resetChangeCount();
    [clipView setBoundsOrigin:NSMakePoint(0, 12)];
    [[NSNotificationCenter defaultCenter] postNotificationName:NSViewBoundsDidChangeNotification object:clipView];
    assert(offset.state().get().y == 12 && "clip-view bounds observation must publish through the NodeState door");
    assert(offset.changeCount() == 1 && "an explicit duplicate notification must remain a same-value no-op");

    offset.state().set(loka::core::Frame(0, 13, 0, 0));
    offset.resetChangeCount();
    controller.relayout(300, 140);
    assert(clipOffset(nativeScrollView) == 13);
    assert(offset.changeCount() == 0 && "the layout-driven scrollPoint echo must not republish the same fact");

    controller.onChange(0, loka::app::scene::NODE_DIRTY_NONE, false);
  }
  LOKA_VERIFY([[root subviews] count] == 0);
  [root release];
  [pool drain];
  std::printf("==== [testMacScrollViewBoundsObservationPublishesOffsetFact] PASSED ====\n");
}

void testMacScrollViewResizeReclampsOffsetOnce()
{
  std::printf("\n==== [testMacScrollViewResizeReclampsOffsetOnce] start ====\n");
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  [NSApplication sharedApplication];
  NSView *root = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 320, 240)];
  LOKA_VERIFY(root != nil);
  {
    MacScenePlatformController controller((void *)root, loka::app::RailMetrics());
    const int buttonHeight = controller.rowLayoutMetrics().buttonHeight;
    const int contentHeight = 6 * buttonHeight + 5 * MacSceneLayout::kGap;
    const int initialMax = contentHeight - (140 - 2 * MacSceneLayout::kRootInset);
    const int resizedMax = contentHeight - (240 - 2 * MacSceneLayout::kRootInset);
    ViewportFact offset(100);
    loka::app::ScrollViewNode scrollView((loka::app::ScrollViewProps(offset.state())));
    addButtonColumn(scrollView, 6);
    establishLayout(controller, &scrollView, 300, 140);
    NSScrollView *nativeScrollView = scrollViewUnderRoot(root);
    LOKA_VERIFY(nativeScrollView != nil);
    assert(offset.state().get().y == (initialMax <= 0 ? 0 : initialMax < 100 ? initialMax : 100));

    offset.resetChangeCount();
    controller.relayout(300, 240);
    assert(offset.state().get().y == (resizedMax > 0 ? resizedMax : 0));
    assert(offset.changeCount() == 1 && "a range-changing resize must publish one clamped fact");
    assert(clipOffset(nativeScrollView) == (resizedMax > 0 ? resizedMax : 0));
    controller.relayout(300, 240);
    assert(offset.changeCount() == 1);

    offset.state().set(loka::core::Frame(0, 20, 0, 0));
    offset.resetChangeCount();
    controller.relayout(300, 100);
    assert(offset.state().get().y == 20);
    assert(offset.changeCount() == 0 && "a still-valid offset must survive resize without a fact write");

    offset.state().set(loka::core::Frame(0, 40000, 0, 0));
    offset.resetChangeCount();
    controller.relayout(300, 100);
    assert(offset.state().get().y == contentHeight - (100 - 2 * MacSceneLayout::kRootInset)
           && "an oversized offset must recover to the exact content maximum");
    assert(offset.changeCount() == 1);
    controller.relayout(300, 100);
    assert(offset.changeCount() == 1);

    const CGFloat widthWhileOverflowing = [nativeScrollView contentSize].width;
    controller.relayout(300, 400);
    assert([nativeScrollView contentSize].width == widthWhileOverflowing
           && "the non-autohiding vertical scroller must keep content width stable");

    controller.onChange(0, loka::app::scene::NODE_DIRTY_NONE, false);
  }
  LOKA_VERIFY([[root subviews] count] == 0);
  [root release];
  [pool drain];
  std::printf("==== [testMacScrollViewResizeReclampsOffsetOnce] PASSED ====\n");
}

void testMacNestedScrollViewRefusesWithoutDisturbingOuterScope()
{
  std::printf("\n==== [testMacNestedScrollViewRefusesWithoutDisturbingOuterScope] start ====\n");
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  [NSApplication sharedApplication];
  NSView *root = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 320, 240)];
  LOKA_VERIFY(root != nil);
  {
    MacScenePlatformController controller((void *)root, loka::app::RailMetrics());
    ViewportFact outerOffset(5);
    ViewportFact innerOffset(3);
    loka::app::ScrollViewNode outer((loka::app::ScrollViewProps(outerOffset.state())));
    loka::app::ScrollViewNode *inner = new loka::app::ScrollViewNode((loka::app::ScrollViewProps(innerOffset.state())));
    loka::app::StackNode *innerColumn = addButtonColumn(*inner, 1);
    loka::app::ButtonNode *innerButton = innerColumn->childrenHead()->asButtonNode();
    outer.addChild(inner);
    addButtonColumn(outer, 6);
    establishLayout(controller, &outer, 300, 140);

    NSScrollView *nativeScrollView = scrollViewUnderRoot(root);
    LOKA_VERIFY(nativeScrollView != nil);
    assert([[root subviews] count] == 1 && "a nested ScrollView must refuse before materializing NSScrollView");
    LOKA_VERIFY(inner->getContext() == 0);
    LOKA_VERIFY(innerButton && innerButton->getContext() == 0);
    assert([buttonsInDocument(nativeScrollView) count] == 6);

    NSClipView *clipView = [nativeScrollView contentView];
    [clipView setBoundsOrigin:NSMakePoint(0, 6)];
    [[NSNotificationCenter defaultCenter] postNotificationName:NSViewBoundsDidChangeNotification object:clipView];
    assert(outerOffset.state().get().y == 6);
    assert(innerOffset.state().get().y == 3);
    controller.relayout(300, 140);
    assert(clipOffset(nativeScrollView) == 6 && "the outer scope must remain live after the inner refusal");

    controller.onChange(0, loka::app::scene::NODE_DIRTY_NONE, false);
  }
  LOKA_VERIFY([[root subviews] count] == 0);
  [root release];
  [pool drain];
  std::printf("==== [testMacNestedScrollViewRefusesWithoutDisturbingOuterScope] PASSED ====\n");
}

void testMacScrollViewButtonClickReachesLokaHandler()
{
  std::printf("\n==== [testMacScrollViewButtonClickReachesLokaHandler] start ====\n");
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  [NSApplication sharedApplication];
  NSView *root = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 320, 240)];
  LOKA_VERIFY(root != nil);
  {
    MacScenePlatformController controller((void *)root, loka::app::RailMetrics());
    loka::core::EmitterState firstClick;
    ViewportFact offset(0);
    loka::app::ScrollViewNode scrollView((loka::app::ScrollViewProps(offset.state())));
    addButtonColumn(scrollView, 2, &firstClick);
    establishLayout(controller, &scrollView, 300, 140);
    NSScrollView *nativeScrollView = scrollViewUnderRoot(root);
    LOKA_VERIFY(nativeScrollView != nil);
    NSArray *buttons = buttonsInDocument(nativeScrollView);
    assert([buttons count] == 2);

    int clicks = 0;
    firstClick.bind(&CountClickThunk, &clicks, false);
    NSButton *button = (NSButton *)[buttons objectAtIndex:0];
    [button performClick:nil];
    LOKA_VERIFY(clicks == 1 && "a click inside the document view must reach the Loka handler");

    controller.onChange(0, loka::app::scene::NODE_DIRTY_NONE, false);
  }
  LOKA_VERIFY([[root subviews] count] == 0);
  [root release];
  [pool drain];
  std::printf("==== [testMacScrollViewButtonClickReachesLokaHandler] PASSED ====\n");
}

#include "app/nodes/Text.hpp"
#include "app/nodes/nestable/Box.hpp"
#include "app/nodes/nestable/Fragment.hpp"
#include "app/ConfirmDialog.hpp"
#include "testing/scene/SceneTestFlow.hpp"
#include <climits>
// Keep Show after RequestSettlement and all other includes (#1359).
#include "app/nodes/nestable/Show.hpp"

void testMacScrollViewContainerGapsAndNoSeats()
{
  using namespace loka::app;
  using namespace loka::app::scene;
  using namespace loka::dsl;
  using namespace loka::dsl::testing;
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  [NSApplication sharedApplication];
  NSView *root = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 300, 160)];
  {
    MacScenePlatformController controller(root, RailMetrics());
    ViewportFact offset(0);
    loka::core::MutableState<bool> hidden(false);
    // No MacWindow owns this plain NSView: the dialog has no transport.
    loka::core::MutableState<ConfirmResult> resultStorage;
    NodeState<ConfirmResult> result(&resultStorage, 0);
    NodeDefinitionBase *definition = (ScrollView(offset.state()).testId("gapScroll")
                                      << Text("First") << Fragment() << (Show(hidden) << Text("Hidden"))
                                      << ConfirmDialog().result(result) << Text("Second") << Text("Third"))
                                         .clone();
    LOKA_VERIFY(definition != 0);
    Scene scene(definition);
    LOKA_VERIFY(scene.mount(&controller));
    SceneTestAccess::updateAttached(scene, true);
    const int visible = 30;
    controller.relayout(300, visible + 2 * MacSceneLayout::kRootInset);
    NSScrollView *viewport = scrollViewUnderRoot(root);
    LOKA_VERIFY(viewport != nil);
    NSArray *children = [[viewport documentView] subviews];
    LOKA_VERIFY([children count] == 3);
    int bottom = 0;
    int totalHeight = 0;
    for (NSUInteger i = 0; i < 3; ++i)
    {
      const NSRect frame = [[children objectAtIndex:i] frame];
      LOKA_VERIFY(NSMinY(frame) == bottom + (i > 0 ? MacSceneLayout::kGap : 0));
      totalHeight += static_cast<int>(frame.size.height);
      bottom = static_cast<int>(NSMaxY(frame));
    }
    LOKA_VERIFY([viewport contentSize].height == visible);
    const int maximum = totalHeight + 2 * MacSceneLayout::kGap - visible;
    LOKA_VERIFY(maximum > 0);
    LOKA_VERIFY([[viewport documentView] frame].size.height == maximum + visible);
    SceneTestAccess::unmount(scene);
    controller.drainNativeRetirements();
  }
  [root release];
  [pool drain];
}

namespace
{
  // kind() supplies the seat verdict; asBoundary() counts actual dispatch.
  class GapDispatchText : public loka::app::TextNode
  {
  public:
    explicit GapDispatchText(int &visits)
        : TextNode(loka::app::TextProps("Following")),
          visits_(visits)
    {
    }
    virtual loka::app::scene::BoundaryNode *asBoundary()
    {
      ++this->visits_;
      return 0;
    }

  private:
    int &visits_;
  };
} // namespace

void testMacScrollViewGappedOriginRefuses()
{
  using namespace loka::app;
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  [NSApplication sharedApplication];
  for (int generic = 0; generic < 2; ++generic)
  {
    NSView *root = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 300, 100)];
    {
      MacScenePlatformController controller(root, RailMetrics());
      ViewportFact offset(0);
      int visits = 0;
      ScrollViewNode scroll((ScrollViewProps(offset.state())));
      loka::app::scene::NestableNode *content = &scroll;
      if (generic)
      {
        FragmentNode *fragment = new FragmentNode((FragmentProps()));
        scroll.addChild(fragment);
        content = fragment;
      }
      BoxNode *leading = new BoxNode(BoxProps().setSize(100, 100));
      TextNode *following = new GapDispatchText(visits);
      content->addChild(leading);
      content->addChild(following);
      establishLayout(controller, &scroll, 300, 100);
      NSScrollView *viewport = scrollViewUnderRoot(root);
      LOKA_VERIFY(viewport != nil);
      const NSRect before = [[viewport documentView] frame];
      LOKA_VERIFY(visits > 0);
      LOKA_VERIFY(before.size.height > [viewport contentSize].height);
      visits = 0;
      // The old end fits short; only the intervening gap crosses the limit.
      leading->props.setSize(100, static_cast<short>(SHRT_MAX - MacSceneLayout::kRootInset - MacSceneLayout::kGap + 1));
      controller.relayout(300, 100);
      LOKA_VERIFY(visits == 0);
      const bool unchanged = NSEqualRects([[viewport documentView] frame], before);
      LOKA_VERIFY(unchanged);
      const int offsetValue = offset.state().get().y;
      LOKA_VERIFY(offsetValue == 0);
      const int settledClip = clipOffset(viewport);
      LOKA_VERIFY(settledClip == 0);
      controller.onChange(0, loka::app::scene::NODE_DIRTY_NONE, false);
    }
    [root release];
  }
  [pool drain];
}

void testMacScrollViewHorizontalRangeAndScroller()
{
  using namespace loka::app;
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  [NSApplication sharedApplication];
  NSView *root = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 300, 140)];
  LOKA_VERIFY(root != nil);
  {
    MacScenePlatformController controller(root, RailMetrics());
    ViewportFact fact(0);
    ScrollViewNode scroll((ScrollViewProps(fact.state())));
    StackNode *column = new StackNode(StackProps(STACK_AXIS_COLUMN));
    ImageViewNode *image = new ImageViewNode(ImageViewProps().size(400, 10));
    column->addChild(image);
    scroll.addChild(column);
    fact.state().set(loka::core::Frame(1000, 0, 0, 0));
    establishLayout(controller, &scroll, 300, 140);
    NSScrollView *native = scrollViewUnderRoot(root);
    LOKA_VERIFY(native != nil);
    NSClipView *clip = [native contentView];
    const loka::macos::MacProjection &projection = controller.projection();
    const int maximum = 400 - projection.clientCapacityToLu([native contentSize].width);
    LOKA_VERIFY([[native documentView] frame].size.width == projection.projectLength(0, 400).pt);
    LOKA_VERIFY([native hasHorizontalScroller] == YES);
    const loka::core::Frame clamped = fact.state().get();
    LOKA_VERIFY(clamped.x == maximum);
    LOKA_VERIFY([clip bounds].origin.x == projection.scrollOffsetToNative(maximum).pt);
    fact.state().set(loka::core::Frame(-7, 0, 0, 0));
    controller.relayout(300, 140);
    const loka::core::Frame negative = fact.state().get();
    LOKA_VERIFY(negative.x == 0);
    image->props.size(50, 10);
    controller.relayout(300, 140);
    const loka::core::Frame narrow = fact.state().get();
    LOKA_VERIFY([native hasHorizontalScroller] == NO);
    LOKA_VERIFY(narrow.x == 0);
    LOKA_VERIFY([[native documentView] frame].size.width == [native contentSize].width);
    // An intrinsic image without a source refuses width measurement.
    image->props.size(0, 10).attr(ImageViewAttr().sizePolicy(IMAGE_VIEW_SIZE_INTRINSIC));
    fact.state().set(loka::core::Frame(50, 0, 0, 0));
    controller.relayout(300, 140);
    const loka::core::Frame unknown = fact.state().get();
    LOKA_VERIFY(unknown.x == 0);
    LOKA_VERIFY([native hasHorizontalScroller] == NO);
    LOKA_VERIFY([clip bounds].origin.x == 0);

    controller.onChange(0, loka::app::scene::NODE_DIRTY_NONE, false);
  }
  LOKA_VERIFY([[root subviews] count] == 0);
  [root release];
  [pool drain];
}

void testMacScrollViewBoundsObservationPublishesBothAxes()
{
  using namespace loka::app;
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  [NSApplication sharedApplication];
  NSView *root = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 300, 140)];
  LOKA_VERIFY(root != nil);
  {
    MacScenePlatformController controller(root, RailMetrics());
    ViewportFact fact(0);
    ScrollViewNode scroll((ScrollViewProps(fact.state())));
    StackNode *column = new StackNode(StackProps(STACK_AXIS_COLUMN));
    ImageViewNode *image = new ImageViewNode(ImageViewProps().size(400, 10));
    column->addChild(image);
    scroll.addChild(column);
    image->props.size(400, 300);
    establishLayout(controller, &scroll, 300, 140);
    NSScrollView *native = scrollViewUnderRoot(root);
    LOKA_VERIFY(native != nil);
    NSClipView *clip = [native contentView];
    LOKA_VERIFY([native hasHorizontalScroller] == YES);
    fact.resetChangeCount();
    [clip setBoundsOrigin:NSMakePoint(12, 7)];
    [[NSNotificationCenter defaultCenter] postNotificationName:NSViewBoundsDidChangeNotification object:clip];
    const loka::core::Frame moved = fact.state().get();
    LOKA_VERIFY(moved.x == 12 && moved.y == 7);
    LOKA_VERIFY(moved.width == 0 && moved.height == 0);
    const bool publishedOnce = fact.changeCount() == 1;
    LOKA_VERIFY(publishedOnce);
    [[NSNotificationCenter defaultCenter] postNotificationName:NSViewBoundsDidChangeNotification object:clip];
    const bool duplicateIsNoOp = fact.changeCount() == 1;
    LOKA_VERIFY(duplicateIsNoOp);

    controller.onChange(0, loka::app::scene::NODE_DIRTY_NONE, false);
  }
  LOKA_VERIFY([[root subviews] count] == 0);
  [root release];
  [pool drain];
}

void testMacScrollViewRelayoutKeepsDocumentWidth()
{
  using namespace loka::app;
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  [NSApplication sharedApplication];
  NSView *root = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 300, 140)];
  LOKA_VERIFY(root != nil);
  {
    MacScenePlatformController controller(root, RailMetrics());
    ViewportFact fact(0);
    ScrollViewNode scroll((ScrollViewProps(fact.state())));
    StackNode *column = new StackNode(StackProps(STACK_AXIS_COLUMN));
    ImageViewNode *image = new ImageViewNode(ImageViewProps().size(400, 10));
    column->addChild(image);
    scroll.addChild(column);
    fact.state().set(loka::core::Frame(50, 0, 0, 0));
    establishLayout(controller, &scroll, 300, 140);
    NSScrollView *native = scrollViewUnderRoot(root);
    LOKA_VERIFY(native != nil);
    NSClipView *clip = [native contentView];
    const NSRect before = [[native documentView] frame];
    const NSPoint origin = [clip bounds].origin;
    LOKA_VERIFY(origin.x == controller.projection().scrollOffsetToNative(50).pt);
    fact.resetChangeCount();
    // Pin relayout itself: the subsequent metrics commit must not hide shrinkage.
    MacScrollViewContext *context = static_cast<MacScrollViewContext *>(scroll.getContext());
    context->relayout(MacSceneLayout::kRootInset,
                      MacSceneLayout::kRootInset,
                      300 - 2 * MacSceneLayout::kRootInset,
                      140 - 2 * MacSceneLayout::kRootInset);
    const bool relayoutPreservedDocument = NSEqualRects([[native documentView] frame], before);
    LOKA_VERIFY(relayoutPreservedDocument);
    LOKA_VERIFY(NSEqualPoints([clip bounds].origin, origin));
    controller.relayout(300, 140);
    const bool layoutPreservedDocument = NSEqualRects([[native documentView] frame], before);
    LOKA_VERIFY(layoutPreservedDocument);
    LOKA_VERIFY(NSEqualPoints([clip bounds].origin, origin));
    const loka::core::Frame unchanged = fact.state().get();
    LOKA_VERIFY(unchanged.x == 50);
    const bool noRepublish = fact.changeCount() == 0;
    LOKA_VERIFY(noRepublish);

    controller.onChange(0, loka::app::scene::NODE_DIRTY_NONE, false);
  }
  LOKA_VERIFY([[root subviews] count] == 0);
  [root release];
  [pool drain];
}

void testMacScrollViewRefusedLayoutKeepsHorizontalMetrics()
{
  using namespace loka::app;
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  [NSApplication sharedApplication];
  NSView *root = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 300, 140)];
  LOKA_VERIFY(root != nil);
  {
    MacScenePlatformController controller(root, RailMetrics());
    ViewportFact fact(0);
    ScrollViewNode scroll((ScrollViewProps(fact.state())));
    int visits = 0;
    ImageViewNode *leading = new ImageViewNode(ImageViewProps().size(400, 100));
    scroll.addChild(leading);
    scroll.addChild(new GapDispatchText(visits));
    fact.state().set(loka::core::Frame(50, 0, 0, 0));
    establishLayout(controller, &scroll, 300, 140);
    NSScrollView *native = scrollViewUnderRoot(root);
    LOKA_VERIFY(native != nil);
    NSClipView *clip = [native contentView];
    const NSRect before = [[native documentView] frame];
    const NSRect clipBefore = [clip bounds];
    const BOOL barBefore = [native hasHorizontalScroller];
    const loka::core::Frame factBefore = fact.state().get();
    LOKA_VERIFY(barBefore == YES && factBefore.x == 50);
    LOKA_VERIFY(visits > 0);
    visits = 0;
    fact.resetChangeCount();
    leading->props.size(1000, SHRT_MAX - MacSceneLayout::kRootInset - MacSceneLayout::kGap + 1);
    controller.relayout(300, 140);
    LOKA_VERIFY(visits == 0);
    const bool documentUnchanged = NSEqualRects([[native documentView] frame], before);
    LOKA_VERIFY(documentUnchanged);
    LOKA_VERIFY([native hasHorizontalScroller] == barBefore);
    const bool clipUnchanged = NSEqualRects([clip bounds], clipBefore);
    LOKA_VERIFY(clipUnchanged);
    const loka::core::Frame refused = fact.state().get();
    LOKA_VERIFY(refused.x == factBefore.x && refused.y == factBefore.y);
    const bool factUnchanged = fact.changeCount() == 0;
    LOKA_VERIFY(factUnchanged);

    controller.onChange(0, loka::app::scene::NODE_DIRTY_NONE, false);
  }
  LOKA_VERIFY([[root subviews] count] == 0);
  [root release];
  [pool drain];
}

void testMacScrollViewHorizontalScrollerShortensChildHeight()
{
  using namespace loka::app;
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  [NSApplication sharedApplication];
  NSView *root = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 300, 140)];
  LOKA_VERIFY(root != nil);
  {
    MacScenePlatformController controller(root, RailMetrics());
    ViewportFact fact(0);
    ScrollViewNode scroll((ScrollViewProps(fact.state())));
    scroll.addChild(new RectSurfaceNode(RectSurfaceProps().size(400, 0)));
    establishLayout(controller, &scroll, 300, 140);
    NSScrollView *native = scrollViewUnderRoot(root);
    LOKA_VERIFY(native != nil);
    NSClipView *clip = [native contentView];
    LOKA_VERIFY([native hasHorizontalScroller] == YES);
    const int seatHeight = 140 - 2 * MacSceneLayout::kRootInset;
    CGFloat thickness = 0;
    if ([NSScroller respondsToSelector:@selector(preferredScrollerStyle)] &&
        [NSScroller respondsToSelector:@selector(scrollerWidthForControlSize:scrollerStyle:)])
    {
      const NSInteger style = [NSScroller preferredScrollerStyle];
      if (style == 1)
      {
        std::printf("[skip] overlay scrollers take no space\n");
        thickness = 0;
      }
      else
        thickness = [NSScroller scrollerWidthForControlSize:LOKA_MAC_CONTROL_SIZE_REGULAR
                                              scrollerStyle:[NSScroller preferredScrollerStyle]];
    }
    else
    {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
      thickness = [NSScroller scrollerWidth];
#pragma clang diagnostic pop
    }
    const CGFloat expectedHeight = seatHeight > thickness ? seatHeight - thickness : 0;
    LOKA_VERIFY([[native documentView] frame].size.height == expectedHeight);
    LOKA_VERIFY([clip bounds].size.height == expectedHeight);
    fact.state().set(loka::core::Frame(0, 1000, 0, 0));
    controller.relayout(300, 140);
    const loka::core::Frame clamped = fact.state().get();
    LOKA_VERIFY(clamped.y == 0);
    LOKA_VERIFY([clip bounds].origin.y == 0);

    controller.onChange(0, loka::app::scene::NODE_DIRTY_NONE, false);
  }
  LOKA_VERIFY([[root subviews] count] == 0);
  [root release];
  [pool drain];
}

void testMacScrollViewMaximumSurvivesFractionalScale()
{
  std::printf("\n==== [testMacScrollViewMaximumSurvivesFractionalScale] start ====\n");
  using namespace loka::app;
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  [NSApplication sharedApplication];
  NSView *root = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 420, 240)];
  LOKA_VERIFY(root != nil);
  {
    // 5/4 space scale: a 401 lu document projects to a fractional edge, and
    // the maximum the bounds notification clamps with must agree with the
    // maximum layout admitted, or the notification republishes one unit short.
    MacScenePlatformController controller((void *)root, RailMetrics(Ratio(), Ratio(5, 4)));
    ViewportFact offset(0);
    offset.state().set(loka::core::Frame(1000, 0, 0, 0));
    ScrollViewNode scrollView((ScrollViewProps(offset.state())));
    StackNode *column = addButtonColumn(scrollView, 2);
    column->addChild(new ImageViewNode(ImageViewProps().size(401, 10)));
    establishLayout(controller, &scrollView, 300, 140);
    NSScrollView *native = scrollViewUnderRoot(root);
    LOKA_VERIFY(native != nil);
    const int admitted = offset.state().get().x;
    LOKA_VERIFY(admitted > 0);
    offset.resetChangeCount();
    NSClipView *clipView = [native contentView];
    [[NSNotificationCenter defaultCenter] postNotificationName:NSViewBoundsDidChangeNotification object:clipView];
    const int republished = offset.state().get().x;
    const int changes = offset.changeCount();
    LOKA_VERIFY(republished == admitted && changes == 0
                && "the bounds notification must clamp with the maximum layout admitted, not one unit short");
    controller.onChange(0, loka::app::scene::NODE_DIRTY_NONE, false);
  }
  LOKA_VERIFY([[root subviews] count] == 0);
  [root release];
  [pool drain];
  std::printf("==== [testMacScrollViewMaximumSurvivesFractionalScale] PASSED ====\n");
}

void testMacScrollViewScrollerStyleChangeRequestsRelayout()
{
  std::printf("\n==== [testMacScrollViewScrollerStyleChangeRequestsRelayout] start ====\n");
  using namespace loka::app;
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  [NSApplication sharedApplication];
  NSView *root = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 320, 240)];
  LOKA_VERIFY(root != nil);
  {
    MacScenePlatformController controller((void *)root, RailMetrics());
    ViewportFact offset(0);
    ScrollViewNode scrollView((ScrollViewProps(offset.state())));
    addButtonColumn(scrollView, 6);
    establishLayout(controller, &scrollView, 300, 140);
    LOKA_VERIFY(!controller.hasPendingRelayout());
    // AppKit retiles every scroll view when the preferred scroller style
    // changes; the children's offered height must follow through a relayout.
    [[NSNotificationCenter defaultCenter] postNotificationName:@"NSPreferredScrollerStyleDidChangeNotification"
                                                        object:nil];
    LOKA_VERIFY(controller.hasPendingRelayout() && "a scroller style change must request a relayout");
    controller.onChange(0, loka::app::scene::NODE_DIRTY_NONE, false);
  }
  LOKA_VERIFY([[root subviews] count] == 0);
  [root release];
  [pool drain];
  std::printf("==== [testMacScrollViewScrollerStyleChangeRequestsRelayout] PASSED ====\n");
}

void testMacScrollViewCongruentWidthsLeaveNoHorizontalRange()
{
  std::printf("\n==== [testMacScrollViewCongruentWidthsLeaveNoHorizontalRange] start ====\n");
  using namespace loka::app;
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  [NSApplication sharedApplication];
  NSView *root = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 420, 240)];
  LOKA_VERIFY(root != nil);
  {
    // 5/4 space scale: a content width one logical unit wider than the clip
    // can project to the same native width; then there is nothing to scroll.
    MacScenePlatformController controller((void *)root, RailMetrics(Ratio(), Ratio(5, 4)));
    ViewportFact offset(0);
    ScrollViewNode scrollView((ScrollViewProps(offset.state())));
    StackNode *column = addButtonColumn(scrollView, 2);
    ImageViewNode *image = new ImageViewNode(ImageViewProps().size(10, 10));
    column->addChild(image);
    establishLayout(controller, &scrollView, 300, 140);
    NSScrollView *native = scrollViewUnderRoot(root);
    LOKA_VERIFY(native != nil);
    // Search a few seat widths for a clip whose projected width a wider
    // logical width still fits into (341 pt holds both 272 and 273 lu).
    int congruent = 0;
    int width = 300;
    CGFloat clipWidth = 0;
    int visible = 0;
    for (width = 289; width <= 300 && !congruent; ++width)
    {
      controller.relayout(width, 140);
      clipWidth = [native contentSize].width;
      visible = controller.projection().clientCapacityToLu(clipWidth);
      for (int candidate = visible + 1; candidate <= visible + 4; ++candidate)
      {
        if (controller.projection().projectLength(0, candidate).pt <= clipWidth)
        {
          congruent = candidate;
          break;
        }
      }
      if (congruent)
        break;
    }
    if (!congruent)
    {
      std::printf("[skip] no logical width wider than the clip projects into it at 5/4 for seat widths 289..300\n");
    }
    else
    {
      image->props.size(congruent, 10);
      offset.state().set(loka::core::Frame(50, 0, 0, 0));
      controller.relayout(width, 140);
      const bool scroller = [native hasHorizontalScroller] == YES;
      const int x = offset.state().get().x;
      LOKA_VERIFY(!scroller && x == 0 && "widths that project to the same native width leave no range");
    }
    controller.onChange(0, loka::app::scene::NODE_DIRTY_NONE, false);
  }
  LOKA_VERIFY([[root subviews] count] == 0);
  [root release];
  [pool drain];
  std::printf("==== [testMacScrollViewCongruentWidthsLeaveNoHorizontalRange] PASSED ====\n");
}

void testMacScrollViewRefusedWidenedLayoutKeepsDocumentWidth()
{
  std::printf("\n==== [testMacScrollViewRefusedWidenedLayoutKeepsDocumentWidth] start ====\n");
  using namespace loka::app;
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  [NSApplication sharedApplication];
  NSView *root = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 300, 140)];
  LOKA_VERIFY(root != nil);
  {
    MacScenePlatformController controller(root, RailMetrics());
    ViewportFact fact(0);
    ScrollViewNode scroll((ScrollViewProps(fact.state())));
    int visits = 0;
    ImageViewNode *leading = new ImageViewNode(ImageViewProps().size(400, 100));
    scroll.addChild(leading);
    scroll.addChild(new GapDispatchText(visits));
    fact.state().set(loka::core::Frame(50, 0, 0, 0));
    establishLayout(controller, &scroll, 300, 140);
    NSScrollView *native = scrollViewUnderRoot(root);
    LOKA_VERIFY(native != nil);
    const NSRect before = [[native documentView] frame];
    LOKA_VERIFY(visits > 0);
    visits = 0;
    // The seat widens past the committed document while the traversal
    // refuses: the document keeps the width of the last successful layout.
    leading->props.size(400, SHRT_MAX - MacSceneLayout::kRootInset - MacSceneLayout::kGap + 1);
    controller.relayout(900, 140);
    LOKA_VERIFY(visits == 0);
    const bool clipWider = [native contentSize].width > before.size.width;
    LOKA_VERIFY(clipWider && "the fixture must widen the clip past the committed document");
    const bool documentUnchanged = NSEqualRects([[native documentView] frame], before);
    LOKA_VERIFY(documentUnchanged && "a refused pass must not grow the document before the commit");
    controller.onChange(0, loka::app::scene::NODE_DIRTY_NONE, false);
  }
  LOKA_VERIFY([[root subviews] count] == 0);
  [root release];
  [pool drain];
  std::printf("==== [testMacScrollViewRefusedWidenedLayoutKeepsDocumentWidth] PASSED ====\n");
}
