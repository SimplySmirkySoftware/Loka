#include "app/core/App.hpp"
#include "app/PlatformContext.hpp"
#include "app/core/AppComposition.hpp"
#include "app/core/AppConfigurable.hpp"
#include "app/nodes/boundary/StdComposition.hpp"
#include "core/Operation.hpp"
#include "core/util/ScopedPtr.hpp"
#include "app/bootstrap/PlatformBootstrap.hpp"
#include <Foundation/Foundation.h>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <fcntl.h>
#include <unistd.h>

namespace
{
  using namespace loka::app;
  using namespace loka::core;
  using namespace loka::dsl;

  void append(const std::string &line)
  {
    const int fd = open("/tmp/loka-url-receive-probe.log", O_WRONLY | O_CREAT | O_APPEND, 0600);
    if (fd < 0)
      std::abort();
    const ssize_t written = write(fd, line.data(), line.size());
    const int closed = close(fd);
    if (written < 0 || static_cast<size_t>(written) != line.size() || closed != 0)
      std::abort();
  }

  std::string process()
  {
    char value[32];
    std::snprintf(value, sizeof(value), "%ld", static_cast<long>(getpid()));
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

int main()
{
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  loka::platform::InitPlatformRuntime();
  {
    ScopedPtr<PlatformContext> context(loka::platform::CreatePlatformContext());
    if (!context.get())
      std::abort();
    ProbeConfig config(context.get());
    ScopedPtr<App> app(context->createApp(&config, 0, 0));
    if (!app.get())
      std::abort();
    const char *executable = [[[NSBundle mainBundle] executablePath] fileSystemRepresentation];
    if (!executable)
      std::abort();
    append("ready pid=" + process() + " rev=" LOKA_PROBE_REVISION " exe=" + executable + "\n");
    app->run();
  }
  [pool drain];
  return 0;
}
