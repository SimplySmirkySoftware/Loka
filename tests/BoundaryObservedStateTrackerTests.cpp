#include "testing/core/StateTrackerTestAccess.hpp"
#include "BoundaryObservedStateTrackerTests.hpp"
#include "support/TestVerify.hpp"
#include "testing/scene/SceneTestFlow.hpp"
#include "testing/scene/NodeObservedUsesTestAccess.hpp"

#include <cassert>
#include <cstdio>
#include <cstring>
#if defined(__linux__)
#include <unistd.h>
#endif

#include "app/nodes/boundary/StdComposition.hpp"
#include "app/nodes/controls/Button.hpp"
#include "app/nodes/nestable/RowColumn.hpp"
#include "app/scene/Scene.hpp"
#include "app/scene/projection/PlatformController.hpp"
#include "core/State.hpp"
#include "core/util/StateTrackerGuard.hpp"

namespace
{
  class DoubleClockParentBoundaryNode;
  class DoubleClockChildBoundaryNode;

  struct DoubleClockTrace
  {
    DoubleClockTrace()
        : parent(0),
          child(0),
          observed(0),
          parentTracker(0),
          childTracker(0),
          ownerDuringParent(0),
          ownerDuringChild(0),
          ownerAfterChild(0),
          ownerBeforeSecondWrite(0)
    {
    }

    DoubleClockParentBoundaryNode *parent;
    DoubleClockChildBoundaryNode *child;
    loka::core::State<bool> *observed;
    loka::core::StateTracker *parentTracker;
    loka::core::StateTracker *childTracker;
    loka::core::StateTracker *ownerDuringParent;
    loka::core::StateTracker *ownerDuringChild;
    loka::core::StateTracker *ownerAfterChild;
    loka::core::StateTracker *ownerBeforeSecondWrite;
  };

  struct DoubleClockChildTypeTag
  {
  };

  struct DoubleClockChildProps
      : public loka::app::scene::NodePropsBase<DoubleClockChildProps>
  {
    typedef DoubleClockChildTypeTag TypeTag;
    typedef DoubleClockChildBoundaryNode NodeType;

    DoubleClockChildProps(loka::core::State<bool> *observedState = 0,
                          DoubleClockTrace *traceValue = 0)
        : observed(observedState),
          trace(traceValue)
    {
    }

    bool operator<(const loka::app::scene::PropsBase &rhs) const
    {
      if (rhs.propsTypeId() != this->propsTypeId())
      {
        return false;
      }
      const DoubleClockChildProps &other =
          static_cast<const DoubleClockChildProps &>(rhs);
      if (this->observed != other.observed)
      {
        return this->observed < other.observed;
      }
      return this->trace < other.trace;
    }

    loka::core::State<bool> *observed;
    DoubleClockTrace *trace;
  };

  void recordObservedOwnerDuringChildSettlement(void *userData)
  {
    DoubleClockTrace *trace = static_cast<DoubleClockTrace *>(userData);
    assert(trace);
    assert(trace->observed);
    trace->ownerDuringChild = trace->observed->trackerOwner();
    std::fprintf(stderr,
                 "double-clock stamp during child settlement: observed=%p owner=%p child=%p\n",
                 static_cast<void *>(trace->observed),
                 static_cast<void *>(trace->ownerDuringChild),
                 static_cast<void *>(trace->childTracker));
  }

  class DoubleClockChildBoundaryNode
      : public loka::app::scene::StdCompositionBoundaryNodeBase<DoubleClockChildProps>
  {
  public:
    explicit DoubleClockChildBoundaryNode(const DoubleClockChildProps &props)
        : loka::app::scene::StdCompositionBoundaryNodeBase<DoubleClockChildProps>(props),
          local_()
    {
      this->state(this->local_, 0);
      if (this->props.trace)
      {
        this->props.trace->child = this;
        this->props.trace->childTracker = this->tracker();
      }
    }

    virtual ~DoubleClockChildBoundaryNode()
    {
      if (this->props.trace && this->props.trace->child == this)
      {
        this->props.trace->child = 0;
      }
    }

    virtual bool flushViewDirtyImmediately(loka::app::scene::NodeDirtyFlags) const
    {
      return false;
    }

    virtual void attachNode(loka::app::scene::NodeComposition &composition)
    {
      (void)composition;
      this->local_.bind(&recordObservedOwnerDuringChildSettlement,
                        this->props.trace,
                        false);
    }

    virtual void composeNode(loka::app::scene::NodeComposition &composition)
    {
      composition.declare(
          loka::app::Button("observed-parent-state").enabled(this->props.observed));
    }

    void setLocal(int value)
    {
      this->local_.set(value);
    }

  private:
    loka::app::scene::NodeState<int> local_;
  };

