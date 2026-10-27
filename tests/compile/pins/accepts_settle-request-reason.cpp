#include "RequestReasonTests.hpp"
#include "app/scene/state/RequestSettlement.hpp"
using namespace loka::app::scene;
using namespace RequestReasonTest;
template <class R> void complete(SettleOwner<Fact> &owner)
{
  SeatOperation<R, Fact> *seat = 0;
  RequestSettlement<Fact>::settle(owner,
                                  *seat,
                                  SETTLE_INPUT
#ifdef TEST_BUILD
                                  ,
                                  Fact()
#endif
  );
}
void pin(SettleOwner<Fact> &owner)
{
  complete<First>(owner);
}
