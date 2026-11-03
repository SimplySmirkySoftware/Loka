#include "URLReceiveDeliveryTests.hpp"
#include "app/core/AppComposition.hpp"
#include "app/nodes/boundary/StdComposition.hpp"
#include "platform/null/NullApp.hpp"
#include "platform/null/NullPlatformContext.hpp"
#include "testing/app/AppTestAccess.hpp"
#include "testing/app/URLInboxAccess.hpp"
#include "testing/app/URLOpenerAccess.hpp"
#include "testing/app/URLReceiveTestHooks.hpp"
#include "testing/core/StateTrackerTestAccess.hpp"
#include "testing/scene/SceneFocusTestAccess.hpp"
#include "core/util/StateTrackerGuard.hpp"
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
  using namespace loka::app::scene;
  using namespace loka::core;
  using namespace loka::dsl;
  typedef loka::app::testing::AppTestAccess Access;
  typedef loka::app::testing::URLInboxAccess InboxAccess;
  typedef loka::core::testing::PushStateTrackerTestAccess LedgerAccess;
  void increment(void *data)
  {
    ++*static_cast<unsigned *>(data);
  }
  bool matches(const IncomingURL &, void *)
  {
    return true;
  }
  WebURL webURL()
  {
    WebURL url;
    LOKA_VERIFY(WebURL::parse(String("https://received/"), url));
    return url;
  }
  class Root : public BoundaryNodeFor<Root>
  {
  public:
    explicit Root(const BoundaryPropsFor<Root> &p)
        : BoundaryNodeFor<Root>(p)
    {
    }
    virtual void composeNode(NodeComposition &) {}
  };
  struct Config : AppConfigurable
  {
    URLInbox<8> inbox;
    URLOpener<4> opener;
    DocumentRoster<DocumentFacts, 8> roster;
    PushStateTracker tracker;
    MutableState<int> result;
    std::vector<String> received;
    std::vector<unsigned> counts;
    unsigned deferred;
    unsigned successes;
    bool openDocument;
    bool postOpen;
    bool offerMore;
    bool launch;
    bool registerOpen;
    bool overrideInput;
    IncomingURL input;
    explicit Config(PlatformContext &context)
        : AppConfigurable(&context),
          inbox("myapp"),
          result(0),
          deferred(0),
          successes(0),
          openDocument(false),
          postOpen(false),
          offerMore(false),
          launch(false),
          registerOpen(false),
          overrideInput(false)
    {
      this->tracker.addState(&this->result);
      URLScheme scheme;
      LOKA_VERIFY(URLScheme::parse("myapp", scheme));
      LOKA_VERIFY(IncomingURL::parse(String("myapp:override"), scheme, this->input));
    }
    ~Config()
    {
      this->tracker.removeState(&this->result);
    }
    struct Adapter
    {
      typedef IncomingURL In;
      typedef int Out;
      Config *config;
      mutable unsigned count;
      explicit Adapter(Config *c)
          : config(c),
            count(0)
      {
      }
      Adapter(const Adapter &other)
          : config(other.config),
            count(other.count)
      {
      }
      StepRunStatus run(const In &in, Out &out, FlowError &) const
      {
        Config &c = *this->config;
        c.received.push_back(in.text());
        c.counts.push_back(++this->count);
        if (c.launch)
        {
          LOKA_VERIFY(Operation::hasActive());
          LOKA_VERIFY(c.deferred == 0);
        }
        {
          StateTrackerGuard guard(&c.tracker);
          c.result.set(static_cast<int>(c.received.size()));
          LedgerAccess::defer(c.tracker, &increment, &c.deferred);
          LOKA_VERIFY(LedgerAccess::depth(c.tracker) > 0);
        }
        if (c.openDocument)
          LOKA_VERIFY(!c.roster.open(DocumentFacts()).isNone());
        if (c.postOpen)
          LOKA_VERIFY(c.opener.post(webURL()) == POST_ACCEPTED);
        if (c.offerMore)
        {
          c.offerMore = false;
          LOKA_VERIFY(InboxAccess::offer(c.inbox, String("myapp:extra")) == RECEIVE_QUEUED);
        }
        out = 42;
        return FLOW_STEP_SUCCEEDED;
      }
    };
    virtual void compose(AppComposition &c)
    {
      StepSpec<Adapter> step = Step(2, Adapter(this)).onSuccess(&this->result, &this->tracker);
      if (this->overrideInput)
        step.input(&this->input);
      FlowChain<IncomingURL, int> child = Flow() | step;
      child.onSuccess(&increment, &this->successes);
      c << ReceiveURLs(this->inbox, Match<IncomingURL, int>(1).arm(&matches, 0, child));
      if (this->registerOpen)
        c << OpenURLs(this->opener);
      c << DocumentWindows(this->roster, &Config::factory, this);
    }
    static WindowProps factory(ItemId, const DocumentFacts &, void *data)
    {
      Config &c = *static_cast<Config *>(data);
      if (c.launch)
      {
        LOKA_VERIFY(!Operation::hasActive());
        LOKA_VERIFY(c.received.size() == 1);
        LOKA_VERIFY(c.deferred == 1);
        LOKA_VERIFY(LedgerAccess::depth(c.tracker) == 0);
      }
      return WindowProps().scene(new Scene(Boundary<Root>()));
    }
  };
  struct WalkProbe : AppComponent
  {
    unsigned &walks;
    explicit WalkProbe(unsigned &count)
        : walks(count)
    {
    }
    virtual Window *asWindow()
    {
      ++this->walks;
      return 0;
    }
  };
  class ProbeApp : public NullApp
  {
  public:
    Config &config;
    unsigned walks;
    unsigned collections;
    explicit ProbeApp(Config &c)
        : NullApp(&c),
          config(c),
          walks(0),
          collections(0)
    {
    }
    void installWalkProbe()
    {
      this->group_->adopt(new WalkProbe(this->walks));
      this->walks = 0;
    }
    using App::admitAndApplyWindows;
    using App::hasQuitAttempt;
    using App::reclaimWindows;

  protected:
    virtual void collectLaunchRequests()
    {
      ++this->collections;
      if (this->config.launch)
        LOKA_VERIFY(this->receiveURL(String("myapp:launch")) == RECEIVE_QUEUED);
    }
  };
  void offer(App &app, const char *text = "myapp:a")
  {
    LOKA_VERIFY(app.receiveURL(String(text)) == RECEIVE_QUEUED);
  }
} // namespace