  class ObservedAsIntEval : public loka::core::DerivedState<int>::EvalFn
  {
  public:
    explicit ObservedAsIntEval(loka::core::State<bool> *observed)
        : observed_(observed)
    {
    }

    virtual int operator()()
    {
      return this->observed_->get() ? 2 : 1;
    }

  private:
    loka::core::State<bool> *observed_;
  };

  struct DoubleClockParentTypeTag
  {
  };

  struct DoubleClockParentProps
      : public loka::app::scene::NodePropsBase<DoubleClockParentProps>
  {
    typedef DoubleClockParentTypeTag TypeTag;
    typedef DoubleClockParentBoundaryNode NodeType;

    explicit DoubleClockParentProps(DoubleClockTrace *traceValue = 0)
        : trace(traceValue)
    {
    }

    bool operator<(const loka::app::scene::PropsBase &rhs) const
    {
      if (rhs.propsTypeId() != this->propsTypeId())
      {
        return false;
      }
      const DoubleClockParentProps &other =
          static_cast<const DoubleClockParentProps &>(rhs);
      return this->trace < other.trace;
    }

    DoubleClockTrace *trace;
  };

  void setChildStateFromParentCommit(void *userData)
  {
    DoubleClockTrace *trace = static_cast<DoubleClockTrace *>(userData);
    assert(trace);
    assert(trace->child);
    assert(trace->observed);
    trace->child->setLocal(1);
    trace->ownerAfterChild = trace->observed->trackerOwner();
    std::fprintf(stderr,
                 "double-clock stamp after child end: observed=%p owner=%p\n",
                 static_cast<void *>(trace->observed),
                 static_cast<void *>(trace->ownerAfterChild));
  }

  void setObservedStateAgainFromParentCommit(void *userData);

  class DoubleClockParentBoundaryNode
      : public loka::app::scene::StdCompositionBoundaryNodeBase<DoubleClockParentProps>
  {
  public:
    explicit DoubleClockParentBoundaryNode(const DoubleClockParentProps &props)
        : loka::app::scene::StdCompositionBoundaryNodeBase<DoubleClockParentProps>(props),
          observed_(),
          derived_(0)
    {
      this->state(this->observed_, false);
      if (this->props.trace)
      {
        this->props.trace->parent = this;
        this->props.trace->parentTracker = this->tracker();
      }
    }

    virtual ~DoubleClockParentBoundaryNode()
    {
      if (this->props.trace && this->props.trace->parent == this)
      {
        this->props.trace->parent = 0;
      }
    }

    virtual bool flushViewDirtyImmediately(loka::app::scene::NodeDirtyFlags) const
    {
      return false;
    }

    virtual void attachNode(loka::app::scene::NodeComposition &composition)
    {
      (void)composition;
      if (!this->derived_)
      {
        this->derived_ = new loka::core::DerivedState<int>(
            this->observed_.state(),
            new ObservedAsIntEval(this->observed_.state()));
        this->adoptState(this->derived_);
      }
      if (this->props.trace)
      {
        this->props.trace->observed = this->observed_.state();
      }
    }

    virtual void composeNode(loka::app::scene::NodeComposition &composition)
    {
      composition.declare(
          loka::app::scene::Boundary<DoubleClockChildBoundaryNode>(
              DoubleClockChildProps(this->observed_.state(), this->props.trace)));
    }

    void runCommitChain()
    {
      assert(this->props.trace);
      {
        loka::core::StateTrackerGuard guard(this->tracker());
        this->props.trace->ownerDuringParent =
            this->observed_.state()->trackerOwner();
        std::fprintf(stderr,
                     "double-clock stamp in parent transaction: observed=%p owner=%p parent=%p\n",
                     static_cast<void *>(this->observed_.state()),
                     static_cast<void *>(this->props.trace->ownerDuringParent),
                     static_cast<void *>(this->tracker()));
        loka::core::testing::PushStateTrackerTestAccess::defer(*this->tracker(), &setChildStateFromParentCommit, this->props.trace);
        loka::core::testing::PushStateTrackerTestAccess::defer(*this->tracker(), &setObservedStateAgainFromParentCommit, this->props.trace);
      }
    }

    void setObserved(bool value)
    {
      this->observed_.set(value);
    }

    int derivedValue() const
    {
      assert(this->derived_);
      return this->derived_ ? this->derived_->get() : 0;
    }

    bool observedValue() const
    {
      return this->observed_.get();
    }

  private:
    loka::app::scene::NodeState<bool> observed_;
    loka::core::DerivedState<int> *derived_;
  };

