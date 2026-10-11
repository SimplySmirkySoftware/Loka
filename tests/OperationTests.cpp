#include "OperationTests.hpp"
#include "app/scene/state/NodeState.hpp"
#include "core/Operation.hpp"
#include "core/State.hpp"
#include "core/util/StateTrackerGuard.hpp"
#include "testing/core/StateTrackerTestAccess.hpp"
#include "support/TestVerify.hpp"
#include <cstdio>
#include <vector>
#if defined(__linux__) && !defined(__SANITIZE_ADDRESS__) && !defined(NDEBUG)
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace
{
  using namespace loka::core;
  typedef testing::PushStateTrackerTestAccess Access;

  struct Ledger
  {
    MutableState<int> source;
    PushStateTracker tracker;
    int invalidations;
    Ledger(LedgerPolicy policy = LEDGER_JOINS) : source(0), tracker(policy), invalidations(0)
    {
      this->tracker.addState(&this->source);
      this->tracker.setInvalidateCallback(&Ledger::invalidate, this);
    }
    static void invalidate(void *data)
    {
      ++static_cast<Ledger *>(data)->invalidations;
    }
  };


  // Records the commit snapshot each time a ledger invalidates, so a cross-round
  // pin can see whether a later round's snapshot still carries the earlier one.
  struct SnapshotTrace
  {
    Ledger *ledger;
    std::vector<std::vector<StateBase *> > snapshots;
    static void invoke(void *data)
    {
      SnapshotTrace &trace = *static_cast<SnapshotTrace *>(data);
      ++trace.ledger->invalidations;
      trace.snapshots.push_back(trace.ledger->tracker.committedSet().identities());
    }
  };

  struct Increment : DerivedState<int>::EvalFn
  {
    State<int> *source;
    explicit Increment(State<int> *s) : source(s) {}
    int operator()() { return this->source->get() + 1; }
  };

  void verifyClosed(const Ledger &ledger)
  {
    LOKA_VERIFY(ledger.tracker.phase() == TRACKER_IDLE);
    LOKA_VERIFY(ledger.source.trackerOwner() == 0);
    LOKA_VERIFY(Access::depth(ledger.tracker) == 0);
    (void)ledger;
  }

  struct Write
  {
    Ledger *from;
    MutableState<int> *to;
    int writes;
    static void invoke(void *data)
    {
      Write &write = *static_cast<Write *>(data);
      ++write.from->invalidations;
      if (write.writes > 0)
      {
        --write.writes;
        if (write.to != &write.from->source)
          LOKA_VERIFY(write.to->trackerOwner()->phase() == TRACKER_PRECOMMIT);
        write.to->set(write.to->get() + 1);
      }
    }
  };

  struct OpenDuringRound
  {
    Operation *operation;
    Ledger *opened;
    std::vector<int> *trace;
    static void invoke(void *data)
    {
      OpenDuringRound &probe = *static_cast<OpenDuringRound *>(data);
      probe.trace->push_back(1);
      LOKA_VERIFY(probe.operation->open(&probe.opened->tracker) == OPEN_OK);
      probe.opened->source.set(1);
    }
    static void record(void *data)
    {
      static_cast<std::vector<int> *>(data)->push_back(2);
    }
  };

  struct DeferredLoop
  {
    PushStateTracker *tracker;
    int calls;
    int cleanupCalls;
    static void invoke(void *data)
    {
      DeferredLoop &loop = *static_cast<DeferredLoop *>(data);
      ++loop.calls;
      if (loop.tracker->phase() == TRACKER_IDLE)
        ++loop.cleanupCalls;
      Access::defer(*loop.tracker, &DeferredLoop::invoke, &loop);
    }
  };

  struct CleanupWrite
  {
    Ledger *from;
    Ledger *to;
    int calls;
    static void invoke(void *data)
    {
      CleanupWrite &probe = *static_cast<CleanupWrite *>(data);
      LOKA_VERIFY(probe.from->tracker.phase() == TRACKER_IDLE);
      LOKA_VERIFY(probe.to->tracker.phase() == TRACKER_IDLE);
      LOKA_VERIFY(probe.from->source.trackerOwner() == 0);
      LOKA_VERIFY(probe.to->source.trackerOwner() == 0);
      ++probe.calls;
      // Cleanup runs outside every ledger's routes: this assignment changes
      // the value but deliberately does not create another transaction.
      probe.to->source.set(9);
    }
  };

  struct Rising : DerivedState<int>::EvalFn
  {
    int value;
    Rising() : value(0) {}
    int operator()() { return ++this->value; }
  };

  struct NonPushTracker : StateTracker
  {
    int begins;
    int ends;
    NonPushTracker() : begins(0), ends(0) {}
    void begin() { ++this->begins; }
    bool end() { ++this->ends; return true; }
    void defer(TrackerDeferKey, void (*)(void *), void *) {}
    void markDirty(StateBase *) {}
    void registerDependency(StateBase *, StateBase *) {}
    TrackerPhase phase() const { return TRACKER_IDLE; }
  };

  void count(void *data) { ++*static_cast<int *>(data); }

#if defined(__linux__) && !defined(__SANITIZE_ADDRESS__) && !defined(NDEBUG)
  void expectAssert(void (*probe)())
  {
    const pid_t child = fork();
    LOKA_VERIFY(child >= 0);
    if (child == 0)
    {
      probe();
      _exit(0);
    }
    int status = 0;
    LOKA_VERIFY(waitpid(child, &status, 0) == child);
    LOKA_VERIFY(WIFSIGNALED(status));
    LOKA_VERIFY(WTERMSIG(status) == SIGABRT);
  }

  void destroyOpenLedger()
  {
    MutableState<int> state(0);
    PushStateTracker *tracker = new PushStateTracker;
    tracker->addState(&state);
    Operation operation;
    LOKA_VERIFY(operation.open(tracker) == OPEN_OK);
    delete tracker;
  }

  void closeTwice()
  {
    Operation operation;
    operation.close();
    operation.close();
  }
  void openAfterClose()
  {
    Ledger ledger;
    Operation operation;
    operation.close();
    operation.open(&ledger.tracker);
    _exit(0); // Do not let the destructor's separate wall mask this probe.
  }
  void closeWithNestedGuard()
  {
    Ledger ledger;
    Operation operation;
    LOKA_VERIFY(operation.open(&ledger.tracker) == OPEN_OK);
    StandaloneTransactionGuard guard(&ledger.tracker);
    operation.close();
  }

#ifdef LOKA_LIFECYCLE_AUDIT
  void collideBegin()
  {
    MutableState<int> state(0);
    PushStateTracker a, b;
    a.addState(&state);
    b.addState(&state);
    a.begin();
    b.begin();
  }
  void collideAdd()
  {
    MutableState<int> state(0);
    PushStateTracker a, b;
    a.addState(&state);
    a.begin();
    b.begin();
    b.addState(&state);
  }
  void collideUnchecked()
  {
    MutableState<int> state(0);
    PushStateTracker a, b;
    a.addStateUnchecked(&state);
    a.begin();
    b.begin();
    b.addStateUnchecked(&state);
  }
#endif
#endif
}

