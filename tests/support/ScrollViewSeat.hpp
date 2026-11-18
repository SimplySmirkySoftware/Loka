#ifndef LOKA_TESTS_SUPPORT_SCROLL_VIEW_SEAT_HPP
#define LOKA_TESTS_SUPPORT_SCROLL_VIEW_SEAT_HPP

#include "app/scene/state/NodeState.hpp"
#include "core/Frame.hpp"
#include "core/State.hpp"
#include "core/StateTracker.hpp"

namespace loka
{
  namespace testing
  {
    /** The app-side viewport seat a test lends to ScrollView(viewport). The seat
        starts at content origin (x, y); with a top inset, y = -top keeps the top gap
        visible (extent 0), and y = 0 starts with the gap scrolled away. */
    class ScrollViewSeat
    {
    public:
      explicit ScrollViewSeat(int y = 0, int x = 0)
          : storage_(loka::core::Frame(x, y, 0, 0)),
            tracker_(),
            state_(&this->storage_, &this->tracker_)
      {
        this->tracker_.addState(&this->storage_);
      }

      loka::app::scene::NodeState<loka::core::Frame> &state()
      {
        return this->state_;
      }

    private:
      loka::core::MutableState<loka::core::Frame> storage_;
      loka::core::PushStateTracker tracker_;
      loka::app::scene::NodeState<loka::core::Frame> state_;

      ScrollViewSeat(const ScrollViewSeat &);
      ScrollViewSeat &operator=(const ScrollViewSeat &);
    };
  } // namespace testing
} // namespace loka

#endif
