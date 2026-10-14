#include "platform/null/NullInputDoor.hpp"
#include "app/nodes/boundary/StdComposition.hpp"
#include "app/nodes/nestable/Show.hpp"
#include "platform/null/NullWindow.hpp"
#include "platform/null/NullPlatformContext.hpp"
#include "platform/null/NullScenePlatformController.hpp"
#include "testing/scene/SceneTestFlow.hpp"
#include "support/WindowAdmissionTestApp.hpp"
#include <cstdio>


#include "WriteSeatTests.hpp"
#include "support/TestVerify.hpp"
#include "app/scene/state/NodeState.hpp"
#include "app/nodes/controls/EditText.hpp"
#include "app/nodes/controls/PopupMenu.hpp"
#include "core/State.hpp"
#include "core/StateTracker.hpp"
#include "core/util/StateTrackerGuard.hpp"
#include "core/util/ScopedPtr.hpp"

using namespace loka::core;
using namespace loka::app;
using namespace loka::app::scene;
namespace { struct Count { int calls; Count() : calls(0) {} static void hit(void *p) { ++static_cast<Count *>(p)->calls; } }; }

void testWriteSeatSettlesIdleOwnerTracker()
{
  MutableState<int> value(0); PushStateTracker tracker; tracker.addState(&value);
  NodeState<int> state(&value, &tracker); Count count; value.bind(&Count::hit, &count, false);
  state.writeSeat().set(2, true);
  LOKA_VERIFY(value.get() == 2 && count.calls == 1 && tracker.phase() == TRACKER_IDLE);
}
void testWriteSeatJoinsOpenTracker()
{
  MutableState<int> value(0); PushStateTracker tracker; tracker.addState(&value);
  NodeState<int> state(&value, &tracker); Count count; value.bind(&Count::hit, &count, false);
  { StateTrackerGuard guard(&tracker); state.writeSeat().set(2, true); LOKA_VERIFY(count.calls == 1); }
  LOKA_VERIFY(count.calls == 1 && tracker.phase() == TRACKER_IDLE);
}
void testWriteSeatRawAndInvalid()
{
  MutableState<int> value(0); Count count; value.bind(&Count::hit, &count, false);
  WriteSeat<int>(&value).set(1, true); LOKA_VERIFY(value.get() == 1 && count.calls == 1);
  WriteSeat<int> invalid; invalid.set(2, true); LOKA_VERIFY(value.get() == 1);
}
void testWriteSeatPropsIdentity()
{
  MutableState<int> value(0); PushStateTracker a; PushStateTracker b; NodeState<int> left(&value, &a), right(&value, &b);
  PopupMenuProps one; one.selectedIndex(left); PopupMenuProps two; two.selectedIndex(right);
  LOKA_VERIFY(!(one < two) && !(two < one));
}

namespace
{
  struct Input977Ledger
  {
    unsigned sequence, destroyed, returned, emitted, applies;
    Input977Ledger() : sequence(0), destroyed(0), returned(0), emitted(0), applies(0) {}
  };
  Input977Ledger *input977Ledger = 0;
  bool input977ParentShown = false;
  class Input977Root;
  class Input977Child;
  Input977Root *input977Root = 0;
  Input977Child *input977Child = 0;

  class Input977Context : public NullScrollBarContext
  {
  public:
    Input977Context(ScrollBarNode &node, NullScenePlatformController &platform)
        : NullScrollBarContext(&node, &platform) {}
    virtual ~Input977Context()
    {
      input977Ledger->destroyed = ++input977Ledger->sequence;
    }
  };
  class Input977Root : public BoundaryNodeFor<Input977Root>
  {
  public:
    NodeState<bool> shown;
    explicit Input977Root(const BoundaryPropsFor<Input977Root> &p) : BoundaryNodeFor<Input977Root>(p)
    {
      input977Root = this;
      this->state(this->shown, true);
    }
    virtual void composeNode(NodeComposition &c);
  };
  class Input977Child : public BoundaryNodeFor<Input977Child>
  {
  public:
    NodeState<int> value;
    EmitterState changed;
    NodeState<bool> shown;
    explicit Input977Child(const BoundaryPropsFor<Input977Child> &p) : BoundaryNodeFor<Input977Child>(p)
    {
      input977Child = this;
      this->state(this->value, 0);
      this->state(this->shown, true);
    }
    virtual void composeNode(NodeComposition &c)
    {
      c.declare(Show(*(input977ParentShown ? input977Root->shown.state() : this->shown.state()))
                    .destroyOnDetach() << ScrollBar(this->value).range(0, 10).onChange(&this->changed));
    }
  };
  void Input977Root::composeNode(NodeComposition &c) { c.declare(Boundary<Input977Child>()); }
  ScrollBarNode *input977Find(Node *node)
  {
    if (node->nodeTypeKey() == NodeTypeToken<ScrollBarNode>())
      return static_cast<ScrollBarNode *>(node);
    INestable *children = node->asNestable();
    for (Node *child = children ? children->childrenHead() : 0; child; child = child->nextInComposition)
    {
      ScrollBarNode *found = input977Find(child);
      if (found) return found;
    }
    return 0;
  }
  class Input977Presenter : public NullScenePlatformController
  {
  public:
    virtual bool canSkipGlobalChangeForBoundaryLocalPaint() const { return false; }
    virtual void onChange(Node *, NodeDirtyFlags, bool) { ++input977Ledger->applies; }
  };
  void input977Emitted(void *)
  {
    LOKA_VERIFY(input977Child->value.get() == 1);
    LOKA_VERIFY(input977Ledger->destroyed == 0);
    input977Ledger->emitted = ++input977Ledger->sequence;
  }
  struct Input977Observer
  {
    NodeState<bool> &shown;
    unsigned calls;
    Input977Observer(NodeState<bool> &s) : shown(s), calls(0)
    { input977Child->value.state()->bind(&changed, this, false); }
    ~Input977Observer() { input977Child->value.state()->unbind(&changed, this); }
    static void changed(void *data)
    {
      Input977Observer &self = *static_cast<Input977Observer *>(data);
      ++self.calls;
      const bool values[] = {false, true, false};
      for (unsigned i = 0; i < 3; ++i)
      {
        self.shown.set(values[i]);
      }
    }
  };
}

