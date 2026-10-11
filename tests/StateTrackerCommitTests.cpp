#include "testing/core/StateTrackerTestAccess.hpp"
#include "testing/scene/SceneTestFlow.hpp"
#include "StateTrackerCommitTests.hpp"
#include <cassert>
#include <cstdio>
#include "core/State.hpp"
#include "core/StateTracker.hpp"
#include "core/util/StateTrackerGuard.hpp"
#include "app/scene/Scene.hpp"
#include "app/scene/boundary/Boundary.hpp"
#include "app/scene/projection/PlatformController.hpp"
#include "app/nodes/boundary/StdComposition.hpp"
#include "app/nodes/nestable/Show.hpp"
#include "app/nodes/controls/Button.hpp"

namespace
{
  struct TrackerCommitProbe
  {
    TrackerCommitProbe()
        : tracker(0),
          initialState(0),
          commitWindowState(0),
          invalidations(0),
          deferredCalls(0)
    {
    }

    loka::core::PushStateTracker *tracker;
    loka::core::MutableState<int> *initialState;
    loka::core::MutableState<int> *commitWindowState;
    int invalidations;
    int deferredCalls;
  };

  bool containsState(const loka::core::PushStateTracker::StateList &states,
                     const loka::core::StateBase *state)
  {
    for (size_t i = 0; i < states.size(); ++i)
    {
      if (states[i] == state)
      {
        return true;
      }
    }
    return false;
  }

  void countDeferredCommitWork(void *userData)
  {
    TrackerCommitProbe *probe = static_cast<TrackerCommitProbe *>(userData);
    ++probe->deferredCalls;
  }

  void deferFromCommitWindowObserver(void *userData)
  {
    TrackerCommitProbe *probe = static_cast<TrackerCommitProbe *>(userData);
    loka::core::testing::PushStateTrackerTestAccess::defer(*probe->tracker, &countDeferredCommitWork, probe);
  }

  void writeFromTrackerInvalidate(void *userData)
  {
    TrackerCommitProbe *probe = static_cast<TrackerCommitProbe *>(userData);
    ++probe->invalidations;
    const loka::core::PushStateTracker::StateList &committed =
        probe->tracker->committedSet().identities();
    if (probe->invalidations == 1)
    {
      (void)committed;
      assert(containsState(committed, probe->initialState));
      assert(!containsState(committed, probe->commitWindowState));
      probe->commitWindowState->set(1);
      return;
    }
    assert(probe->invalidations == 2);
    assert(!containsState(committed, probe->initialState));
    assert(containsState(committed, probe->commitWindowState));
  }

  struct TrackerCommitLimitProbe
  {
    TrackerCommitLimitProbe()
        : state(0),
          invalidations(0)
    {
    }

    loka::core::MutableState<int> *state;
    int invalidations;
  };

  void keepWritingFromTrackerInvalidate(void *userData)
  {
    TrackerCommitLimitProbe *probe =
        static_cast<TrackerCommitLimitProbe *>(userData);
    ++probe->invalidations;
    probe->state->set(probe->state->get() + 1);
  }

  class IncrementedStateEval : public loka::core::DerivedState<int>::EvalFn
  {
  public:
    explicit IncrementedStateEval(loka::core::State<int> *source)
        : source_(source)
    {
    }

    virtual int operator()()
    {
      return this->source_->get() + 1;
    }

  private:
    loka::core::State<int> *source_;
  };

  void incrementGuardInvalidations(void *userData)
  {
    int *invalidations = static_cast<int *>(userData);
    ++*invalidations;
  }

  struct SettlingGuardProbe
  {
    SettlingGuardProbe()
        : tracker(0),
          derived(0),
          written(0),
          invalidations(0)
    {
    }

    loka::core::PushStateTracker *tracker;
    loka::core::State<int> *derived;
    loka::core::MutableState<int> *written;
    int invalidations;
  };

  void writeFromSettlingDerived(void *userData)
  {
    SettlingGuardProbe *probe = static_cast<SettlingGuardProbe *>(userData);
    {
      loka::core::StateTrackerGuard guard(
          probe->tracker, &incrementGuardInvalidations, &probe->invalidations);
      probe->written->set(probe->derived->get());
    }
    // A joined level is not a closed transaction: nothing may acknowledge the
    // outer settlement's dirt from inside it (MenuComposition::declare() is the
    // manual begin/end/peekDirty client this protects).
    assert(!probe->tracker->peekDirty());
    assert(!probe->tracker->consumeDirty());
  }