void testURLReceiveDeliveryFIFO()
{
  NullPlatformContext context;
  Config config(context);
  ProbeApp app(config);
  app.run();
  offer(app);
  offer(app, "myapp:b");
  offer(app);
  for (unsigned i = 0; i < 3; ++i)
  {
    LOKA_VERIFY(Access::completeTurn(app));
    LOKA_VERIFY(config.received.size() == i + 1);
    LOKA_VERIFY(config.counts[i] == 1);
    LOKA_VERIFY(config.successes == i + 1);
    LOKA_VERIFY(config.result.get() == 42);
  }
  LOKA_VERIFY(config.received[0].equals(String("myapp:a")));
  LOKA_VERIFY(config.received[1].equals(String("myapp:b")));
  LOKA_VERIFY(config.received[2].equals(config.received[0]));
  LOKA_VERIFY(!Access::completeTurn(app));
  FlowRunResult first = FLOW_RUN_CANCELED;
  URLReceiveSeat<int> empty(config.inbox, Match<IncomingURL, int>(1));
  LOKA_VERIFY(!empty.serveOne(first) && first == FLOW_RUN_CANCELED);
}

void testURLReceiveDeliveryChildEdges()
{
  NullPlatformContext context;
  Config config(context);
  config.overrideInput = true;
  ProbeApp app(config);
  app.run();
  offer(app);
  LOKA_VERIFY(Access::completeTurn(app));
  LOKA_VERIFY(config.received[0].equals(String("myapp:override")));
  LOKA_VERIFY(config.result.get() == 42 && config.successes == 1);
}

void testURLReceiveDeliverySecondAdmission()
{
  NullPlatformContext context;
  Config config(context);
  config.openDocument = true;
  ProbeApp app(config);
  app.run();
  offer(app);
  LOKA_VERIFY(Access::completeTurn(app));
  LOKA_VERIFY(config.roster.count() == 1 && app.adoptedWindows().size() == 1);
}

