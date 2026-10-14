#ifndef LOKA_APP_SCENE_STATE_WRITE_SEAT_HPP
#define LOKA_APP_SCENE_STATE_WRITE_SEAT_HPP

#include "core/State.hpp"
#include "core/StateTracker.hpp"
#include "core/Operation.hpp"

namespace loka
{
  namespace app
  {
    namespace scene
    {
      template <typename T> class NodeState;

      /** Input-write door carrying the state owner's tracker.
          Inside a turn an eligible owner ledger joins the clock; the write's derived
          values, dirty summary and Scene projection complete at the turn's
          settle/apply, not when set returns. Source value and direct observers
          are synchronous as before. Standalone ledgers keep the synchronous
          begin/set/end bracket. */
      template <typename T> class WriteSeat
      {
      public:
        WriteSeat() : state_(0), tracker_(0) {}

        /** Test-only compatibility door. It cannot settle an owner tracker. */
        explicit WriteSeat(loka::core::MutableState<T> *state) : state_(state), tracker_(0) {}

        bool isValid() const { return this->state_ != 0; }
        loka::core::State<T> *state() const { return this->state_; }

        /** Tests transaction compatibility without exposing mutation authority. */
        bool usesTracker(const loka::core::StateTracker *tracker) const
        {
          return tracker && this->tracker_ == tracker;
        }

        void set(const T &value, bool forceUpdate = false) const
        {
          if (!this->state_)
          {
            return;
          }
          if (!this->tracker_)
          {
            this->state_->set(value, forceUpdate);
            return;
          }
          switch (loka::core::Operation::openActive(this->tracker_))
          {
          case loka::core::OPEN_OK:
          case loka::core::OPEN_ALREADY_OPEN:
          case loka::core::OPEN_REFUSED_BUSY:
          case loka::core::OPEN_REFUSED_CLOSING:
            this->state_->set(value, forceUpdate);
            return;
          case loka::core::OPEN_NO_CLOCK:
          case loka::core::OPEN_CLOCK_REFUSED:
          case loka::core::OPEN_REFUSED_NOT_PUSH:
            if (this->tracker_->phase() == loka::core::TRACKER_IDLE)
            {
              this->tracker_->begin();
              this->state_->set(value, forceUpdate);
              this->tracker_->end();
            }
            else
            {
              this->state_->set(value, forceUpdate);
            }
            return;
          }
        }

      private:
        friend class NodeState<T>;
        WriteSeat(loka::core::MutableState<T> *state, loka::core::StateTracker *tracker)
            : state_(state), tracker_(tracker) {}

        loka::core::MutableState<T> *state_;
        loka::core::StateTracker *tracker_;
      };
    }
  }
}

#endif // LOKA_APP_SCENE_STATE_WRITE_SEAT_HPP