  void setObservedStateAgainFromParentCommit(void *userData)
  {
    DoubleClockTrace *trace = static_cast<DoubleClockTrace *>(userData);
    assert(trace);
    assert(trace->parent);
    assert(trace->observed);
    trace->ownerBeforeSecondWrite = trace->observed->trackerOwner();
    std::fprintf(stderr,
                 "double-clock stamp before second parent write: observed=%p owner=%p\n",
                 static_cast<void *>(trace->observed),
                 static_cast<void *>(trace->ownerBeforeSecondWrite));
    trace->parent->setObserved(true);
  }

  class DoubleClockPlatformController
      : public loka::app::scene::IPlatformController
  {
  public:
    virtual void onChange(loka::app::scene::Node *,
                          loka::app::scene::NodeDirtyFlags,
                          bool)
    {
    }
    virtual void synchronize() {}
    virtual bool hasPendingSync() const { return false; }
    virtual void destroy() {}
  };
} // namespace

void testObservedStateDoesNotJoinChildBoundaryTracker()
{
  std::printf("\n==== [testObservedStateDoesNotJoinChildBoundaryTracker] start ====\n");
  DoubleClockTrace trace;
  DoubleClockPlatformController platform;
  loka::app::scene::Scene scene(
      loka::app::scene::Boundary<DoubleClockParentBoundaryNode>(
          DoubleClockParentProps(&trace)));
  scene.mount(&platform);
  loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);

  assert(trace.parent);
  assert(trace.child);
  assert(trace.observed);
  assert(trace.parentTracker);
  assert(trace.childTracker);
  assert(trace.parent->derivedValue() == 1);

  trace.parent->runCommitChain();

  std::fprintf(stderr,
               "double-clock result: observed=%d derived=%d expected=%d\n",
               trace.parent->observedValue() ? 1 : 0,
               trace.parent->derivedValue(),
               trace.parent->observedValue() ? 2 : 1);

  assert(trace.parent->derivedValue() ==
         (trace.parent->observedValue() ? 2 : 1));
  assert(trace.ownerDuringParent == trace.parentTracker);
  assert(trace.ownerDuringChild == trace.parentTracker);
  assert(trace.ownerAfterChild == trace.parentTracker);
  assert(trace.ownerBeforeSecondWrite == trace.parentTracker);

  loka::dsl::testing::SceneTestAccess::updateAttached(scene, false);
  loka::dsl::testing::SceneTestAccess::unmount(scene);
  std::printf("==== [testObservedStateDoesNotJoinChildBoundaryTracker] end ====\n");
}

namespace
{
  class LifetimeGuardBoundary : public loka::app::scene::BoundaryNodeFor<LifetimeGuardBoundary>
  {
  public:
    explicit LifetimeGuardBoundary(const loka::app::scene::BoundaryPropsFor<LifetimeGuardBoundary> &props
        = loka::app::scene::BoundaryPropsFor<LifetimeGuardBoundary>())
        : loka::app::scene::BoundaryNodeFor<LifetimeGuardBoundary>(props) {}
    virtual void composeNode(loka::app::scene::NodeComposition &) {}
  };
  void lifetimeGuardChanged(void *) {}
}

void testObservedStateGuardSharesRegistrationTokenAndUnbindsLate()
{
  using namespace loka::app::scene;
  using loka::core::StateBase;
  using loka::dsl::testing::BoundaryObservedStateTestAccess;
  LifetimeGuardBoundary boundary;
  BoundaryObservedState observed;
  loka::core::MutableState<int> *state = new loka::core::MutableState<int>(0);
  observed.beginPass();
  LOKA_VERIFY(observed.registerState(&boundary, state, NODE_DIRTY_PROPS, &lifetimeGuardChanged, &boundary));
  const BoundaryObservedStateBinding *binding = BoundaryObservedStateTestAccess::firstBinding(observed);
  LOKA_VERIFY(binding != 0);
  LOKA_VERIFY(binding->stateLifetimeToken != 0);
  void *registered = state->retainExternalLifetimeToken();
  LOKA_VERIFY(binding->stateLifetimeToken == registered);
  LOKA_VERIFY(StateBase::isExternalLifetimeTokenAlive(registered));
  delete state;
  LOKA_VERIFY(!StateBase::isExternalLifetimeTokenAlive(binding->stateLifetimeToken));
  observed.clearEntries(&lifetimeGuardChanged);
  LOKA_VERIFY(BoundaryObservedStateTestAccess::entryCount(observed) == 0);
  LOKA_VERIFY(!StateBase::isExternalLifetimeTokenAlive(registered));
  StateBase::releaseExternalLifetimeToken(registered);
}

