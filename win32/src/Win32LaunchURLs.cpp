#include "Win32LaunchURLs.hpp"
#include "app/core/App.hpp"
#include "core/String.hpp"
#include "platform/Win32String.hpp"
#include <cassert>
#include <cwchar>

void Win32OfferLaunchURLs(App &app, LPCWSTR commandLine, Win32SplitCommandLine split, Win32ReleaseArgv release)
{
  assert(split && release);
  int count = 0;
  LPWSTR *argv = split(commandLine, &count);
  if (!argv)
    return;
  // Element zero is the executable as parsed; the contract skips it, nothing more.
  for (int i = 1; i < count; ++i)
    (void)app.receiveURL(loka::core::String(loka::win32::CreateWin32StringFromUtf16(argv[i], std::wcslen(argv[i]))));
  release(argv);
}
