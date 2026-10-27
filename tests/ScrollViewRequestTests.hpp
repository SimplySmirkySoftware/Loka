#ifndef LOKA_TESTS_SCROLL_VIEW_REQUEST_TESTS_HPP
#define LOKA_TESTS_SCROLL_VIEW_REQUEST_TESTS_HPP
#include "app/nodes/nestable/ScrollView.hpp"
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
#endif
