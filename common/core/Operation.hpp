#ifndef LOKA_CORE_OPERATION_HPP
#define LOKA_CORE_OPERATION_HPP

#include <cstddef>

namespace loka
{
  namespace core
  {
    namespace testing { struct OperationTestAccess; }
    class StateTracker;
    class PushStateTracker;

    /** Remaining clock work shared by every ledger in an Operation. */
    struct OperationBudget
    {
      explicit OperationBudget(size_t workRounds = 1000, size_t stateSteps = 1000)
          : rounds(workRounds), stateIterations(stateSteps) {}
      size_t rounds;
      size_t stateIterations;
    };

    enum OperationStatus
    {
      OPERATION_SETTLED,
      OPERATION_JOINED,
      OPERATION_REFUSED_CHAIN_LIMIT,
      OPERATION_REFUSED_STATE_BUDGET
    };

    /** Completed clock result; rounds counts work rounds, never empty probes. */
    struct OperationOutcome
    {
      OperationOutcome(OperationStatus result, size_t workRounds)
          : status(result), rounds(workRounds) {}
      const OperationStatus status;
      const size_t rounds;
    };

    enum OpenResult
    {
      OPEN_OK,
      OPEN_ALREADY_OPEN,
      OPEN_REFUSED_BUSY,
      OPEN_REFUSED_NOT_PUSH,
      OPEN_NO_CLOCK,
      OPEN_REFUSED_CLOSING,
      /** The active clock refused an earlier settle; it runs no further work
          round, so a ledger not yet enrolled keeps its own transaction. */
      OPEN_CLOCK_REFUSED
    };

    /** Main-thread stack clock borrowing ledgers until close. Nested clocks join
        the outermost clock, which alone settles and closes all ledgers. A ledger
        that still holds registered States must outlive the close; an owner that
        has unregistered every State may destroy its ledger at any time, which
        withdraws it. Destroying the stepping ledger from its own callback remains
        a contract violation. Nested tracker guards
        must end before settlement. Completion turns collect, settle, apply, close,
        then reclaim; collection turns only collect, settle and close. Fair rounds
        visit the ledgers present at each round's start. WriteSeat enrolls its
        owner ledger through openActive, as does StateTrackerGuard.
        StandaloneTransactionGuard brackets an idle, unenrolled ledger and never joins. */
    class Operation
    {
    public:
      /** Declares the rail run-loop interval in which joining writes require an
          Operation turn. Bootstrap and shutdown remain outside this scope.
          Main-thread only; nested declarations are a contract violation. */
      class Regime
      {
      public:
        Regime();
        ~Regime();
      private:
        Regime(const Regime &);
        Regime &operator=(const Regime &);
      };

      explicit Operation(const OperationBudget &budget = OperationBudget());
      ~Operation();
      /** Opens an idle joining ledger, or recognizes one already in this clock.
          May be called during work rounds, never during cleanup or after close. */
      OpenResult open(StateTracker *tracker);
      /** The single active-clock door for write seats and guards. After honoring
          an active clock's cleanup/refusal, null/non-Push trackers
          are refused before answering OPEN_NO_CLOCK.
          An eligible tracker without a clock returns OPEN_NO_CLOCK for legacy
          begin/end, and asserts in debug if a rail Regime requires a turn.
          Cleanup returns OPEN_REFUSED_CLOSING so the caller writes without a route. A clock
          whose settle was refused returns OPEN_CLOCK_REFUSED for a ledger it
          does not hold (the caller keeps its own transaction) and
          OPEN_ALREADY_OPEN for one it does. Otherwise forwards to open without
          exposing the active clock pointer. */
      static OpenResult openActive(StateTracker *tracker);
      /** Checkpoint with routes and ledger levels retained. The shared budget,
          first refusal and work-round count survive every checkpoint. */
      OperationOutcome settle();
      /** True only while the outermost clock drives or cleans up ledgers. */
      static bool isSettling();
      /** True while any clock is active on the main thread. Rails assert the
          absence of a clock before blocking in the OS; tests observe identity
          through the testing access layer instead. */
      static bool hasActive();
      /** Drives and closes once. The destructor closes if this was not called.
          The first refusal wins; cleanup never invalidates a ledger. */
      OperationOutcome close();

    private:
      Operation(const Operation &);
      Operation &operator=(const Operation &);
      friend class PushStateTracker;
      friend struct testing::OperationTestAccess;

      enum Phase { OPEN, DRIVING, CLOSING, CLOSED };
      static Operation *active_;
      static Regime *regime_;
      static bool regimeDeclared();
      Operation *const outer_;
      PushStateTracker *head_;
      PushStateTracker *tail_;
      /** Suspended round positions, repaired by withdraw; null outside DRIVING. */
      PushStateTracker *cursor_;
      PushStateTracker *frontier_;
      /** The ledger whose step or cleanup is on the stack; its destruction is a
          contract violation even when it holds no rows. Null between visits. */
      PushStateTracker *driving_;
      OperationBudget budget_;
      Phase phase_;
      OperationStatus status_;
      size_t rounds_;

      bool hasWork() const;
      /** Unlink an empty dying ledger and repair suspended round positions.
          Also retains the release fallback for a violated registered-row contract.
          Never touches State routes; the stepping ledger must remain alive. */
      void withdraw(PushStateTracker *tracker);
    };
  }
}

#endif
