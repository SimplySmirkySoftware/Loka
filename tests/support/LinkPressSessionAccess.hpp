#ifndef LOKA_TESTS_SUPPORT_LINK_PRESS_SESSION_ACCESS_HPP
#define LOKA_TESTS_SUPPORT_LINK_PRESS_SESSION_ACCESS_HPP
#include "app/internal/LinkPressSession.hpp"
#include "support/TestVerify.hpp"

namespace loka
{
  namespace testing
  {
    /** The one test access to a link press session's private candidate and occupancy, shared by every rail's pins
        so the friend named in LinkPressSession.hpp has a single definition. */
    class LinkPressSessionAccess
    {
    public:
      template <class Payload>
      static void expectInvalidated(app::internal::LinkPressSession<Payload> &session, bool active)
      {
        Payload payload;
        LOKA_VERIFY(!session.candidate_.take(payload));
        LOKA_VERIFY((session.occupancy_.active_ != 0) == active);
        if (active)
          LOKA_VERIFY(session.occupancy_.active_->invalidated());
      }
    };
  } // namespace testing
} // namespace loka
#endif
