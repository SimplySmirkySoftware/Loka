#include "ScrollViewScopeTests.hpp"

#include <cassert>
#include "support/TestVerify.hpp"
#include <climits>

#include "app/layout/StackSpans.hpp"
#include "app/nodes/Text.hpp"
#include "app/nodes/nestable/Box.hpp"
#include "app/nodes/nestable/RowColumn.hpp"
#include "app/nodes/nestable/ScrollView.hpp"
#include "app/scene/state/NodeState.hpp"
#include "core/State.hpp"
#include "core/StateTracker.hpp"
#include "platform/null/NullScenePlatformController.hpp"

namespace
{
  class ViewportFact
  {
  public:
    explicit ViewportFact(int initial, int x = 0)
        : storage_(loka::core::Frame(x, initial, 0, 0)),
          tracker_(),
          state_(&this->storage_, &this->tracker_)
    {
      this->tracker_.addState(&this->storage_);
    }

    loka::app::scene::NodeState<loka::core::Frame> &state()
    {
      return this->state_;
    }

  private:
    loka::core::MutableState<loka::core::Frame> storage_;
    loka::core::PushStateTracker tracker_;
    loka::app::scene::NodeState<loka::core::Frame> state_;

    ViewportFact(const ViewportFact &);
    ViewportFact &operator=(const ViewportFact &);
  };

  class FixedLayoutProbeNode : public loka::app::scene::Node,
                               public loka::app::scene::IProjectedLayoutNode
  {
  public:
    explicit FixedLayoutProbeNode(short fixedHeight)
        : fixedHeight_(fixedHeight),
          wasLaidOut_(false),
          scopeDepth_(0),
          geometry_()
    {
    }

    virtual loka::app::scene::IProjectedLayoutNode *asProjectedLayoutNode()
    {
      return this;
    }

    virtual short layoutProjected(loka::app::scene::IPlatformController *controller,
                                  loka::app::scene::LayoutState &state)
    {
      NullScenePlatformController *nullController =
          static_cast<NullScenePlatformController *>(controller);
      this->wasLaidOut_ = true;
      this->scopeDepth_ = nullController
                              ? nullController->projectionParentScopeDepthForTesting()
                              : 0;
      this->geometry_ = state;
      return static_cast<short>(state.y + this->fixedHeight_);
    }

    bool wasLaidOut() const
    {
      return this->wasLaidOut_;
    }

    unsigned scopeDepth() const
    {
      return this->scopeDepth_;
    }

    const loka::app::scene::LayoutState &geometry() const
    {
      return this->geometry_;
    }

  private:
    short fixedHeight_;
    bool wasLaidOut_;
    unsigned scopeDepth_;
    loka::app::scene::LayoutState geometry_;
  };

  loka::app::scene::LayoutState makeState(short x,
                                          short y,
                                          short width,
                                          short height)
  {
    loka::app::scene::LayoutState state;
    state.x = x;
    state.y = y;
    state.width = width;
    state.height = height;
    state.lineHeight = 10;
    return state;
  }

  loka::app::ScrollViewNode *makeScrollView(ViewportFact &offset)
  {
    loka::app::ScrollViewProps props;
    props.viewport(offset.state());
    return new loka::app::ScrollViewNode(props);
  }

  loka::app::BoxNode *makeTallBox(short height, FixedLayoutProbeNode *probe)
  {
    loka::app::BoxProps props;
    props.setSize(10, height);
    loka::app::BoxNode *box = new loka::app::BoxNode(props);
    box->addChild(probe);
    return box;
  }
} // namespace

