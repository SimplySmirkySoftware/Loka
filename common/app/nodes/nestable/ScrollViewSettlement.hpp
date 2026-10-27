#ifndef LOKA_APP_NODES_NESTABLE_SCROLL_VIEW_SETTLEMENT_HPP
#define LOKA_APP_NODES_NESTABLE_SCROLL_VIEW_SETTLEMENT_HPP
#include "app/nodes/nestable/ScrollView.hpp"
#include "app/scene/state/RequestSettlement.hpp"
namespace loka
{
  namespace app
  {
    /** Shared stack seat policy. Rails supply only their apply-and-report operation. */
    class ScrollViewSeat : public scene::SeatOperation<ScrollTarget, core::Frame>
    {
    public:
      explicit ScrollViewSeat(const ScrollViewVisit &visit)
          : visit_(visit)
      {
      }
      virtual scene::Admission admit(scene::Node &base, scene::RequestBinding<ScrollTarget> &binding)
      {
        binding = static_cast<ScrollViewNode &>(base).props.scrollTo_;
        return base.lifecycleFact() == scene::NODE_FACT_ATTACHED && binding.isValid()
                       && !binding.state()->get().isNone()
                   ? scene::ADMISSION_TAKE
                   : scene::ADMISSION_EMPTY;
      }
      virtual bool current(scene::Node &base, const scene::RequestBinding<ScrollTarget> &binding)
      {
        return base.lifecycleFact() == scene::NODE_FACT_ATTACHED
               && static_cast<ScrollViewNode &>(base).props.scrollTo_.same(binding);
      }
      virtual ScrollRequestResult resolve(scene::Node &base, const scene::RequestBinding<ScrollTarget> &binding)
      {
        return this->visit_.succeeded() && this->current(base, binding)
                       && static_cast<ScrollViewNode &>(base).props.viewport_.isValid()
                   ? SCROLL_OK
                   : SCROLL_UNAVAILABLE;
      }
      virtual ScrollRequestResult validate(scene::Node &base, const ScrollTarget &pending)
      {
        ScrollViewNode &node = static_cast<ScrollViewNode &>(base);
        const core::Frame target = node.normalizedOrigin(pending);
        const core::Frame current = node.requestedOrigin();
        return (target.x != current.x && !this->visit_.canMove(ScrollViewVisit::X))
                       || (target.y != current.y && !this->visit_.canMove(ScrollViewVisit::Y))
                   ? SCROLL_UNAVAILABLE
                   : SCROLL_OK;
      }
      virtual ScrollRequestResult report(scene::Node &, const core::Frame &)
      {
        return SCROLL_OK;
      }
      virtual scene::FollowUp finishTake(scene::Node &,
                                         const scene::Reply<ScrollTarget> &,
                                         const scene::RequestApplication<core::Frame, ScrollRequestResult> &)
      {
        return scene::FOLLOW_NONE;
      }

    private:
      const ScrollViewVisit visit_;
    };
  } // namespace app
} // namespace loka
#endif
