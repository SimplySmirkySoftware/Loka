#include "core/StateTracker.hpp"
#include "core/State.hpp"
#include "core/Operation.hpp"
#include <cassert>
#include <cstdio>

namespace loka
{
  namespace core
  {

    namespace
    {
      void reportOperationStatus(OperationStatus status)
      {
        switch (status)
        {
        case OPERATION_SETTLED:
        case OPERATION_JOINED:
          break;
        case OPERATION_REFUSED_STATE_BUDGET:
          fprintf(stderr, "[Loka] StateTracker transaction did not settle before the iteration limit.\n");
          break;
        case OPERATION_REFUSED_CHAIN_LIMIT:
          fprintf(stderr, "[Loka] StateTracker commit chain did not settle before the iteration limit.\n");
          break;
        }
      }
    }

    Operation *Operation::active_ = 0;
    Operation::Regime *Operation::regime_ = 0;

    Operation::Regime::Regime()
    {
      assert(!Operation::regimeDeclared() && "Operation::Regime declarations must not nest");
      Operation::regime_ = this;
    }

    Operation::Regime::~Regime()
    {
      Operation::regime_ = 0;
    }

    bool Operation::regimeDeclared()
    {
      return Operation::regime_ != 0;
    }

    Operation::Operation(const OperationBudget &budget)
        : outer_(Operation::active_), head_(0), tail_(0), cursor_(0), frontier_(0),
          driving_(0), budget_(budget), phase_(OPEN),
          status_(OPERATION_SETTLED), rounds_(0)
    {
      if (!this->outer_)
        Operation::active_ = this;
    }

    Operation::~Operation()
    {
      if (!this->outer_ && this->phase_ != CLOSED)
        this->close();
    }

    OpenResult Operation::open(StateTracker *tracker)
    {
      if (this->outer_)
        return this->outer_->open(tracker);
      assert(this->phase_ == OPEN || this->phase_ == DRIVING);
      assert(tracker);
      PushStateTracker *ledger = tracker->asPushTracker();
      if (!ledger)
        return OPEN_REFUSED_NOT_PUSH;
      if (ledger->op_ == this)
        return OPEN_ALREADY_OPEN;
      if (ledger->phase() != TRACKER_IDLE)
        return OPEN_REFUSED_BUSY;
      ledger->begin();
      ledger->op_ = this;
      ledger->opNext_ = 0;
      if (this->tail_)
        this->tail_->opNext_ = ledger;
      else
        this->head_ = ledger;
      this->tail_ = ledger;
      return OPEN_OK;
    }

    OpenResult Operation::openActive(StateTracker *tracker)
    {
      if (Operation::active_ && Operation::active_->phase_ == CLOSING)
        return OPEN_REFUSED_CLOSING;
      if (Operation::active_ && Operation::active_->status_ != OPERATION_SETTLED)
      {
        // No further work round will run in this clock: a new ledger keeps its
        // own transaction; one already held stays held (its intake was refused).
        PushStateTracker *const ledger = tracker ? tracker->asPushTracker() : 0;
        return ledger && ledger->op_ == Operation::active_ ? OPEN_ALREADY_OPEN : OPEN_CLOCK_REFUSED;
      }
      PushStateTracker *const ledger = tracker ? tracker->asPushTracker() : 0;
      if (!ledger)
        return OPEN_REFUSED_NOT_PUSH;
      if (!Operation::active_)
      {
        assert(!Operation::regimeDeclared()
               && "Open an Operation turn on the rail path that reached this write");
        return OPEN_NO_CLOCK;
      }
      return Operation::active_->open(tracker);
    }

    bool Operation::hasWork() const
    {
      for (PushStateTracker *t = this->head_; t; t = t->opNext_)
        if (t->hasWork())
          return true;
      return false;
    }

    bool Operation::hasActive()
    {
      return Operation::active_ != 0;
    }

    bool Operation::isSettling()
    {
      return Operation::active_ && (Operation::active_->phase_ == DRIVING
                                    || Operation::active_->phase_ == CLOSING);
    }