void testScrollViewScopePushPopRestoresRootProjectionParent()
{
  NullScenePlatformController platform;
  ViewportFact offset(4);
  loka::app::StackNode column((loka::app::StackProps(loka::app::STACK_AXIS_COLUMN)));
  loka::app::ScrollViewNode *scrollView = makeScrollView(offset);
  FixedLayoutProbeNode *first = new FixedLayoutProbeNode(7);
  FixedLayoutProbeNode *second = new FixedLayoutProbeNode(5);
  FixedLayoutProbeNode *rootSibling = new FixedLayoutProbeNode(5);
  scrollView->addChild(first);
  scrollView->addChild(second);
  column.addChild(scrollView);
  column.addChild(rootSibling);

  const int resultY = platform.projectLayoutForTesting(
      &column, makeState(10, 20, 80, 40));
  (void)resultY;

  assert(first->wasLaidOut());
  assert(second->wasLaidOut());
  assert(first->scopeDepth() == 1);
  assert(second->scopeDepth() == 1);
  assert(first->geometry().y == 16);
  assert(second->geometry().y == 35);
  assert(rootSibling->wasLaidOut());
  assert(rootSibling->scopeDepth() == 0);
  assert(rootSibling->geometry().y == 72);
  assert(platform.projectionParentScopeDepthForTesting() == 0);
  assert(resultY == 77);
}

void testScrollViewOffsetIsReappliedOnEveryRelayout()
{
  NullScenePlatformController platform;
  ViewportFact offset(0);
  loka::app::ScrollViewNode scrollView((loka::app::ScrollViewProps(offset.state())));
  FixedLayoutProbeNode *child = new FixedLayoutProbeNode(7);
  scrollView.addChild(child);
  const loka::app::scene::LayoutState input = makeState(10, 20, 80, 30);

  const int firstResult = platform.projectLayoutForTesting(&scrollView, input);
  const short atZero = child->geometry().y;
  offset.state().set(loka::core::Frame(0, 6, 0, 0));
  const int secondResult = platform.projectLayoutForTesting(&scrollView, input);
  const short afterChange = child->geometry().y;
  const int thirdResult = platform.projectLayoutForTesting(&scrollView, input);
  const short afterUnchangedRelayout = child->geometry().y;
  (void)firstResult;
  (void)atZero;
  (void)secondResult;
  (void)afterChange;
  (void)thirdResult;
  (void)afterUnchangedRelayout;

  assert(atZero == 20);
  assert(afterChange == 14);
  assert(afterUnchangedRelayout == afterChange);
  assert(firstResult == 50);
  assert(secondResult == firstResult);
  assert(thirdResult == secondResult);
  assert(platform.projectionParentScopeDepthForTesting() == 0);
}