void testNullInputDoorLifetime()
{
  for (unsigned arrangement = 0; arrangement < 2; ++arrangement)
  {
    Input977Ledger ledger;
    input977Ledger = &ledger;
    input977ParentShown = arrangement != 0;
    NullPlatformContext context;
    Input977Presenter platform;
    WindowProps props;
    props.scene(new Scene(Boundary<Input977Root>()));
    NullWindow window(&context, props, &platform);
    WindowAdmissionTestApp app(window);
    app.flush();
    Scene &scene = *window.scene();
    ScrollBarNode *node = input977Find(loka::dsl::testing::SceneTestAccess::rootBoundary(scene));
    LOKA_VERIFY(node);
    Input977Context *input = new Input977Context(*node, platform);
    node->setContext(input);
    input->readLifecycleFactOnAttach();
    NodeState<bool> &shown = arrangement ? input977Root->shown : input977Child->shown;
    Input977Observer observer(shown);
    input977Child->changed.bind(&input977Emitted, 0, false);
    ledger.applies = 0;
    NullInputDoor::simulateThumbDragTo(*input, 1);
    ledger.returned = ++ledger.sequence;
    std::fprintf(stderr, "[977] arrangement=%u returned=%u destroyed=%u applies=%u\n",
                 arrangement, ledger.returned, ledger.destroyed, ledger.applies);
    LOKA_VERIFY(observer.calls == 1 && input977Child->value.get() == 1);
    LOKA_VERIFY(ledger.destroyed == 0);
    LOKA_VERIFY(ledger.emitted != 0 && ledger.returned > ledger.emitted);
    LOKA_VERIFY(ledger.applies == 0);
    LOKA_VERIFY(!platform.borrowPhase().open());
    // Returning through the door never pumps: even the final false waits.
    LOKA_VERIFY(input977Find(loka::dsl::testing::SceneTestAccess::rootBoundary(scene)) == node);
    app.flush();
    LOKA_VERIFY(ledger.destroyed > ledger.returned && ledger.applies > 0);
    LOKA_VERIFY(!input977Find(loka::dsl::testing::SceneTestAccess::rootBoundary(scene)));
    input977Child->changed.unbind(&input977Emitted, 0);
  }
  input977Ledger = 0;
  input977Root = 0;
  input977Child = 0;
}

#include "testing/core/StateTrackerTestAccess.hpp"
#include "app/nodes/nestable/RowColumn.hpp"
#include "app/nodes/nestable/Box.hpp"

namespace
{
  typedef loka::core::testing::PushStateTrackerTestAccess SeatAccess;
  typedef loka::core::testing::OperationTestAccess SeatClockAccess;
  struct SeatDouble : DerivedState<int>::EvalFn
  {
    State<int> &source;
    explicit SeatDouble(State<int> &value) : source(value) {}
    virtual int operator()() { return this->source.get() * 2; }
  };
  struct SeatLedger
  {
    MutableState<int> source;
    DerivedState<int> derived;
    Count commits, direct;
    PushStateTracker tracker;
    NodeState<int> seat;
    SeatLedger()
        : source(1), derived(&source, new SeatDouble(source)), tracker(), seat(&source, &tracker)
    {
      this->tracker.addState(&this->source);
      this->tracker.addState(&this->derived);
      this->tracker.setInvalidateCallback(&Count::hit, &this->commits);
      this->source.bind(&Count::hit, &this->direct, false);
    }
  };
  struct CleanupSeat
  {
    SeatLedger &target;
    unsigned calls;
    static void write(void *data)
    {
      CleanupSeat &self = *static_cast<CleanupSeat *>(data);
      ++self.calls;
      LOKA_VERIFY(self.target.tracker.phase() == TRACKER_IDLE);
      LOKA_VERIFY(!self.target.source.trackerOwner());
      LOKA_VERIFY(Operation::openActive(&self.target.tracker) == OPEN_REFUSED_CLOSING);
      self.target.seat.set(9);
    }
  };
  struct AbstractSeatTracker : StateTracker
  {
    unsigned begins, ends;
    AbstractSeatTracker() : begins(0), ends(0) {}
    virtual void begin() { ++this->begins; }
    virtual bool end() { ++this->ends; return true; }
    virtual void defer(TrackerDeferKey, void (*)(void *), void *) {}
    virtual void markDirty(StateBase *) {}
    virtual void registerDependency(StateBase *, StateBase *) {}
    virtual TrackerPhase phase() const { return TRACKER_IDLE; }
  };
}

