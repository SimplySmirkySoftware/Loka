#ifndef LOKA_WEB_TEST_HOOKS_HPP
#define LOKA_WEB_TEST_HOOKS_HPP
#ifdef TEST_BUILD
class WebElementContext;
namespace loka
{
  namespace web
  {
    namespace testing
    {
      /** Test translation unit supplies instrumentation; shipping builds have no hooks. */
      void beforeClick(WebElementContext &);
      void routeProbe();
      bool refuseCommit();
      bool refuseInstallation();
      void tailCompleted();
    } // namespace testing
  } // namespace web
} // namespace loka
#endif
#endif
