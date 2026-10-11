#if !defined(LOKA_STANDALONE_FLOW_CONFIG_HEADER)
#error StandaloneFlowMain requires LOKA_STANDALONE_FLOW_CONFIG_HEADER
#endif

#if !defined(LOKA_STANDALONE_FLOW_CONFIG_TYPE)
#error StandaloneFlowMain requires LOKA_STANDALONE_FLOW_CONFIG_TYPE
#endif

#include LOKA_STANDALONE_FLOW_CONFIG_HEADER
#include "StandaloneFlowRunner.hpp"

#include <Cocoa/Cocoa.h>

namespace loka
{
  namespace standalone_tests
  {
    // Native twin of tests/win32/StandaloneFlowMain.cpp; text comes from the runner.
    void ShowStandaloneAuditRefusal(const char *message)
    {
      NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
      [NSApplication sharedApplication];
      NSAlert *alert = [[NSAlert alloc] init];
#if defined(MAC_OS_X_VERSION_MAX_ALLOWED) && (MAC_OS_X_VERSION_MAX_ALLOWED >= 101200)
      [alert setAlertStyle:NSAlertStyleCritical];
#else
      [alert setAlertStyle:NSCriticalAlertStyle];
#endif
      [alert setMessageText:[NSString stringWithFormat:@"%@: LOG.TXT unavailable",
                                                      [[NSProcessInfo processInfo] processName]]];
      [alert setInformativeText:[NSString stringWithUTF8String:message]];
      [alert addButtonWithTitle:@"OK"];
      [NSApp activateIgnoringOtherApps:YES];
      [alert runModal];
      [alert release];
      [pool drain];
    }
  } // namespace standalone_tests
} // namespace loka

int main(int argc, char **argv)
{
  (void)argc;
  (void)argv;
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  const int result =
      loka::standalone_tests::RunStandaloneFlowWithConfig<LOKA_STANDALONE_FLOW_CONFIG_TYPE>(0, 0);
  (void)pool;
  return result;
}