void testOperationLedgerOpenedDuringRoundWaitsForNextRound()
{
  Ledger a, c;
  std::vector<int> trace;
  Operation operation;
  OpenDuringRound probe = { &operation, &c, &trace };
  a.tracker.setInvalidateCallback(&OpenDuringRound::invoke, &probe);
  c.tracker.setInvalidateCallback(&OpenDuringRound::record, &trace);
  LOKA_VERIFY(operation.open(&a.tracker) == OPEN_OK);
  a.source.set(1);
  const OperationOutcome outcome = operation.close();
  LOKA_VERIFY(outcome.status == OPERATION_SETTLED);
  LOKA_VERIFY(outcome.rounds == 2);
  LOKA_VERIFY(trace.size() == 2 && trace[0] == 1 && trace[1] == 2);
  verifyClosed(a);
  verifyClosed(c);
}

void testOperationWriteToVisitedLedgerSettlesNextRound()
{
  Ledger a, b;
  DerivedState<int> derived(&b.source, new Increment(&b.source));
  b.tracker.addState(&derived);
  Write write = { &a, &b.source, 1 };
  a.tracker.setInvalidateCallback(&Write::invoke, &write);
  Operation operation;
  LOKA_VERIFY(operation.open(&b.tracker) == OPEN_OK);
  LOKA_VERIFY(operation.open(&a.tracker) == OPEN_OK);
  b.source.set(1);
  a.source.set(1);
  const OperationOutcome outcome = operation.close();
  LOKA_VERIFY(outcome.status == OPERATION_SETTLED && outcome.rounds == 2);
  assert(derived.get() == 3);
  assert(b.invalidations == 2);
  assert(b.tracker.transactionDirty());
  assert(b.tracker.peekDirty());
  b.tracker.removeState(&derived);
}

void testOperationSecondRoundSnapshotHoldsOnlyNewIdentities()
{
  // #1062 bot P2: a write into an already-stepped ledger lands in current, so the
  // next step must not keep the previous round's commit snapshot under it.
  Ledger a, b;
  MutableState<int> second(0);
  b.tracker.addState(&second);
  SnapshotTrace trace = { &b, std::vector<std::vector<StateBase *> >() };
  b.tracker.setInvalidateCallback(&SnapshotTrace::invoke, &trace);
  Write write = { &a, &second, 1 };
  a.tracker.setInvalidateCallback(&Write::invoke, &write);
  Operation operation;
  LOKA_VERIFY(operation.open(&b.tracker) == OPEN_OK);
  LOKA_VERIFY(operation.open(&a.tracker) == OPEN_OK);
  b.source.set(1);
  a.source.set(1);
  const OperationOutcome outcome = operation.close();
  LOKA_VERIFY(outcome.status == OPERATION_SETTLED && outcome.rounds == 2);
  LOKA_VERIFY(trace.snapshots.size() == 2);
  LOKA_VERIFY(trace.snapshots[0].size() == 1 && trace.snapshots[0][0] == &b.source);
  LOKA_VERIFY(trace.snapshots[1].size() == 1 && trace.snapshots[1][0] == &second);
  b.tracker.removeState(&second);
}

void testOperationWriteToUnvisitedLedgerSettlesSameRound()
{
  Ledger a, b;
  DerivedState<int> derived(&b.source, new Increment(&b.source));
  b.tracker.addState(&derived);
  Write write = { &a, &b.source, 1 };
  a.tracker.setInvalidateCallback(&Write::invoke, &write);
  Operation operation;
  LOKA_VERIFY(operation.open(&a.tracker) == OPEN_OK);
  LOKA_VERIFY(operation.open(&b.tracker) == OPEN_OK);
  a.source.set(1);
  const OperationOutcome outcome = operation.close();
  LOKA_VERIFY(outcome.status == OPERATION_SETTLED && outcome.rounds == 1);
  assert(derived.get() == 2);
  assert(b.invalidations == 1);
  b.tracker.removeState(&derived);
}