namespace
{
  URLOpenKind nothing(const WebURL &, void *)
  {
    return URL_OPENED;
  }
  URLOpenKind launching(const WebURL &, void *data)
  {
    ProbeApp &app = *static_cast<ProbeApp *>(data);
    LOKA_VERIFY(app.serveURLsAtCompletion(App::COMPLETION_OUTERMOST));
    LOKA_VERIFY(app.config.received.size() == 1);
    return URL_OPENED;
  }
} // namespace
void testURLReceiveDeliveryDirections()
{
  NullPlatformContext context;
  Config config(context);
  config.registerOpen = true;
  ProbeApp app(config);
  app.run();
  app.installWalkProbe();
  LOKA_VERIFY(!app.serveURLsAtCompletion(App::COMPLETION_OUTERMOST));
  LOKA_VERIFY(app.walks == 0);
  LOKA_VERIFY(config.opener.post(webURL()) == POST_ACCEPTED);
  LOKA_VERIFY(loka::app::testing::URLOpenerAccess::launchNext(config.opener, &nothing, 0));
  LOKA_VERIFY(config.opener.post(webURL()) == POST_ACCEPTED);
  LOKA_VERIFY(!app.serveURLsAtCompletion(App::COMPLETION_OUTERMOST));
  LOKA_VERIFY(app.walks == 0); // SEALED with queued work is still not due.
  LOKA_VERIFY(loka::app::testing::URLOpenerAccess::deliverSealed(config.opener));
  offer(app);
  LOKA_VERIFY(loka::app::testing::URLOpenerAccess::launchNext(config.opener, &launching, &app));
  LOKA_VERIFY(app.walks > 0); // Positive control: receive due while LAUNCHING.
  LOKA_VERIFY(loka::app::testing::URLOpenerAccess::deliverSealed(config.opener));
  config.postOpen = true;
  offer(app);
  LOKA_VERIFY(config.opener.pending() == 0);
  LOKA_VERIFY(Access::completeTurn(app));
  LOKA_VERIFY(context.openedURLs().size() == 1); // Same completion, initially empty opener.
  LOKA_VERIFY(config.opener.post(webURL()) == POST_ACCEPTED);
  LOKA_VERIFY(Access::completeTurn(app)); // Empty inbox does not block open.
  LOKA_VERIFY(context.openedURLs().size() == 2 && config.received.size() == 2);
}

void testURLReceiveDeliveryInheritedClock()
{
  NullPlatformContext context;
  Config config(context);
  ProbeApp app(config);
  app.run();
  offer(app);
  {
    Operation outer;
    LOKA_VERIFY(!Access::completeTurn(app));
    LOKA_VERIFY(config.received.empty());
  }
  LOKA_VERIFY(Access::completeTurn(app));
}

namespace
{
  void busy(void *data)
  {
    ProbeApp &app = *static_cast<ProbeApp *>(data);
    LOKA_VERIFY(app.adoptedWindows()[0]->scene()->isBusy());
    LOKA_VERIFY(!app.serveURLsAtCompletion(App::COMPLETION_OUTERMOST));
    LOKA_VERIFY(app.config.received.empty());
  }
  class Controller : public NullScenePlatformController
  {
  public:
    ProbeApp *app;
    unsigned runs;
    Controller()
        : app(0),
          runs(0)
    {
    }
    virtual bool canSkipGlobalChangeForBoundaryLocalPaint() const
    {
      return false;
    }
    virtual void onChange(Node *root, NodeDirtyFlags flags, bool full)
    {
      NullScenePlatformController::onChange(root, flags, full);
      if (this->app)
      {
        ++this->runs;
        LOKA_VERIFY(this->app->adoptedWindows()[0]->scene()->isRunInProgress());
        busy(this->app);
      }
    }
  };
  class Context : public NullPlatformContext
  {
  public:
    Controller controller;
    virtual Window *createWindow(const WindowProps &props)
    {
      return new NullWindow(this, props, &this->controller);
    }
  };
} // namespace
void testURLReceiveDeliveryBusy()
{
  Context context;
  Config config(context);
  LOKA_VERIFY(!config.roster.open(DocumentFacts()).isNone());
  ProbeApp app(config);
  app.run();
  Access::completeTurn(app);
  offer(app);
  LOKA_VERIFY(!Operation::hasActive());
  {
    BorrowScope borrow(context.controller);
    busy(&app);
  }
  SceneFocusTestAccess::duringPublication(app.adoptedWindows()[0]->scene()->focus(), &busy, &app);
  context.controller.app = &app;
  app.adoptedWindows()[0]->scene()->invalidate(NODE_DIRTY_MYSELF);
  app.admitAndApplyWindows();
  context.controller.app = 0;
  app.reclaimWindows();
  LOKA_VERIFY(context.controller.runs > 0);
  LOKA_VERIFY(Access::completeTurn(app));
}