    OperationOutcome Operation::settle()
    {
      if (this->outer_)
        return OperationOutcome(OPERATION_JOINED, 0);
      assert(this->phase_ == OPEN && "Operation settles only while open, outside its callbacks");
      this->phase_ = DRIVING;
      while (this->status_ == OPERATION_SETTLED && this->hasWork())
      {
        if (this->budget_.rounds == 0)
        {
          this->status_ = OPERATION_REFUSED_CHAIN_LIMIT;
          break;
        }
        --this->budget_.rounds;
        this->frontier_ = this->tail_;
        this->cursor_ = this->head_;
        ++this->rounds_;
        while (this->cursor_ && this->frontier_)
        {
          PushStateTracker *t = this->cursor_;
          this->cursor_ = t->opNext_;
          if (t->hasWork())
          {
            this->driving_ = t;
            const PushStateTracker::StepResult result = t->step(this->budget_);
            this->driving_ = 0;
            if (result == PushStateTracker::STEP_STATE_BUDGET)
            {
              this->status_ = OPERATION_REFUSED_STATE_BUDGET;
              break;
            }
          }
          if (t == this->frontier_)
            break;
        }
        this->cursor_ = 0;
        this->frontier_ = 0;
        if (this->status_ != OPERATION_SETTLED)
          break;
      }
      this->phase_ = OPEN;
      return OperationOutcome(this->status_, this->rounds_);
    }

    OperationOutcome Operation::close()
    {
      if (this->outer_)
        return OperationOutcome(OPERATION_JOINED, 0);
      this->settle();
      this->phase_ = CLOSING;
      for (PushStateTracker *t = this->head_; t; t = t->opNext_)
        t->removeRoutes();
      for (PushStateTracker *t = this->head_; t; t = t->opNext_)
      {
        this->driving_ = t;
        const bool drained = t->drainDeferred(this->budget_);
        this->driving_ = 0;
        if (!drained && this->status_ == OPERATION_SETTLED)
          this->status_ = OPERATION_REFUSED_CHAIN_LIMIT;
      }
      while (this->head_)
      {
        PushStateTracker *t = this->head_;
        t->releaseClockLevel();
        this->head_ = t->opNext_;
        t->opNext_ = 0;
        t->op_ = 0;
      }
      this->tail_ = 0;
      this->phase_ = CLOSED;
      Operation::active_ = 0;
      reportOperationStatus(this->status_);
      return OperationOutcome(this->status_, this->rounds_);
    }

    void Operation::withdraw(PushStateTracker *tracker)
    {
      PushStateTracker *previous = 0;
      for (PushStateTracker *t = this->head_; t; t = t->opNext_)
      {
        if (t == tracker)
        {
          if (this->cursor_ == t)
            this->cursor_ = t->opNext_;
          if (this->frontier_ == t)
            this->frontier_ = previous;
          if (previous)
            previous->opNext_ = t->opNext_;
          else
            this->head_ = t->opNext_;
          if (this->tail_ == t)
            this->tail_ = previous;
          t->opNext_ = 0;
          t->op_ = 0;
          return;
        }
        previous = t;
      }
    }

    PushStateTracker::PushStateTracker()
        : phase_(TRACKER_IDLE),
          pendingDirty_(false),
          depth_(0),
          reentrantDepth_(0),
          invalidateFn_(0),
          invalidateUserData_(0),
          invalidateTarget_(0),
          visitPass_(0),
          initialEntry_(),
          statesHead_(0),
          statesTail_(0),
          freeEntries_(&initialEntry_),
          chunks_(0),
          op_(0),
          opNext_(0)
    {
    }

    PushStateTracker::PushStateTracker(const std::vector<StateBase *> &states)
        : phase_(TRACKER_IDLE),
          pendingDirty_(false),
          depth_(0),
          reentrantDepth_(0),
          invalidateFn_(0),
          invalidateUserData_(0),
          invalidateTarget_(0),
          visitPass_(0),
          initialEntry_(),
          statesHead_(0),
          statesTail_(0),
          freeEntries_(&initialEntry_),
          chunks_(0),
          op_(0),
          opNext_(0)
    {
      for (size_t i = 0; i < states.size(); ++i)
      {
        addState(states[i]);
      }
    }

