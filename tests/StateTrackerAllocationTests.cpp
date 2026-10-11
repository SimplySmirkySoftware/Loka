#include "testing/core/StateTrackerTestAccess.hpp"
#include "StateTrackerAllocationTests.hpp"
#include "core/State.hpp"
#include "core/LokaAlloc.hpp"
#include "core/util/StateTrackerGuard.hpp"
#include "support/TestVerify.hpp"
#include <cassert>
#include <cstdio>
#include <new>
#ifdef LOKA_STATE_TRACKER_ALLOC_CENSUS
#include "support/AllocCensus.hpp"
#endif
#if defined(__linux__)
#include <unistd.h>
#include <cstring>
#endif

namespace
{
  using namespace loka::core;
  unsigned long gateCalls = 0;
  void *countGate(std::size_t size, const LokaAllocationSite &)
  {
    ++gateCalls;
    return new (std::nothrow) char[size];
  }
  void freeGate(void *p, const LokaAllocationSite &)
  {
    delete[] static_cast<char *>(p);
  }

  struct JoinedWrite
  {
    PushStateTracker *tracker;
    State<int> *source;
    MutableState<int> *target;
    TrackerPhase expectedPhase;
    int calls;
  };
  void writeJoined(void *p)
  {
    JoinedWrite &write = *static_cast<JoinedWrite *>(p);
    assert(write.tracker->phase() == write.expectedPhase);
    ++write.calls;
    if (write.target->get() != write.source->get())
    {
      StateTrackerGuard guard(write.tracker);
      write.target->set(write.source->get());
    }
  }

  struct DoubleEval : DerivedState<int>::EvalFn
  {
    State<int> *source;
    JoinedWrite *joined;
    int calls;
    explicit DoubleEval(State<int> *s)
        : source(s),
          joined(0),
          calls(0)
    {
    }
    int operator()()
    {
      ++this->calls;
      if (this->joined)
        writeJoined(this->joined);
      return this->source->get() * 2;
    }
  };

  struct RemoveDuringRecompute : StateBase
  {
    PushStateTracker *tracker;
    StateBase *removed;
    RemoveDuringRecompute(PushStateTracker *t, StateBase *s)
        : tracker(t),
          removed(s)
    {
    }
    bool recompute()
    {
      this->tracker->removeState(this->removed);
      return false;
    }
  };

  void countDeferredWork(void *data)
  {
    ++*static_cast<int *>(data);
  }

  struct RecomputeCount : StateBase
  {
    int calls;
    RecomputeCount()
        : calls(0)
    {
    }
    bool recompute()
    {
      ++this->calls;
      return false;
    }
  };
} // namespace


