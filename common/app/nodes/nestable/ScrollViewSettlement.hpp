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
    /** The ScrollView visit's completion owner. It captures the node's context
        identity when constructed; SettleScrollViewVisit constructs it after the
        projection helper returns, so a context the helper creates is the one
        the settle borrows. */
    class ScrollViewVisitOwner : public scene::SettleOwner<core::Frame>
    {
    public:
      ScrollViewVisitOwner(scene::IPlatformController &controller, scene::Node *node)
          : scene::SettleOwner<core::Frame>(controller, node, node->getContext())
      {
      }
      virtual scene::FollowUpResult finishSettle(scene::Node &, const scene::FollowUps &)
      {
        return scene::FOLLOW_UP_NONE;
      }
#ifdef TEST_BUILD
      virtual core::Frame fact(scene::Node &base) const
      {
        const ScrollViewNode &node = static_cast<ScrollViewNode &>(base);
        return node.props.viewport_.isValid() ? node.props.viewport_.state()->get() : core::Frame();
      }
#endif
    };

    /** Borrows the controller and completed helper result for one synchronous settle. */
    template <class C, class Result> class ScrollViewVisitSeat : public ScrollViewSeat
    {
    public:
      ScrollViewVisitSeat(C &controller,
                          const Result &result,
                          bool (C::*apply)(ScrollViewNode &, const Result &, const core::Frame &))
          : ScrollViewSeat(result.visit),
            controller_(controller),
            result_(result),
            apply_(apply)
      {
      }
      virtual scene::RequestApplication<core::Frame, ScrollRequestResult> apply(scene::Node &base,
                                                                                const ScrollTarget &pending)
      {
        ScrollViewNode &node = static_cast<ScrollViewNode &>(base);
        const core::Frame target = node.normalizedOrigin(pending);
        const bool ok = (this->controller_.*this->apply_)(node, this->result_, target);
        return scene::RequestApplication<core::Frame, ScrollRequestResult>(node.props.viewport_.state()->get(),
                                                                           ok ? SCROLL_OK : SCROLL_UNAVAILABLE);
      }

    private:
      C &controller_;
      const Result &result_;
      bool (C::*const apply_)(ScrollViewNode &, const Result &, const core::Frame &);
    };

    namespace detail
    {
      template <class T> struct Identity
      {
        typedef T type;
      };

      /** Completion body called only by the adjacent visit runner overloads below. */
      template <class C, class Result>
      void SettleScrollViewResult(C &controller,
                                  bool (C::*apply)(ScrollViewNode &, const Result &, const core::Frame &),
                                  ScrollViewNode &node,
                                  const Result &result)
      {
        ScrollViewVisitOwner owner(controller, &node);
        ScrollViewVisitSeat<C, Result> seat(controller, result, apply);
        scene::RequestSettlement<core::Frame>::settle(owner,
                                                      seat,
                                                      scene::SETTLE_PROPS
#ifdef TEST_BUILD
                                                      ,
                                                      owner.fact(node)
#endif
        );
      }
    } // namespace detail

    /** Owns borrow -> helper -> identity -> seat -> one settle order. Rails supply
        the helper and native step, mirroring scene::detail::InputInvocation.
        These three arity overloads are one family and must stay adjacent; their
        common completion body is detail::SettleScrollViewResult above. */
    template <class C, class Result, class A>
    Result SettleScrollViewVisit(C &controller,
                                 Result (C::*layout)(ScrollViewNode *, A),
                                 bool (C::*apply)(ScrollViewNode &, const Result &, const core::Frame &),
                                 ScrollViewNode *node,
                                 typename detail::Identity<A>::type a)
    {
      scene::BorrowScope visit(controller);
      const Result result = (controller.*layout)(node, a);
      detail::SettleScrollViewResult(controller, apply, *node, result);
      return result;
    }
    template <class C, class Result, class A, class B>
    Result SettleScrollViewVisit(C &controller,
                                 Result (C::*layout)(ScrollViewNode *, A, B),
                                 bool (C::*apply)(ScrollViewNode &, const Result &, const core::Frame &),
                                 ScrollViewNode *node,
                                 typename detail::Identity<A>::type a,
                                 typename detail::Identity<B>::type b)
    {
      scene::BorrowScope visit(controller);
      const Result result = (controller.*layout)(node, a, b);
      detail::SettleScrollViewResult(controller, apply, *node, result);
      return result;
    }
    template <class C, class Result, class A, class B, class D>
    Result SettleScrollViewVisit(C &controller,
                                 Result (C::*layout)(scene::Node *, A, B, D),
                                 bool (C::*apply)(ScrollViewNode &, const Result &, const core::Frame &),
                                 ScrollViewNode *node,
                                 typename detail::Identity<A>::type a,
                                 typename detail::Identity<B>::type b,
                                 typename detail::Identity<D>::type d)
    {
      scene::BorrowScope visit(controller);
      const Result result = (controller.*layout)(node, a, b, d);
      detail::SettleScrollViewResult(controller, apply, *node, result);
      return result;
    }
  } // namespace app
} // namespace loka
#endif