void testSeatInsideTurnEnrollsOwnerAndSettlesAtTail()
{
  SeatLedger f;
  LOKA_VERIFY(f.derived.get() == 2);
  Operation turn;
  f.seat.set(2);
  LOKA_VERIFY(f.source.get() == 2 && f.direct.calls == 1 && f.derived.get() == 2);
  LOKA_VERIFY(f.commits.calls == 0 && SeatAccess::depth(f.tracker) == 1);
  LOKA_VERIFY(Operation::openActive(&f.tracker) == OPEN_ALREADY_OPEN);
  f.seat.set(3);
  LOKA_VERIFY(f.source.get() == 3 && f.direct.calls == 2 && f.derived.get() == 2);
  LOKA_VERIFY(f.commits.calls == 0 && SeatAccess::depth(f.tracker) == 1);
  const OperationOutcome settled = turn.settle();
  LOKA_VERIFY(settled.rounds == 1 && settled.status == OPERATION_SETTLED);
  LOKA_VERIFY(f.derived.get() == 6 && f.commits.calls == 1);
  LOKA_VERIFY(SeatAccess::depth(f.tracker) == 1);
  turn.close();
  LOKA_VERIFY(f.tracker.phase() == TRACKER_IDLE && SeatAccess::depth(f.tracker) == 0);
}

void testSeatOutsideClockKeepsLegacyTransaction()
{
  SeatLedger f;
  LOKA_VERIFY(!Operation::hasActive() && f.derived.get() == 2);
  f.seat.set(4);
  LOKA_VERIFY(f.derived.get() == 8 && f.commits.calls == 1 && f.direct.calls == 1);
  LOKA_VERIFY(f.tracker.phase() == TRACKER_IDLE && !f.source.trackerOwner());
}

void testSeatUnderLegacyGuardWritesOnly()
{
  SeatLedger f;
  Operation turn;
  {
    StandaloneTransactionGuard guard(&f.tracker);
    f.seat.set(4);
    LOKA_VERIFY(Operation::openActive(&f.tracker) == OPEN_REFUSED_BUSY);
    LOKA_VERIFY(SeatClockAccess::empty(turn));
    LOKA_VERIFY(f.derived.get() == 2 && f.commits.calls == 0);
    LOKA_VERIFY(SeatAccess::depth(f.tracker) == 1);
  }
  LOKA_VERIFY(f.derived.get() == 8 && f.commits.calls == 1);
  const OperationOutcome closed = turn.close();
  LOKA_VERIFY(closed.rounds == 0 && f.tracker.phase() == TRACKER_IDLE);
}

void testSeatFirstNestedGuardCommitsAtTail()
{
  SeatLedger f;
  Operation turn;
  f.seat.set(2);
  {
    StateTrackerGuard guard(&f.tracker);
    f.seat.set(4);
    LOKA_VERIFY(SeatAccess::depth(f.tracker) == 1);
  }
  LOKA_VERIFY(SeatAccess::depth(f.tracker) == 1 && f.commits.calls == 0 && f.derived.get() == 2);
  turn.close();
  LOKA_VERIFY(f.derived.get() == 8 && f.commits.calls == 1);
}

void testSeatDuringCleanupWritesWithoutRoute()
{
  SeatLedger f;
  Operation turn(OperationBudget(0));
  LOKA_VERIFY(turn.open(&f.tracker) == OPEN_OK);
  CleanupSeat probe = { f, 0 };
  SeatAccess::defer(f.tracker, &CleanupSeat::write, &probe);
  const OperationOutcome closed = turn.close();
  LOKA_VERIFY(closed.status == OPERATION_REFUSED_CHAIN_LIMIT && probe.calls == 1);
  LOKA_VERIFY(f.source.get() == 9 && f.direct.calls == 1 && f.derived.get() == 2);
  LOKA_VERIFY(f.commits.calls == 0 && !f.tracker.transactionDirty());
  LOKA_VERIFY(SeatAccess::currentDirtyCount(f.tracker) == 0 && SeatAccess::nextDirtyCount(f.tracker) == 0);
}

void testSeatAfterRefusedSettleKeepsLegacyTransaction()
{
  // #1079 bot P2: once the clock refused a settle it runs no further work
  // round, so a later write must not be queued into a ledger nobody settles.
  SeatLedger a, b;
  Operation turn(OperationBudget(0));
  a.seat.set(2);
  const OperationOutcome refused = turn.settle();
  LOKA_VERIFY(refused.status == OPERATION_REFUSED_CHAIN_LIMIT);
  LOKA_VERIFY(Operation::openActive(&b.tracker) == OPEN_CLOCK_REFUSED);
  b.seat.set(5);
  LOKA_VERIFY(b.source.get() == 5 && b.derived.get() == 10 && b.commits.calls == 1);
  LOKA_VERIFY(b.tracker.phase() == TRACKER_IDLE && SeatAccess::depth(b.tracker) == 0);
  LOKA_VERIFY(Operation::openActive(&a.tracker) == OPEN_ALREADY_OPEN);
  a.seat.set(3);
  LOKA_VERIFY(a.source.get() == 3 && a.derived.get() == 2);
  const OperationOutcome closed = turn.close();
  LOKA_VERIFY(closed.status == OPERATION_REFUSED_CHAIN_LIMIT);
  LOKA_VERIFY(a.tracker.phase() == TRACKER_IDLE && b.tracker.phase() == TRACKER_IDLE);
}

void testSeatAbstractTrackerKeepsLegacyTransaction()
{
  MutableState<int> source(0);
  AbstractSeatTracker tracker;
  NodeState<int> seat(&source, &tracker);
  Operation turn;
  seat.set(2);
  LOKA_VERIFY(source.get() == 2 && tracker.begins == 1 && tracker.ends == 1);
  LOKA_VERIFY(SeatClockAccess::empty(turn));
}

