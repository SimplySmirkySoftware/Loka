#ifndef LOKA_UTIL_STATE_TRACKER_GUARD_HPP
#define LOKA_UTIL_STATE_TRACKER_GUARD_HPP
#include "core/StateTracker.hpp"
#include "core/Operation.hpp"
#include <cassert>

namespace loka
{
  namespace core
  {
    /** A transaction that joins the active clock when its ledger can enroll.
        When joined to a turn the callback is not invoked; owners that need a post-commit
        callback observe the State.
        Refused enrollment retains the legacy begin/end bracket. */
    struct StateTrackerGuard
    {
      typedef void (*InvalidateFn)(void *userData);
      StateTracker *tracker;
      InvalidateFn invalidateFn;
      void *invalidateUserData;
      StateTrackerGuard(StateTracker *t, InvalidateFn fn = 0, void *userData = 0)
          : tracker(t),
            invalidateFn(fn),
            invalidateUserData(userData),
            mode_(NONE)
      {
        if (!this->tracker)
          return;
        switch (Operation::openActive(this->tracker))
        {
        case OPEN_OK:
        case OPEN_ALREADY_OPEN:
          this->mode_ = JOINED;
          return;
        case OPEN_REFUSED_BUSY:
        case OPEN_REFUSED_CLOSING:
        case OPEN_NO_CLOCK:
        case OPEN_CLOCK_REFUSED:
        case OPEN_REFUSED_NOT_PUSH:
          this->tracker->begin();
          this->mode_ = OWNED;
        }
      }
      ~StateTrackerGuard()
      {
        if (this->mode_ == OWNED)
        {
          bool settled = tracker->end();
          assert(settled && "StateTracker transaction did not settle");
          if (settled && invalidateFn && tracker->phase() == TRACKER_IDLE)
          {
            PushStateTracker *push = tracker->asPushTracker();
            if (push && push->transactionDirty())
              invalidateFn(invalidateUserData);
          }
        }
      }
    private:
      enum Mode { NONE, JOINED, OWNED };
      Mode mode_;
      StateTrackerGuard(const StateTrackerGuard &);
      StateTrackerGuard &operator=(const StateTrackerGuard &);
    };

    /** The bootstrap bracket. Its precondition is an idle ledger that no
        Operation has enrolled; an unrelated turn may be active, and the bracket
        never joins it (bootstrap visibility is pinned inside a turn). Its
        begin/end settles preparation before its owner reads the result; its
        callback mechanism mirrors the OWNED bracket above. */
    struct StandaloneTransactionGuard
    {
      typedef void (*InvalidateFn)(void *userData);
      StateTracker *tracker;
      InvalidateFn invalidateFn;
      void *invalidateUserData;
      StandaloneTransactionGuard(StateTracker *t, InvalidateFn fn = 0, void *userData = 0)
          : tracker(t),
            invalidateFn(fn),
            invalidateUserData(userData)
      {
        if (tracker)
        {
          assert(tracker->phase() == TRACKER_IDLE && "bootstrap bracket opens on an idle ledger");
          tracker->begin();
        }
      }
      ~StandaloneTransactionGuard()
      {
        if (tracker)
        {
          bool settled = tracker->end();
          assert(settled && "StateTracker transaction did not settle");
          if (settled && invalidateFn && tracker->phase() == TRACKER_IDLE)
          {
            PushStateTracker *push = tracker->asPushTracker();
            if (push && push->transactionDirty())
              invalidateFn(invalidateUserData);
          }
        }
      }
    private:
      StandaloneTransactionGuard(const StandaloneTransactionGuard &);
      StandaloneTransactionGuard &operator=(const StandaloneTransactionGuard &);
    };

  } // namespace core
} // namespace loka

#endif // LOKA_UTIL_STATE_TRACKER_GUARD_HPP