    PushStateTracker::~PushStateTracker()
    {
      if (this->op_)
      {
        // An empty ledger may leave the clock, unless its own step or cleanup
        // is on the stack: the driver resumes into this object afterwards.
        if (this->statesHead_ || this->op_->driving_ == this)
          assert(!"ledger destroyed while open by an Operation");
        this->op_->withdraw(this);
      }
      releaseEntries();
    }

    void PushStateTracker::begin()
    {
      if (depth_ > 0)
      {
        ++depth_;
        return;
      }
      if (phase_ != TRACKER_IDLE)
      {
        ++reentrantDepth_;
        return;
      }
      ++depth_;
      for (StateEntry *e = statesHead_; e; e = e->next)
        this->installRoute(e->state);
      transaction_.begin();
      phase_ = TRACKER_PRECOMMIT;
    }

    void PushStateTracker::defer(TrackerDeferKey, void (*fn)(void *), void *userData)
    {
      transaction_.intake(phase_).deferred.push_back(std::make_pair(fn, userData));
    }

    void PushStateTracker::markDirty(StateBase *state)
    {
      if (!state)
      {
        return;
      }
      // Every completed walk clears its active stamps. On unsigned wrap,
      // skip the idle sentinel; no graph-wide reset is needed.
      if (++this->visitPass_ == 0)
        ++this->visitPass_;
      this->propagateDirty(state, this->visitPass_);
    }

    void PushStateTracker::propagateDirty(StateBase *state, unsigned long pass)
    {
      TransactionIntake &intake = this->transaction_.markDirty(this->phase_);
      pendingDirty_ = true;
      DependencyMap::iterator it = dependents.find(state);
      if (it != dependents.end())
      {
        Dependents &row = it->second;
        if (row.visitPass == pass)
        {
          fprintf(stderr, "[Loka] Circular state dependency detected: StateBase %p\n", (void *)state);
          return;
        }
        row.visitPass = pass;
        for (size_t i = 0; i < row.states.size(); ++i)
        {
          this->propagateDirty(row.states[i], pass);
        }
        row.visitPass = 0;
      }
      // A walk stamp describes the active path, not intake membership across
      // separate writes. Keep the duplicate scan for both rows and leaves.
      for (size_t i = 0; i < intake.dirtyStates.size(); ++i)
      {
        if (intake.dirtyStates[i] == state)
        {
          return;
        }
      }
      intake.dirtyStates.push_back(state);
    }

    void PushStateTracker::addState(StateBase *state)
    {
      if (!state)
      {
        return;
      }
      // Check for duplicates by traversing the linked list
      for (StateEntry *e = statesHead_; e; e = e->next)
      {
        if (e->state == state)
        {
          return;
        }
      }
      // Append to linked list
      StateEntry *entry = allocateEntry(state);
      if (statesTail_)
      {
        statesTail_->next = entry;
      }
      else
      {
        statesHead_ = entry;
      }
      statesTail_ = entry;

      std::vector<StateBase *> deps = state->getDependencyStates();
      if (!deps.empty())
      {
        for (size_t j = 0; j < deps.size(); ++j)
        {
          registerDependency(state, deps[j]);
        }
      }
      if (phase_ != TRACKER_IDLE)
      {
        this->installRoute(state);
      }
    }

    void PushStateTracker::addStateUnchecked(StateBase *state)
    {
      if (!state)
      {
        return;
      }
      // Skip duplicate check - caller guarantees uniqueness
      // Append to linked list
      StateEntry *entry = allocateEntry(state);
      if (statesTail_)
      {
        statesTail_->next = entry;
      }
      else
      {
        statesHead_ = entry;
      }
      statesTail_ = entry;

      std::vector<StateBase *> deps = state->getDependencyStates();
      if (!deps.empty())
      {
        for (size_t j = 0; j < deps.size(); ++j)
        {
          registerDependency(state, deps[j]);
        }
      }
      if (phase_ != TRACKER_IDLE)
      {
        this->installRoute(state);
      }
    }

