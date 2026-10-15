#include "ScrollViewScopeTests.hpp"

#include <cassert>
#include <climits>

#include "app/nodes/nestable/Box.hpp"
#include "app/nodes/nestable/RowColumn.hpp"
#include "app/nodes/nestable/ScrollView.hpp"
#include "app/scene/state/NodeState.hpp"
#include "core/State.hpp"
#include "core/StateTracker.hpp"
#include "platform/null/NullScenePlatformController.hpp"

namespace
{
  class OffsetFact
  {
  public:
    explicit OffsetFact(int initial)
        : storage_(initial),
          tracker_(),
          state_(&this->storage_, &this->tracker_)
    {
      this->tracker_.addState(&this->storage_);
    }

    loka::app::scene::NodeState<int> &state()
    {
      return this->state_;
    }

  private:
    loka::core::MutableState<int> storage_;
    loka::core::PushStateTracker tracker_;
    loka::app::scene::NodeState<int> state_;

    OffsetFact(const OffsetFact &);
    OffsetFact &operator=(const OffsetFact &);
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

  loka::app::ScrollViewNode *makeScrollView(OffsetFact &offset)
  {
    loka::app::ScrollViewProps props;
    props.offset(offset.state());
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
  OffsetFact offset(4);
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
  OffsetFact offset(0);
  loka::app::ScrollViewNode scrollView((loka::app::ScrollViewProps(offset.state())));
  FixedLayoutProbeNode *child = new FixedLayoutProbeNode(7);
  scrollView.addChild(child);
  const loka::app::scene::LayoutState input = makeState(10, 20, 80, 30);

  const int firstResult = platform.projectLayoutForTesting(&scrollView, input);
  const short atZero = child->geometry().y;
  offset.state().set(6);
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
  OffsetFact offset(0);

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
  OffsetFact outerOffset(3);
  OffsetFact innerOffset(5);
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
