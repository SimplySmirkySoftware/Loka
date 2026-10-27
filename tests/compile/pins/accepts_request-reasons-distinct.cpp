#include "RequestReasonTests.hpp"
#include "app/scene/state/RequestSettlement.hpp"
using namespace loka::app::scene;
using namespace RequestReasonTest;
void pin(SettleOwner<Fact> &owner, SeatOperation<First, Fact> &first, SeatOperation<Second, Fact> &second)
{
  FirstReason (Reply<First>::*firstReason)() const = &Reply<First>::reason;
  SecondReason (Reply<Second>::*secondReason)() const = &Reply<Second>::reason;
  FirstReason (RequestApplication<Fact, FirstReason>::*firstResult)() const =
      &RequestApplication<Fact, FirstReason>::result;
  SecondReason (RequestApplication<Fact, SecondReason>::*secondResult)() const =
      &RequestApplication<Fact, SecondReason>::result;
  (void)firstReason;
  (void)secondReason;
  (void)firstResult;
  (void)secondResult;
  Reply<First>::Refused(First(), FIRST_REFUSED);
  RequestSettlement<Fact>::settle(owner,
                                  first,
                                  second,
                                  SETTLE_INPUT
#ifdef TEST_BUILD
                                  ,
                                  Fact()
#endif
  );
#ifdef TEST_BUILD
  loka::app::testing::SettleTraceRow<First, Fact> a(SETTLE_INPUT, Fact());
  FirstReason *firstSeam = a.seam;
  loka::app::testing::SettleTraceRow<Second, Fact> b(SETTLE_INPUT, Fact());
  SecondReason *secondSeam = b.seam;
  (void)firstSeam;
  (void)secondSeam;
#endif
}