void testOperationCleanLedgerClosesIdleWithoutInvalidate()
{
  Ledger ledger;
  Operation operation;
  LOKA_VERIFY(operation.open(&ledger.tracker) == OPEN_OK);
  const OperationOutcome outcome = operation.close();
  LOKA_VERIFY(outcome.status == OPERATION_SETTLED && outcome.rounds == 0);
  verifyClosed(ledger);
  assert(ledger.invalidations == 0);
  assert(!ledger.tracker.transactionDirty());
}

void testOperationOneWriteSettlesInOneRound()
{
  Ledger ledger;
  Operation operation;
  LOKA_VERIFY(operation.open(&ledger.tracker) == OPEN_OK);
  ledger.source.set(1);
  const OperationOutcome outcome = operation.close();
  LOKA_VERIFY(outcome.status == OPERATION_SETTLED && outcome.rounds == 1);
  verifyClosed(ledger);
  assert(ledger.invalidations == 1);
  assert(ledger.tracker.committedSet().identities().size() == 1);
}

void testOperationChainLimitClosesAllLedgers()
{
  Ledger a, b;
  Write write = { &a, &a.source, 1001 };
  a.tracker.setInvalidateCallback(&Write::invoke, &write);
  Operation operation;
  LOKA_VERIFY(operation.open(&a.tracker) == OPEN_OK);
  LOKA_VERIFY(operation.open(&b.tracker) == OPEN_OK);
  a.source.set(1);
  const OperationOutcome outcome = operation.close();
  LOKA_VERIFY(outcome.status == OPERATION_REFUSED_CHAIN_LIMIT);
  LOKA_VERIFY(outcome.rounds == 1000);
  assert(a.invalidations == 1000 && b.invalidations == 0);
  assert(a.source.get() == 1001);
  verifyClosed(a);
  verifyClosed(b);
}

void testOperationStateBudgetClosesAllLedgers()
{
  Ledger a, b;
  DerivedState<int> cycle(&a.source, new Rising);
  a.tracker.addState(&cycle);
  a.tracker.registerDependency(&cycle, &cycle);
  Operation operation(OperationBudget(10, 3));
  LOKA_VERIFY(operation.open(&a.tracker) == OPEN_OK);
  LOKA_VERIFY(operation.open(&b.tracker) == OPEN_OK);
  a.source.set(1);
  const OperationOutcome outcome = operation.close();
  LOKA_VERIFY(outcome.status == OPERATION_REFUSED_STATE_BUDGET);
  LOKA_VERIFY(outcome.rounds == 1);
  verifyClosed(a);
  verifyClosed(b);
  LOKA_VERIFY(cycle.trackerOwner() == 0);
  assert(a.invalidations == 0);
  a.tracker.removeState(&cycle);
}

void testOperationSettlesOnLastPermittedRound()
{
  Ledger ledger;
  Write write = { &ledger, &ledger.source, 2 };
  ledger.tracker.setInvalidateCallback(&Write::invoke, &write);
  Operation operation(OperationBudget(3));
  LOKA_VERIFY(operation.open(&ledger.tracker) == OPEN_OK);
  ledger.source.set(1);
  const OperationOutcome outcome = operation.close();
  LOKA_VERIFY(outcome.status == OPERATION_SETTLED && outcome.rounds == 3);
  assert(ledger.invalidations == 3);
}

void testOperationCloseDrainIsBounded()
{
  Ledger a, b;
  DeferredLoop loop = { &a.tracker, 0, 0 };
  Operation operation(OperationBudget(3));
  LOKA_VERIFY(operation.open(&a.tracker) == OPEN_OK);
  LOKA_VERIFY(operation.open(&b.tracker) == OPEN_OK);
  Access::defer(a.tracker, &DeferredLoop::invoke, &loop);
  const OperationOutcome outcome = operation.close();
  LOKA_VERIFY(outcome.status == OPERATION_REFUSED_CHAIN_LIMIT && outcome.rounds == 3);
  assert(loop.calls == 4 && loop.cleanupCalls == 1);
  assert(a.invalidations == 0);
  assert(Access::currentDeferredCount(a.tracker) == 0);
  assert(Access::nextDeferredCount(a.tracker) == 0);
  verifyClosed(a);
  verifyClosed(b);
}

void testOperationWriteDuringCloseIsUnmarked()
{
  Ledger a, b;
  CleanupWrite probe = { &a, &b, 0 };
  Operation operation(OperationBudget(0));
  LOKA_VERIFY(operation.open(&a.tracker) == OPEN_OK);
  LOKA_VERIFY(operation.open(&b.tracker) == OPEN_OK);
  Access::defer(a.tracker, &CleanupWrite::invoke, &probe);
  const OperationOutcome outcome = operation.close();
  LOKA_VERIFY(outcome.status == OPERATION_REFUSED_CHAIN_LIMIT && outcome.rounds == 0);
  assert(probe.calls == 1);
  assert(b.source.get() == 9);
  assert(!b.tracker.transactionDirty());
  assert(!b.tracker.peekDirty());
  LOKA_VERIFY(Access::currentDirtyCount(b.tracker) == 0);
  LOKA_VERIFY(Access::nextDirtyCount(b.tracker) == 0);
  assert(a.invalidations == 0 && b.invalidations == 0);
  verifyClosed(a);
  verifyClosed(b);
}

