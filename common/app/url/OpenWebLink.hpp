#ifndef LOKA_APP_URL_OPEN_WEB_LINK_HPP
#define LOKA_APP_URL_OPEN_WEB_LINK_HPP

#include "app/style/LinkTarget.hpp"
#include "dsl/flow/Flow.hpp"

namespace loka
{
  namespace app
  {
    /** Match handler borrowing a URLOpenerBase through opener. posted means
        queued, not OS-opened. Null, parse refusal and full queues succeed
        with posted false; the handler never delivers requests itself. */
    dsl::StepRunStatus OpenWebLink(const LinkTarget &target, bool &posted, dsl::FlowError &error, void *opener);
  } // namespace app
} // namespace loka
#endif
