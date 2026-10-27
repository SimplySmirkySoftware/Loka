#include "PendingRequestsTests.hpp"
#include "RequestReasonTests.hpp"
#include "app/nodes/nestable/Box.hpp"
#include "support/Headless.hpp"
#include "support/TestVerify.hpp"
namespace
{
  using namespace loka::app;
  using namespace loka::app::scene;
  using namespace RequestReasonTest;
  struct PendingProps : BoxProps
  {
    template <class T> static RequestBinding<T> binding(Request<T> &request)
    {
      return RequestBinding<T>(requestSeat(request));
    }
    template <class T> static RequestBinding<T> binding(RequestQueueBase<T> &queue)
    {
      return RequestBinding<T>(requestSeat(queue), replySeat(queue), queue);
    }
  };
  struct PendingFixture : HeadlessStateOwner
  {
    Request<First> first;
    Request<Second> second;
    Request<Second> replacement;
    RequestQueue<First, 3> firstQueue;
    RequestQueue<Second, 3> secondQueue;
    PendingFixture()
    {
      StateBatchBase::CreateImmediateState(this, this->first, First(1));
      StateBatchBase::CreateImmediateState(this, this->second, Second(2));
      StateBatchBase::CreateImmediateState(this, this->replacement, Second(2));
      StateBatchBase::CreateImmediateState(this, this->firstQueue, First::None());
      StateBatchBase::CreateImmediateState(this, this->secondQueue, Second::None());
    }
  };
  struct CrossPost
  {
    PendingFixture &fixture;
    const Second value;
    CrossPost(PendingFixture &f, Second v)
        : fixture(f),
          value(v)
    {
      f.first.state()->bind(&changed, this, false);
    }
    ~CrossPost()
    {
      this->fixture.first.state()->unbind(&changed, this);
    }
    static void changed(void *data)
    {
      CrossPost &self = *static_cast<CrossPost *>(data);
      LOKA_VERIFY(self.fixture.first.get().isNone());
      // The second snapshot must precede the first publication, which must
      // itself precede cancellation of the second slot.
      LOKA_VERIFY(self.fixture.second.get().value == 2);
      self.fixture.second.set(self.value);
    }
  };
  struct ReplaceBinding
  {
    PendingFixture &fixture;
    RequestBinding<Second> &binding;
    ReplaceBinding(PendingFixture &f, RequestBinding<Second> &b)
        : fixture(f),
          binding(b)
    {
      f.first.state()->bind(&changed, this, false);
    }
    ~ReplaceBinding()
    {
      this->fixture.first.state()->unbind(&changed, this);
    }
    static void changed(void *data)
    {
      ReplaceBinding &self = *static_cast<ReplaceBinding *>(data);
      self.binding = PendingProps::binding(self.fixture.replacement);
    }
  };
} // namespace
void testPendingRequestsSingleAndPair()
{
  PendingFixture f;
  PendingRequests::cancel(PendingProps::binding(f.first));
  LOKA_VERIFY(f.first.get().isNone() && f.second.get().value == 2);
  f.first.set(First(1));
  PendingRequests::cancel(PendingProps::binding(f.first), PendingProps::binding(f.second));
  LOKA_VERIFY(f.first.get().isNone() && f.second.get().isNone());
  LOKA_VERIFY(f.firstQueue.post(First(1)) == POST_ACCEPTED);
  LOKA_VERIFY(f.firstQueue.post(First(2)) == POST_ACCEPTED);
  LOKA_VERIFY(f.secondQueue.post(Second(3)) == POST_ACCEPTED);
  LOKA_VERIFY(f.secondQueue.post(Second(4)) == POST_ACCEPTED);
  PendingRequests::cancel(PendingProps::binding<First>(f.firstQueue));
  LOKA_VERIFY(f.firstQueue.state()->get().isNone() && f.firstQueue.pending() == 0);
  LOKA_VERIFY(f.secondQueue.state()->get().value == 3 && f.secondQueue.pending() == 1);
  LOKA_VERIFY(f.firstQueue.post(First(1)) == POST_ACCEPTED);
  LOKA_VERIFY(f.firstQueue.post(First(2)) == POST_ACCEPTED);
  PendingRequests::cancel(PendingProps::binding<First>(f.firstQueue), PendingProps::binding<Second>(f.secondQueue));
  LOKA_VERIFY(f.firstQueue.state()->get().isNone() && f.firstQueue.pending() == 0);
  LOKA_VERIFY(f.secondQueue.state()->get().isNone() && f.secondQueue.pending() == 0);
  LOKA_VERIFY(f.firstQueue.reply().state()->get().kind() == Reply<First>::NO_REPLY);
  LOKA_VERIFY(f.secondQueue.reply().state()->get().kind() == Reply<Second>::NO_REPLY);
}
void testPendingRequestsInvalidBinding()
{
  PendingFixture f;
  const RequestBinding<First> invalid;
  PendingRequests::cancel(invalid);
  PendingRequests::cancel(invalid, PendingProps::binding(f.second));
  LOKA_VERIFY(f.second.get().isNone());
  LOKA_VERIFY(f.secondQueue.post(Second(1)) == POST_ACCEPTED);
  LOKA_VERIFY(f.secondQueue.post(Second(2)) == POST_ACCEPTED);
  PendingRequests::cancel(PendingProps::binding<Second>(f.secondQueue), invalid);
  LOKA_VERIFY(f.secondQueue.state()->get().isNone() && f.secondQueue.pending() == 0);
  PendingRequests::cancel(invalid, RequestBinding<Second>());
}
void testPendingRequestsCrossPostSurvives()
{
  PendingFixture f;
  CrossPost observer(f, Second(3));
  PendingRequests::cancel(PendingProps::binding(f.first), PendingProps::binding(f.second));
  LOKA_VERIFY(f.first.get().isNone() && f.second.get().value == 3);
}
void testPendingRequestsEqualCrossPostClears()
{
  PendingFixture f;
  CrossPost observer(f, Second(2));
  PendingRequests::cancel(PendingProps::binding(f.first), PendingProps::binding(f.second));
  LOKA_VERIFY(f.first.get().isNone() && f.second.get().isNone());
}
void testPendingRequestsCopiesBindingsBeforePublication()
{
  PendingFixture f;
  RequestBinding<First> first = PendingProps::binding(f.first);
  RequestBinding<Second> second = PendingProps::binding(f.second);
  ReplaceBinding observer(f, second);
  PendingRequests::cancel(first, second);
  LOKA_VERIFY(f.second.get().isNone());
  LOKA_VERIFY(f.replacement.get().value == 2);
  LOKA_VERIFY(second.same(PendingProps::binding(f.replacement)));
}