void testURLReceiveDeliveryQuit()
{
  NullPlatformContext context;
  Config config(context);
  LOKA_VERIFY(!config.roster.open(DocumentFacts()).isNone());
  ProbeApp app(config);
  app.run();
  offer(app);
  app.requestQuitAtCompletion();
  LOKA_VERIFY(app.hasQuitAttempt());
  LOKA_VERIFY(!app.serveURLsAtCompletion(App::COMPLETION_OUTERMOST)); // REQUESTED
  LOKA_VERIFY(!Access::completeTurn(app));                            // ASKING
  LOKA_VERIFY(!config.roster.closeAsked().get().id.isNone());
  LOKA_VERIFY(!app.serveURLsAtCompletion(App::COMPLETION_OUTERMOST));
  LOKA_VERIFY(config.received.empty());
  config.roster.declineClose(config.roster.closeAsked().get());
  LOKA_VERIFY(Access::completeTurn(app));
  LOKA_VERIFY(!app.hasQuitAttempt() && config.received.size() == 1);
}

void testURLReceiveDeliveryLaunch()
{
  NullPlatformContext context;
  Config config(context);
  config.launch = config.openDocument = config.offerMore = true;
  ProbeApp app(config);
  app.run();
  LOKA_VERIFY(config.received.size() == 1 && app.collections == 1);
  LOKA_VERIFY(config.received[0].equals(String("myapp:launch")));
  LOKA_VERIFY(InboxAccess::count(config.inbox) == 1);
  LOKA_VERIFY(app.adoptedWindows().size() == 1 && !app.bootstrapWindowlessReported());
  LOKA_VERIFY(!Operation::hasActive());
  config.launch = false;
  LOKA_VERIFY(Access::completeTurn(app));
  LOKA_VERIFY(config.received.size() == 2 && config.received[1].equals(String("myapp:extra")));
  app.run();
  LOKA_VERIFY(app.collections == 1);
  Config empty(context);
  empty.launch = true;
  ProbeApp windowless(empty);
  windowless.run();
  LOKA_VERIFY(empty.received.size() == 1 && windowless.bootstrapWindowlessReported());
}

