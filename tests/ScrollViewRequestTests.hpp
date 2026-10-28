#ifndef LOKA_TESTS_SCROLL_VIEW_REQUEST_TESTS_HPP
#define LOKA_TESTS_SCROLL_VIEW_REQUEST_TESTS_HPP
#include "app/nodes/nestable/ScrollView.hpp"
#include "app/nodes/nestable/ScrollViewSettlement.hpp"
#include "app/nodes/Text.hpp"
#include "app/scene/Scene.hpp"
#include "support/Headless.hpp"
#include "support/TestVerify.hpp"
#include "testing/scene/SceneTestFlow.hpp"
#include "platform/null/NullScenePlatformController.hpp"
namespace scroll_request_tests
{
  using namespace loka::app;
  using namespace loka::app::scene;
  using loka::core::Frame;
  class Storage : private HeadlessStateOwner
  {
  public:
    NodeState<Frame> viewport;
    NodeState<Frame> otherViewport;
    Request<ScrollTarget> request, other;
    Storage()
    {
      StateBatchBase::CreateImmediateState(this, this->viewport, Frame(0, 0, 80, 30));
      StateBatchBase::CreateImmediateState(this, this->otherViewport, Frame(0, 0, 80, 30));
      StateBatchBase::CreateImmediateState(this, this->request, ScrollTarget::None());
      StateBatchBase::CreateImmediateState(this, this->other, ScrollTarget::None());
    }
  };
  inline LayoutState bounds(short y = 0)
  {
    LayoutState result;
    result.width = 80;
    result.height = 30;
    result.y = y;
    return result;
  }
  struct Fixture
  {
    NullScenePlatformController platform;
    Storage storage;
    ScrollViewNode node;
    Fixture()
        : node(ScrollViewProps(storage.viewport)
                   .scrollTo(storage.request)
                   .contentInsets(ScrollViewContentInsets::of(12, 3)))
    {
    }
    void visit()
    {
      this->platform.projectLayoutForTesting(&this->node, bounds());
    }
  };
  inline void count(void *data)
  {
    ++*static_cast<int *>(data);
  }
  struct Reposter
  {
    Request<ScrollTarget> &request;
    int remaining, takes;
    Reposter(Request<ScrollTarget> &r, int limit)
        : request(r),
          remaining(limit),
          takes(0)
    {
    }
    static void changed(void *data)
    {
      Reposter &self = *static_cast<Reposter *>(data);
      if (!self.request.get().isNone())
        return;
      ++self.takes;
      if (self.remaining != 0)
      {
        if (self.remaining > 0)
          --self.remaining;
        self.request.set(ScrollTarget::Origin(0, self.takes));
      }
    }
  };
  class Presenter : public NullScenePlatformController
  {
  public:
    virtual void onChange(Node *root, NodeDirtyFlags, bool)
    {
      this->projectLayoutForTesting(root, bounds());
    }
  };
  class Root : public BoundaryNodeFor<Root>
  {
  public:
    NodeState<Frame> viewport;
    Request<ScrollTarget> request;
    explicit Root(const BoundaryPropsFor<Root> &p)
        : BoundaryNodeFor<Root>(p)
    {
      this->declareStates(2).state(this->viewport, Frame(0, 0, 80, 30)).state(this->request, ScrollTarget::None());
    }
    virtual bool flushViewDirtyImmediately(NodeDirtyFlags) const
    {
      return false;
    }
    virtual void composeNode(NodeComposition &c)
    {
      c.declare(ScrollView(this->viewport).scrollTo(this->request) << Text("child"));
    }
  };
  struct BorrowObserver
  {
    NullScenePlatformController &platform;
    int calls;
    BorrowObserver(NullScenePlatformController &p)
        : platform(p),
          calls(0)
    {
    }
    static void changed(void *data)
    {
      BorrowObserver &self = *static_cast<BorrowObserver *>(data);
      ++self.calls;
      LOKA_VERIFY(self.platform.borrowPhase().open());
    }
  };
  struct VisitResult
  {
    const ScrollViewVisit visit;
    VisitResult()
        : visit(ScrollViewVisit::succeededUnclamped())
    {
    }
  };
  class VisitController : public NullScenePlatformController
  {
  public:
    enum Mode
    {
      REFUSE,
      CREATE_CONTEXT,
      CHANGE_FACT
    };
    explicit VisitController(Mode mode)
        : mode_(mode),
          applications_(0)
    {
    }
    VisitResult layout(ScrollViewNode *node, const LayoutState &)
    {
      LOKA_VERIFY(this->borrowPhase().open());
      if (this->mode_ == CREATE_CONTEXT)
        node->setContext(new NodeContext(node));
      if (this->mode_ == CHANGE_FACT)
        node->publishOrigin(9, 10);
      return VisitResult();
    }
    bool apply(ScrollViewNode &node, const VisitResult &, const Frame &)
    {
      ++this->applications_;
      if (this->mode_ == REFUSE)
        return false;
      // Deliberately reports a fact different from the requested target.
      node.publishOrigin(21, 22);
      return true;
    }
    int applications() const
    {
      return this->applications_;
    }