namespace
{
  unsigned seatBranchConstructions = 0;
  unsigned seatOwnersDestroyedInTurn = 0;
  class SeatBranch : public BoundaryNodeFor<SeatBranch>
  {
  public:
    explicit SeatBranch(const BoundaryPropsFor<SeatBranch> &p) : BoundaryNodeFor<SeatBranch>(p)
    { ++seatBranchConstructions; }
    virtual void composeNode(NodeComposition &c) { c.declare(Box()); }
  };
  struct SeatNodeDouble : DerivedState<int>::EvalFn
  {
    const NodeState<int> &source;
    explicit SeatNodeDouble(const NodeState<int> &value) : source(value) {}
    virtual int operator()() { return this->source.get() * 2; }
  };
  struct SeatVisible : DerivedState<bool>::EvalFn
  {
    const NodeState<bool> &first, &second;
    SeatVisible(const NodeState<bool> &a, const NodeState<bool> &b) : first(a), second(b) {}
    virtual bool operator()() { return this->first.get() && this->second.get(); }
  };
  class SeatRoot : public BoundaryNodeFor<SeatRoot>
  {
  public:
    NodeState<bool> shown, allowed;
    DerivedNodeState<bool> visible;
    NodeState<int> width;
    DerivedNodeState<int> doubled;
    explicit SeatRoot(const BoundaryPropsFor<SeatRoot> &p) : BoundaryNodeFor<SeatRoot>(p)
    {
      this->state(this->shown, false);
      this->state(this->allowed, true);
      this->derived(this->visible, this->shown, this->allowed, new SeatVisible(this->shown, this->allowed));
      this->state(this->width, 10);
      this->derived(this->doubled, this->width, new SeatNodeDouble(this->width));
    }
    virtual ~SeatRoot() { if (Operation::hasActive()) ++seatOwnersDestroyedInTurn; }
    virtual void composeNode(NodeComposition &c)
    {
      c.declare(Column() << Box().width(this->doubled.state())
                        << (Show(*this->visible.state()).destroyOnDetach() << Boundary<SeatBranch>()));
    }
    static void toggle(void *data)
    {
      SeatRoot &root = *static_cast<SeatRoot *>(data);
      root.shown.set(true);
      root.allowed.set(false);
    }
  };
  class SeatPresenter : public NullScenePlatformController
  {
  public:
    unsigned applies;
    SeatPresenter() : applies(0) {}
    virtual void beginApplyCycle() { ++this->applies; }
    virtual bool canSkipGlobalChangeForBoundaryLocalPaint() const { return false; }
  };
  struct SeatScene
  {
    NullPlatformContext context;
    SeatPresenter platform;
    NullWindow window;
    WindowAdmissionTestApp app;
    SeatScene() : window(&context, props(), &platform), app(window) { this->app.flush(); }
    static WindowProps props()
    { return WindowProps().scene(new Scene(Boundary<SeatRoot>())); }
    SeatRoot &root()
    { return *static_cast<SeatRoot *>(loka::dsl::testing::SceneTestAccess::rootBoundary(*this->window.scene())); }
  };
}

void testTwoSeatsOneHandlerProjectOnce()
{
  SeatAccess::InvalidationProbe commits;
  SeatScene f;
  commits.install(*f.root().tracker()->asPushTracker());
  const unsigned before = f.platform.applies;
  seatBranchConstructions = 0;
  f.app.operationLoop(&SeatRoot::toggle, &f.root());
  LOKA_VERIFY(f.platform.applies == before + 1 && commits.calls == 1);
  LOKA_VERIFY(seatBranchConstructions == 0 && !f.root().visible.get());
}

void testTwoSeatsWithoutClockProjectTwice()
{
  SeatScene f;
  const unsigned before = f.platform.applies;
  seatBranchConstructions = 0;
  SeatRoot::toggle(&f.root());
  LOKA_VERIFY(f.platform.applies == before + 2);
  LOKA_VERIFY(seatBranchConstructions == 1 && !f.root().visible.get());
}

namespace
{
  struct ThreeSeatOwners
  {
    SeatLedger owners[3];
    Scene *scenes[2];
    static void invalidate(void *data)
    {
      ThreeSeatOwners &self = *static_cast<ThreeSeatOwners *>(data);
      for (unsigned i = 0; i < 2; ++i)
        self.scenes[i]->requestInvalidate();
    }
    static void collect(void *data)
    {
      ThreeSeatOwners &self = *static_cast<ThreeSeatOwners *>(data);
      for (unsigned i = 0; i < 3; ++i)
      {
        self.owners[i].seat.set(3 + i);
        self.owners[i].seat.set(4 + i);
      }
    }
  };
  class ThreeSeatPresenter : public SeatPresenter
  {
  public:
    ThreeSeatOwners *owners;
    ThreeSeatPresenter() : owners(0) {}
    virtual void beginApplyCycle()
    {
      SeatPresenter::beginApplyCycle();
      if (!this->owners) return;
      for (unsigned i = 0; i < 3; ++i)
      {
        LOKA_VERIFY(this->owners->owners[i].derived.get() == static_cast<int>((4 + i) * 2));
        LOKA_VERIFY(this->owners->owners[i].tracker.transactionDirty());
      }
    }
  };
}

void testThreeSeatOwnersTwoScenesSeeEveryCommit()
{
  SeatAccess::InvalidationProbe commits[3];
  ThreeSeatOwners owners;
  NullPlatformContext context;
  ThreeSeatPresenter first, second;
  NullWindow a(&context, SeatScene::props(), &first), b(&context, SeatScene::props(), &second);
  WindowAdmissionTestApp app(a, &b);
  app.flush();
  owners.scenes[0] = a.scene(); owners.scenes[1] = b.scene();
  for (unsigned i = 0; i < 3; ++i)
  {
    owners.owners[i].tracker.setInvalidateCallback(&ThreeSeatOwners::invalidate, &owners);
    commits[i].install(owners.owners[i].tracker);
  }
  first.owners = &owners; second.owners = &owners;
  const unsigned aBefore = first.applies, bBefore = second.applies;
  app.operationLoop(&ThreeSeatOwners::collect, &owners);
  LOKA_VERIFY(first.applies == aBefore + 1 && second.applies == bBefore + 1);
  for (unsigned i = 0; i < 3; ++i) LOKA_VERIFY(commits[i].calls == 1);
  first.owners = 0; second.owners = 0;
}