void testStateTrackerRegistrationGrowth()
{
  using namespace loka::core;
  enum { kStates = 33 };
  MutableState<int> states[kStates];
  StateBase *order[kStates];
  {
    PushStateTracker single;
#ifdef LOKA_STATE_TRACKER_ALLOC_CENSUS
    allocpin::BeginCapture(0);
#endif
    single.addState(&states[0]);
#ifdef LOKA_STATE_TRACKER_ALLOC_CENSUS
    allocpin::EndCapture();
    // MineSweeper's one-state owners must use their inline registration row.
    LOKA_VERIFY(allocpin::CaptureAllocCount(0) == 0);
    LOKA_VERIFY(allocpin::CaptureAllocBytes(0) == 0);
#endif
  }
  {
    PushStateTracker::StateList initial;
    initial.push_back(0);
    initial.push_back(&states[0]);
    initial.push_back(&states[0]);
#ifdef LOKA_STATE_TRACKER_ALLOC_CENSUS
    allocpin::BeginCapture(0);
#endif
    PushStateTracker fromVector(initial);
#ifdef LOKA_STATE_TRACKER_ALLOC_CENSUS
    allocpin::EndCapture();
    LOKA_VERIFY(allocpin::CaptureAllocCount(0) == 0);
#endif
    StateBase *singleOrder[] = {&states[0]};
    LOKA_VERIFY(testing::PushStateTrackerTestAccess::hasOrder(fromVector, singleOrder, 1));
  }
#ifdef LOKA_STATE_TRACKER_ALLOC_CENSUS
  allocpin::BeginCapture(0);
#endif
  PushStateTracker tracker;
#ifdef LOKA_STATE_TRACKER_ALLOC_CENSUS
  allocpin::EndCapture();
  LOKA_VERIFY(allocpin::CaptureAllocCount(0) == 0);
  allocpin::BeginCapture(0);
#endif
  for (int i = 0; i < kStates; ++i)
  {
    order[i] = &states[i];
    if (i % 2)
      tracker.addStateUnchecked(order[i]);
    else
      tracker.addState(order[i]);
  }
#ifdef LOKA_STATE_TRACKER_ALLOC_CENSUS
  allocpin::EndCapture();
  const unsigned long calls = allocpin::CaptureAllocCount(0);
  const unsigned long bytes = allocpin::CaptureAllocBytes(0);
  std::printf("tracker 33 registrations: heap=%lu bytes=%lu\n", calls, bytes);
  // Budget both heap traffic and slack: preallocating a huge array is not a win.
  LOKA_VERIFY(calls <= 4);
  LOKA_VERIFY(bytes <= 1024);
#endif
  LOKA_VERIFY(testing::PushStateTrackerTestAccess::hasOrder(tracker, order, kStates));
  for (int i = 0; i < kStates; ++i)
    tracker.removeState(&states[i]);
#ifdef LOKA_STATE_TRACKER_ALLOC_CENSUS
  allocpin::BeginCapture(0);
#endif
  for (int i = 0; i < kStates; ++i)
  {
    order[i] = &states[kStates - 1 - i];
    tracker.addState(order[i]);
    tracker.addState(order[i]); // duplicate registration must remain inert
  }
#ifdef LOKA_STATE_TRACKER_ALLOC_CENSUS
  allocpin::EndCapture();
  LOKA_VERIFY(allocpin::CaptureAllocCount(0) == 0);
#endif
  LOKA_VERIFY(testing::PushStateTrackerTestAccess::hasOrder(tracker, order, kStates));
  {
    StateTrackerGuard guard(&tracker);
    for (int i = 0; i < kStates; ++i)
    {
      StateTracker *owner = states[i].trackerOwner();
      LOKA_VERIFY(owner == &tracker);
      states[i].set(i + 1);
    }
  }
  for (int i = 0; i < kStates; ++i)
  {
    LOKA_VERIFY(states[i].get() == i + 1);
    StateTracker *owner = states[i].trackerOwner();
    LOKA_VERIFY(owner == 0);
  }
  // Explicit reservation remains additional even when reusable capacity exists.
  tracker.removeState(&states[0]);
  const size_t before = testing::PushStateTrackerTestAccess::freeEntryCount(tracker);
  LOKA_VERIFY(before > 0);
  tracker.reserveStates(3);
  LOKA_VERIFY(testing::PushStateTrackerTestAccess::freeEntryCount(tracker) == before + 3);
}