  struct CommitGuardProbe
  {
    CommitGuardProbe()
        : tracker(0),
          source(0),
          written(0),
          invalidations(0)
    {
    }

    loka::core::PushStateTracker *tracker;
    loka::core::State<int> *source;
    loka::core::MutableState<int> *written;
    int invalidations;
  };

  void writeFromDeferredCommit(void *userData)
  {
    CommitGuardProbe *probe = static_cast<CommitGuardProbe *>(userData);
    {
      loka::core::StateTrackerGuard guard(
          probe->tracker, &incrementGuardInvalidations, &probe->invalidations);
      probe->written->set(probe->source->get());
    }
    assert(!probe->tracker->peekDirty());
    assert(!probe->tracker->consumeDirty());
  }

  using namespace loka::app::scene;

  class CommitWindowBoundaryNode;
  typedef BoundaryPropsFor<CommitWindowBoundaryNode> CommitWindowBoundaryProps;
  CommitWindowBoundaryNode *g_commitWindowBoundary = 0;

  class CommitWindowBoundaryNode : public BoundaryNodeFor<CommitWindowBoundaryNode>
  {
  public:
    explicit CommitWindowBoundaryNode(const CommitWindowBoundaryProps &props)
        : BoundaryNodeFor<CommitWindowBoundaryNode>(props),
          shown_()
    {
      this->state(this->shown_, true);
      g_commitWindowBoundary = this;
    }

    virtual ~CommitWindowBoundaryNode()
    {
      if (g_commitWindowBoundary == this)
      {
        g_commitWindowBoundary = 0;
      }
    }

    virtual void composeNode(NodeComposition &composition)
    {
      using namespace loka::app;
      composition.declare(Show(*this->shown_.state()) << Button("commit-window"));
    }

    virtual void composeWithContext(ComponentContext &context, ComposeEvent event)
    {
      if (event == COMPOSE_EVENT_UPDATE && this->isApplyingPlatform())
      {
        ++updatesDuringApply;
      }
      BoundaryNodeFor<CommitWindowBoundaryNode>::composeWithContext(context, event);
    }

    void toggle()
    {
      this->shown_.set(!this->shown_.get());
    }

    static int updatesDuringApply;

  private:
    NodeState<bool> shown_;
  };

  int CommitWindowBoundaryNode::updatesDuringApply = 0;

  class CommitWindowPlatformController : public IPlatformController
  {
  public:
    CommitWindowPlatformController()
        : toggleOnNextChange(false),
          onChangeCalls(0),
          nestedCallsDuringToggle(-1)
    {
    }

    virtual void onChange(Node *, NodeDirtyFlags, bool)
    {
      ++this->onChangeCalls;
      if (!this->toggleOnNextChange || !g_commitWindowBoundary)
      {
        return;
      }
      this->toggleOnNextChange = false;
      const int callsBeforeToggle = this->onChangeCalls;
      g_commitWindowBoundary->toggle();
      this->nestedCallsDuringToggle = this->onChangeCalls - callsBeforeToggle;
    }

    virtual void synchronize() {}
    virtual bool hasPendingSync() const { return false; }
    virtual void destroy() {}

    bool toggleOnNextChange;
    int onChangeCalls;
    int nestedCallsDuringToggle;
  };
} // namespace

void testStateTrackerCommitQueuesNextTransaction()
{
  (void)&containsState;
  printf("\n==== [testStateTrackerCommitQueuesNextTransaction] start ====\n");
  // #60: a state write from invalidate belongs to a distinct next commit.
  loka::core::MutableState<int> initialState(0);
  loka::core::MutableState<int> commitWindowState(0);
  loka::core::PushStateTracker tracker;
  TrackerCommitProbe probe;
  probe.tracker = &tracker;
  probe.initialState = &initialState;
  probe.commitWindowState = &commitWindowState;
  tracker.addState(&initialState);
  tracker.addState(&commitWindowState);
  tracker.setInvalidateCallback(&writeFromTrackerInvalidate, &probe);
  commitWindowState.bind(&deferFromCommitWindowObserver, &probe, false);

  tracker.begin();
  initialState.set(1);
  const bool settled = tracker.end();

  (void)settled;
  assert(settled);
  assert(probe.invalidations == 2);
  assert(probe.deferredCalls == 1);
  assert(commitWindowState.get() == 1);
  assert(tracker.phase() == loka::core::TRACKER_IDLE);
  printf("==== [testStateTrackerCommitQueuesNextTransaction] end ====\n");
}