void testScrollViewContentHeightRefusesBeforeShortWrap()
{
  NullScenePlatformController platform;
  ViewportFact offset(0);

  loka::app::ScrollViewNode nearLimit((loka::app::ScrollViewProps(offset.state())));
  FixedLayoutProbeNode *nearProbe = new FixedLayoutProbeNode(1);
  nearLimit.addChild(makeTallBox(static_cast<short>(SHRT_MAX - 1), nearProbe));
  const int nearResult = platform.projectLayoutForTesting(
      &nearLimit, makeState(0, 0, 100, 100));
  (void)nearResult;

  assert(nearProbe->wasLaidOut());
  assert(nearProbe->geometry().y == 0);
  assert(platform.scrollViewShortRangeRefusalCount() == 0);
  assert(nearResult == 100);

  loka::app::ScrollViewNode overflow((loka::app::ScrollViewProps(offset.state())));
  FixedLayoutProbeNode *first = new FixedLayoutProbeNode(1);
  FixedLayoutProbeNode *second = new FixedLayoutProbeNode(1);
  FixedLayoutProbeNode *afterOverflow = new FixedLayoutProbeNode(1);
  overflow.addChild(makeTallBox(20000, first));
  overflow.addChild(makeTallBox(20000, second));
  overflow.addChild(afterOverflow);
  const int overflowResult = platform.projectLayoutForTesting(
      &overflow, makeState(0, 0, 100, 100));
  (void)overflowResult;

  assert(first->wasLaidOut());
  assert(first->geometry().y == 0);
  assert(second->wasLaidOut());
  assert(second->geometry().y == 20012);
  assert(second->geometry().y >= 0);
  assert(!afterOverflow->wasLaidOut());
  assert(platform.scrollViewShortRangeRefusalCount() == 1);
  assert(platform.projectionParentScopeDepthForTesting() == 0);
  assert(overflowResult == 100);

  loka::app::ScrollViewNode highSeat((loka::app::ScrollViewProps(offset.state())));
  FixedLayoutProbeNode *unreachable = new FixedLayoutProbeNode(1);
  highSeat.addChild(unreachable);
  const int highSeatResult = platform.projectLayoutForTesting(
      &highSeat, makeState(0, 32000, 100, 1000));
  (void)highSeatResult;

  assert(!unreachable->wasLaidOut());
  assert(platform.scrollViewShortRangeRefusalCount() == 2);
  assert(highSeatResult == 32000);
  assert(platform.projectionParentScopeDepthForTesting() == 0);

  // The nested-container path: a Column between the ScrollView and the tall
  // children runs its own accumulate-then-narrow loop, so the wrap must be
  // refused at the traversal edge before the narrowed coordinate reaches the
  // next grandchild.
  loka::app::ScrollViewNode nestedNarrow((loka::app::ScrollViewProps(offset.state())));
  loka::app::StackNode *tallColumn =
      new loka::app::StackNode((loka::app::StackProps(loka::app::STACK_AXIS_COLUMN)));
  FixedLayoutProbeNode *firstGrandChild = new FixedLayoutProbeNode(1);
  FixedLayoutProbeNode *secondGrandChild = new FixedLayoutProbeNode(1);
  FixedLayoutProbeNode *wrappedGrandChild = new FixedLayoutProbeNode(1);
  tallColumn->addChild(makeTallBox(20000, firstGrandChild));
  tallColumn->addChild(makeTallBox(20000, secondGrandChild));
  tallColumn->addChild(wrappedGrandChild);
  nestedNarrow.addChild(tallColumn);
  const int nestedResult = platform.projectLayoutForTesting(
      &nestedNarrow, makeState(0, 0, 100, 100));
  (void)nestedResult;

  assert(firstGrandChild->wasLaidOut());
  assert(firstGrandChild->geometry().y == 0);
  assert(secondGrandChild->wasLaidOut());
  assert(secondGrandChild->geometry().y == 20012);
  assert(!wrappedGrandChild->wasLaidOut());
  assert(platform.scrollViewShortRangeRefusalCount() == 3);
  assert(nestedResult == 100);
  assert(platform.projectionParentScopeDepthForTesting() == 0);
}

void testNestedScrollViewRefusalPreservesOuterScope()
{
  NullScenePlatformController platform;
  ViewportFact outerOffset(3);
  ViewportFact innerOffset(5);
  loka::app::ScrollViewNode outer((loka::app::ScrollViewProps(outerOffset.state())));
  loka::app::ScrollViewNode *inner = makeScrollView(innerOffset);
  FixedLayoutProbeNode *innerChild = new FixedLayoutProbeNode(9);
  FixedLayoutProbeNode *outerSibling = new FixedLayoutProbeNode(4);
  inner->addChild(innerChild);
  outer.addChild(inner);
  outer.addChild(outerSibling);

  const int resultY = platform.projectLayoutForTesting(
      &outer, makeState(10, 20, 80, 40));
  (void)resultY;

  assert(platform.nestedScrollViewRefusalCount() == 1);
  assert(!innerChild->wasLaidOut());
  assert(outerSibling->wasLaidOut());
  assert(outerSibling->scopeDepth() == 1);
  assert(outerSibling->geometry().y == 29);
  assert(platform.scrollViewShortRangeRefusalCount() == 0);
  assert(platform.projectionParentScopeDepthForTesting() == 0);
  assert(resultY == 60);
}

