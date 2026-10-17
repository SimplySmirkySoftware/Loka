#ifndef LOKA_TESTS_SUPPORT_MAC_WINDOW_SESSION_HPP
#define LOKA_TESTS_SUPPORT_MAC_WINDOW_SESSION_HPP

#include <AppKit/AppKit.h>
#include <ApplicationServices/ApplicationServices.h>

namespace loka
{
  namespace testing
  {
    /** Shared test-session probe. Absence of the private key is not proof that
        a window can activate or capture; callers still test those operations. */
    inline bool MacConsoleScreenIsLocked()
    {
      bool locked = false;
      CFDictionaryRef session = CGSessionCopyCurrentDictionary();
      if (session)
      {
        // Not a documented key: present and true while the console screen is locked.
        id value = [(NSDictionary *)session objectForKey:@"CGSSessionScreenIsLocked"];
        locked = value && [value respondsToSelector:@selector(boolValue)] && [value boolValue];
        CFRelease(session);
      }
      return locked;
    }
  } // namespace testing
} // namespace loka
#endif
