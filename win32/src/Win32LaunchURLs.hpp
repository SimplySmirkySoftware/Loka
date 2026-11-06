#ifndef LOKA_WIN32_LAUNCH_URLS_HPP
#define LOKA_WIN32_LAUNCH_URLS_HPP

#include <windows.h>

class App;

typedef LPWSTR *(WINAPI *Win32SplitCommandLine)(LPCWSTR commandLine, int *count);
typedef HLOCAL(WINAPI *Win32ReleaseArgv)(HLOCAL memory);

/** Splits commandLine with split, offers every parsed element after element
    zero to app.receiveURL in order, and releases a successful split exactly
    once. Refused elements (not a URL of the app's scheme, no room, no seat)
    are dropped silently; a failed split offers nothing and releases nothing.
    Borrows both functions for this call only. */
void Win32OfferLaunchURLs(App &app, LPCWSTR commandLine, Win32SplitCommandLine split, Win32ReleaseArgv release);

#endif