    void PushStateTracker::removeState(StateBase *state)
    {
      if (!state)
      {
        return;
      }

      StateEntry *prev = 0;
      StateEntry *entry = statesHead_;
      while (entry)
      {
        if (entry->state == state)
        {
          StateEntry *next = entry->next;
          if (prev)
          {
            prev->next = next;
          }
          else
          {
            statesHead_ = next;
          }
          if (statesTail_ == entry)
          {
            statesTail_ = prev;
          }
          entry->state = 0;
          entry->next = freeEntries_;
          freeEntries_ = entry;
          break;
        }
        prev = entry;
        entry = entry->next;
      }

      transaction_.removeState(state);
      // Preserve the active loop index when a recompute removes a later state.
      for (size_t i = 0; i < this->scratch_.size(); ++i)
      {
        if (this->scratch_[i] == state)
          this->scratch_[i] = 0;
      }

      dependents.erase(state);
      for (DependencyMap::iterator it = dependents.begin(); it != dependents.end(); ++it)
      {
        StateList &list = it->second.states;
        for (size_t i = 0; i < list.size();)
        {
          if (list[i] == state)
          {
            list.erase(list.begin() + i);
          }
          else
          {
            ++i;
          }
        }
      }
      if (state->currentTracker == this)
      {
        state->currentTracker = 0;
      }
    }

    void PushStateTracker::reserveStates(size_t count)
    {
      if (count == 0)
      {
        return;
      }
      transaction_.current.dirtyStates.reserve(count);
      transaction_.next.dirtyStates.reserve(count);
      this->transaction_.committed.reserve(count);
      this->scratch_.reserve(count);
      allocateEntries(count);
    }

    bool PushStateTracker::end()
    {
      if (depth_ == 0)
      {
        if (reentrantDepth_ > 0)
        {
          --reentrantDepth_;
        }
        return true;
      }
      --depth_;
      if (depth_ > 0)
        return true;
      assert(this->op_ == 0 && "Operation owns the final clock level");
      OperationBudget budget;
      OperationStatus status = OPERATION_SETTLED;
      while (this->hasWork() && budget.rounds > 0)
      {
        --budget.rounds;
        if (this->step(budget) == STEP_STATE_BUDGET)
        {
          status = OPERATION_REFUSED_STATE_BUDGET;
          break;
        }
      }
      if (this->hasWork() && status == OPERATION_SETTLED)
        status = OPERATION_REFUSED_CHAIN_LIMIT;
      this->removeRoutes();
      if (!this->drainDeferred(budget) && status == OPERATION_SETTLED)
        status = OPERATION_REFUSED_CHAIN_LIMIT;
      // end() consumed the legacy level above; Operation releases its level
      // only after the same route-removal and deferred-drain stages.
      reportOperationStatus(status);
      return status == OPERATION_SETTLED;
    }

    PushStateTracker::StepResult PushStateTracker::step(OperationBudget &budget)
    {
      // Rotate on entry only: the completed snapshot of the previous step
      // stays readable until the next step or the next begin(). A step that
      // starts on work already in current (a write from another ledger of the
      // clock) opens a fresh snapshot too, so the invalidate callback sees only
      // this commit's identities.
      if (!this->transaction_.current.hasWork() && this->transaction_.next.hasWork())
        this->transaction_.advance();
      else
        this->transaction_.beginCommit();
      this->phase_ = TRACKER_PRECOMMIT;
      if (!this->settleCurrentTransaction(budget.stateIterations))
        return STEP_STATE_BUDGET;
      this->phase_ = TRACKER_COMMIT;
      for (size_t i = 0; i < this->transaction_.current.deferred.size(); ++i)
        this->transaction_.current.deferred[i].first(
            this->transaction_.current.deferred[i].second);
      this->transaction_.current.deferred.clear();
      if (this->transaction_.current.dirty() && this->invalidateFn_)
        this->invalidateFn_(this->invalidateUserData_);
      this->transaction_.current.finishCommit();
      this->phase_ = TRACKER_PRECOMMIT;
      return STEP_DONE;
    }