void testScrollViewXScopePushPopRestoresRootProjectionParent()
{
  NullScenePlatformController platform;
  ViewportFact offset(0, 4);
  loka::app::StackNode column((loka::app::StackProps(loka::app::STACK_AXIS_COLUMN)));
  loka::app::ScrollViewNode *scrollView = makeScrollView(offset);
  FixedLayoutProbeNode *first = new FixedLayoutProbeNode(7);
  FixedLayoutProbeNode *second = new FixedLayoutProbeNode(5);
  FixedLayoutProbeNode *rootSibling = new FixedLayoutProbeNode(5);
  scrollView->addChild(first);
  scrollView->addChild(second);
  column.addChild(scrollView);
  column.addChild(rootSibling);

  const int resultY = platform.projectLayoutForTesting(&column, makeState(10, 20, 80, 40));
  (void)resultY;

  assert(first->wasLaidOut());
  assert(second->wasLaidOut());
  assert(first->scopeDepth() == 1);
  assert(second->scopeDepth() == 1);
  assert(first->geometry().x == 6);
  assert(second->geometry().x == 6);
  assert(rootSibling->wasLaidOut());
  assert(rootSibling->scopeDepth() == 0);
  assert(rootSibling->geometry().x == 10);
  assert(platform.projectionParentScopeDepthForTesting() == 0);
  assert(resultY == 77);
}

void testScrollViewXOriginIsReappliedOnEveryRelayout()
{
  NullScenePlatformController platform;
  ViewportFact offset(0);
  loka::app::ScrollViewNode scrollView((loka::app::ScrollViewProps(offset.state())));
  FixedLayoutProbeNode *child = new FixedLayoutProbeNode(7);
  scrollView.addChild(child);
  const loka::app::scene::LayoutState input = makeState(10, 20, 80, 30);

  const int firstResult = platform.projectLayoutForTesting(&scrollView, input);
  const short atZero = child->geometry().x;
  offset.state().set(loka::core::Frame(6, 0, 0, 0));
  const int secondResult = platform.projectLayoutForTesting(&scrollView, input);
  const short afterChange = child->geometry().x;
  const int thirdResult = platform.projectLayoutForTesting(&scrollView, input);
  const short afterUnchangedRelayout = child->geometry().x;
  (void)firstResult;
  (void)atZero;
  (void)secondResult;
  (void)afterChange;
  (void)thirdResult;
  (void)afterUnchangedRelayout;

  assert(atZero == 10);
  assert(afterChange == 4);
  assert(afterUnchangedRelayout == afterChange);
  assert(firstResult == 50);
  assert(secondResult == firstResult);
  assert(thirdResult == secondResult);
  assert(platform.projectionParentScopeDepthForTesting() == 0);
}

namespace
{
  void countOriginWrites(void *context)
  {
    ++*static_cast<int *>(context);
  }
} // namespace

void testScrollViewRequestedOriginNormalizesBothAxes()
{
  using loka::core::Frame;
  loka::app::ScrollViewNode unbound((loka::app::ScrollViewProps()));
  LOKA_VERIFY(unbound.requestedOrigin().x == 0);
  LOKA_VERIFY(unbound.requestedOrigin().y == 0);
  ViewportFact fact(0);
  loka::app::ScrollViewNode node((loka::app::ScrollViewProps(fact.state())));
  fact.state().set(Frame(-7, -9, 0, 0));
  LOKA_VERIFY(node.requestedOrigin().x == 0);
  LOKA_VERIFY(node.requestedOrigin().y == 0);
  fact.state().set(Frame(SHRT_MAX + 1, SHRT_MAX + 1, 0, 0));
  LOKA_VERIFY(node.requestedOrigin().x == SHRT_MAX);
  LOKA_VERIFY(node.requestedOrigin().y == SHRT_MAX);
  fact.state().set(Frame());
  LOKA_VERIFY(node.requestedOrigin().x == 0);
  LOKA_VERIFY(node.requestedOrigin().y == 0);
  fact.state().set(loka::app::ScrollViewProps::topOrigin());
  LOKA_VERIFY(node.requestedOrigin() == Frame(0, 0, 0, 0));
  node.props.contentInsets(loka::app::ScrollViewContentInsets::of(12, 0));
  const int requests[] = {-5, -12, -40, 0, SHRT_MAX, INT_MAX, INT_MIN};
  const int expected[] = {7, 0, 0, 12, SHRT_MAX, SHRT_MAX, 0};
  for (unsigned i = 0; i < sizeof(requests) / sizeof(requests[0]); ++i)
  {
    fact.state().set(Frame(-7, requests[i], 0, 0));
    LOKA_VERIFY(node.requestedOrigin().x == 0);
    LOKA_VERIFY(node.requestedOrigin().y == expected[i]);
  }
  fact.state().set(loka::app::ScrollViewProps::topOrigin());
  LOKA_VERIFY(node.requestedOrigin().x == 0);
  LOKA_VERIFY(node.requestedOrigin().y == 0);
  unbound.props.contentInsets(loka::app::ScrollViewContentInsets::of(12, 0));
  LOKA_VERIFY(unbound.requestedOrigin() == Frame(0, 0, 0, 0));
}