#if defined(__linux__) && !defined(__SANITIZE_ADDRESS__) && !defined(NDEBUG)
namespace
{
  // #1078 bot P2: a rowless ledger deleting itself from its own cleanup callback
  // is still a contract violation; the driver resumes into the freed object.
  void deleteSelfFromCleanup(void *data)
  {
    delete static_cast<PushStateTracker *>(data);
  }
  void destroySteppingLedger()
  {
    PushStateTracker *ledger = new PushStateTracker;
    Operation operation;
    LOKA_VERIFY(operation.open(ledger) == OPEN_OK);
    Access::defer(*ledger, &deleteSelfFromCleanup, ledger);
    operation.close();
  }
}
#endif

void testSteppingLedgerCannotDestroyItselfEvenWithoutRows()
{
#if defined(__linux__) && !defined(__SANITIZE_ADDRESS__) && !defined(NDEBUG)
  expectAssert(&destroySteppingLedger);
#else
  std::printf("[skip] stepping-ledger death pin requires Linux debug without ASan.\n");
#endif
}

void testLedgerWithRowsCannotBeDestroyedWhileOpen()
{
#if defined(__linux__) && !defined(__SANITIZE_ADDRESS__) && !defined(NDEBUG)
  expectAssert(&destroyOpenLedger);
#else
  std::printf("[skip] Operation destruction death pin requires Linux debug without ASan.\n");
#endif
}

void testStateTrackerNestedGuardInsideOperationDoesNotDrive()
{
  Ledger ledger;
  int guardInvalidations = 0;
  Operation operation;
  LOKA_VERIFY(operation.open(&ledger.tracker) == OPEN_OK);
  {
    StateTrackerGuard guard(&ledger.tracker, &count, &guardInvalidations);
    ledger.source.set(1);
  }
  LOKA_VERIFY(Access::depth(ledger.tracker) == 1);
  LOKA_VERIFY(ledger.tracker.phase() == TRACKER_PRECOMMIT);
  assert(ledger.invalidations == 0 && guardInvalidations == 0);
  const OperationOutcome outcome = operation.close();
  LOKA_VERIFY(outcome.status == OPERATION_SETTLED && outcome.rounds == 1);
  assert(ledger.invalidations == 1 && guardInvalidations == 0);
  verifyClosed(ledger);
}

void testOperationAuditRejectsRouteInstallOverAnotherLedgerBegin()
{
#if defined(__linux__) && !defined(__SANITIZE_ADDRESS__) && !defined(NDEBUG) && defined(LOKA_LIFECYCLE_AUDIT)
  expectAssert(&collideBegin);
#else
  std::printf("[skip] Route begin death pin requires Linux debug audit without ASan.\n");
#endif
}
void testOperationAuditRejectsRouteInstallOverAnotherLedgerAddState()
{
#if defined(__linux__) && !defined(__SANITIZE_ADDRESS__) && !defined(NDEBUG) && defined(LOKA_LIFECYCLE_AUDIT)
  expectAssert(&collideAdd);
#else
  std::printf("[skip] Route addState death pin requires Linux debug audit without ASan.\n");
#endif
}
void testOperationAuditRejectsRouteInstallOverAnotherLedgerAddStateUnchecked()
{
#if defined(__linux__) && !defined(__SANITIZE_ADDRESS__) && !defined(NDEBUG) && defined(LOKA_LIFECYCLE_AUDIT)
  expectAssert(&collideUnchecked);
#else
  std::printf("[skip] Route unchecked death pin requires Linux debug audit without ASan.\n");
#endif
}

void testOperationRefusesNonPushAndBusyLedgers()
{
  NonPushTracker mock;
  Ledger busy, available;
  StateTrackerGuard guard(&busy.tracker);
  Operation operation;
  LOKA_VERIFY(Operation::openActive(&mock) == OPEN_REFUSED_NOT_PUSH);
  LOKA_VERIFY(Operation::openActive(&busy.tracker) == OPEN_REFUSED_BUSY);
  LOKA_VERIFY(Operation::openActive(&available.tracker) == OPEN_OK);
  LOKA_VERIFY(Operation::openActive(&available.tracker) == OPEN_ALREADY_OPEN);
  LOKA_VERIFY(Access::depth(available.tracker) == 1);
  assert(mock.begins == 0 && mock.ends == 0);
  const OperationOutcome outcome = operation.close();
  LOKA_VERIFY(outcome.status == OPERATION_SETTLED && outcome.rounds == 0);
  LOKA_VERIFY(busy.tracker.phase() == TRACKER_PRECOMMIT);
  LOKA_VERIFY(Access::depth(busy.tracker) == 1);
}

namespace
{
  struct RecordDerived
  {
    State<int> *derived;
    int value;
    static void invoke(void *data)
    {
      RecordDerived &probe = *static_cast<RecordDerived *>(data);
      probe.value = probe.derived->get();
    }
  };
}

