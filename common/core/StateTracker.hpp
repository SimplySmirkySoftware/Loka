#ifndef LOKA_STATETRACKER_HPP
#define LOKA_STATETRACKER_HPP

#include "core/diag/LifecycleAudit.hpp"
#include <vector>
#include <functional>
#include <map>
#include <string>

namespace loka
{
  namespace app
  {
    namespace scene
    {
      class BoundaryNode;
    }
  }
  namespace core
  {

    class StateBase;
    class Operation;
    struct OperationBudget;

    // StateTracker transaction phase.
    enum TrackerPhase
    {
      TRACKER_IDLE = 0,
      TRACKER_PRECOMMIT = 1,
      TRACKER_COMMIT = 2
    };

    class PushStateTracker; // forward declaration
    namespace testing
    {
      struct PushStateTrackerTestAccess;
    }

    /** Permission for the lifetime-validated Boundary deferred callback path.
        Copies carry no ownership or lifetime extension. */
    class TrackerDeferKey
    {
    public:
      TrackerDeferKey(const TrackerDeferKey &) {}

    private:
      friend class loka::app::scene::BoundaryNode;
      friend struct testing::PushStateTrackerTestAccess;
      TrackerDeferKey() {}
    };

    class StateTracker
    {
    public:
      virtual void begin() = 0;
      /** Internal, not app-facing: a deferred (fn, void*) has no lifetime edge
          to the caller's owner. The sole production caller, BoundaryNode,
          carries its own refcounted, self-validating record. */
      virtual void defer(TrackerDeferKey, void (*fn)(void *), void *userData) = 0;
      virtual void markDirty(StateBase *state) = 0;
      virtual bool end() = 0;
      virtual void registerDependency(StateBase *dependent, StateBase *dependency) = 0;
      virtual TrackerPhase phase() const = 0;
      virtual PushStateTracker *asPushTracker()
      {
        return 0;
      }
      virtual ~StateTracker() {}
    };

    /** A standalone ledger never joins the clock: its owner commits before it
        reads the result (menu composition, Scene installation). It is the bridge
        until those owners become DSL seats with Boundary ledgers. */
    enum LedgerPolicy { LEDGER_JOINS, LEDGER_STANDALONE };

    class PushStateTracker : public StateTracker LOKA_AUDITED_AS(PushStateTracker)
    {
    public:
      typedef void (*InvalidateFn)(void *);
      typedef std::vector<StateBase *> StateList;
      /** One commit's surviving identities and whether any were erased.
          Readers borrow this value until the next step, begin, or removal. */
      class CommittedSet
      {
      public:
        CommittedSet() : identities_(), complete_(true) {}
        const StateList &identities() const { return this->identities_; }
        bool complete() const { return this->complete_; }
        /** Null means identities are unavailable: empty or partially erased. */
        const StateList *knownIdentities() const
        {
          return this->complete_ && !this->identities_.empty() ? &this->identities_ : 0;
        }
        void reserve(size_t count) { this->identities_.reserve(count); }
        void clear();
        void add(StateBase *state);
        void removeState(StateBase *state);

      private:
        StateList identities_;
        bool complete_;
      };

      PushStateTracker(const std::vector<StateBase *> &states, LedgerPolicy policy = LEDGER_JOINS);
      PushStateTracker(LedgerPolicy policy = LEDGER_JOINS);
      /** Begins a transaction. A guard opened while this tracker is settling
          joins the running transaction; its writes land in the current phase's
          intake and are drained by the settlement already in progress. */
      void begin();
      void defer(TrackerDeferKey, void (*fn)(void *), void *userData);
      void markDirty(StateBase *state);
      void addState(StateBase *state);
      void addStateUnchecked(StateBase *state);
      void removeState(StateBase *state);
      /** Reserves additional state entries and at least count dirty-state slots
          in every transaction buffer. Writes/settlement within that bound do
          not grow those buffers; unreserved or larger owners retain growth.
          Deferred callbacks have separate, unreserved storage. */
      void reserveStates(size_t count);
      bool end();
      /** Returns whether the current (or just-ended) transaction marked any state dirty. */
      bool transactionDirty() const
      {
        return this->transaction_.dirty();
      }
      /** Returns whether committed dirt is waiting to be acknowledged. */
      bool peekDirty() const;
      /** Returns and acknowledges committed dirt. */
      bool consumeDirty();
      const CommittedSet &committedSet() const
      {
        return this->transaction_.committed;
      }
      void setInvalidateCallback(InvalidateFn fn, void *userData)
      {
        invalidateFn_ = fn;
        invalidateUserData_ = userData;
      }
      /**
       * Names who this tracker's invalidate callback ends up notifying, as an
       * opaque identity that is compared and never dereferenced. A notified
       * party that also observes states directly uses it to recognize the
       * transactions whose commit already reaches it, so one mutation stays
       * one invalidation.
       */
      void setInvalidateTarget(const void *target)
      {
        invalidateTarget_ = target;
      }
      bool invalidatesTarget(const void *target) const
      {
        return target && invalidateTarget_ == target;
      }
      /**
       * Registers a dependency edge used for derived-state propagation.
       * Usually built from DerivedState dependencies.
       */
      void registerDependency(StateBase *dependent, StateBase *dependency);
      TrackerPhase phase() const;
      PushStateTracker *asPushTracker()
      {
        return this;
      }
      const PushStateTracker *asPushTracker() const
      {
        return this;
      }
      /** An empty ledger may be destroyed while enrolled and withdraws quietly.
          A ledger with registered States must outlive Operation close. Destroying
          the stepping ledger from its own callback remains a contract violation. */
      ~PushStateTracker();

