#ifndef LOKA_TESTS_MAC_QUIT_EVENT_TEST_ACCESS_HPP
#define LOKA_TESTS_MAC_QUIT_EVENT_TEST_ACCESS_HPP

#include "MacApp.hpp"
#include <Foundation/Foundation.h>

/** Uses the same attachment and clock doors as run(), without entering NSApp's loop. */
class MacQuitEventTestAccess
{
public:
  static void attach(MacApp &app, bool clock)
  {
    app.attachAppleEventRouter();
    if (clock)
      app.startInvalidationFlushTimer();
  }
  static NSTimer *timer(const MacApp &app)
  {
    return (NSTimer *)app.flushTimer_;
  }
  static void stop(MacApp &app)
  {
    app.stopInvalidationFlushTimer();
  }
  static bool held(const MacApp &app)
  {
    return app.quitEventReply_.isHeld();
  }
  static bool pending(const MacApp &app)
  {
    return app.hasPendingWindowAdmission();
  }
};

#endif
