#include "URLReceiveAppTests.hpp"
#include "app/core/AppComposition.hpp"
#include "platform/null/NullApp.hpp"
#include "platform/null/NullPlatformContext.hpp"
#include "testing/app/AppTestAccess.hpp"
#include "testing/app/URLInboxAccess.hpp"
#include "support/TestVerify.hpp"
#include <cstdio>
#if defined(__linux__) && !defined(NDEBUG) && !defined(__SANITIZE_ADDRESS__)
#include <sys/wait.h>
#include <unistd.h>
#include <csignal>
#endif

namespace
{
  using namespace loka::app;
  using namespace loka::core;
  using namespace loka::dsl;
  typedef loka::app::testing::AppTestAccess Access;
  typedef loka::app::testing::URLInboxAccess InboxAccess;
  struct Probe
  {
    typedef IncomingURL In;
    typedef bool Out;
    static int live;
    Probe()
    {
      ++live;
    }
    Probe(const Probe &)
    {
      ++live;
    }
    ~Probe()
    {
      --live;
    }
    Probe &operator=(const Probe &)
    {
      return *this;
    }
    StepRunStatus run(const In &, Out &out, FlowError &) const
    {
      out = true;
      return FLOW_STEP_SUCCEEDED;
    }
  };
  int Probe::live = 0;
  bool matches(const IncomingURL &, void *)
  {
    return true;
  }
  struct Config : AppConfigurable
  {
    URLInbox<1> inbox;
    explicit Config(PlatformContext &context)
        : AppConfigurable(&context),
          inbox("myapp")
    {
    }
    virtual void compose(AppComposition &c)
    {
      c << ReceiveURLs(this->inbox, Match<IncomingURL, bool>(1).arm(&matches, 0, Flow() | Step(2, Probe())));
    }
  };
  struct StoredConfig : Config
  {
    explicit StoredConfig(PlatformContext &context)
        : Config(context)
    {
    }
    virtual void compose(AppComposition &c)
    {
      URLReceiveSeatDefinition<bool> declaration =
          ReceiveURLs(this->inbox, Match<IncomingURL, bool>(1).arm(&matches, 0, Flow() | Step(2, Probe())));
      c << declaration;
    }
  };
  struct LateOffer : AppComponent
  {
    App &app;
    explicit LateOffer(App &owner)
        : app(owner)
    {
    }
    virtual ~LateOffer()
    {
      LOKA_VERIFY(this->app.receiveURL(String("myapp:late")) == RECEIVE_QUEUED);
    }
  };
  class ProbeApp : public NullApp
  {
  public:
    explicit ProbeApp(AppConfigurable &config)
        : NullApp(&config)
    {
    }
    void addLateOffer()
    {
      this->group_->adopt(new LateOffer(*this));
    }
  };
} // namespace

void testURLReceiveAppRegistration()
{
  const int baseline = Probe::live;
  NullPlatformContext context;
  NullApp absent(0);
  LOKA_VERIFY(absent.receiveURL(String("myapp:x")) == RECEIVE_NOT_REGISTERED);
  Config config(context);
  {
    NullApp app(&config);
    LOKA_VERIFY(app.receiveURL(String("myapp:x")) == RECEIVE_NOT_REGISTERED);
    app.run();
    LOKA_VERIFY(app.receiveURL(String("myapp:x")) == RECEIVE_QUEUED);
    LOKA_VERIFY(InboxAccess::count(config.inbox) == 1);
    LOKA_VERIFY(Probe::live == baseline + 1);
    LOKA_VERIFY(app.receiveURL(String("other:x")) == RECEIVE_INVALID);
    LOKA_VERIFY(app.receiveURL(String("myapp:full")) == RECEIVE_FULL);
  }
  LOKA_VERIFY(Probe::live == baseline);
  LOKA_VERIFY(InboxAccess::count(config.inbox) == 0);
}

void testURLReceiveAppStoredDeclaration()
{
  const int baseline = Probe::live;
  NullPlatformContext context;
  StoredConfig config(context);
  {
    NullApp app(&config);
    app.run();
    LOKA_VERIFY(Probe::live == baseline + 1);
    LOKA_VERIFY(app.receiveURL(String("myapp:x")) == RECEIVE_QUEUED);
    LOKA_VERIFY(InboxAccess::count(config.inbox) == 1);
  }
  LOKA_VERIFY(Probe::live == baseline);
  LOKA_VERIFY(InboxAccess::count(config.inbox) == 0);
}

void testURLReceiveAppUntaken()
{
  const int baseline = Probe::live;
  NullPlatformContext context;
  Config config(context);
  {
    AppComposition composition(&context);
    config.compose(composition);
    LOKA_VERIFY(Probe::live == baseline + 1);
    LOKA_VERIFY(InboxAccess::offer(config.inbox, String("myapp:waiting")) == RECEIVE_QUEUED);
  }
  LOKA_VERIFY(Probe::live == baseline);
  LOKA_VERIFY(InboxAccess::count(config.inbox) == 1);
}

