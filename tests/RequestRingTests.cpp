#include "RequestRingTests.hpp"
#include "app/scene/state/RequestRing.hpp"
#include "RequestReasonTests.hpp"
#include "support/TestVerify.hpp"
namespace
{
  using loka::app::scene::RequestRing;
  using RequestReasonTest::First;
} // namespace
void testRequestRingFullRefusalPreservesStorage()
{
  First storage[3];
  storage[0] = First(9);
  RequestRing<First> ring(storage, 3);
  // Construction must not touch the holder's storage.
  LOKA_VERIFY(storage[0].value == 9 && ring.count() == 0);
  storage[0] = First::None();
  for (unsigned i = 1; i <= 3; ++i)
  {
    LOKA_VERIFY(ring.pushBack(First(i)));
    LOKA_VERIFY(ring.count() == i);
  }
  LOKA_VERIFY(!ring.pushBack(First(4)));
  LOKA_VERIFY(ring.count() == 3);
  for (unsigned i = 0; i < 3; ++i)
    LOKA_VERIFY(storage[i].value == static_cast<int>(i + 1));
}
void testRequestRingAdvanceVacatesAndWraps()
{
  First storage[3];
  RequestRing<First> ring(storage, 3);
  First next(9);
  LOKA_VERIFY(!ring.advance(next));
  LOKA_VERIFY(next.value == 9 && ring.count() == 0);
  for (unsigned i = 1; i <= 3; ++i)
    LOKA_VERIFY(ring.pushBack(First(i)));
  LOKA_VERIFY(ring.advance(next));
  LOKA_VERIFY(next.value == 1 && ring.count() == 2);
  LOKA_VERIFY(storage[0].isNone());
  LOKA_VERIFY(ring.pushBack(First(4)));
  LOKA_VERIFY(storage[0].value == 4 && ring.count() == 3);
  for (unsigned i = 2; i <= 4; ++i)
  {
    LOKA_VERIFY(ring.advance(next));
    LOKA_VERIFY(next.value == static_cast<int>(i) && ring.count() == 4 - i);
    LOKA_VERIFY(storage[(i - 1) % 3].isNone());
  }
  LOKA_VERIFY(!ring.advance(next));
  LOKA_VERIFY(next.value == 4 && ring.count() == 0);
}
void testRequestRingClearResetsStorageAndHead()
{
  First storage[4];
  RequestRing<First> ring(storage, 4);
  for (unsigned i = 1; i <= 3; ++i)
    LOKA_VERIFY(ring.pushBack(First(i)));
  First next;
  LOKA_VERIFY(ring.advance(next));
  LOKA_VERIFY(next.value == 1 && ring.count() == 2);
  ring.clear();
  LOKA_VERIFY(ring.count() == 0);
  for (unsigned i = 0; i < 4; ++i)
    LOKA_VERIFY(storage[i].isNone());
  LOKA_VERIFY(ring.pushBack(First(4)));
  LOKA_VERIFY(storage[0].value == 4 && ring.count() == 1);
  LOKA_VERIFY(ring.advance(next));
  LOKA_VERIFY(next.value == 4 && ring.count() == 0);
  // Clearing an empty ring must also reset a nonzero head.
  ring.clear();
  LOKA_VERIFY(ring.pushBack(First(5)));
  LOKA_VERIFY(storage[0].value == 5 && ring.count() == 1);
}
