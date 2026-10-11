#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>

#if !defined(LOKA_STANDALONE_FLOW_CONFIG_HEADER)
#error StandaloneFlowMain requires LOKA_STANDALONE_FLOW_CONFIG_HEADER
#endif

#if !defined(LOKA_STANDALONE_FLOW_CONFIG_TYPE)
#error StandaloneFlowMain requires LOKA_STANDALONE_FLOW_CONFIG_TYPE
#endif

#include LOKA_STANDALONE_FLOW_CONFIG_HEADER
#include "StandaloneFlowRunner.hpp"

#if !defined(TEST_BUILD)
#error Loka Standalone Flow Win32 requires TEST_BUILD
#endif

namespace loka
{
  namespace standalone_tests
  {
    // Native twin of tests/macos/StandaloneFlowMain.mm; text comes from the runner.
    void ShowStandaloneAuditRefusal(const char *message)
    {
      WCHAR modulePath[32768];
      const DWORD length = GetModuleFileNameW(0, modulePath, sizeof(modulePath) / sizeof(modulePath[0]));
      std::wstring applicationName(L"Loka Standalone Flow");
      if (length > 0 && length < sizeof(modulePath) / sizeof(modulePath[0]))
      {
        applicationName.assign(modulePath, length);
        const std::wstring::size_type separator = applicationName.find_last_of(L"\\/");
        if (separator != std::wstring::npos)
        {
          applicationName.erase(0, separator + 1);
        }
      }
      const std::wstring title = applicationName + L": LOG.TXT unavailable";
      const int textLength = MultiByteToWideChar(CP_UTF8, 0, message, -1, 0, 0);
      if (textLength > 0)
      {
        std::wstring text(static_cast<std::wstring::size_type>(textLength), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, message, -1, &text[0], textLength);
        MessageBoxW(0, text.c_str(), title.c_str(), MB_ICONSTOP | MB_OK);
      }
    }
  } // namespace standalone_tests
} // namespace loka

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR commandLine, int nCmdShow)
{
  (void)hPrevInstance;
  (void)commandLine;
  return loka::standalone_tests::RunStandaloneFlowWithConfig<LOKA_STANDALONE_FLOW_CONFIG_TYPE>(hInstance,
                                                                                              nCmdShow);
}