void testStateTrackerReservedPropagation()
{
  using namespace loka::core;
  // Keep the injected backend around the complete lifetime of its storage.
  LokaAllocSetBackend(&countGate, &freeGate);
  {
    MutableState<int> a(0), precommit(0), commit(0);
    DoubleEval *bEval = new DoubleEval(&a);
    DerivedState<int> b(&a, bEval);
    DoubleEval *cEval = new DoubleEval(&b);
    DerivedState<int> c(&b, cEval);
    PushStateTracker tracker;
    tracker.reserveStates(5);
    tracker.addState(&a);
    tracker.addState(&b);
    tracker.addState(&c);
    tracker.addState(&precommit);
    tracker.addState(&commit);
    JoinedWrite settleWrite = {&tracker, &b, &precommit, TRACKER_PRECOMMIT, 0};
    JoinedWrite commitWrite = {&tracker, &c, &commit, TRACKER_COMMIT, 0};

    // Exercise the chain alone, then both forms of joined transaction.
    // Use EvalFn for precommit work: State handler snapshot allocations are
    // separate from tracker propagation and must not enter this census.
    for (int mode = 0; mode != 2; ++mode)
    {
      if (mode == 0)
      {
        cEval->joined = 0;
        tracker.setInvalidateCallback(0, 0);
      }
      else
      {
        cEval->joined = &settleWrite;
        tracker.setInvalidateCallback(&writeJoined, &commitWrite);
      }
#ifdef LOKA_STATE_TRACKER_ALLOC_CENSUS
      // Reservation must also cover the first use of each rotating buffer.
      allocpin::BeginCapture(mode);
#endif
      {
        StateTrackerGuard guard(&tracker);
        a.set(a.get() + 1);
      }
#ifdef LOKA_STATE_TRACKER_ALLOC_CENSUS
      allocpin::EndCapture();
      const unsigned long firstHeapCalls = allocpin::CaptureAllocCount(mode);
      std::printf("tracker mode %d: first transaction heap=%lu\n", mode, firstHeapCalls);
      LOKA_VERIFY(firstHeapCalls == 0);
#endif
      const unsigned long beforeGate = gateCalls;
      const int beforeEvaluations = bEval->calls;
      const int beforeSettle = settleWrite.calls;
      const int beforeCommit = commitWrite.calls;
#ifdef LOKA_STATE_TRACKER_ALLOC_CENSUS
      allocpin::BeginCapture(mode);
#endif
      for (int i = 0; i != 100; ++i)
      {
        StateTrackerGuard guard(&tracker);
        a.set(a.get() + 1);
        a.set(a.get() + 1);
      }
#ifdef LOKA_STATE_TRACKER_ALLOC_CENSUS
      allocpin::EndCapture();
      const unsigned long heapCalls = allocpin::CaptureAllocCount(mode);
      std::printf("tracker mode %d: 100 transactions, heap=%lu gate=%lu\n", mode, heapCalls, gateCalls - beforeGate);
      LOKA_VERIFY(heapCalls == 0);
#endif
      LOKA_VERIFY(gateCalls == beforeGate);
      LOKA_VERIFY(bEval->calls == beforeEvaluations + 100);
      assert(b.get() == a.get() * 2);
      assert(c.get() == a.get() * 4);
      if (mode == 1)
      {
        assert(precommit.get() == b.get());
        assert(commit.get() == c.get());
        LOKA_VERIFY(settleWrite.calls == beforeSettle + 200);
        LOKA_VERIFY(commitWrite.calls == beforeCommit + 200);
        LOKA_VERIFY(tracker.committedSet().identities().size() == 1);
        LOKA_VERIFY(tracker.committedSet().identities()[0] == &commit);
      }
    }
  }
  LokaAllocSetBackend(0, 0);

  // Successful legacy clocks retain their warmed deferred intake capacity.
  PushStateTracker deferredTracker;
  int deferredCalls = 0;
  {
    StateTrackerGuard guard(&deferredTracker);
    testing::PushStateTrackerTestAccess::defer(deferredTracker, &countDeferredWork, &deferredCalls);
  }
#ifdef LOKA_STATE_TRACKER_ALLOC_CENSUS
  allocpin::BeginCapture(0);
#endif
  for (int i = 0; i < 100; ++i)
  {
    StateTrackerGuard guard(&deferredTracker);
    testing::PushStateTrackerTestAccess::defer(deferredTracker, &countDeferredWork, &deferredCalls);
  }
#ifdef LOKA_STATE_TRACKER_ALLOC_CENSUS
  allocpin::EndCapture();
  const unsigned long deferredAllocations = allocpin::CaptureAllocCount(0);
  std::printf("tracker warmed deferred: 100 transactions, heap=%lu\n", deferredAllocations);
  LOKA_VERIFY(deferredAllocations == 0);
#endif
  assert(deferredCalls == 101);
}

void testStateTrackerCycleAndDiamond()
{
  using namespace loka::core;
  RecomputeCount a, b, c, d;
  PushStateTracker tracker;
  tracker.reserveStates(4);
  tracker.addState(&a);
  tracker.addState(&b);
  tracker.addState(&c);
  tracker.addState(&d);
  tracker.registerDependency(&b, &a);
  tracker.registerDependency(&a, &b);
#if defined(__linux__)
  std::FILE *capture = std::tmpfile();
  LOKA_VERIFY(capture != 0);
  const int saved = dup(fileno(stderr));
  LOKA_VERIFY(saved >= 0);
  LOKA_VERIFY(std::fflush(stderr) == 0);
  LOKA_VERIFY(dup2(fileno(capture), fileno(stderr)) >= 0);
#endif
  tracker.begin();
  tracker.markDirty(&a);
  LOKA_VERIFY(tracker.end());

  assert(a.calls == 1);
  assert(b.calls == 1);
  LOKA_VERIFY(tracker.committedSet().identities().size() == 2);

  // Removal invalidates old edges. Reuse the state in an acyclic diamond;
  // a shared descendant with dependents must not be mistaken for a cycle.
  tracker.removeState(&a);
  tracker.addState(&a);
  tracker.registerDependency(&b, &a);
  tracker.registerDependency(&c, &a);
  tracker.registerDependency(&b, &c);
  tracker.registerDependency(&d, &b);
  tracker.begin();
  tracker.markDirty(&a);
  tracker.markDirty(&a);
  LOKA_VERIFY(tracker.end());
  assert(a.calls == 2);
  assert(b.calls == 2);
  assert(c.calls == 1);
  assert(d.calls == 1);
  LOKA_VERIFY(tracker.committedSet().identities().size() == 4);
#if defined(__linux__)
  LOKA_VERIFY(std::fflush(stderr) == 0);
  LOKA_VERIFY(dup2(saved, fileno(stderr)) >= 0);
  LOKA_VERIFY(close(saved) == 0);
  std::rewind(capture);
  char line[256];
  LOKA_VERIFY(std::fgets(line, sizeof(line), capture) != 0);
  assert(std::strstr(line, "[Loka] Circular state dependency detected: StateBase ") == line);
  LOKA_VERIFY(std::fgets(line, sizeof(line), capture) == 0);
  LOKA_VERIFY(std::fclose(capture) == 0);
#else
  std::printf("[skip] exact cycle diagnostic capture requires Linux; dirty-count pins still run.\n");
#endif
}