void testURLReceiveAppRetirement()
{
  const int baseline = Probe::live;
  NullPlatformContext context;
  Config config(context);
  {
    NullApp app(&config);
    app.run();
    LOKA_VERIFY(Probe::live == baseline + 1);
    LOKA_VERIFY(app.receiveURL(String("myapp:x")) == RECEIVE_QUEUED);
    Access::retireComponents(app);
    LOKA_VERIFY(Probe::live == baseline);
    LOKA_VERIFY(InboxAccess::count(config.inbox) == 0);
    LOKA_VERIFY(InboxAccess::offer(config.inbox, String("myapp:between")) == RECEIVE_QUEUED);
    Access::retireComponents(app);
    LOKA_VERIFY(app.receiveURL(String("myapp:x")) == RECEIVE_NOT_REGISTERED);
    LOKA_VERIFY(InboxAccess::count(config.inbox) == 1);
    LOKA_VERIFY(Probe::live == baseline);
  }
  LOKA_VERIFY(Probe::live == baseline);
  LOKA_VERIFY(InboxAccess::count(config.inbox) == 1);
}

void testURLReceiveAppLateOffer()
{
  const int baseline = Probe::live;
  NullPlatformContext context;
  Config config(context);
  {
    ProbeApp app(config);
    app.run();
    app.addLateOffer();
    LOKA_VERIFY(Probe::live == baseline + 1);
    Access::retireComponents(app);
    LOKA_VERIFY(InboxAccess::count(config.inbox) == 0);
    LOKA_VERIFY(Probe::live == baseline);
    LOKA_VERIFY(app.receiveURL(String("myapp:x")) == RECEIVE_NOT_REGISTERED);
  }
  LOKA_VERIFY(Probe::live == baseline);
}

void testURLReceiveAppNeverRan()
{
  const int baseline = Probe::live;
  NullPlatformContext context;
  Config config(context);
  LOKA_VERIFY(InboxAccess::offer(config.inbox, String("myapp:waiting")) == RECEIVE_QUEUED);
  {
    NullApp app(&config);
  }
  LOKA_VERIFY(InboxAccess::count(config.inbox) == 1);
  LOKA_VERIFY(Probe::live == baseline);
}

void testURLReceiveAppConfigReuse()
{
  const int baseline = Probe::live;
  NullPlatformContext context;
  Config config(context);
  {
    NullApp app(&config);
    app.run();
    LOKA_VERIFY(Probe::live == baseline + 1);
    LOKA_VERIFY(app.receiveURL(String("myapp:x")) == RECEIVE_QUEUED);
  }
  LOKA_VERIFY(Probe::live == baseline);
  {
    NullApp app(&config);
    app.run();
    LOKA_VERIFY(Probe::live == baseline + 1);
    LOKA_VERIFY(InboxAccess::count(config.inbox) == 0);
  }
  LOKA_VERIFY(Probe::live == baseline);
}

namespace
{
  struct DuplicateConfig : Config
  {
    URLInbox<1> second;
    explicit DuplicateConfig(PlatformContext &context)
        : Config(context),
          second("myapp")
    {
    }
    virtual void compose(AppComposition &c)
    {
      Config::compose(c);
      c << ReceiveURLs(this->second, Match<IncomingURL, bool>(1).arm(&matches, 0, Flow() | Step(2, Probe())));
    }
  };
} // namespace

void testURLReceiveAppDuplicate()
{
  const int baseline = Probe::live;
  NullPlatformContext context;
  DuplicateConfig config(context);
#ifdef NDEBUG
  {
    NullApp app(&config);
    app.run();
    LOKA_VERIFY(Probe::live == baseline + 1);
    LOKA_VERIFY(app.receiveURL(String("myapp:x")) == RECEIVE_QUEUED);
    LOKA_VERIFY(InboxAccess::count(config.inbox) == 1);
    LOKA_VERIFY(InboxAccess::count(config.second) == 0);
  }
#elif defined(__linux__) && !defined(__SANITIZE_ADDRESS__)
  const pid_t child = fork();
  LOKA_VERIFY(child >= 0);
  if (child == 0)
  {
    NullApp app(&config);
    app.run();
    _exit(0);
  }
  int status = 0;
  LOKA_VERIFY(waitpid(child, &status, 0) == child);
  LOKA_VERIFY(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
#else
  std::puts("[skip] duplicate registration death pin requires Linux debug without ASan");
#endif
  LOKA_VERIFY(Probe::live == baseline);
}