void testOperationScheduleOrderIsObservable()
{
  Ledger a, b;
  DerivedState<int> derived(&a.source, new Increment(&a.source));
  a.tracker.addState(&derived);
  Write write = { &a, &a.source, 1 };
  RecordDerived record = { &derived, 0 };
  a.tracker.setInvalidateCallback(&Write::invoke, &write);
  b.tracker.setInvalidateCallback(&RecordDerived::invoke, &record);
  Operation operation;
  LOKA_VERIFY(operation.open(&a.tracker) == OPEN_OK);
  LOKA_VERIFY(operation.open(&b.tracker) == OPEN_OK);
  a.source.set(1);
  b.source.set(1);
  const OperationOutcome outcome = operation.close();
  LOKA_VERIFY(outcome.status == OPERATION_SETTLED && outcome.rounds == 2);
  // A depth-first driver would settle A's second commit before B and record 3.
  assert(record.value == 2);
  assert(derived.get() == 3);
  a.tracker.removeState(&derived);
}

void testOperationStateBudgetIsSharedAcrossLedgers()
{
  Ledger a, b;
  Operation operation(OperationBudget(10, 1));
  LOKA_VERIFY(operation.open(&a.tracker) == OPEN_OK);
  LOKA_VERIFY(operation.open(&b.tracker) == OPEN_OK);
  a.source.set(1);
  b.source.set(1);
  const OperationOutcome outcome = operation.close();
  LOKA_VERIFY(outcome.status == OPERATION_REFUSED_STATE_BUDGET && outcome.rounds == 1);
  assert(a.invalidations == 1 && b.invalidations == 0);
  verifyClosed(a);
  verifyClosed(b);
}

void testOperationStateRefusalCleanupUsesRemainingRounds()
{
  Ledger ledger;
  DeferredLoop loop = { &ledger.tracker, 0, 0 };
  Operation operation(OperationBudget(3, 0));
  LOKA_VERIFY(operation.open(&ledger.tracker) == OPEN_OK);
  ledger.source.set(1);
  Access::defer(ledger.tracker, &DeferredLoop::invoke, &loop);
  const OperationOutcome outcome = operation.close();
  LOKA_VERIFY(outcome.status == OPERATION_REFUSED_STATE_BUDGET && outcome.rounds == 1);
  assert(loop.calls == 3 && loop.cleanupCalls == 3);
  assert(Access::currentDeferredCount(ledger.tracker) == 0);
  verifyClosed(ledger);
}

void testOperationDestructorClosesClock()
{
  Ledger ledger;
  {
    Operation operation;
    LOKA_VERIFY(operation.open(&ledger.tracker) == OPEN_OK);
    ledger.source.set(1);
  }
  verifyClosed(ledger);
  assert(ledger.invalidations == 1);
  // A new clock can follow the destroyed clock in the same main-thread stack.
  Operation next;
  LOKA_VERIFY(next.open(&ledger.tracker) == OPEN_OK);
  const OperationOutcome outcome = next.close();
  LOKA_VERIFY(outcome.status == OPERATION_SETTLED && outcome.rounds == 0);
}

void testOperationReleaseWithdrawalAndRouteReplacement()
{
#if defined(NDEBUG) && !defined(LOKA_LIFECYCLE_AUDIT)
  // The release fallback unlinks without visiting rows whose States died
  // with their window. Exercise middle, tail, head, and sole membership.
  Operation operation;
  PushStateTracker *a = new PushStateTracker;
  PushStateTracker *b = new PushStateTracker;
  PushStateTracker *c = new PushStateTracker;
  MutableState<int> *state = new MutableState<int>(0);
  b->addState(state);
  LOKA_VERIFY(operation.open(a) == OPEN_OK);
  LOKA_VERIFY(operation.open(b) == OPEN_OK);
  LOKA_VERIFY(operation.open(c) == OPEN_OK);
  delete state;
  delete b;
  delete c;
  delete a;
  Ledger survivor;
  LOKA_VERIFY(operation.open(&survivor.tracker) == OPEN_OK);
  survivor.source.set(1);
  const OperationOutcome outcome = operation.close();
  LOKA_VERIFY(outcome.status == OPERATION_SETTLED && outcome.rounds == 1);
  LOKA_VERIFY(survivor.invalidations == 1);

  MutableState<int> shared(0);
  PushStateTracker first, second;
  first.addState(&shared);
  second.addState(&shared);
  first.begin();
  second.begin();
  StateTracker *route = shared.trackerOwner();
  LOKA_VERIFY(route == &second);
  LOKA_VERIFY(first.end());
  route = shared.trackerOwner();
  LOKA_VERIFY(route == &second);
  shared.set(1);
  LOKA_VERIFY(second.end());
  const bool dirty = second.transactionDirty();
  LOKA_VERIFY(dirty);
  route = shared.trackerOwner();
  LOKA_VERIFY(route == 0);
#else
  std::printf("[skip] Release withdrawal/route replacement requires NDEBUG without audit.\n");
#endif
}

void testOperationClockContracts()
{
#if defined(__linux__) && !defined(__SANITIZE_ADDRESS__) && !defined(NDEBUG)
  expectAssert(&closeTwice);
  expectAssert(&openAfterClose);
  expectAssert(&closeWithNestedGuard);
#else
  std::printf("[skip] Operation clock death pins require Linux debug without ASan.\n");
#endif
}

namespace
{
  struct JoinDuringDrive
  {
    Operation *outer;
    Ledger *target;
    int calls;
    static void invoke(void *data)
    {
      JoinDuringDrive &probe = *static_cast<JoinDuringDrive *>(data);
      ++probe.calls;
      LOKA_VERIFY(Operation::isSettling());
      Operation inner;
      LOKA_VERIFY(testing::OperationTestAccess::active() == probe.outer);
      LOKA_VERIFY(inner.open(&probe.target->tracker) == OPEN_OK);
      probe.target->source.set(1);
      const OperationOutcome checkpoint = inner.settle();
      const OperationOutcome closed = inner.close();
      LOKA_VERIFY(checkpoint.status == OPERATION_JOINED && checkpoint.rounds == 0);
      LOKA_VERIFY(closed.status == OPERATION_JOINED && closed.rounds == 0);
      LOKA_VERIFY(probe.target->invalidations == 0);
    }
  };
}

