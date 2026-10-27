#include "RequestReasonTests.hpp"
#include "app/scene/state/RequestSettlement.hpp"
#include "app/nodes/nestable/Box.hpp"
#include "support/TestVerify.hpp"
namespace
{
  using namespace loka::app;
  using namespace loka::app::scene;
  using namespace RequestReasonTest;
  class Owner : public SettleOwner<Fact>
  {
  public:
    Fact value;
    explicit Owner(BorrowPhase &phase, Node &node)
        : SettleOwner<Fact>(phase, &node, node.getContext())
    {
    }
    virtual Fact fact(Node &) const
    {
      return this->value;
    }
    virtual FollowUpResult finishSettle(Node &, const FollowUps &)
    {
      return FOLLOW_UP_NONE;
    }
  };
  template <class R> class Seat : public SeatOperation<R, Fact>
  {
    typedef typename RequestTraits<R>::Reason Reason;

  public:
    loka::core::MutableState<R> pending;
    loka::core::MutableState<Reply<R> > reply;
    Owner &owner;
    const Reason outcome;
    Seat(Owner &o, Reason result)
        : pending(R(7)),
          owner(o),
          outcome(result)
    {
    }
    virtual Admission admit(Node &, RequestBinding<R> &binding)
    {
      binding = RequestBinding<R>(WriteSeat<R>(&this->pending), WriteSeat<Reply<R> >(&this->reply));
      return this->pending.get().isNone() ? ADMISSION_EMPTY : ADMISSION_TAKE;
    }
    virtual Reason resolve(Node &, const RequestBinding<R> &)
    {
      return RequestTraits<R>::accepted();
    }
    virtual Reason validate(Node &, const R &)
    {
      return this->outcome;
    }
    virtual RequestApplication<Fact, Reason> apply(Node &, const R &request)
    {
      return RequestApplication<Fact, Reason>(Fact(request.value), RequestTraits<R>::accepted());
    }
    virtual Reason report(Node &, const Fact &fact)
    {
      this->owner.value = fact;
      return RequestTraits<R>::accepted();
    }
    virtual bool current(Node &, const RequestBinding<R> &)
    {
      return true;
    }
    virtual FollowUp finishTake(Node &, const Reply<R> &, const RequestApplication<Fact, Reason> &)
    {
      return FOLLOW_NONE;
    }
  };
  template <class R> void checkReplies(typename RequestTraits<R>::Reason refusal)
  {
    const R wanted(7), clamped(3);
    const Reply<R> granted = formReply(wanted, wanted, RequestTraits<R>::accepted());
    const Reply<R> limited = formReply(wanted, clamped, RequestTraits<R>::accepted());
    const Reply<R> refused = formReply(wanted, clamped, refusal);
    const Reply<R> heterogeneous = formReply(wanted, Fact(3), RequestTraits<R>::accepted());
    const Reply<R> heterogeneousRefused = formReply(wanted, Fact(3), refusal);
    LOKA_VERIFY(granted.kind() == Reply<R>::GRANTED && granted.applied().value == 7);
    LOKA_VERIFY(limited.kind() == Reply<R>::CLAMPED && limited.applied().value == 3);
    LOKA_VERIFY(limited.requested().value == 7);
    LOKA_VERIFY(refused.kind() == Reply<R>::REFUSED && refused.reason() == refusal);
    LOKA_VERIFY(heterogeneous.kind() == Reply<R>::GRANTED && heterogeneous.applied().value == 7);
    LOKA_VERIFY(heterogeneousRefused.kind() == Reply<R>::REFUSED && heterogeneousRefused.reason() == refusal);
  }
} // namespace
void testRequestReasonsStayRequestTyped()
{
  checkReplies<First>(FIRST_REFUSED);
  checkReplies<Second>(SECOND_REFUSED);
  BorrowPhase &phase = loka::app::testing::controllerlessBorrowPhase();
  BoxNode node((BoxProps()));
  Owner owner(phase, node);
  Seat<First> first(owner, FIRST_ACCEPTED);
  Seat<Second> second(owner, SECOND_REFUSED);
  loka::app::testing::SettleTraceCapture<Fact>::clear();
  LOKA_VERIFY(RequestSettlement<Fact>::settle(owner, first, second, SETTLE_INPUT, Fact()) == FOLLOW_UP_NONE);
  LOKA_VERIFY(owner.value.value == 7);
  LOKA_VERIFY(first.reply.get().kind() == Reply<First>::GRANTED);
  LOKA_VERIFY(second.reply.get().kind() == Reply<Second>::REFUSED);
  LOKA_VERIFY(second.reply.get().reason() == SECOND_REFUSED);
  LOKA_VERIFY((loka::app::testing::SettleTrace<First, Fact>::instance().at(0).seam[0] == FIRST_ACCEPTED));
  LOKA_VERIFY((loka::app::testing::SettleTrace<Second, Fact>::instance().at(0).seam[0] == SECOND_REFUSED));
}
void testRequestSingleHeterogeneousSeat()
{
  BorrowPhase &phase = loka::app::testing::controllerlessBorrowPhase();
  BoxNode node((BoxProps()));
  Owner owner(phase, node);
  Seat<First> first(owner, FIRST_ACCEPTED);
  LOKA_VERIFY(RequestSettlement<Fact>::settle(owner, first, SETTLE_INPUT, Fact()) == FOLLOW_UP_NONE);
  LOKA_VERIFY(first.pending.get().isNone());
  LOKA_VERIFY(first.reply.get().kind() == Reply<First>::GRANTED);
  LOKA_VERIFY(owner.value.value == 7);
  Seat<Second> successfulSecond(owner, SECOND_ACCEPTED);
  successfulSecond.pending.set(Second(9));
  LOKA_VERIFY(RequestSettlement<Fact>::settle(owner, successfulSecond, SETTLE_INPUT, owner.value) == FOLLOW_UP_NONE);
  LOKA_VERIFY(successfulSecond.reply.get().kind() == Reply<Second>::GRANTED);
  LOKA_VERIFY(owner.value.value == 9);
  Seat<Second> second(owner, SECOND_REFUSED);
  LOKA_VERIFY(RequestSettlement<Fact>::settle(owner, second, SETTLE_INPUT, owner.value) == FOLLOW_UP_NONE);
  LOKA_VERIFY(second.pending.get().isNone());
  LOKA_VERIFY(second.reply.get().kind() == Reply<Second>::REFUSED);
  LOKA_VERIFY(second.reply.get().reason() == SECOND_REFUSED);
  LOKA_VERIFY(owner.value.value == 9);
}
