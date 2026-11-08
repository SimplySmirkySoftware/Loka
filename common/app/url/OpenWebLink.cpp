#include "app/url/OpenWebLink.hpp"
#include "app/url/URLOpener.hpp"

namespace loka
{
  namespace app
  {
    dsl::StepRunStatus OpenWebLink(const LinkTarget &target, bool &posted, dsl::FlowError &, void *opener)
    {
      posted = false;
      if (!opener)
        return dsl::FLOW_STEP_SUCCEEDED;
      WebURL url;
      if (!WebURL::parse(target.text(), url))
        return dsl::FLOW_STEP_SUCCEEDED;
      posted = static_cast<URLOpenerBase *>(opener)->post(url) == scene::POST_ACCEPTED;
      return dsl::FLOW_STEP_SUCCEEDED;
    }
  } // namespace app
} // namespace loka