namespace
{
  struct CountingTracker : PushStateTracker
  {
    unsigned begins, ends;
    CountingTracker()
        : begins(0),
          ends(0)
    {
    }
    virtual void begin()
    {
      ++this->begins;
      PushStateTracker::begin();
    }
    virtual bool end()
    {
      ++this->ends;
      return PushStateTracker::end();
    }
  };
  struct PendingConfig : AppConfigurable
  {
    URLInbox<4> inbox;
    CountingTracker tracker;
    bool loading;
    unsigned runs, finals, stepFinals;
    enum Kind
    {
      CHILD,
      VALUE,
      NESTED
    } kind;
    explicit PendingConfig(PlatformContext &context, Kind k = CHILD)
        : AppConfigurable(&context),
          inbox("myapp"),
          loading(false),
          runs(0),
          finals(0),
          stepFinals(0),
          kind(k)
    {
    }
    struct Pending
    {
      typedef IncomingURL In;
      typedef int Out;
      PendingConfig *config;
      explicit Pending(PendingConfig *c)
          : config(c)
      {
      }
      StepRunStatus run(const In &in, Out &out, FlowError &) const
      {
        LOKA_VERIFY(this->config->loading);
        LOKA_VERIFY(LedgerAccess::depth(this->config->tracker) > 0);
        ++this->config->runs;
        out = 1;
        return in.restStartsWith("pending") ? FLOW_STEP_PENDING : FLOW_STEP_SUCCEEDED;
      }
    };
    static StepRunStatus value(const IncomingURL &in, int &out, FlowError &, void *data)
    {
      ++static_cast<PendingConfig *>(data)->runs;
      out = 1;
      return in.restStartsWith("pending") ? FLOW_STEP_PENDING : FLOW_STEP_SUCCEEDED;
    }
    MatchSpec<IncomingURL, int> match()
    {
      FlowChain<IncomingURL, int> child = Flow() | Step(2, Pending(this)).onFinally(&increment, &this->stepFinals);
      child.trackLoading(&this->loading).withTracker(&this->tracker).onFinally(&increment, &this->finals);
      MatchSpec<IncomingURL, int> result = Match<IncomingURL, int>(1);
      switch (this->kind)
      {
      case CHILD:
        result.arm(&matches, 0, child);
        break;
      case VALUE:
        result.arm(&matches, this, &value);
        break;
      case NESTED:
        result.arm(&matches, 0, Flow() | Match<IncomingURL, int>(3).arm(&matches, 0, child));
        break;
      }
      return result;
    }
    virtual void compose(AppComposition &c)
    {
      c << ReceiveURLs(this->inbox, this->match());
    }
  };
  void pendingCase(PendingConfig::Kind kind, bool joined)
  {
    NullPlatformContext context;
    PendingConfig config(context, kind);
    NullApp app(&config);
    app.run();
    offer(app, "myapp:pending");
    offer(app, "myapp:done");
    loka::testing::tolerateReceivePending(1);
    if (joined)
    {
      Operation turn;
      LOKA_VERIFY(app.serveURLsAtCompletion(App::COMPLETION_OUTERMOST));
      if (kind != PendingConfig::VALUE)
      {
        LOKA_VERIFY(config.tracker.begins == 1 && config.tracker.ends == 0);
        LOKA_VERIFY(LedgerAccess::depth(config.tracker) == 1);
      }
      turn.close();
      if (kind != PendingConfig::VALUE)
        LOKA_VERIFY(LedgerAccess::depth(config.tracker) == 0);
    }
    else
    {
      LOKA_VERIFY(!Operation::hasActive());
      LOKA_VERIFY(app.serveURLsAtCompletion(App::COMPLETION_OUTERMOST));
      if (kind != PendingConfig::VALUE)
        LOKA_VERIFY(config.tracker.begins == 2 && config.tracker.ends == 2);
    }
    LOKA_VERIFY(!loka::testing::consumeReceivePendingTolerance());
    LOKA_VERIFY(config.runs == 1 && !config.loading && config.stepFinals == 0);
    LOKA_VERIFY(config.finals == (kind == PendingConfig::VALUE ? 0u : 1u));
    LOKA_VERIFY(LedgerAccess::depth(config.tracker) == 0);
    LOKA_VERIFY(Access::completeTurn(app));
    LOKA_VERIFY(config.runs == 2);
    LOKA_VERIFY(config.finals == (kind == PendingConfig::VALUE ? 0u : 2u));
  }
} // namespace
void testURLReceiveDeliveryPending()
{
  pendingCase(PendingConfig::CHILD, false);
  pendingCase(PendingConfig::CHILD, true);
  pendingCase(PendingConfig::VALUE, true);
  pendingCase(PendingConfig::NESTED, false);
  pendingCase(PendingConfig::NESTED, true);
  // Pin the exact Flow result used by the seat's cancel/resume sequence.
  NullPlatformContext context;
  PendingConfig config(context);
  IncomingURL input;
  URLScheme scheme;
  LOKA_VERIFY(URLScheme::parse("myapp", scheme));
  LOKA_VERIFY(IncomingURL::parse(String("myapp:pending"), scheme, input));
  MatchSpec<IncomingURL, int> match = config.match();
  FlowChain<IncomingURL, int> chain = Flow() | Step(match.id(), match.adapter()).input(&input);
  LOKA_VERIFY(chain.runResult() == FLOW_RUN_PENDING);
  chain.cancel();
  LOKA_VERIFY(chain.resumeResult(match.id()) == FLOW_RUN_CANCELED);
  LOKA_VERIFY(config.finals == 1 && !config.loading);
}

void testURLReceiveDeliveryPendingWall()
{
#ifdef NDEBUG
  std::puts("[branch] release PENDING cleanup without tolerance");
  NullPlatformContext context;
  PendingConfig config(context);
  NullApp app(&config);
  app.run();
  offer(app, "myapp:pending");
  LOKA_VERIFY(Access::completeTurn(app));
  LOKA_VERIFY(config.finals == 1 && !config.loading);
#elif defined(__linux__) && !defined(__SANITIZE_ADDRESS__)
  std::puts("[branch] Linux debug PENDING SIGABRT");
  const pid_t child = fork();
  LOKA_VERIFY(child >= 0);
  if (child == 0)
  {
    NullPlatformContext context;
    PendingConfig config(context);
    NullApp app(&config);
    app.run();
    offer(app, "myapp:pending");
    Access::completeTurn(app);
    _exit(0);
  }
  int status = 0;
  LOKA_VERIFY(waitpid(child, &status, 0) == child);
  LOKA_VERIFY(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
#else
  std::puts("[skip] PENDING death pin requires Linux debug without ASan; tolerance cleanup runs separately");
#endif
}
