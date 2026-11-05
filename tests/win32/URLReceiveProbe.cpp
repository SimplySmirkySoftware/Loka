#include "app/core/App.hpp"
#include "app/PlatformContext.hpp"
#include "app/core/AppComposition.hpp"
#include "app/core/AppConfigurable.hpp"
#include "app/nodes/boundary/StdComposition.hpp"
#include "app/bootstrap/PlatformBootstrap.hpp"
#include "core/Operation.hpp"
#include "core/util/ScopedPtr.hpp"
#include <windows.h>
#include <cstdio>
#include <string>

// Manual interactive-desktop probe for launch URLs (B3 page 2b R4); never part
// of the unit-test registry. tests/win32/url-receive-probe.ps1 drives it.
namespace
{
  using namespace loka::app;
  using namespace loka::core;
  using namespace loka::dsl;

  const int WRITE_FAILED = 3;

  // Appends one ASCII line to %TEMP%\loka-url-receive-probe.log through the
  // wide file API; any failure ends the process so a silent probe cannot pass.
  void append(const std::string &line)
  {
    wchar_t folder[MAX_PATH + 1];
    const DWORD length = GetTempPathW(MAX_PATH + 1, folder);
    if (length == 0 || length > MAX_PATH)
      ExitProcess(WRITE_FAILED);
    const std::wstring path = std::wstring(folder, length) + L"loka-url-receive-probe.log";
    const HANDLE file = CreateFileW(
        path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE)
      ExitProcess(WRITE_FAILED);
    DWORD written = 0;
    const BOOL wrote = WriteFile(file, line.data(), static_cast<DWORD>(line.size()), &written, 0);
    const BOOL closed = CloseHandle(file);
    if (!wrote || written != line.size() || !closed)
      ExitProcess(WRITE_FAILED);
  }

  // The command line as ASCII, with non-ASCII and control units as \uXXXX, so
  // the script can match its phase nonce against the launched process.
  std::string commandLine()
  {
    std::string out;
    for (const wchar_t *unit = GetCommandLineW(); *unit; ++unit)
    {
      if (*unit >= 0x20 && *unit < 0x7F)
        out += static_cast<char>(*unit);
      else
      {
        char escaped[8];
        std::sprintf(escaped, "\\u%04X", static_cast<unsigned>(*unit));
        out += escaped;
      }
    }
    return out;
  }

  std::string process()
  {
    char value[32];
    std::sprintf(value, "%lu", static_cast<unsigned long>(GetCurrentProcessId()));
    return value;
  }

  class ProbeConfig : public AppConfigurable
  {
  public:
    explicit ProbeConfig(PlatformContext *context)
        : AppConfigurable(context),
          inbox_("lokaurlprobe")
    {
    }
    virtual void compose(AppComposition &composition)
    {
      // One launch window: a windowless app would exit before its loop.
      composition << WindowDef(WindowProps());
      composition << ReceiveURLs(this->inbox_, Match<IncomingURL, int>(1).arm(&matches, this, &receive));
    }

  private:
    static bool matches(const IncomingURL &, void *)
    {
      return true;
    }
    static StepRunStatus receive(const IncomingURL &url, int &out, FlowError &, void *)
    {
      const StringBuffer bytes = url.text().bufferWithEncoding(StringEncodingUtf8);
      append("received pid=" + process() + " clock=" + (Operation::hasActive() ? "1 " : "0 ")
             + std::string(static_cast<const char *>(bytes.data()), bytes.length()) + "\n");
      out = 1;
      return FLOW_STEP_SUCCEEDED;
    }
    URLInbox<4> inbox_;
  };
} // namespace

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow)
{
  loka::platform::InitPlatformRuntime();
  ScopedPtr<PlatformContext> context(loka::platform::CreatePlatformContext());
  if (!context.get())
    return 2;
  ProbeConfig config(context.get());
  ScopedPtr<App> app(context->createApp(&config, hInstance, nCmdShow));
  if (!app.get())
    return 2;
  append("ready pid=" + process() + " rev=" LOKA_PROBE_REVISION " cmd=" + commandLine() + "\n");
  app->run();
  return 0;
}