void testNestedOperationJoinsOuterClock()
{
  Ledger a, b, c;
  Operation outer;
  LOKA_VERIFY(outer.open(&a.tracker) == OPEN_OK);
  JoinDuringDrive probe = { &outer, &c, 0 };
  a.tracker.setInvalidateCallback(&JoinDuringDrive::invoke, &probe);
  {
    Operation inner;
    LOKA_VERIFY(testing::OperationTestAccess::active() == &outer);
    LOKA_VERIFY(inner.open(&b.tracker) == OPEN_OK);
    a.source.set(1);
    b.source.set(1);
    const OperationOutcome checkpoint = inner.settle();
    const OperationOutcome closed = inner.close();
    LOKA_VERIFY(checkpoint.status == OPERATION_JOINED && checkpoint.rounds == 0);
    LOKA_VERIFY(closed.status == OPERATION_JOINED && closed.rounds == 0);
    LOKA_VERIFY(probe.calls == 0 && b.invalidations == 0);
  }
  LOKA_VERIFY(testing::OperationTestAccess::active() == &outer);
  const OperationOutcome outcome = outer.close();
  LOKA_VERIFY(outcome.status == OPERATION_SETTLED && outcome.rounds == 2);
  LOKA_VERIFY(probe.calls == 1 && b.invalidations == 1 && c.invalidations == 1);
  LOKA_VERIFY(!testing::OperationTestAccess::active());
  verifyClosed(a);
  verifyClosed(b);
  verifyClosed(c);
}

void testSettleLeavesLedgersOpenWithRoutes()
{
  Ledger ledger;
  Operation turn;
  LOKA_VERIFY(turn.open(&ledger.tracker) == OPEN_OK);
  ledger.source.set(1);
  const OperationOutcome first = turn.settle();
  LOKA_VERIFY(first.status == OPERATION_SETTLED && first.rounds == 1);
  LOKA_VERIFY(testing::OperationTestAccess::isOpen(turn));
  LOKA_VERIFY(!Operation::isSettling());
  LOKA_VERIFY(ledger.source.trackerOwner() == &ledger.tracker);
  LOKA_VERIFY(ledger.tracker.phase() == TRACKER_PRECOMMIT);
  LOKA_VERIFY(Access::depth(ledger.tracker) == 1);
  ledger.source.set(2);
  LOKA_VERIFY(Access::currentDirtyCount(ledger.tracker) == 1);
  LOKA_VERIFY(Access::nextDirtyCount(ledger.tracker) == 0);
  LOKA_VERIFY(ledger.invalidations == 1);
  const OperationOutcome last = turn.close();
  LOKA_VERIFY(last.status == OPERATION_SETTLED && last.rounds == 2);
  LOKA_VERIFY(ledger.invalidations == 2);
  verifyClosed(ledger);
}

void testSettleThenCloseShareOneBudgetAndKeepFirstRefusal()
{
  {
    Ledger ledger;
    Operation turn(OperationBudget(2, 10));
    LOKA_VERIFY(turn.open(&ledger.tracker) == OPEN_OK);
    ledger.source.set(1);
    const OperationOutcome first = turn.settle();
    LOKA_VERIFY(first.status == OPERATION_SETTLED && first.rounds == 1);
    Write write = { &ledger, &ledger.source, 1 };
    ledger.tracker.setInvalidateCallback(&Write::invoke, &write);
    ledger.source.set(2);
    const OperationOutcome last = turn.close();
    LOKA_VERIFY(last.status == OPERATION_REFUSED_CHAIN_LIMIT && last.rounds == 2);
    LOKA_VERIFY(ledger.invalidations == 2);
    verifyClosed(ledger);
  }
  {
    Ledger ledger;
    DeferredLoop loop = { &ledger.tracker, 0, 0 };
    Operation turn(OperationBudget(5, 1));
    LOKA_VERIFY(turn.open(&ledger.tracker) == OPEN_OK);
    ledger.source.set(1);
    const OperationOutcome first = turn.settle();
    LOKA_VERIFY(first.status == OPERATION_SETTLED && first.rounds == 1);
    ledger.source.set(2);
    Access::defer(ledger.tracker, &DeferredLoop::invoke, &loop);
    const OperationOutcome refused = turn.settle();
    LOKA_VERIFY(refused.status == OPERATION_REFUSED_STATE_BUDGET && refused.rounds == 2);
    LOKA_VERIFY(ledger.invalidations == 1 && loop.calls == 0);
    const OperationOutcome repeated = turn.settle();
    LOKA_VERIFY(repeated.status == refused.status && repeated.rounds == refused.rounds);
    const OperationOutcome closed = turn.close();
    LOKA_VERIFY(closed.status == refused.status && closed.rounds == refused.rounds);
    LOKA_VERIFY(ledger.invalidations == 1);
    LOKA_VERIFY(loop.calls == 4 && loop.cleanupCalls == 4);
    verifyClosed(ledger);
  }
}

