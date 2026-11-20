#ifndef LOKA_TESTS_SUPPORT_REFUSED_SCROLL_VIEW_SEAT_HPP
#define LOKA_TESTS_SUPPORT_REFUSED_SCROLL_VIEW_SEAT_HPP

#include "app/scene/boundary/BoundaryInnerStateOwner.hpp"
#include "app/scene/state/StateBatchBase.hpp"
#include "core/Frame.hpp"
#include "support/LokaAllocFailure.hpp"
#include "support/TestVerify.hpp"

namespace loka
{
  namespace testing
  {
    /** Heap-backed owner whose viewport declaration encounters a real allocation refusal. */
    class RefusedScrollViewSeat : private app::scene::BoundaryInnerStateOwner
    {
    public:
      app::scene::NodeState<core::Frame> viewport;

      RefusedScrollViewSeat()
          : refused_(false)
      {
        core::testing::failLokaAllocRaw("StateOwner", "MutableState", 1);
        app::scene::StateBatchBase::CreateImmediateState(this, this->viewport, core::Frame(3, 7, 0, 0));
        LOKA_VERIFY(this->refused_ && !this->viewport.isValid());
        core::testing::allowLokaAllocRaw();
      }

    private:
      virtual void noteStateAllocationFailure()
      {
        this->refused_ = true;
      }
      bool refused_;
    };
  } // namespace testing
} // namespace loka
#endif
