#include "URLOpenerTests.hpp"
#include "testing/app/URLOpenerAccess.hpp"
#include "core/Operation.hpp"
#include "support/TestVerify.hpp"

namespace
{
  using namespace loka::app;
  using namespace loka::app::scene;
  using namespace loka::core;
  typedef loka::app::testing::URLOpenerAccess Access;

  WebURL url(const char *text)
  {
    WebURL value;
    LOKA_VERIFY(WebURL::parse(String(text), value));
    return value;
  }

  struct Opening
  {
    WebURL expected;
    URLOpenKind result;
    unsigned calls;
    Opening(const WebURL &value, URLOpenKind kind)
        : expected(value),
          result(kind),
          calls(0)
    {
    }
    static URLOpenKind open(const WebURL &value, void *user)
    {
      Opening &self = *static_cast<Opening *>(user);
      LOKA_VERIFY(value == self.expected);
      ++self.calls;
      return self.result;
    }
  };

  struct Reentry
  {
    URLOpener<1> owner;
    WebURL first, second;
    unsigned notifications;
    Reentry()
        : first(url("https://first/")),
          second(url("https://second/")),
          notifications(0)
    {
    }
    static URLOpenKind open(const WebURL &value, void *user)
    {
      Reentry &self = *static_cast<Reentry *>(user);
      LOKA_VERIFY(value == self.first);
      LOKA_VERIFY(self.owner.pending() == 0);
      LOKA_VERIFY(self.owner.post(self.second) == POST_ACCEPTED);
      // Filling the vacated slot must not change the stack copy passed to open.
      LOKA_VERIFY(value == self.first);
      Opening nested(self.second, URL_OPENED);
      LOKA_VERIFY(!Access::launchNext(self.owner, &Opening::open, &nested));
      LOKA_VERIFY(nested.calls == 0);
      LOKA_VERIFY(!Access::deliverSealed(self.owner));
      LOKA_VERIFY(self.owner.outcome().get().isNone());
      return URL_NOT_OPENED;
    }
    static void observed(void *user)
    {
      Reentry &self = *static_cast<Reentry *>(user);
      ++self.notifications;
      LOKA_VERIFY(self.owner.outcome().get() == URLOpenOutcome(URL_NOT_OPENED, self.first));
      Opening nested(self.second, URL_OPENED);
      LOKA_VERIFY(!Access::launchNext(self.owner, &Opening::open, &nested));
      LOKA_VERIFY(nested.calls == 0);
      LOKA_VERIFY(self.owner.pending() == 1);
    }
  };
} // namespace

void testURLOpenerOutcomeValue()
{
  const WebURL a = url("https://a/"), b = url("https://b/");
  LOKA_VERIFY(URLOpenOutcome().isNone());
  LOKA_VERIFY(URLOpenOutcome() == URLOpenOutcome::None());
  LOKA_VERIFY(URLOpenOutcome(URL_OPENED, WebURL()).isNone());
  LOKA_VERIFY(!URLOpenOutcome(URL_OPENED, a).isNone());
  LOKA_VERIFY(URLOpenOutcome(URL_OPENED, a) == URLOpenOutcome(URL_OPENED, a));
  LOKA_VERIFY(URLOpenOutcome(URL_OPENED, a) != URLOpenOutcome(URL_NOT_OPENED, a));
  LOKA_VERIFY(URLOpenOutcome(URL_OPENED, a) != URLOpenOutcome(URL_OPENED, b));
}