void testStateTrackerRemovesSettlementBorrow()
{
  using namespace loka::core;
  PushStateTracker tracker;
  RecomputeCount removed, survivor;
  RemoveDuringRecompute remover(&tracker, &removed);
  tracker.reserveStates(3);
  tracker.addState(&remover);
  tracker.addState(&removed);
  tracker.addState(&survivor);
  tracker.begin();
  tracker.markDirty(&remover);
  tracker.markDirty(&removed);
  tracker.markDirty(&survivor);
  LOKA_VERIFY(tracker.end());
  assert(removed.calls == 0);
  assert(survivor.calls == 1);
  LOKA_VERIFY(tracker.committedSet().identities().size() == 2);
  tracker.begin();
  tracker.markDirty(&survivor);
  LOKA_VERIFY(tracker.end());
  assert(survivor.calls == 2);
  LOKA_VERIFY(tracker.committedSet().identities().size() == 1);
}

void testStateTrackerVisitPassWrapsPastZero()
{
  using namespace loka::core;
  // The walk counter skips the idle sentinel on wrap: seeded at ULONG_MAX, the
  // next walk is 1, propagation still reaches the dependent once, and a cycle
  // is still refused exactly once instead of recursing forever.
  RecomputeCount a, b, c, d;
  PushStateTracker tracker;
  tracker.reserveStates(4);
  tracker.addState(&a);
  tracker.addState(&b);
  tracker.addState(&c);
  tracker.addState(&d);
  tracker.registerDependency(&b, &a);
  tracker.registerDependency(&d, &c);
  tracker.registerDependency(&c, &d);
  testing::PushStateTrackerTestAccess::seedVisitPass(tracker, ~0ul);
#if defined(__linux__)
  std::FILE *capture = std::tmpfile();
  LOKA_VERIFY(capture != 0);
  const int saved = dup(fileno(stderr));
  LOKA_VERIFY(saved >= 0);
  LOKA_VERIFY(std::fflush(stderr) == 0);
  LOKA_VERIFY(dup2(fileno(capture), fileno(stderr)) >= 0);
#endif
  tracker.begin();
  tracker.markDirty(&a); // walk wraps: ULONG_MAX + 1 == 0 is skipped, pass 1
  tracker.markDirty(&c); // pass 2, the cycle c <-> d is refused once
  LOKA_VERIFY(tracker.end());
  assert(a.calls == 1);
  assert(b.calls == 1);
  assert(c.calls == 1);
  assert(d.calls == 1);
  LOKA_VERIFY(tracker.committedSet().identities().size() == 4);
#if defined(__linux__)
  LOKA_VERIFY(std::fflush(stderr) == 0);
  LOKA_VERIFY(dup2(saved, fileno(stderr)) >= 0);
  close(saved);
  std::rewind(capture);
  char line[160];
  int diagnostics = 0;
  while (std::fgets(line, sizeof(line), capture))
  {
    if (std::strstr(line, "[Loka] Circular state dependency detected: StateBase ") == line)
      ++diagnostics;
  }
  std::fclose(capture);
  const bool refusedOnce = (diagnostics == 1);
  LOKA_VERIFY(refusedOnce);
#else
  std::printf("[skip] exact cycle diagnostic capture requires Linux; dirty-count pins still run.\n");
#endif
}