namespace
{
  void replaceSeatOwner(void *data)
  {
    SeatScene &f = *static_cast<SeatScene *>(data);
    f.root().width.set(30);
    LOKA_VERIFY(f.window.sceneManager()->commitTransaction(0, new Scene(Boundary<SeatRoot>())));
  }
}
void testSeatOwnerDestroyedDuringApplyLeavesClock()
{
  SeatScene f;
  seatOwnersDestroyedInTurn = 0;
  f.app.operationLoop(&replaceSeatOwner, &f);
  LOKA_VERIFY(seatOwnersDestroyedInTurn == 1);
  LOKA_VERIFY(!Operation::hasActive());
  LOKA_VERIFY(f.root().width.get() == 10 && f.root().doubled.get() == 20);
  f.app.operationLoop();
  LOKA_VERIFY(!f.window.scene()->hasPendingInvalidation());
}

#include "dsl/flow/Flow.hpp"
namespace
{
  struct GuardedSeatStep
  {
    typedef int In;
    typedef int Out;
    SeatLedger &owner;
    explicit GuardedSeatStep(SeatLedger &value) : owner(value) {}
    loka::dsl::StepRunStatus run(const int &input, int &out, loka::dsl::FlowError &) const
    {
      StateTrackerGuard guard(&this->owner.tracker);
      this->owner.seat.set(input);
      out = input;
      return loka::dsl::FLOW_STEP_SUCCEEDED;
    }
  };
}
void testFlowStepSeatGuardOrders()
{
  for (unsigned clocked = 0; clocked != 2; ++clocked)
    for (unsigned seatFirst = 0; seatFirst != 2; ++seatFirst)
    {
      SeatLedger owner;
      int input = 5;
      // Bounded one-shot chain; the owner outlives every step invocation.
      loka::dsl::FlowChain<int, int> flow =
          loka::dsl::Flow() | loka::dsl::Step(1, GuardedSeatStep(owner)).input(&input);
      ScopedPtr<Operation> turn(clocked ? new Operation : 0);
      if (seatFirst) owner.seat.set(2);
      LOKA_VERIFY(flow.run());
      LOKA_VERIFY(owner.source.get() == 5);
      LOKA_VERIFY(owner.derived.get() == (clocked ? 2 : 10));
      LOKA_VERIFY(owner.commits.calls == (clocked ? 0 : (seatFirst ? 2 : 1)));
      if (turn.get()) turn->close();
      LOKA_VERIFY(owner.derived.get() == 10 && owner.commits.calls == (clocked || !seatFirst ? 1 : 2));
    }
}

void testGuardInsideTurnJoinsAndSettlesAtTail()
{
  SeatLedger f;
  Operation turn;
  for (int value = 2; value <= 3; ++value)
  {
    {
      StateTrackerGuard guard(&f.tracker);
      f.source.set(value);
      LOKA_VERIFY(f.source.get() == value && f.direct.calls == value - 1);
      LOKA_VERIFY(SeatAccess::depth(f.tracker) == 1);
    }
    LOKA_VERIFY(f.derived.get() == 2 && f.commits.calls == 0);
    LOKA_VERIFY(f.tracker.phase() == TRACKER_PRECOMMIT && !f.tracker.peekDirty());
  }
  turn.close();
  LOKA_VERIFY(f.derived.get() == 6 && f.commits.calls == 1);
  LOKA_VERIFY(f.tracker.phase() == TRACKER_IDLE && SeatAccess::depth(f.tracker) == 0);
}

void testGuardOutsideClockKeepsLegacyBracket()
{
  SeatLedger f;
  for (int value = 2; value <= 3; ++value)
  {
    {
      StateTrackerGuard guard(&f.tracker);
      f.source.set(value);
      LOKA_VERIFY(f.direct.calls == value - 1);
    }
    LOKA_VERIFY(f.derived.get() == value * 2 && f.commits.calls == value - 1);
    LOKA_VERIFY(f.tracker.phase() == TRACKER_IDLE);
  }
}

void testGuardOnBusyLedgerNestsLegacyLevel()
{
  SeatLedger f;
  StandaloneTransactionGuard outer(&f.tracker);
  Operation turn;
  {
    StateTrackerGuard guard(&f.tracker);
    f.source.set(4);
    LOKA_VERIFY(SeatAccess::depth(f.tracker) == 2);
  }
  LOKA_VERIFY(SeatAccess::depth(f.tracker) == 1 && f.commits.calls == 0);
  LOKA_VERIFY(SeatClockAccess::empty(turn));
  turn.close();
}

void testGuardCallbackFiresOnlyOutsideTurn()
{
  for (unsigned clocked = 0; clocked != 2; ++clocked)
  {
    SeatLedger f;
    Count callback;
    ScopedPtr<Operation> turn(clocked ? new Operation : 0);
    {
      StateTrackerGuard guard(&f.tracker, &Count::hit, &callback);
      f.source.set(4);
    }
    LOKA_VERIFY(callback.calls == (clocked ? 0 : 1));
    if (turn.get()) turn->close();
    LOKA_VERIFY(f.commits.calls == 1 && callback.calls == (clocked ? 0 : 1));
  }
}