void testEmptyLedgerLeavesClockOnDestruction()
{
  for (int position = 0; position != 4; ++position)
  {
    Ledger a, b, appended;
    MutableState<int> state(0);
    PushStateTracker *empty = new PushStateTracker;
    empty->addState(&state);
    Operation turn;
    if (position == 0 || position == 3)
      LOKA_VERIFY(Operation::openActive(empty) == OPEN_OK);
    if (position != 3)
    {
      LOKA_VERIFY(Operation::openActive(&a.tracker) == OPEN_OK);
      if (position == 1)
        LOKA_VERIFY(Operation::openActive(empty) == OPEN_OK);
      LOKA_VERIFY(Operation::openActive(&b.tracker) == OPEN_OK);
      if (position == 2)
        LOKA_VERIFY(Operation::openActive(empty) == OPEN_OK);
    }
    LOKA_VERIFY(state.trackerOwner() == empty);
    empty->removeState(&state);
    LOKA_VERIFY(state.trackerOwner() == 0);
    delete empty;
    if (position == 3)
      LOKA_VERIFY(testing::OperationTestAccess::empty(turn));
    LOKA_VERIFY(Operation::openActive(&appended.tracker) == OPEN_OK);
    if (position != 3)
    {
      a.source.set(1);
      b.source.set(1);
    }
    appended.source.set(1);
    const OperationOutcome outcome = turn.close();
    LOKA_VERIFY(outcome.status == OPERATION_SETTLED && outcome.rounds == 1);
    LOKA_VERIFY(a.invalidations == (position == 3 ? 0 : 1));
    LOKA_VERIFY(b.invalidations == (position == 3 ? 0 : 1));
    LOKA_VERIFY(appended.invalidations == 1);
    LOKA_VERIFY(testing::OperationTestAccess::empty(turn));
    verifyClosed(a);
    verifyClosed(b);
    verifyClosed(appended);
  }
}

namespace
{
  struct WithdrawDuringRound
  {
    PushStateTracker *victim;
    Ledger *appended;
    bool appendFirst;
    int calls;
    void append()
    {
      LOKA_VERIFY(Operation::openActive(&this->appended->tracker) == OPEN_OK);
      this->appended->source.set(1);
    }
    static void invoke(void *data)
    {
      WithdrawDuringRound &probe = *static_cast<WithdrawDuringRound *>(data);
      ++probe.calls;
      if (probe.appendFirst)
        probe.append();
      delete probe.victim;
      probe.victim = 0;
      if (!probe.appendFirst)
        probe.append();
    }
  };
}

void testWithdrawDuringRoundKeepsDriverSafe()
{
  // Cursor, frontier, earlier member (neither), and cursor == frontier.
  // Appending on either side of withdrawal must still wait for a new round.
  for (int position = 0; position != 4; ++position)
    for (int appendFirst = 0; appendFirst != 2; ++appendFirst)
    {
      Ledger a, b, appended;
      PushStateTracker *victim = new PushStateTracker;
      WithdrawDuringRound probe = { victim, &appended, appendFirst != 0, 0 };
      a.tracker.setInvalidateCallback(&WithdrawDuringRound::invoke, &probe);
      Operation turn;
      if (position == 2)
        LOKA_VERIFY(Operation::openActive(victim) == OPEN_OK);
      LOKA_VERIFY(Operation::openActive(&a.tracker) == OPEN_OK);
      if (position == 0 || position == 3)
        LOKA_VERIFY(Operation::openActive(victim) == OPEN_OK);
      if (position != 3)
        LOKA_VERIFY(Operation::openActive(&b.tracker) == OPEN_OK);
      if (position == 1)
        LOKA_VERIFY(Operation::openActive(victim) == OPEN_OK);
      a.source.set(1);
      if (position != 3)
        b.source.set(1);
      const OperationOutcome outcome = turn.close();
      LOKA_VERIFY(outcome.status == OPERATION_SETTLED && outcome.rounds == 2);
      LOKA_VERIFY(probe.calls == 1 && probe.victim == 0);
      LOKA_VERIFY(b.invalidations == (position == 3 ? 0 : 1));
      LOKA_VERIFY(appended.invalidations == 1);
      LOKA_VERIFY(testing::OperationTestAccess::empty(turn));
      verifyClosed(a);
      verifyClosed(b);
      verifyClosed(appended);
    }
}

void testOpenActiveWithoutClockReportsNoClock()
{
  Ledger ledger;
  LOKA_VERIFY(Operation::openActive(&ledger.tracker) == OPEN_NO_CLOCK);
  verifyClosed(ledger);
  {
    Operation turn;
    Operation nested;
    LOKA_VERIFY(Operation::openActive(&ledger.tracker) == OPEN_OK);
    LOKA_VERIFY(Operation::openActive(&ledger.tracker) == OPEN_ALREADY_OPEN);
    LOKA_VERIFY(Access::depth(ledger.tracker) == 1);
  }
  LOKA_VERIFY(Operation::openActive(&ledger.tracker) == OPEN_NO_CLOCK);
  verifyClosed(ledger);
}

namespace
{
  struct ClosingOpen
  {
    Ledger *target;
    Ledger *idle;
    int calls;
    static void invoke(void *data)
    {
      ClosingOpen &probe = *static_cast<ClosingOpen *>(data);
      ++probe.calls;
      LOKA_VERIFY(Operation::openActive(&probe.target->tracker) == OPEN_REFUSED_CLOSING);
      LOKA_VERIFY(Operation::openActive(&probe.idle->tracker) == OPEN_REFUSED_CLOSING);
      LOKA_VERIFY(probe.target->source.trackerOwner() == 0);
      probe.target->source.set(9);
    }
  };
}