namespace
{
  struct RemoveCommittedState
  {
    loka::core::PushStateTracker *tracker;
    loka::core::StateBase *state;
    static void run(void *data)
    {
      RemoveCommittedState *removal = static_cast<RemoveCommittedState *>(data);
      removal->tracker->removeState(removal->state);
    }
  };

  class UnobservedCommitBoundary
      : public loka::app::scene::BoundaryNodeFor<UnobservedCommitBoundary>
  {
  public:
    explicit UnobservedCommitBoundary(
        const loka::app::scene::BoundaryPropsFor<UnobservedCommitBoundary> &props)
        : loka::app::scene::BoundaryNodeFor<UnobservedCommitBoundary>(props)
    {
      this->state(this->layout, loka::app::STACK_AXIS_COLUMN);
      this->state(this->paint, true);
      this->state(this->privateCount, 0);
    }
    virtual bool flushViewDirtyImmediately(loka::app::scene::NodeDirtyFlags) const
    {
      return false;
    }
    virtual void composeNode(loka::app::scene::NodeComposition &composition)
    {
      composition.declare(loka::app::Stack(this->layout.state())
                          << loka::app::Button("paint").enabled(this->paint.state()));
    }
    loka::app::scene::NodeState<loka::app::StackAxis> layout;
    loka::app::scene::NodeState<bool> paint;
    loka::app::scene::NodeState<int> privateCount;
  };
}

