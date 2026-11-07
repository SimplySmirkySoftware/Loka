#include "ToolboxOpenURL.hpp"
#include "ToolboxPlatformContext.hpp"
#include "app/bootstrap/PlatformBootstrap.hpp"
#include "core/io/File.hpp"
#include "platform/file/AppLocation.hpp"
#include "platform/file/FileIO.hpp"
#include <Processes.h>
#include <cstdio>

namespace
{
  std::FILE *logFile;
  const ToolboxInternetConfigCalls &native()
  {
    return ToolboxInternetConfigNativeCalls();
  }
  void enter(const char *stage)
  {
    std::fprintf(logFile, "%s enter\r", stage);
    std::fflush(logFile);
  }
  void returned(const char *stage, long status)
  {
    std::fprintf(logFile, "%s return %ld\r", stage, status);
    std::fflush(logFile);
  }
  bool cm()
  {
    enter("cm");
    const bool r = native().hasComponentManager();
    returned("cm", r);
    return r;
  }
  long count()
  {
    enter("count");
    const long r = native().countLaunchers();
    returned("count", r);
    return r;
  }
  ComponentInstance open()
  {
    enter("open");
    ComponentInstance r = native().openLauncher();
    returned("open", reinterpret_cast<long>(r));
    return r;
  }
  OSType creator()
  {
    enter("creator");
    const OSType r = native().currentCreator();
    returned("creator", r);
    return r;
  }
  OSStatus start(ComponentInstance i, OSType c)
  {
    enter("start");
    const OSStatus r = native().start(i, c);
    returned("start", r);
    return r;
  }
  OSStatus find(ComponentInstance i)
  {
    enter("find");
    const OSStatus r = native().findConfigFile(i);
    returned("find", r);
    return r;
  }
  OSStatus launch(ComponentInstance i, ConstStr255Param h, Ptr d, long n, long *s, long *e)
  {
    enter("launch");
    const OSStatus r = native().launch(i, h, d, n, s, e);
    returned("launch", r);
    return r;
  }
  OSStatus stop(ComponentInstance i)
  {
    enter("stop");
    const OSStatus r = native().stop(i);
    returned("stop", r);
    return r;
  }
  OSErr close(ComponentInstance i)
  {
    enter("close");
    const OSErr r = native().close(i);
    returned("close", r);
    return r;
  }
} // namespace
int main()
{
  loka::platform::InitPlatformRuntime();
  loka::platform::file::FileHandle file;
  if (!loka::platform::file::ResolveApplicationSidecar(loka::file::File::Application() << loka::file::File("LOG.TXT"),
                                                       file))
    return 1;
  logFile = loka::platform::file::OpenWriteTruncate(file);
  if (!logFile)
    return 1;
  loka::app::WebURL url;
  if (!loka::app::WebURL::parse(loka::core::String::Literal("https://example.com/"), url))
    return 1;
  const ToolboxInternetConfigCalls calls = {cm, count, open, creator, start, find, launch, stop, close};
  const loka::app::URLOpenKind seam = ToolboxOpenWebURL(url, calls);
  std::fprintf(logFile, "seam kind %d\r", static_cast<int>(seam));
  ToolboxPlatformContext platform;
  const loka::app::URLOpenKind context = platform.openURL(url);
  std::fprintf(logFile, "platform kind %d\r", static_cast<int>(context));
  const bool flushed = loka::platform::file::FlushWrite(logFile, file);
  std::fclose(logFile);
  ExitToShell();
  return flushed ? 0 : 1;
}