void testBootstrapGuardSettlesIdleJoiningLedger()
{
  SeatLedger f;
  Count callback;
  LOKA_VERIFY(!Operation::hasActive() && f.tracker.phase() == TRACKER_IDLE);
  {
    StandaloneTransactionGuard guard(&f.tracker, &Count::hit, &callback);
    LOKA_VERIFY(f.tracker.phase() == TRACKER_PRECOMMIT && SeatAccess::depth(f.tracker) == 1);
    f.source.set(4);
  }
  LOKA_VERIFY(f.tracker.peekDirty() && f.derived.get() == 8 && callback.calls == 1);
  LOKA_VERIFY(f.tracker.consumeDirty());
  LOKA_VERIFY(!f.tracker.peekDirty());
  LOKA_VERIFY(f.tracker.phase() == TRACKER_IDLE && SeatAccess::depth(f.tracker) == 0);
}

namespace
{
  void guardedToggle(void *data)
  {
    SeatScene &f = *static_cast<SeatScene *>(data);
    const unsigned before = f.platform.applies;
    {
      StateTrackerGuard guard(f.root().tracker());
      f.root().shown.set(true);
    }
    if (Operation::hasActive())
      LOKA_VERIFY(f.platform.applies == before && !f.root().visible.get());
    else
      LOKA_VERIFY(f.platform.applies == before + 1 && f.root().visible.get());
    {
      StateTrackerGuard guard(f.root().tracker());
      f.root().allowed.set(false);
    }
    if (Operation::hasActive()) LOKA_VERIFY(f.platform.applies == before);
  }
}
void testGuardedBlocksProjectOnceAtTail()
{
  for (unsigned clocked = 0; clocked != 2; ++clocked)
  {
    SeatScene f;
    const unsigned before = f.platform.applies;
    if (clocked) f.app.operationLoop(&guardedToggle, &f);
    else guardedToggle(&f);
    LOKA_VERIFY(f.platform.applies == before + (clocked ? 1 : 2));
    LOKA_VERIFY(!f.root().visible.get());
  }
}

#include "app/Menu.hpp"
namespace
{
  class BootstrapApp : public WindowAdmissionTestApp
  {
  public:
    explicit BootstrapApp(Window &window) : WindowAdmissionTestApp(window) {}
    using App::projectInitialVisibilityChunks;
  };
}
void testBootstrapVisibilityCommitsBeforeReadInsideTurn()
{
  for (unsigned clocked = 0; clocked != 2; ++clocked)
  {
    NullPlatformContext context;
    NullWindow window(&context, WindowProps());
    BootstrapApp app(window);
    app.flush();
    SeatAccess::InvalidationProbe commits;
    commits.install(*window.getTracker()->asPushTracker());
    ScopedPtr<Operation> turn(clocked ? new Operation : 0);
    app.projectInitialVisibilityChunks();
    LOKA_VERIFY(commits.calls == 1 && window.getTracker()->phase() == TRACKER_IDLE);
    if (turn.get()) LOKA_VERIFY(SeatClockAccess::empty(*turn));
  }
}

namespace
{
  struct GuardFlowStep
  {
    typedef int In;
    typedef int Out;
    bool &ready;
    explicit GuardFlowStep(bool &value) : ready(value) {}
    loka::dsl::StepRunStatus run(const int &input, int &out, loka::dsl::FlowError &) const
    {
      if (!this->ready) return loka::dsl::FLOW_STEP_PENDING;
      out = input;
      return loka::dsl::FLOW_STEP_SUCCEEDED;
    }
  };
  struct GuardFlowRead
  {
    SeatLedger &owner;
    int calls, value;
    explicit GuardFlowRead(SeatLedger &ledger) : owner(ledger), calls(0), value(0) {}
    static void finally(void *data)
    {
      GuardFlowRead &self = *static_cast<GuardFlowRead *>(data);
      ++self.calls;
      self.value = self.owner.derived.get();
    }
  };
}
void testFlowOnSuccessJoinsTurn()
{
  for (unsigned clocked = 0; clocked != 2; ++clocked)
    for (unsigned path = 0; path != 3; ++path)
    {
      SeatLedger owner;
      GuardFlowRead read(owner);
      bool ready = path == 0;
      int input = 5;
      loka::dsl::FlowChain<int, int> flow = loka::dsl::Flow()
          | loka::dsl::Step(1, GuardFlowStep(ready)).input(&input)
                .onSuccess(&owner.source, &owner.tracker);
      flow.onFinally(&GuardFlowRead::finally, &read);
      ScopedPtr<Operation> turn(clocked ? new Operation : 0);
      if (path)
      {
        LOKA_VERIFY(flow.runResult() == loka::dsl::FLOW_RUN_PENDING);
        LOKA_VERIFY(read.calls == 0 && owner.source.get() == 1 && owner.derived.get() == 2);
        LOKA_VERIFY(owner.commits.calls == 0);
        if (turn.get()) LOKA_VERIFY(SeatClockAccess::empty(*turn));
        ready = true;
        if (path == 2) flow.cancel();
        LOKA_VERIFY(flow.resumeResult(1) == (path == 2 ? loka::dsl::FLOW_RUN_CANCELED : loka::dsl::FLOW_RUN_SUCCEEDED));
      }
      else LOKA_VERIFY(flow.runResult() == loka::dsl::FLOW_RUN_SUCCEEDED);
      LOKA_VERIFY(read.calls == 1 && read.value == (clocked || path == 2 ? 2 : 10));
      LOKA_VERIFY(owner.source.get() == (path == 2 ? 1 : 5));
      LOKA_VERIFY(owner.derived.get() == (clocked || path == 2 ? 2 : 10));
      if (turn.get()) turn->close();
      LOKA_VERIFY(owner.derived.get() == (path == 2 ? 2 : 10));
      LOKA_VERIFY(owner.commits.calls == (path == 2 ? 0 : 1));
    }
}