    bool PushStateTracker::hasWork() const
    {
      return this->transaction_.current.hasWork() || this->transaction_.next.hasWork();
    }

    void PushStateTracker::installRoute(StateBase *state)
    {
#ifdef LOKA_LIFECYCLE_AUDIT
      assert(state->currentTracker == 0 || state->currentTracker == this);
#endif
      state->currentTracker = this;
    }

    void PushStateTracker::removeRoutes()
    {
      for (StateEntry *e = this->statesHead_; e; e = e->next)
        if (e->state->currentTracker == this)
          e->state->currentTracker = 0;
      this->phase_ = TRACKER_IDLE;
    }

    bool PushStateTracker::drainDeferred(OperationBudget &budget)
    {
      // Keep the empty intakes' capacity on ordinary successful legacy clocks.
      if (this->transaction_.current.deferred.empty() &&
          this->transaction_.next.deferred.empty())
        return true;
      // The first pass discharges leftover obligations even after refusal has
      // spent every round. Only callbacks queued again consume retry rounds.
      for (;;)
      {
        DeferredList current;
        DeferredList next;
        current.swap(this->transaction_.current.deferred);
        next.swap(this->transaction_.next.deferred);
        for (size_t i = 0; i < current.size(); ++i)
          current[i].first(current[i].second);
        for (size_t i = 0; i < next.size(); ++i)
          next[i].first(next[i].second);
        if (this->transaction_.current.deferred.empty() &&
            this->transaction_.next.deferred.empty())
          return true;
        if (budget.rounds == 0)
        {
          this->transaction_.current.deferred.clear();
          this->transaction_.next.deferred.clear();
          return false;
        }
        --budget.rounds;
      }
    }

    void PushStateTracker::releaseClockLevel()
    {
      assert(this->depth_ == 1);
      this->depth_ = 0;
    }

    bool PushStateTracker::settleCurrentTransaction(size_t &iterationsRemaining)
    {
      while (!transaction_.current.dirtyStates.empty() && iterationsRemaining > 0)
      {
        --iterationsRemaining;
        this->scratch_.swap(transaction_.current.dirtyStates);
        for (size_t i = 0; i < this->scratch_.size(); ++i)
        {
          StateBase *state = this->scratch_[i];
          if (!state)
            continue;
          this->transaction_.committed.add(state);
          if (!state->recompute())
          {
            continue;
          }
          DependencyMap::iterator it = dependents.find(state);
          if (it == dependents.end())
          {
            continue;
          }
          for (size_t dependentIndex = 0;
               dependentIndex < it->second.states.size();
               ++dependentIndex)
          {
            markDirty(it->second.states[dependentIndex]);
          }
        }
        this->scratch_.clear();
      }
      return transaction_.current.dirtyStates.empty();
    }

    void PushStateTracker::CommittedSet::clear()
    {
      this->identities_.clear();
      this->complete_ = true;
    }

    void PushStateTracker::CommittedSet::add(StateBase *state)
    {
      for (size_t i = 0; i < this->identities_.size(); ++i)
      {
        if (this->identities_[i] == state)
          return;
      }
      this->identities_.push_back(state);
    }

    void PushStateTracker::CommittedSet::removeState(StateBase *state)
    {
      for (size_t i = 0; i < this->identities_.size();)
      {
        if (this->identities_[i] == state)
        {
          this->identities_.erase(this->identities_.begin() + i);
          this->complete_ = false;
        }
        else
        {
          ++i;
        }
      }
    }

    void PushStateTracker::TransactionIntake::clear()
    {
      this->dirtyStates.clear();
      this->deferred.clear();
      this->finishCommit();
    }

    bool PushStateTracker::TransactionIntake::hasWork() const
    {
      return this->dirty_ || !this->dirtyStates.empty() || !this->deferred.empty();
    }

    void PushStateTracker::TransactionIntake::removeState(StateBase *state)
    {
      for (size_t i = 0; i < this->dirtyStates.size();)
      {
        if (this->dirtyStates[i] == state)
        {
          this->dirtyStates.erase(this->dirtyStates.begin() + i);
        }
        else
        {
          ++i;
        }
      }
    }