void testScrollViewPublishesOnlyChangedOriginAndPreservesSize()
{
  using loka::core::Frame;
  ViewportFact fact(0);
  fact.state().set(Frame(3, 4, 123, 456));
  int writes = 0;
  fact.state().state()->bind(&countOriginWrites, &writes, false);
  loka::app::ScrollViewNode node((loka::app::ScrollViewProps(fact.state())));
  node.publishOrigin(3, 4);
  LOKA_VERIFY(writes == 0);
  node.publishOrigin(7, 8);
  LOKA_VERIFY(writes == 1);
  LOKA_VERIFY(fact.state().get() == Frame(7, 8, 123, 456));
  node.publishOrigin(9, 8);
  LOKA_VERIFY(writes == 2);
  LOKA_VERIFY(fact.state().get() == Frame(9, 8, 123, 456));
  node.publishOrigin(9, 10);
  LOKA_VERIFY(writes == 3);
  LOKA_VERIFY(fact.state().get() == Frame(9, 10, 123, 456));
  fact.state().state()->unbind(&countOriginWrites, &writes);
  loka::app::ScrollViewNode unbound((loka::app::ScrollViewProps()));
  unbound.publishOrigin(1, 2);
  LOKA_VERIFY(!unbound.props.viewport_.isValid());
}

void testScrollViewContentTranslationSubtractsTopInset()
{
  using loka::core::Frame;
  ViewportFact fact(0);
  loka::app::ScrollViewNode node(
      loka::app::ScrollViewProps(fact.state()).contentInsets(loka::app::ScrollViewContentInsets::of(12, 3)));
  fact.state().set(loka::app::ScrollViewProps::topOrigin());
  LOKA_VERIFY(node.contentTranslation() == Frame(0, -12, 0, 0));
  fact.state().set(Frame(5, 8, 123, 456));
  LOKA_VERIFY(node.contentTranslation() == Frame(5, 8, 123, 456));
  fact.state().set(Frame(5, -3, 123, 456));
  LOKA_VERIFY(node.contentTranslation() == Frame(5, -3, 123, 456));
}

void testScrollViewPublishOriginWritesContentCoordinates()
{
  using loka::core::Frame;
  ViewportFact fact(0);
  fact.state().set(Frame(0, 0, 123, 456));
  int writes = 0;
  fact.state().state()->bind(&countOriginWrites, &writes, false);
  loka::app::ScrollViewNode node(
      loka::app::ScrollViewProps(fact.state()).contentInsets(loka::app::ScrollViewContentInsets::of(12, 0)));
  node.publishOrigin(0, 0);
  LOKA_VERIFY(fact.state().get() == Frame(0, -12, 123, 456));
  LOKA_VERIFY(writes == 1);
  node.publishOrigin(0, 0);
  LOKA_VERIFY(writes == 1);
  node.publishOrigin(0, 12);
  LOKA_VERIFY(fact.state().get() == Frame(0, 0, 123, 456));
  LOKA_VERIFY(writes == 2);
  node.publishOrigin(0, 12);
  LOKA_VERIFY(writes == 2);
  fact.state().state()->unbind(&countOriginWrites, &writes);
}