void testURLOpenerFIFO()
{
  URLOpener<2> owner;
  const WebURL a = url("https://a/"), b = url("https://b/"), c = url("https://c/");
  Opening opening(a, URL_OPENED);
  LOKA_VERIFY(owner.pending() == 0);
  LOKA_VERIFY(owner.outcome().get().isNone());
  LOKA_VERIFY(!Access::launchNext(owner, &Opening::open, &opening));
  LOKA_VERIFY(!Access::deliverSealed(owner));
  LOKA_VERIFY(opening.calls == 0);
  LOKA_VERIFY(owner.post(WebURL::None()) == POST_INVALID);
  LOKA_VERIFY(owner.pending() == 0);
  LOKA_VERIFY(owner.post(a) == POST_ACCEPTED);
  LOKA_VERIFY(owner.pending() == 1);
  LOKA_VERIFY(owner.post(b) == POST_ACCEPTED);
  LOKA_VERIFY(owner.pending() == 2);
  LOKA_VERIFY(owner.post(c) == POST_QUEUE_FULL);
  LOKA_VERIFY(owner.post(WebURL::None()) == POST_INVALID);
  LOKA_VERIFY(owner.pending() == 2);
  LOKA_VERIFY(owner.outcome().get().isNone());
  const WebURL expected[] = {a, b, c};
  const URLOpenKind kinds[] = {URL_OPENED, URL_NOT_OPENED, URL_UNAVAILABLE};
  for (unsigned i = 0; i < 3; ++i)
  {
    opening.expected = expected[i];
    opening.result = kinds[i];
    const URLOpenOutcome before = owner.outcome().get();
    LOKA_VERIFY(Access::launchNext(owner, &Opening::open, &opening));
    LOKA_VERIFY(owner.outcome().get() == before);
    if (i == 0)
      LOKA_VERIFY(owner.post(c) == POST_ACCEPTED); // Wrap into the vacated slot.
    LOKA_VERIFY(!Access::launchNext(owner, &Opening::open, &opening));
    LOKA_VERIFY(Access::deliverSealed(owner));
    LOKA_VERIFY(owner.outcome().get() == URLOpenOutcome(kinds[i], expected[i]));
    LOKA_VERIFY(!Access::deliverSealed(owner));
    LOKA_VERIFY(owner.outcome().get() == URLOpenOutcome(kinds[i], expected[i]));
  }
  LOKA_VERIFY(opening.calls == 3);
  LOKA_VERIFY(owner.pending() == 0);
  LOKA_VERIFY(!Access::launchNext(owner, &Opening::open, &opening));
  // Repeated values are still taken and delivered, without a notification promise.
  LOKA_VERIFY(owner.post(c) == POST_ACCEPTED);
  LOKA_VERIFY(Access::launchNext(owner, &Opening::open, &opening));
  LOKA_VERIFY(Access::deliverSealed(owner));
  LOKA_VERIFY(owner.outcome().get() == URLOpenOutcome(URL_UNAVAILABLE, c));
  LOKA_VERIFY(opening.calls == 4);
}

void testURLOpenerReentry()
{
  Reentry fixture;
  State<URLOpenOutcome> &fact = const_cast<State<URLOpenOutcome> &>(fixture.owner.outcome());
  fact.bind(&Reentry::observed, &fixture, false);
  LOKA_VERIFY(fixture.owner.post(fixture.first) == POST_ACCEPTED);
  LOKA_VERIFY(Access::launchNext(fixture.owner, &Reentry::open, &fixture));
  LOKA_VERIFY(fixture.notifications == 0);
  LOKA_VERIFY(Access::deliverSealed(fixture.owner));
  LOKA_VERIFY(fixture.notifications == 1);
  LOKA_VERIFY(!Access::deliverSealed(fixture.owner));
  LOKA_VERIFY(fixture.notifications == 1);
  fact.unbind(&Reentry::observed, &fixture);
  Opening next(fixture.second, URL_OPENED);
  LOKA_VERIFY(Access::launchNext(fixture.owner, &Opening::open, &next));
  LOKA_VERIFY(next.calls == 1);
  LOKA_VERIFY(Access::deliverSealed(fixture.owner));
  LOKA_VERIFY(fixture.owner.outcome().get() == URLOpenOutcome(URL_OPENED, fixture.second));
}

void testURLOpenerRegime()
{
  URLOpener<1> owner;
  Opening opening(url("https://turn/"), URL_OPENED);
  Operation::Regime regime;
  {
    Operation turn;
    LOKA_VERIFY(owner.post(opening.expected) == POST_ACCEPTED);
    LOKA_VERIFY(Access::launchNext(owner, &Opening::open, &opening));
    LOKA_VERIFY(Access::deliverSealed(owner));
    LOKA_VERIFY(owner.outcome().get() == URLOpenOutcome(URL_OPENED, opening.expected));
    LOKA_VERIFY(turn.close().status == OPERATION_SETTLED);
  }
  LOKA_VERIFY(owner.outcome().get() == URLOpenOutcome(URL_OPENED, opening.expected));
}
