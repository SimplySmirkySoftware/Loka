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

    /** Bound on waiting for AppKit to report a key-status change. */
    const double kKeyStatusWaitSeconds = 2.0;

    inline bool MacIsKeyWindow(NSWindow *window)
    {
      return [window isKeyWindow] ? true : false;
    }

    /** Activation and key changes arrive through the event queue: deliver queued
        events until the window's key status matches, for a bounded time. */
    inline bool MacWaitForKeyStatus(NSWindow *window, bool key)
    {
#if defined(MAC_OS_X_VERSION_MAX_ALLOWED) && (MAC_OS_X_VERSION_MAX_ALLOWED >= 101200)
      const NSEventMask anyEvent = NSEventMaskAny;
#else
      const NSUInteger anyEvent = NSAnyEventMask;
#endif
      NSDate *limit = [NSDate dateWithTimeIntervalSinceNow:kKeyStatusWaitSeconds];
      while (MacIsKeyWindow(window) != key && [limit timeIntervalSinceNow] > 0)
      {
        NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
        NSEvent *event = [NSApp nextEventMatchingMask:anyEvent
                                            untilDate:[NSDate dateWithTimeIntervalSinceNow:0.05]
                                               inMode:NSDefaultRunLoopMode
                                              dequeue:YES];
        if (event)
          [NSApp sendEvent:event];
        [pool drain];
      }
      return MacIsKeyWindow(window) == key;
    }

    /** An unbundled test process is not a regular application until it asks, as
        MacApp::run() does (apple/macos/src/MacApp.mm); only then can activation
        make its window key. */
    inline bool MacRequestKeyWindow(NSWindow *window)
    {
      if ([NSApp respondsToSelector:@selector(setActivationPolicy:)])
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
      [NSApp activateIgnoringOtherApps:YES];
      [window makeKeyAndOrderFront:nil];
      return MacWaitForKeyStatus(window, true);
    }

    /** Diagnostic only; the discriminating condition is the window's key status. */
    inline const char *MacKeyRefusalReason()
    {
      if (loka::testing::MacConsoleScreenIsLocked())
        return "the console screen is locked";
      return [NSApp isActive] ? "the application is active but the window is not key"
                              : "the application was not activated";
    }
  } // namespace testing
} // namespace loka
#endif