void testOpenActiveDuringClosingIsRefused()
{
  Ledger a, b, idle;
  ClosingOpen probe = { &b, &idle, 0 };
  Operation turn(OperationBudget(0));
  LOKA_VERIFY(Operation::openActive(&a.tracker) == OPEN_OK);
  LOKA_VERIFY(Operation::openActive(&b.tracker) == OPEN_OK);
  Access::defer(a.tracker, &ClosingOpen::invoke, &probe);
  const OperationOutcome outcome = turn.close();
  LOKA_VERIFY(outcome.status == OPERATION_REFUSED_CHAIN_LIMIT && outcome.rounds == 0);
  LOKA_VERIFY(probe.calls == 1 && b.source.get() == 9);
  const bool transactionDirty = b.tracker.transactionDirty();
  const bool pendingDirty = b.tracker.peekDirty();
  LOKA_VERIFY(!transactionDirty && !pendingDirty);
  LOKA_VERIFY(Access::currentDirtyCount(b.tracker) == 0);
  LOKA_VERIFY(Access::nextDirtyCount(b.tracker) == 0);
  LOKA_VERIFY(a.invalidations == 0 && b.invalidations == 0);
  verifyClosed(a);
  verifyClosed(b);
  verifyClosed(idle);
}

namespace
{
#if defined(__linux__) && !defined(__SANITIZE_ADDRESS__) && !defined(NDEBUG)
  void regimeSeatWithoutClock()
  {
    Ledger ledger;
    loka::app::scene::NodeState<int> seat(&ledger.source, &ledger.tracker);
    Operation::Regime regime;
    seat.writeSeat().set(1);
  }

  void regimeGuardWithoutClock()
  {
    Ledger ledger;
    Operation::Regime regime;
    StateTrackerGuard guard(&ledger.tracker);
    ledger.source.set(1);
  }

  void nestedRegime()
  {
    Operation::Regime outer;
    Operation::Regime inner;
  }
#endif
}

void testRegimeNoClockSeatWriteAsserts()
{
#if defined(__linux__) && !defined(__SANITIZE_ADDRESS__) && !defined(NDEBUG)
  expectAssert(&regimeSeatWithoutClock);
#else
  std::printf("[skip] regime seat death pin requires Linux debug without ASan.\n");
#endif
}

void testRegimeNoClockGuardAsserts()
{
#if defined(__linux__) && !defined(__SANITIZE_ADDRESS__) && !defined(NDEBUG)
  expectAssert(&regimeGuardWithoutClock);
#else
  std::printf("[skip] regime guard death pin requires Linux debug without ASan.\n");
#endif
}

void testRegimeNestedDeclarationAsserts()
{
#if defined(__linux__) && !defined(__SANITIZE_ADDRESS__) && !defined(NDEBUG)
  expectAssert(&nestedRegime);
#else
  std::printf("[skip] nested regime death pin requires Linux debug without ASan.\n");
#endif
}

void testRegimeInsideTurnSeatJoins()
{
  Ledger ledger;
  loka::app::scene::NodeState<int> seat(&ledger.source, &ledger.tracker);
  Operation::Regime regime;
  {
    Operation turn;
    seat.writeSeat().set(1);
    LOKA_VERIFY(ledger.source.get() == 1 && ledger.invalidations == 0);
    LOKA_VERIFY(Operation::openActive(&ledger.tracker) == OPEN_ALREADY_OPEN);
    LOKA_VERIFY(Access::depth(ledger.tracker) == 1);
  }
  LOKA_VERIFY(ledger.invalidations == 1);
  verifyClosed(ledger);
}

void testRegimeStandaloneLedgerWithoutClockStaysSynchronous()
{
  Ledger ledger(LEDGER_STANDALONE);
  loka::app::scene::NodeState<int> seat(&ledger.source, &ledger.tracker);
  Operation::Regime regime;
  seat.writeSeat().set(1);
  LOKA_VERIFY(ledger.source.get() == 1 && ledger.invalidations == 1);
  const std::vector<StateBase *> &committed = ledger.tracker.committedSet().identities();
  LOKA_VERIFY(committed.size() == 1 && committed[0] == &ledger.source);
  LOKA_VERIFY(Operation::openActive(&ledger.tracker) == OPEN_REFUSED_STANDALONE);
  verifyClosed(ledger);
}

void testOpenActiveOrderWithClock()
{
  Ledger standalone(LEDGER_STANDALONE), joining;
  NonPushTracker nonPush;
  Operation turn(OperationBudget(0));
  LOKA_VERIFY(Operation::openActive(&standalone.tracker) == OPEN_REFUSED_STANDALONE);
  LOKA_VERIFY(Operation::openActive(&nonPush) == OPEN_REFUSED_NOT_PUSH);
  LOKA_VERIFY(Operation::openActive(&joining.tracker) == OPEN_OK);
  joining.source.set(1);
  const OperationOutcome outcome = turn.settle();
  LOKA_VERIFY(outcome.status == OPERATION_REFUSED_CHAIN_LIMIT);
  LOKA_VERIFY(Operation::openActive(&standalone.tracker) == OPEN_CLOCK_REFUSED);
  LOKA_VERIFY(Operation::openActive(&nonPush) == OPEN_CLOCK_REFUSED);
}