    void PushStateTracker::TransactionIntake::swap(TransactionIntake &other)
    {
      this->dirtyStates.swap(other.dirtyStates);
      this->deferred.swap(other.deferred);
      const bool otherDirty = other.dirty_;
      other.dirty_ = this->dirty_;
      this->dirty_ = otherDirty;
    }

    void PushStateTracker::TrackerTransaction::begin()
    {
      this->current.clear();
      this->next.clear();
      this->beginCommit();
      this->anyDirty_ = false;
    }

    PushStateTracker::TransactionIntake &
    PushStateTracker::TrackerTransaction::intake(TrackerPhase phase)
    {
      return phase == TRACKER_COMMIT ? this->next : this->current;
    }

    PushStateTracker::TransactionIntake &
    PushStateTracker::TrackerTransaction::markDirty(TrackerPhase phase)
    {
      TransactionIntake &result = this->intake(phase);
      result.markDirty();
      this->anyDirty_ = true;
      return result;
    }

    void PushStateTracker::TrackerTransaction::advance()
    {
      this->current.clear();
      this->current.swap(this->next);
      this->next.clear();
      this->beginCommit();
    }

    void PushStateTracker::TrackerTransaction::beginCommit()
    {
      this->committed.clear();
    }

    void PushStateTracker::TrackerTransaction::removeState(StateBase *state)
    {
      this->current.removeState(state);
      this->next.removeState(state);
      this->committed.removeState(state);
    }

    PushStateTracker::StateEntry *PushStateTracker::allocateEntry(StateBase *state)
    {
      StateEntry *entry = 0;
      if (freeEntries_)
      {
        entry = freeEntries_;
        freeEntries_ = freeEntries_->next;
      }
      else
      {
        // The first row is inline; amortize further unreserved growth. The
        // chunk chain owns only heap rows, and removals recycle either kind.
        enum { kEntryChunkCapacity = 16 };
        allocateEntries(kEntryChunkCapacity);
        entry = freeEntries_;
        freeEntries_ = freeEntries_->next;
      }
      entry->state = state;
      entry->next = 0;
      return entry;
    }

    void PushStateTracker::allocateEntries(size_t count)
    {
      StateEntry *entries = new StateEntry[count];
      StateEntryChunk *chunk = new StateEntryChunk();
      chunk->entries = entries;
      chunk->count = count;
      chunk->next = chunks_;
      chunks_ = chunk;
      for (size_t i = 0; i < count; ++i)
      {
        StateEntry *entry = &entries[i];
        entry->next = freeEntries_;
        freeEntries_ = entry;
      }
    }

    void PushStateTracker::releaseEntries()
    {
      statesHead_ = 0;
      statesTail_ = 0;
      freeEntries_ = 0;
      while (chunks_)
      {
        StateEntryChunk *next = chunks_->next;
        delete[] chunks_->entries;
        delete chunks_;
        chunks_ = next;
      }
    }

    // A transaction is closed only when no level is open and no settlement is
    // running: a level joined during settlement (begin() while phase_ is not
    // idle) sees depth_ == 0 yet must not read or clear the owner's pending
    // dirt; the outer end() owns it until the phase returns to idle.
    bool PushStateTracker::peekDirty() const
    {
      return depth_ == 0 && phase_ == TRACKER_IDLE && pendingDirty_;
    }

    bool PushStateTracker::consumeDirty()
    {
      if (depth_ > 0 || phase_ != TRACKER_IDLE)
      {
        return false;
      }
      bool dirty = pendingDirty_;
      pendingDirty_ = false;
      return dirty;
    }

    void PushStateTracker::registerDependency(StateBase *dependent, StateBase *dependency)
    {
      if (!dependent || !dependency)
      {
        return;
      }
      StateList &list = dependents[dependency].states;
      for (size_t i = 0; i < list.size(); ++i)
      {
        if (list[i] == dependent)
        {
          return;
        }
      }
      list.push_back(dependent);
    }

    TrackerPhase PushStateTracker::phase() const
    {
      return phase_;
    }

  } // namespace core
} // namespace loka