void testUnobservedCommitDoesNotDirtyBoundary()
{
  using namespace loka::app::scene;
  using loka::dsl::testing::SceneTestAccess;
  DoubleClockPlatformController platform;
  Scene scene((Boundary<UnobservedCommitBoundary>()));
  scene.mount(&platform);
  SceneTestAccess::updateAttached(scene, true);
  UnobservedCommitBoundary *boundary =
      static_cast<UnobservedCommitBoundary *>(SceneTestAccess::rootBoundary(scene));
  LOKA_VERIFY(boundary != 0);
  LOKA_VERIFY(loka::app::testing::NodeObservedUsesTestAccess::mark(*boundary) == NODE_DIRTY_NONE);
  LOKA_VERIFY(SceneTestAccess::director(scene).pendingDirtyFlagsForBoundary(boundary) == NODE_DIRTY_NONE);
  LOKA_VERIFY(!scene.hasPendingInvalidation());
#if defined(__linux__) && !defined(NDEBUG)
  // Follow the existing StateTracker diagnostic capture fixture.
  std::FILE *capture = std::tmpfile();
  LOKA_VERIFY(capture != 0);
  const int saved = dup(fileno(stderr));
  LOKA_VERIFY(saved >= 0);
  LOKA_VERIFY(std::fflush(stderr) == 0);
  LOKA_VERIFY(dup2(fileno(capture), fileno(stderr)) >= 0);
#endif
  {
    loka::core::StateTrackerGuard guard(boundary->tracker());
    boundary->privateCount.set(1);
  }
#if defined(__linux__) && !defined(NDEBUG)
  LOKA_VERIFY(std::fflush(stderr) == 0);
  LOKA_VERIFY(dup2(saved, fileno(stderr)) >= 0);
  LOKA_VERIFY(close(saved) == 0);
  std::rewind(capture);
  char diagnostic[512];
  const size_t bytes = std::fread(diagnostic, 1, sizeof(diagnostic) - 1, capture);
  diagnostic[bytes] = 0;
  char expected[256];
  std::sprintf(expected, "committed StateBase %p is not observed by this Boundary",
               static_cast<void *>(boundary->privateCount.state()));
  LOKA_VERIFY(std::strstr(diagnostic, expected) != 0);
  LOKA_VERIFY(std::fclose(capture) == 0);
#else
  std::printf("[skip] unobserved State diagnostic capture requires Linux debug; routing pins still run.\n");
#endif
  std::fprintf(stderr, "unobserved commit: dirty=%u pending=%d\n",
               static_cast<unsigned int>(SceneTestAccess::director(scene).pendingDirtyFlagsForBoundary(boundary)),
               scene.hasPendingInvalidation() ? 1 : 0);
  LOKA_VERIFY(loka::app::testing::NodeObservedUsesTestAccess::mark(*boundary) == NODE_DIRTY_NONE);
  LOKA_VERIFY(SceneTestAccess::director(scene).pendingDirtyFlagsForBoundary(boundary) == NODE_DIRTY_NONE);
  LOKA_VERIFY(!scene.hasPendingInvalidation());
  boundary->layout.set(loka::app::STACK_AXIS_ROW);
  LOKA_VERIFY(SceneTestAccess::director(scene).pendingDirtyFlagsForBoundary(boundary) == NODE_DIRTY_LAYOUT);
  LOKA_VERIFY(scene.hasPendingInvalidation());
  scene.flushInvalidation();
  boundary->paint.set(false);
  LOKA_VERIFY(SceneTestAccess::director(scene).pendingDirtyFlagsForBoundary(boundary) == NODE_DIRTY_PROPS);
  LOKA_VERIFY(scene.hasPendingInvalidation());
  scene.flushInvalidation();

  // A mixed commit contributes only its observed source, even if the private
  // source is processed before or after it.
  {
    loka::core::StateTrackerGuard guard(boundary->tracker());
    boundary->privateCount.set(2);
    boundary->paint.set(true);
  }
  LOKA_VERIFY(SceneTestAccess::director(scene).pendingDirtyFlagsForBoundary(boundary) == NODE_DIRTY_PROPS);
  scene.flushInvalidation();

  // Deferred removal erases the last identity before the tracker callback.
  RemoveCommittedState removal;
  removal.tracker = boundary->tracker()->asPushTracker();
  removal.state = boundary->privateCount.state();
  {
    loka::core::StateTrackerGuard guard(boundary->tracker());
    boundary->privateCount.set(3);
    loka::core::testing::PushStateTrackerTestAccess::defer(*boundary->tracker(), &RemoveCommittedState::run, &removal);
  }
  LOKA_VERIFY(removal.tracker->committedSet().identities().empty());
  LOKA_VERIFY(SceneTestAccess::director(scene).pendingDirtyFlagsForBoundary(boundary) ==
              static_cast<NodeDirtyFlags>(NODE_DIRTY_LAYOUT | NODE_DIRTY_PROPS));
  scene.flushInvalidation();

  // Deferred removal that erases one of several committed identities leaves a
  // nonempty but incomplete set: the Boundary must fall back to the union, not
  // apply only the survivor's flags (a removed LAYOUT source must not downgrade
  // the update to PROPS).
  RemoveCommittedState partialRemoval;
  partialRemoval.tracker = boundary->tracker()->asPushTracker();
  partialRemoval.state = boundary->layout.state();
  {
    loka::core::StateTrackerGuard guard(boundary->tracker());
    boundary->layout.set(loka::app::STACK_AXIS_ROW);
    boundary->paint.set(false);
    loka::core::testing::PushStateTrackerTestAccess::defer(*boundary->tracker(), &RemoveCommittedState::run, &partialRemoval);
  }
  LOKA_VERIFY(!partialRemoval.tracker->committedSet().identities().empty());
  LOKA_VERIFY(!partialRemoval.tracker->committedSet().complete());
  LOKA_VERIFY(SceneTestAccess::director(scene).pendingDirtyFlagsForBoundary(boundary) ==
              static_cast<NodeDirtyFlags>(NODE_DIRTY_LAYOUT | NODE_DIRTY_PROPS));
  scene.flushInvalidation();

  // The inner-owner notification must use the same known-NONE policy.
  loka::core::MutableState<int> innerPrivate(0);
  loka::core::PushStateTracker innerTracker;
  innerTracker.addState(&innerPrivate);
  LifetimeGuardBoundary otherBoundary;
  otherBoundary.registerObservedState(&innerPrivate, NODE_DIRTY_LAYOUT);
  {
    loka::core::StateTrackerGuard guard(&innerTracker);
    innerPrivate.set(1);
  }
  boundary->noteInnerTrackerCommit(&innerTracker);
  LOKA_VERIFY(!scene.hasPendingInvalidation());
  LOKA_VERIFY(SceneTestAccess::director(scene).pendingDirtyFlagsForBoundary(boundary) == NODE_DIRTY_NONE);

  // Removal loses commit identities, so an empty list is conservative/unknown.
  innerTracker.removeState(&innerPrivate);
  boundary->noteInnerTrackerCommit(&innerTracker);
  LOKA_VERIFY(SceneTestAccess::director(scene).pendingDirtyFlagsForBoundary(boundary) ==
              static_cast<NodeDirtyFlags>(NODE_DIRTY_LAYOUT | NODE_DIRTY_PROPS));
  scene.flushInvalidation();
  boundary->noteInnerTrackerCommit(0);
  LOKA_VERIFY(SceneTestAccess::director(scene).pendingDirtyFlagsForBoundary(boundary) ==
              static_cast<NodeDirtyFlags>(NODE_DIRTY_LAYOUT | NODE_DIRTY_PROPS));
  scene.flushInvalidation();
  SceneTestAccess::unmount(scene);
}
