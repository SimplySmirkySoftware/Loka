#include "LinkPressSessionTests.hpp"
#include "app/internal/LinkPressSession.hpp"
#include "support/TestVerify.hpp"

namespace
{
  using loka::app::internal::PressedRun;
  typedef loka::app::internal::LinkPressSession<PressedRun> Session;
  typedef loka::app::internal::LinkPressProcedure<PressedRun> Procedure;

  PressedRun run(std::size_t first, std::size_t last)
  {
    loka::app::AttributedString::LinkRun resolved;
    resolved.first = first;
    resolved.last = last;
    return PressedRun(resolved);
  }
} // namespace

void testLinkPressSessionRefusedAdmission()
{
  Session session;
  Procedure first(session);
  LOKA_VERIFY(first.admitted());
  const PressedRun placed = run(1, 3);
  first.placeCandidate(placed);
  {
    Procedure nested(session);
    LOKA_VERIFY(!nested.admitted());
    nested.close();
  }
  Procedure stillRefused(session);
  LOKA_VERIFY(!stillRefused.admitted());
  LOKA_VERIFY(!first.invalidated());
  LOKA_VERIFY(first.candidateHolds(placed));
  session.invalidate();
  LOKA_VERIFY(first.invalidated());
  LOKA_VERIFY(!stillRefused.invalidated());
}

void testLinkPressSessionInvalidationAndLoss()
{
  Session session;
  PressedRun out;
  const PressedRun placed = run(1, 3);
  Procedure first(session);
  first.placeCandidate(placed);
  session.lose();
  LOKA_VERIFY(!first.takeCandidate(out));
  LOKA_VERIFY(!first.invalidated());
  first.placeCandidate(placed);
  session.invalidate();
  LOKA_VERIFY(!first.takeCandidate(out));
  LOKA_VERIFY(first.invalidated());
  session.lose();
  LOKA_VERIFY(first.invalidated());
  first.close();

  Procedure second(session);
  LOKA_VERIFY(!second.invalidated());
  second.placeCandidate(placed);
  second.close();
  session.invalidate();
  Procedure third(session);
  LOKA_VERIFY(!third.invalidated());
  LOKA_VERIFY(!third.takeCandidate(out));
  third.placeCandidate(placed);
  third.close();
  session.lose();
  Procedure fourth(session);
  LOKA_VERIFY(!fourth.invalidated());
  LOKA_VERIFY(!fourth.takeCandidate(out));
  LOKA_VERIFY(!second.invalidated() && !third.invalidated());
}

void testLinkPressSessionCloseBeforeObserver()
{
  Session session;
  const PressedRun placed = run(2, 4);
  PressedRun out;
  {
    Procedure release(session);
    release.close();
    LOKA_VERIFY(!release.admitted());
    Procedure observer(session);
    LOKA_VERIFY(observer.admitted());
    observer.placeCandidate(placed);
    LOKA_VERIFY(observer.takeCandidate(out));
    LOKA_VERIFY(out == placed);
    observer.placeCandidate(placed);
  }
  Procedure next(session);
  LOKA_VERIFY(next.admitted());
  LOKA_VERIFY(next.takeCandidate(out));
  LOKA_VERIFY(out == placed);
}

void testLinkPressSessionCandidateIdentity()
{
  Session session;
  Procedure down(session);
  down.placeCandidate(run(1, 3));
  down.placeCandidate(run(2, 3));
  LOKA_VERIFY(!down.candidateHolds(run(1, 3)));
  LOKA_VERIFY(!down.candidateHolds(run(2, 4)));
  LOKA_VERIFY(down.candidateHolds(run(2, 3)));
  down.close();
  Procedure up(session);
  PressedRun out;
  LOKA_VERIFY(up.takeCandidate(out));
  LOKA_VERIFY(out.first() == 2 && out.last() == 3);
  LOKA_VERIFY(!up.takeCandidate(out));
  LOKA_VERIFY(!up.candidateHolds(out));
}