namespace
{
  struct BracketFlowStep
  {
    typedef int In;
    typedef int Out;
    SeatLedger &owner;
    loka::dsl::StepRunStatus &status;
    int &calls;
    BracketFlowStep(SeatLedger &ledger, loka::dsl::StepRunStatus &result, int &count)
        : owner(ledger), status(result), calls(count) {}
    loka::dsl::StepRunStatus run(const int &input, int &out, loka::dsl::FlowError &) const
    {
      LOKA_VERIFY(SeatAccess::depth(this->owner.tracker) == 1);
      LOKA_VERIFY(this->owner.tracker.phase() == TRACKER_PRECOMMIT);
      ++this->calls;
      // Raw step write: only the Flow run bracket supplies its transaction.
      this->owner.source.set(input);
      out = input;
      return this->status;
    }
  };

  void verifyFlowRunBracket(bool clocked)
  {
    using namespace loka::dsl;
    const StepRunStatus statuses[] = { FLOW_STEP_PENDING, FLOW_STEP_SUCCEEDED, FLOW_STEP_FAILED };
    const FlowRunResult results[] = { FLOW_RUN_PENDING, FLOW_RUN_SUCCEEDED, FLOW_RUN_FAILED };
    for (unsigned path = 0; path != 3; ++path)
    {
      SeatLedger owner;
      GuardFlowRead read(owner);
      StepRunStatus status = statuses[path];
      int input = 5, calls = 0;
      bool loading = false;
      FlowChain<int, int> flow = Flow() | Step(1, BracketFlowStep(owner, status, calls)).input(&input);
      flow.withTracker(&owner.tracker).trackLoading(&loading).onFinally(&GuardFlowRead::finally, &read);
      ScopedPtr<Operation> turn(clocked ? new Operation : 0);
      const bool joined = clocked;
      for (int run = 1; run <= 2; ++run)
      {
        LOKA_VERIFY(flow.runResult() == results[path]);
        LOKA_VERIFY(calls == run && owner.source.get() == input && owner.direct.calls == run);
        LOKA_VERIFY(owner.derived.get() == (joined ? 2 : input * 2));
        LOKA_VERIFY(owner.commits.calls == (joined ? 0 : run));
        LOKA_VERIFY(SeatAccess::depth(owner.tracker) == (joined ? 1u : 0u));
        LOKA_VERIFY(loading == (path == 0));
        LOKA_VERIFY(read.calls == (path == 0 ? 0 : run));
        if (path != 0) LOKA_VERIFY(read.value == (joined ? 2 : input * 2));
        if (turn.get()) LOKA_VERIFY(SeatClockAccess::empty(*turn) == !joined);
        ++input;
      }
      if (turn.get())
      {
        LOKA_VERIFY(turn->settle().status == OPERATION_SETTLED);
        LOKA_VERIFY(owner.derived.get() == 12 && owner.commits.calls == (joined ? 1 : 2));
        LOKA_VERIFY(SeatAccess::depth(owner.tracker) == (joined ? 1u : 0u));
        LOKA_VERIFY(turn->close().status == OPERATION_SETTLED);
      }
      LOKA_VERIFY(SeatAccess::depth(owner.tracker) == 0 && owner.tracker.phase() == TRACKER_IDLE);
      LOKA_VERIFY(!owner.source.trackerOwner());
      LOKA_VERIFY(owner.commits.calls == (joined ? 1 : 2));
    }
  }
}

void testFlowRunBracketJoinsTurn()
{
  verifyFlowRunBracket(true);
}

void testFlowRunBracketOutsideClockKeepsLegacy()
{
  verifyFlowRunBracket(false);
}

void testFlowPendingAndResumeInsideTurn()
{
  using namespace loka::dsl;
  for (unsigned clocked = 0; clocked != 2; ++clocked)
    for (unsigned cancel = 0; cancel != 2; ++cancel)
    {
      SeatLedger owner;
      GuardFlowRead read(owner);
      StepRunStatus status = FLOW_STEP_PENDING;
      int input = 5, calls = 0;
      bool loading = false;
      FlowChain<int, int> flow = Flow() | Step(1, BracketFlowStep(owner, status, calls)).input(&input);
      flow.withTracker(&owner.tracker).trackLoading(&loading).onFinally(&GuardFlowRead::finally, &read);
      {
        ScopedPtr<Operation> turn(clocked ? new Operation : 0);
        LOKA_VERIFY(flow.runResult() == FLOW_RUN_PENDING);
        LOKA_VERIFY(calls == 1 && loading && read.calls == 0 && owner.source.get() == 5);
        LOKA_VERIFY(owner.derived.get() == (clocked ? 2 : 10));
        LOKA_VERIFY(SeatAccess::depth(owner.tracker) == (clocked ? 1u : 0u));
        if (turn.get())
        {
          LOKA_VERIFY(!SeatClockAccess::empty(*turn));
          LOKA_VERIFY(turn->settle().status == OPERATION_SETTLED);
          LOKA_VERIFY(SeatAccess::depth(owner.tracker) == 1);
        }
        LOKA_VERIFY(owner.derived.get() == 10 && owner.commits.calls == 1);
      }
      LOKA_VERIFY(SeatAccess::depth(owner.tracker) == 0 && !owner.source.trackerOwner());
      input = 7;
      status = FLOW_STEP_SUCCEEDED;
      if (cancel) flow.cancel();
      {
        // Public resume is a fresh segment, joining this later turn.
        ScopedPtr<Operation> turn(clocked ? new Operation : 0);
        LOKA_VERIFY(flow.resumeResult(1) == (cancel ? FLOW_RUN_CANCELED : FLOW_RUN_SUCCEEDED));
        LOKA_VERIFY(calls == (cancel ? 1 : 2) && !loading && read.calls == 1);
        LOKA_VERIFY(owner.source.get() == (cancel ? 5 : 7));
        LOKA_VERIFY(owner.derived.get() == (clocked || cancel ? 10 : 14));
        LOKA_VERIFY(read.value == (clocked || cancel ? 10 : 14));
        LOKA_VERIFY(owner.commits.calls == (clocked || cancel ? 1 : 2));
        LOKA_VERIFY(SeatAccess::depth(owner.tracker) == (clocked ? 1u : 0u));
        if (turn.get())
        {
          LOKA_VERIFY(!SeatClockAccess::empty(*turn));
          LOKA_VERIFY(turn->settle().status == OPERATION_SETTLED);
        }
        LOKA_VERIFY(owner.derived.get() == (cancel ? 10 : 14));
        LOKA_VERIFY(owner.commits.calls == (cancel ? 1 : 2));
      }
      LOKA_VERIFY(SeatAccess::depth(owner.tracker) == 0 && !owner.source.trackerOwner());
    }
}