void testStateTrackerCommitWriteReachesNextSceneApply()
{
  printf("\n==== [testStateTrackerCommitWriteReachesNextSceneApply] start ====\n");
  // #60: the queued commit must reach Scene without re-entering platform apply.
  CommitWindowBoundaryNode::updatesDuringApply = 0;
  loka::app::scene::Scene scene(
      (loka::app::scene::Boundary<CommitWindowBoundaryNode>()));
  CommitWindowPlatformController platform;
  scene.mount(&platform);
  loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);
  assert(g_commitWindowBoundary != 0);

  const int callsBeforeWrite = platform.onChangeCalls;
  platform.toggleOnNextChange = true;
  g_commitWindowBoundary->toggle();
  scene.flushInvalidation();

  assert(!platform.toggleOnNextChange);
  assert(platform.nestedCallsDuringToggle == 0);
  assert(CommitWindowBoundaryNode::updatesDuringApply == 0);
  (void)callsBeforeWrite;
  assert(platform.onChangeCalls == callsBeforeWrite + 2);

  loka::dsl::testing::SceneTestAccess::updateAttached(scene, false);
  loka::dsl::testing::SceneTestAccess::unmount(scene);
  printf("==== [testStateTrackerCommitWriteReachesNextSceneApply] end ====\n");
}

void testStateTrackerCommitChainReportsIterationLimit()
{
  printf("\n==== [testStateTrackerCommitChainReportsIterationLimit] start ====\n");
  loka::core::MutableState<int> state(0);
  loka::core::PushStateTracker tracker;
  TrackerCommitLimitProbe probe;
  probe.state = &state;
  tracker.addState(&state);
  tracker.setInvalidateCallback(&keepWritingFromTrackerInvalidate, &probe);

  tracker.begin();
  state.set(1);
  const bool settled = tracker.end();

  (void)settled;
  assert(!settled);
  assert(probe.invalidations == 1000);
  assert(state.get() == 1001);
  assert(tracker.phase() == loka::core::TRACKER_IDLE);
  printf("==== [testStateTrackerCommitChainReportsIterationLimit] end ====\n");
}

void testStateTrackerGuardOpenedDuringSettlementJoinsTransaction()
{
  printf("\n==== [testStateTrackerGuardOpenedDuringSettlementJoinsTransaction] start ====\n");
  loka::core::MutableState<int> a(1);
  loka::core::DerivedState<int> d(&a, new IncrementedStateEval(&a));
  loka::core::MutableState<int> b(1);
  loka::core::DerivedState<int> e(&b, new IncrementedStateEval(&b));
  loka::core::PushStateTracker tracker;
  SettlingGuardProbe probe;
  probe.tracker = &tracker;
  probe.derived = &d;
  probe.written = &b;
  tracker.addState(&a);
  tracker.addState(&d);
  tracker.addState(&b);
  tracker.addState(&e);
  d.bind(&writeFromSettlingDerived, &probe, false);

  {
    loka::core::StateTrackerGuard guard(
        &tracker, &incrementGuardInvalidations, &probe.invalidations);
    a.set(4);
  }

  assert(d.get() == 5);
  assert(b.get() == 5);
  assert(e.get() == 6);
  assert(probe.invalidations == 1);
  assert(tracker.phase() == loka::core::TRACKER_IDLE);
  // The dirt the joined level could not consume is still pending for the owner.
  assert(tracker.peekDirty());
  assert(tracker.consumeDirty());
  assert(!tracker.peekDirty());

  {
    loka::core::StateTrackerGuard guard(&tracker);
    a.set(7);
  }

  assert(d.get() == 8);
  assert(e.get() == 9);
  assert(tracker.phase() == loka::core::TRACKER_IDLE);
  printf("==== [testStateTrackerGuardOpenedDuringSettlementJoinsTransaction] end ====\n");
}

