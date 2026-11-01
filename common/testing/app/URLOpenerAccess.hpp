#ifndef LOKA_TESTING_URL_OPENER_ACCESS_HPP
#define LOKA_TESTING_URL_OPENER_ACCESS_HPP
#include "app/url/URLOpener.hpp"
namespace loka
{
  namespace app
  {
    namespace testing
    {
      /** Test-only forwarding of the two completion doors. */
      struct URLOpenerAccess
      {
        static bool launchNext(URLOpenerBase &owner, URLOpenKind (*open)(const WebURL &, void *), void *user)
        {
          return owner.launchNext(open, user);
        }
        static bool deliverSealed(URLOpenerBase &owner)
        {
          return owner.deliverSealed();
        }
      };
    } // namespace testing
  } // namespace app
} // namespace loka
#endif