void testScrollViewContentInsetsPlaceChildrenAndGrowHeightlessExtent()
{
  NullScenePlatformController platform;
  ViewportFact fact(0);
  fact.state().set(loka::app::ScrollViewProps::topOrigin());
  loka::app::ScrollViewNode node(
      loka::app::ScrollViewProps(fact.state()).contentInsets(loka::app::ScrollViewContentInsets::of(12, 3)));
  FixedLayoutProbeNode *child = new FixedLayoutProbeNode(7);
  node.addChild(child);
  LOKA_VERIFY(platform.projectLayoutForTesting(&node, makeState(10, 20, 80, 30)) == 50);
  const loka::app::scene::LayoutState atTop = child->geometry();
  LOKA_VERIFY(atTop.y == 32);
  LOKA_VERIFY(atTop.height == 30);
  fact.state().set(loka::core::Frame(0, 6, 0, 0));
  LOKA_VERIFY(platform.projectLayoutForTesting(&node, makeState(10, 20, 80, 30)) == 50);
  const short scrolledY = child->geometry().y;
  LOKA_VERIFY(scrolledY == 14);
  fact.state().set(loka::app::ScrollViewProps::topOrigin());
  LOKA_VERIFY(platform.projectLayoutForTesting(&node, makeState(0, 0, 100, 0)) == 22);
  const unsigned long refusalsBeforeOverflow = platform.scrollViewShortRangeRefusalCount();
  LOKA_VERIFY(refusalsBeforeOverflow == 0);

  loka::app::ScrollViewNode collected(
      loka::app::ScrollViewProps(fact.state()).contentInsets(loka::app::ScrollViewContentInsets::of(12, 3)));
  loka::app::StackNode *column = new loka::app::StackNode(loka::app::StackProps(loka::app::STACK_AXIS_COLUMN));
  column->addChild(loka::app::Text("content").create());
  collected.addChild(column);
  loka::app::layout::StackSpans spans;
  LOKA_VERIFY(platform.projectLayoutForTesting(&collected, makeState(0, 0, 100, 30), 0, &spans) == 30);
  LOKA_VERIFY(spans.valid());
  LOKA_VERIFY(spans.placedViewport() == loka::core::Frame(0, -12, 100, 30));

  // Keep content at zero so only the inset-adjusted extent exceeds the limit.
  fact.state().set(loka::core::Frame(0, 0, 0, 0));
  loka::app::ScrollViewNode overflow(
      loka::app::ScrollViewProps(fact.state()).contentInsets(loka::app::ScrollViewContentInsets::of(12, 0)));
  FixedLayoutProbeNode *probe = new FixedLayoutProbeNode(1);
  overflow.addChild(makeTallBox(static_cast<short>(SHRT_MAX - 1), probe));
  LOKA_VERIFY(platform.projectLayoutForTesting(&overflow, makeState(0, 0, 100, 0)) == 0);
  const bool laidOutBeforeRefusal = probe->wasLaidOut();
  LOKA_VERIFY(laidOutBeforeRefusal);
  const unsigned long refusalsAfterOverflow = platform.scrollViewShortRangeRefusalCount();
  LOKA_VERIFY(refusalsAfterOverflow == 1);
}

void testScrollViewContentInsetsValueAndPropsIdentity()
{
  using loka::app::ScrollViewContentInsets;
  const ScrollViewContentInsets zero;
  LOKA_VERIFY(sizeof(ScrollViewContentInsets) == 4);
  LOKA_VERIFY(zero == ScrollViewContentInsets::of(0, 0));
  LOKA_VERIFY(zero != ScrollViewContentInsets::of(0, 1));
  LOKA_VERIFY(ScrollViewContentInsets::of(0, 2) < ScrollViewContentInsets::of(1, 0));
  LOKA_VERIFY(ScrollViewContentInsets::of(1, 2) < ScrollViewContentInsets::of(1, 3));
  LOKA_VERIFY(ScrollViewContentInsets::of(SHRT_MAX, SHRT_MAX).extentExtra() == 2 * SHRT_MAX);
  const loka::app::ScrollView small = loka::app::ScrollView().contentInsets(12, 3);
  const loka::app::ScrollView equal = loka::app::ScrollView().contentInsets(12, 3);
  const loka::app::ScrollView larger = loka::app::ScrollView().contentInsets(12, 4);
  LOKA_VERIFY(!(small.props < equal.props));
  LOKA_VERIFY(!(equal.props < small.props));
  LOKA_VERIFY(small.props < larger.props);
  LOKA_VERIFY(!(larger.props < small.props));
}