namespace
{
  struct BracketHookObservation
  {
    SeatLedger &owner;
    const bool clocked;
    int calls;
    static void begin(void *, void *) {}
    static void end(void *, void *data)
    {
      BracketHookObservation &self = *static_cast<BracketHookObservation *>(data);
      ++self.calls;
      // Both the inner and outer execution hooks see the released bracket.
      LOKA_VERIFY(self.owner.derived.get() == (self.clocked ? 2 : 14));
      LOKA_VERIFY(SeatAccess::depth(self.owner.tracker) == (self.clocked ? 1u : 0u));
    }
  };

  struct BracketContinuation
  {
    loka::dsl::StepRunStatus &status;
    int &input;
    loka::dsl::StepRunStatus next;
    int calls;
    static void advance(void *data)
    {
      BracketContinuation &self = *static_cast<BracketContinuation *>(data);
      ++self.calls;
      self.status = self.next;
      self.input = 7;
    }
  };
}

void testFlowFinallyReadsOldDerivedInsideTurn()
{
  using namespace loka::dsl;
  for (unsigned clocked = 0; clocked != 2; ++clocked)
    for (unsigned pending = 0; pending != 2; ++pending)
    {
      SeatLedger owner;
      GuardFlowRead read(owner);
      StepRunStatus status = FLOW_STEP_SUCCEEDED;
      int input = 5, calls = 0;
      bool loading = false;
      BracketContinuation continuation = {status, input, pending ? FLOW_STEP_PENDING : FLOW_STEP_FAILED, 0};
      BracketHookObservation hooks = {owner, clocked != 0, 0};
      FlowChain<int, int> flow = Flow() | Step(1, BracketFlowStep(owner, status, calls)).input(&input);
      flow.withTracker(&owner.tracker).trackLoading(&loading)
          .onSuccess(&BracketContinuation::advance, &continuation, 1)
          .onFinally(&GuardFlowRead::finally, &read);
      flow.setExecutionHooks(&BracketHookObservation::begin, &BracketHookObservation::end, &hooks);
      ScopedPtr<Operation> turn(clocked ? new Operation : 0);
      // The internal continuation must share the original bracket: no extra
      // begin, and its release precedes finally even outside a clock.
      LOKA_VERIFY(flow.runResult() == (pending ? FLOW_RUN_PENDING : FLOW_RUN_FAILED));
      LOKA_VERIFY(calls == 2 && continuation.calls == 1 && hooks.calls == 2 && owner.source.get() == 7);
      LOKA_VERIFY(loading == (pending != 0) && read.calls == (pending ? 0 : 1));
      if (!pending) LOKA_VERIFY(read.value == (clocked ? 2 : 14));
      LOKA_VERIFY(owner.derived.get() == (clocked ? 2 : 14));
      LOKA_VERIFY(owner.commits.calls == (clocked ? 0 : 1));
      LOKA_VERIFY(SeatAccess::depth(owner.tracker) == (clocked ? 1u : 0u));
      if (turn.get()) LOKA_VERIFY(turn->close().status == OPERATION_SETTLED);
      LOKA_VERIFY(owner.derived.get() == 14 && owner.commits.calls == 1);
      LOKA_VERIFY(SeatAccess::depth(owner.tracker) == 0);
    }
}

void testFlowRunBracketAbstractTrackerEndsOnce()
{
  using namespace loka::dsl;
  for (unsigned clocked = 0; clocked != 2; ++clocked)
    for (unsigned pending = 0; pending != 2; ++pending)
    {
      AbstractSeatTracker tracker;
      bool ready = !pending;
      int input = 5;
      FlowChain<int, int> flow = Flow() | Step(1, GuardFlowStep(ready)).input(&input);
      flow.withTracker(&tracker);
      ScopedPtr<Operation> turn(clocked ? new Operation : 0);
      LOKA_VERIFY(flow.runResult() == (pending ? FLOW_RUN_PENDING : FLOW_RUN_SUCCEEDED));
      // Explicit release plus destructor must consume only one owned level.
      LOKA_VERIFY(tracker.begins == 1 && tracker.ends == 1);
      if (turn.get()) LOKA_VERIFY(SeatClockAccess::empty(*turn));
    }
}