  private:
    const Mode mode_;
    int applications_;
  };
  typedef loka::app::testing::SettleTrace<ScrollTarget, Frame> VisitTrace;
  inline void verifyVisitTrace(ScrollRequestResult result, const Frame &before, const Frame &after)
  {
    LOKA_VERIFY(VisitTrace::instance().size() == 1);
    const loka::app::testing::SettleTraceRow<ScrollTarget, Frame> &row = VisitTrace::instance().at(0);
    LOKA_VERIFY(row.stimulus == SETTLE_PROPS);
    LOKA_VERIFY(row.count == 1);
    LOKA_VERIFY(row.seam[0] == result);
    LOKA_VERIFY(row.before == before);
    LOKA_VERIFY(row.after == after);
  }
} // namespace scroll_request_tests
inline void testScrollRequestNormalizedTargets()
{
  using namespace scroll_request_tests;
  Fixture f;
  const ScrollTarget targets[] = {ScrollTarget::Origin(7, 8),
                                  ScrollTarget::Top(),
                                  ScrollTarget::Origin(-1, -5),
                                  ScrollTarget::Origin(0, -100),
                                  ScrollTarget::Origin(SHRT_MAX, SHRT_MAX),
                                  ScrollTarget::Origin(INT_MAX, INT_MAX),
                                  ScrollTarget::Origin(INT_MIN, INT_MIN)};
  const Frame expected[] = {Frame(7, 8, 80, 30),
                            Frame(0, -12, 80, 30),
                            Frame(0, -5, 80, 30),
                            Frame(0, -12, 80, 30),
                            Frame(SHRT_MAX, SHRT_MAX - 12, 80, 30),
                            Frame(SHRT_MAX, SHRT_MAX - 12, 80, 30),
                            Frame(0, -12, 80, 30)};
  for (unsigned i = 0; i < sizeof(targets) / sizeof(targets[0]); ++i)
  {
    f.storage.request.set(targets[i]);
    f.visit();
    LOKA_VERIFY(f.storage.request.get().isNone());
    LOKA_VERIFY(f.storage.viewport.get() == expected[i]);
  }
  LOKA_VERIFY(ScrollTarget::None() != ScrollTarget::Top());
  LOKA_VERIFY(ScrollTarget::Origin(1, 2) != ScrollTarget::Origin(2, 2));
}
inline void testScrollRequestSamePosition()
{
  using namespace scroll_request_tests;
  Fixture f;
  int writes = 0;
  f.storage.viewport.state()->bind(&count, &writes, false);
  f.storage.request.set(ScrollTarget::Origin(0, 0));
  f.visit();
  LOKA_VERIFY(f.storage.request.get().isNone());
  LOKA_VERIFY(writes == 0);
  f.storage.viewport.state()->unbind(&count, &writes);
}
inline void testScrollRequestInputOrdering()
{
  using namespace scroll_request_tests;
  for (int order = 0; order < 3; ++order)
  {
    Fixture f;
    if (order == 1)
      f.node.publishOrigin(3, 16);
    f.storage.request.set(ScrollTarget::Origin(7, 8));
    if (order == 0)
      f.node.publishOrigin(3, 16);
    f.visit();
    if (order == 2)
      f.node.publishOrigin(3, 16);
    LOKA_VERIFY(f.storage.viewport.get() == (order == 2 ? Frame(3, 4, 80, 30) : Frame(7, 8, 80, 30)));
    LOKA_VERIFY(f.storage.request.get().isNone());
  }
}
inline void testScrollRequestRefusedVisit()
{
  using namespace scroll_request_tests;
  Fixture f;
  f.storage.request.set(ScrollTarget::Origin(7, 8));
  f.platform.projectLayoutForTesting(&f.node, bounds(SHRT_MAX));
  LOKA_VERIFY(f.storage.request.get().isNone());
  LOKA_VERIFY(f.storage.viewport.get() == Frame(0, 0, 80, 30));
  LOKA_VERIFY(f.platform.scrollViewShortRangeRefusalCount() == 1);
}
inline void testScrollRequestUnboundViewport()
{
  using namespace scroll_request_tests;
  Fixture f;
  LOKA_VERIFY((NodePropsApplier<ScrollViewNode, ScrollViewProps>::apply(
      &f.node, ScrollViewProps().scrollTo(f.storage.request))));
  f.storage.request.set(ScrollTarget::Top());
  f.visit();
  LOKA_VERIFY(f.storage.request.get().isNone());
  LOKA_VERIFY(f.storage.viewport.get() == Frame(0, 0, 80, 30));
}
inline void testScrollRequestBindingReplacement()
{
  using namespace scroll_request_tests;
  Fixture f;
  f.storage.request.set(ScrollTarget::Top());
  LOKA_VERIFY((NodePropsApplier<ScrollViewNode, ScrollViewProps>::apply(
      &f.node, ScrollViewProps(f.storage.viewport).scrollTo(f.storage.other))));
  LOKA_VERIFY(f.storage.request.get().isNone());
  const ScrollViewProps a = ScrollViewProps().scrollTo(f.storage.request);
  const ScrollViewProps b = ScrollViewProps().scrollTo(f.storage.other);
  LOKA_VERIFY((a < b) != (b < a));
  LOKA_VERIFY(!(a < a));
}
inline void testScrollRequestViewportReplacement()
{
  using namespace scroll_request_tests;
  Fixture f;
  f.storage.request.set(ScrollTarget::Origin(4, 5));
  LOKA_VERIFY((
      NodePropsApplier<ScrollViewNode, ScrollViewProps>::apply(&f.node,
                                                               ScrollViewProps(f.storage.otherViewport)
                                                                   .scrollTo(f.storage.request)
                                                                   .contentInsets(ScrollViewContentInsets::of(3, 4)))));
  LOKA_VERIFY(!f.storage.request.get().isNone());
  f.visit();
  LOKA_VERIFY(f.storage.otherViewport.get() == Frame(4, 5, 80, 30));
  LOKA_VERIFY(f.storage.viewport.get() == Frame(0, 0, 80, 30));
}
inline void testScrollRequestDetachReentry()
{
  using namespace scroll_request_tests;
  Fixture f;
  f.storage.request.set(ScrollTarget::Origin(4, 5));
  NotifySubtreeNodeDetached(&f.node);
  LOKA_VERIFY(f.storage.request.get().isNone());
  NotifySubtreeNodeAttached(&f.node);
  f.visit();
  LOKA_VERIFY(f.storage.viewport.get() == Frame(0, 0, 80, 30));
}
inline void testScrollRequestUnreachedChild()
{
  using namespace scroll_request_tests;
  Fixture f;
  ScrollViewNode parent((ScrollViewProps()));
  ScrollViewNode *child = new ScrollViewNode(f.node.props);
  parent.addChild(child);
  f.storage.request.set(ScrollTarget::Origin(4, 5));
  f.platform.projectLayoutForTesting(&parent, bounds(SHRT_MAX));
  LOKA_VERIFY(!f.storage.request.get().isNone());
  // A later visit reaches the child; nested refusal takes and drops it.
  f.platform.projectLayoutForTesting(&parent, bounds());
  LOKA_VERIFY(f.storage.request.get().isNone());
  LOKA_VERIFY(f.storage.viewport.get() == Frame(0, 0, 80, 30));
}
inline void testScrollRequestRepostBound()
{
  using namespace scroll_request_tests;
  Fixture f;
  Reposter reposter(f.storage.request, 2);
  f.storage.request.state()->bind(&Reposter::changed, &reposter, false);
  f.storage.request.set(ScrollTarget::Origin(0, 10));
  f.visit();
  LOKA_VERIFY(reposter.takes == 2);
  LOKA_VERIFY(f.storage.request.get() == ScrollTarget::Origin(0, 2));
  LOKA_VERIFY(f.storage.viewport.get() == Frame(0, 1, 80, 30));
  f.visit();
  LOKA_VERIFY(reposter.takes == 3);
  LOKA_VERIFY(f.storage.request.get().isNone());
  LOKA_VERIFY(f.storage.viewport.get() == Frame(0, 2, 80, 30));
  f.storage.request.state()->unbind(&Reposter::changed, &reposter);
}
inline void testScrollRequestSceneDeliveryAndPlacement()
{
  using namespace scroll_request_tests;
  Presenter platform;
  Scene scene((Boundary<Root>()));
  scene.mount(&platform);
  Root *root = static_cast<Root *>(loka::dsl::testing::SceneTestAccess::rootBoundary(scene));
  loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);
  for (int i = 0; scene.hasPendingInvalidation() && i < 12; ++i)
    LOKA_VERIFY(scene.flushInvalidation());
  const unsigned long before = platform.leafLayoutVisitsForTesting();
  root->request.set(ScrollTarget::Origin(7, 8));
  LOKA_VERIFY(scene.hasPendingInvalidation());
  for (int i = 0; scene.hasPendingInvalidation() && i < 12; ++i)
    LOKA_VERIFY(scene.flushInvalidation());
  LOKA_VERIFY(root->request.get().isNone());
  LOKA_VERIFY(root->viewport.get() == Frame(7, 8, 80, 30));
  LOKA_VERIFY(platform.leafLayoutVisitsForTesting() - before == 2);
  LOKA_VERIFY(!scene.hasPendingInvalidation());
  loka::dsl::testing::SceneTestAccess::unmount(scene);
}
inline void testScrollRequestSceneEndlessRepost()
{
  using namespace scroll_request_tests;
  Presenter platform;
  Scene scene((Boundary<Root>()));
  scene.mount(&platform);
  Root *root = static_cast<Root *>(loka::dsl::testing::SceneTestAccess::rootBoundary(scene));
  loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);
  for (int i = 0; scene.hasPendingInvalidation() && i < 12; ++i)
    LOKA_VERIFY(scene.flushInvalidation());
  Reposter reposter(root->request, -1);
  root->request.state()->bind(&Reposter::changed, &reposter, false);
  root->request.set(ScrollTarget::Origin(0, 10));
  LOKA_VERIFY(scene.flushInvalidation());
  LOKA_VERIFY(reposter.takes > 0 && !root->request.get().isNone());
  LOKA_VERIFY(scene.hasPendingInvalidation());
  root->request.state()->unbind(&Reposter::changed, &reposter);
  for (int i = 0; scene.hasPendingInvalidation() && i < 12; ++i)
    LOKA_VERIFY(scene.flushInvalidation());
  LOKA_VERIFY(root->request.get().isNone());
  loka::dsl::testing::SceneTestAccess::unmount(scene);
}
inline void testScrollRequestVisitBorrowCoversLayout()
{
  using namespace scroll_request_tests;
  Fixture f;
  f.storage.viewport.set(Frame(0, 0, 0, 0));
  BorrowObserver observer(f.platform);
  f.storage.viewport.state()->bind(&BorrowObserver::changed, &observer, false);
  f.storage.request.set(ScrollTarget::Origin(7, 8));
  f.visit();
  LOKA_VERIFY(observer.calls == 2);
  LOKA_VERIFY(!f.platform.borrowPhase().open());
  f.storage.viewport.state()->unbind(&BorrowObserver::changed, &observer);
}
inline void testScrollRequestVisitApplyRefusal()
{
  using namespace scroll_request_tests;
  VisitController platform(VisitController::REFUSE);
  Storage storage;
  ScrollViewNode node(ScrollViewProps(storage.viewport).scrollTo(storage.request));
  storage.request.set(ScrollTarget::Origin(7, 8));
  VisitTrace::instance().clear();
  SettleScrollViewVisit(platform, &VisitController::layout, &VisitController::apply, &node, bounds());
  LOKA_VERIFY(platform.applications() == 1);
  LOKA_VERIFY(storage.request.get().isNone());
  verifyVisitTrace(SCROLL_UNAVAILABLE, Frame(0, 0, 80, 30), Frame(0, 0, 80, 30));
}
inline void testScrollRequestVisitCreatesContext()
{
  using namespace scroll_request_tests;
  VisitController platform(VisitController::CREATE_CONTEXT);
  Storage storage;
  ScrollViewNode node(ScrollViewProps(storage.viewport).scrollTo(storage.request));
  LOKA_VERIFY(node.getContext() == 0);
  storage.request.set(ScrollTarget::Origin(7, 8));
  VisitTrace::instance().clear();
  SettleScrollViewVisit(platform, &VisitController::layout, &VisitController::apply, &node, bounds());
  LOKA_VERIFY(node.getContext() != 0);
  LOKA_VERIFY(platform.applications() == 1);
  LOKA_VERIFY(storage.request.get().isNone());
  verifyVisitTrace(SCROLL_OK, Frame(0, 0, 80, 30), Frame(21, 22, 80, 30));
}
inline void testScrollRequestVisitDestinationFact()
{
  using namespace scroll_request_tests;
  VisitController platform(VisitController::CHANGE_FACT);
  Storage storage;
  ScrollViewNode node((ScrollViewProps(storage.viewport)));
  const VisitResult result;
  ScrollViewVisitSeat<VisitController, VisitResult> seat(platform, result, &VisitController::apply);
  const RequestApplication<Frame, ScrollRequestResult> applied = seat.apply(node, ScrollTarget::Origin(7, 8));
  LOKA_VERIFY(applied.value() == Frame(21, 22, 80, 30));
  LOKA_VERIFY(applied.result() == SCROLL_OK);
  LOKA_VERIFY(platform.applications() == 1);
}
inline void testScrollRequestVisitFinishSettle()
{
  using namespace scroll_request_tests;
  Fixture f;
  ScrollViewVisitOwner owner(f.platform, &f.node);
  LOKA_VERIFY(owner.finishSettle(f.node, FollowUps()) == FOLLOW_UP_NONE);
}
inline void testScrollRequestVisitFactAfterHelper()
{
  using namespace scroll_request_tests;
  VisitController platform(VisitController::CHANGE_FACT);
  Storage storage;
  ScrollViewNode node(ScrollViewProps(storage.viewport).scrollTo(storage.request));
  storage.request.set(ScrollTarget::Origin(7, 8));
  VisitTrace::instance().clear();
  SettleScrollViewVisit(platform, &VisitController::layout, &VisitController::apply, &node, bounds());
  LOKA_VERIFY(storage.request.get().isNone());
  verifyVisitTrace(SCROLL_OK, Frame(9, 10, 80, 30), Frame(21, 22, 80, 30));
}
inline void testScrollRequestVisitTwoNodes()
{
  using namespace scroll_request_tests;
  NullScenePlatformController platform;
  Storage storage;
  ScrollViewNode first(ScrollViewProps(storage.viewport).scrollTo(storage.request));
  ScrollViewNode second(ScrollViewProps(storage.otherViewport).scrollTo(storage.other));
  storage.otherViewport.set(Frame(3, 4, 80, 30));
  // Both belong to this projection, but only the reached node receives a visit.
  platform.projectLayoutForTesting(&first, bounds());
  platform.projectLayoutForTesting(&second, bounds());
  storage.request.set(ScrollTarget::Origin(7, 8));
  platform.projectLayoutForTesting(&first, bounds());
  LOKA_VERIFY(storage.viewport.get() == Frame(7, 8, 80, 30));
  LOKA_VERIFY(storage.request.get().isNone());
  LOKA_VERIFY(storage.otherViewport.get() == Frame(3, 4, 80, 30));
  LOKA_VERIFY(storage.other.get().isNone());
  // A pre-existing pending request on the unreached node is also preserved.
  storage.other.set(ScrollTarget::Origin(31, 32));
  storage.request.set(ScrollTarget::Origin(11, 12));
  platform.projectLayoutForTesting(&first, bounds());
  LOKA_VERIFY(storage.viewport.get() == Frame(11, 12, 80, 30));
  LOKA_VERIFY(storage.request.get().isNone());
  LOKA_VERIFY(storage.otherViewport.get() == Frame(3, 4, 80, 30));
  LOKA_VERIFY(storage.other.get() == ScrollTarget::Origin(31, 32));
}
#endif
