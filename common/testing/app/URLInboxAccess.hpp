#ifndef LOKA_TESTING_URL_INBOX_ACCESS_HPP
#define LOKA_TESTING_URL_INBOX_ACCESS_HPP
#include "app/url/URLInbox.hpp"
namespace loka
{
  namespace app
  {
    namespace testing
    {
      /** Test-only forwarding of the inbox doors, like URLOpenerAccess. */
      struct URLInboxAccess
      {
        /** Offers text through the production parse and capacity checks. */
        static ReceiveResult offer(URLInboxBase &owner, const core::String &text)
        {
          return owner.offer(text);
        }
        /** Takes one waiting value. */
        static bool take(URLInboxBase &owner, IncomingURL &out)
        {
          return owner.take(out);
        }
        /** Reads the waiting count. */
        static unsigned count(const URLInboxBase &owner)
        {
          return owner.count();
        }
        /** Silently drops all waiting values. */
        static void discardAll(URLInboxBase &owner)
        {
          owner.discardAll();
        }
      };
    } // namespace testing
  } // namespace app
} // namespace loka
#endif