void testStateTrackerGuardOpenedDuringCommitJoinsTransaction()
{
  printf("\n==== [testStateTrackerGuardOpenedDuringCommitJoinsTransaction] start ====\n");
  loka::core::MutableState<int> a(1);
  loka::core::MutableState<int> b(1);
  loka::core::DerivedState<int> e(&b, new IncrementedStateEval(&b));
  loka::core::PushStateTracker tracker;
  CommitGuardProbe probe;
  probe.tracker = &tracker;
  probe.source = &a;
  probe.written = &b;
  tracker.addState(&a);
  tracker.addState(&b);
  tracker.addState(&e);

  {
    loka::core::StateTrackerGuard guard(
        &tracker, &incrementGuardInvalidations, &probe.invalidations);
    a.set(10);
    loka::core::testing::PushStateTrackerTestAccess::defer(tracker, &writeFromDeferredCommit, &probe);
  }

  assert(b.get() == 10);
  assert(e.get() == 11);
  assert(probe.invalidations == 1);
  assert(tracker.phase() == loka::core::TRACKER_IDLE);
  printf("==== [testStateTrackerGuardOpenedDuringCommitJoinsTransaction] end ====\n");
}

namespace
{
  struct RefusalDeferredProbe
  {
    loka::core::PushStateTracker *tracker;
    loka::core::MutableState<int> *source;
    int invalidations;
    int deferredCalls;
  };

  void countRefusalDeferred(void *data)
  {
    ++static_cast<RefusalDeferredProbe *>(data)->deferredCalls;
  }

  void deferOnLastCommit(void *data)
  {
    RefusalDeferredProbe &probe = *static_cast<RefusalDeferredProbe *>(data);
    ++probe.invalidations;
    probe.source->set(probe.source->get() + 1);
    if (probe.invalidations == 1000)
      loka::core::testing::PushStateTrackerTestAccess::defer(
          *probe.tracker, &countRefusalDeferred, &probe);
  }

  struct UnsettledState : loka::core::StateBase
  {
    loka::core::PushStateTracker *tracker;
    explicit UnsettledState(loka::core::PushStateTracker *t) : tracker(t) {}
    bool recompute()
    {
      this->tracker->markDirty(this);
      return false;
    }
  };
}

void testStateTrackerRefusalRunsLeftoverDeferred()
{
  loka::core::MutableState<int> source(0);
  loka::core::PushStateTracker tracker;
  RefusalDeferredProbe probe = { &tracker, &source, 0, 0 };
  tracker.addState(&source);
  tracker.setInvalidateCallback(&deferOnLastCommit, &probe);
  tracker.begin();
  source.set(1);
  const bool settled = tracker.end();
  (void)settled;
  assert(!settled);
  assert(probe.invalidations == 1000);
  assert(source.get() == 1001);
  assert(probe.deferredCalls == 1);
  tracker.begin();
  const bool nextSettled = tracker.end();
  (void)nextSettled;
  assert(nextSettled);
  assert(probe.deferredCalls == 1);
}

void testStateTrackerStateBudgetRunsCurrentDeferred()
{
  loka::core::PushStateTracker tracker;
  UnsettledState source(&tracker);
  RefusalDeferredProbe probe = { &tracker, 0, 0, 0 };
  tracker.addState(&source);
  tracker.begin();
  tracker.markDirty(&source);
  loka::core::testing::PushStateTrackerTestAccess::defer(
      tracker, &countRefusalDeferred, &probe);
  const bool settled = tracker.end();
  (void)settled;
  assert(!settled);
  assert(probe.deferredCalls == 1);
  assert(tracker.phase() == loka::core::TRACKER_IDLE);
  assert(source.trackerOwner() == 0);
}

void testStateTrackerLegacyEndPreservesCompletedSnapshot()
{
  loka::core::MutableState<int> source(0);
  loka::core::PushStateTracker tracker;
  tracker.addState(&source);
  tracker.begin();
  source.set(1);
  const bool settled = tracker.end();
  (void)settled;
  assert(settled);
  assert(tracker.committedSet().identities().size() == 1);
  assert(tracker.committedSet().identities()[0] == &source);
  assert(tracker.transactionDirty());
  assert(tracker.peekDirty());
}