    private:
      PushStateTracker(const PushStateTracker &);
      PushStateTracker &operator=(const PushStateTracker &);

      struct StateEntry
      {
        StateEntry()
            : state(0),
              next(0)
        {
        }
        StateBase *state;
        StateEntry *next;
      };
      struct StateEntryChunk
      {
        StateEntryChunk()
            : entries(0),
              count(0),
              next(0)
        {
        }
        StateEntry *entries;
        size_t count;
        StateEntryChunk *next;
      };

      // Typedefs to avoid nested template closers in C++98
      typedef std::pair<void (*)(void *), void *> DeferredEntry;
      typedef std::vector<DeferredEntry> DeferredList;
      /** Dependency rows own their active recursive-path stamp. Zero is idle. */
      struct Dependents
      {
        Dependents() : states(), visitPass(0) {}
        StateList states;
        unsigned long visitPass;
      };
      typedef std::map<StateBase *, Dependents> DependencyMap;

      /** Mutable intake owned by one side of a tracker transaction. */
      struct TransactionIntake
      {
        TransactionIntake()
            : dirtyStates(),
              deferred(),
              dirty_(false)
        {
        }

        void clear();
        bool hasWork() const;
        void markDirty() { this->dirty_ = true; }
        bool dirty() const { return this->dirty_; }
        void finishCommit() { this->dirty_ = false; }
        void removeState(StateBase *state);
        void swap(TransactionIntake &other);

        StateList dirtyStates;
        DeferredList deferred;
      private:
        bool dirty_;
      };

      /** Owns the current transaction, its snapshot, and next-commit intake. */
      struct TrackerTransaction
      {
        TrackerTransaction()
            : current(),
              next(),
              committed(),
              anyDirty_(false)
        {
        }

        void begin();
        TransactionIntake &intake(TrackerPhase phase);
        TransactionIntake &markDirty(TrackerPhase phase);
        bool dirty() const { return this->anyDirty_; }
        void advance();
        /** Opens a fresh commit snapshot without touching either intake. */
        void beginCommit();
        void removeState(StateBase *state);

        TransactionIntake current;
        TransactionIntake next;
        CommittedSet committed;

      private:
        /** Transaction history survives drained intakes and fresh snapshots. */
        bool anyDirty_;
      };

      const LedgerPolicy policy_;
      TrackerTransaction transaction_;
      /// dependents: dependency graph from a source state to dependent states.
      DependencyMap dependents;
      /// phase_: current tracker transaction phase.
      TrackerPhase phase_;
      /// pendingDirty_: committed dirt waiting for owner acknowledgment.
      bool pendingDirty_;
      /// depth_: nested begin/end depth counter.
      unsigned int depth_;
      /// reentrantDepth_: begin/end levels joined while settlement is running.
      unsigned int reentrantDepth_;
      /// invalidate callback (optional)
      InvalidateFn invalidateFn_;
      void *invalidateUserData_;
      const void *invalidateTarget_;
      /** Advanced only by the top-level markDirty door, independently of
          transaction nesting and settlement phase. Zero is never a walk. */
      unsigned long visitPass_;
      /** Borrowed settlement batch; retains storage across iterations. */
      StateList scratch_;
      /** The first registration row shares this tracker's identity and lifetime. */
      StateEntry initialEntry_;
      /// states: linked list (head/tail for O(1) append)
      StateEntry *statesHead_;
      StateEntry *statesTail_;
      StateEntry *freeEntries_;
      StateEntryChunk *chunks_;

      /** Borrowed clock membership; Operation owns the intrusive list. */
      Operation *op_;
      PushStateTracker *opNext_;

      friend struct testing::PushStateTrackerTestAccess;
      friend class Operation;
      enum StepResult { STEP_DONE, STEP_STATE_BUDGET };
      StepResult step(OperationBudget &budget);
      bool hasWork() const;
      void installRoute(StateBase *state);
      void removeRoutes();
      bool drainDeferred(OperationBudget &budget);
      void releaseClockLevel();
      void propagateDirty(StateBase *state, unsigned long pass);
      StateEntry *allocateEntry(StateBase *state);
      void allocateEntries(size_t count);
      void releaseEntries();
      bool settleCurrentTransaction(size_t &iterationsRemaining);
    };

  } // namespace core
} // namespace loka

#endif // LOKA_STATETRACKER_HPP
