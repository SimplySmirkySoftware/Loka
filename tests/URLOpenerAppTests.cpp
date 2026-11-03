#include "URLOpenerAppTests.hpp"
#include <cstdio>
#include "app/core/AppComposition.hpp"
#include "app/nodes/controls/EditText.hpp"
#include "app/ConfirmDialog.hpp"
#include "app/nodes/nestable/Show.hpp"
#include "app/nodes/nestable/RowColumn.hpp"
#include "testing/scene/SceneTestFlow.hpp"
#include "app/nodes/boundary/StdComposition.hpp"
#include "platform/null/NullApp.hpp"
#include "platform/null/NullPlatformContext.hpp"
#include "testing/app/AppTestAccess.hpp"
#include "testing/app/URLOpenerAccess.hpp"
#include "testing/core/StateTrackerTestAccess.hpp"
#include "testing/scene/SceneFocusTestAccess.hpp"
#include "core/util/StateTrackerGuard.hpp"
#include "support/TestVerify.hpp"
#if defined(__linux__) && !defined(NDEBUG) && !defined(__SANITIZE_ADDRESS__)
#include <sys/wait.h>
#include <unistd.h>
#include <csignal>
#endif

enum URLField
{
  URL_FIELD
};
namespace loka
{
  namespace app
  {
    template <> struct FocusKeyTraits<URLField> : UnsignedFocusKeyTraits<URLField>
    {
    };
  } // namespace app
} // namespace loka
namespace
{
  using namespace loka::app;
  using namespace loka::app::scene;
  using namespace loka::core;
  typedef loka::app::testing::AppTestAccess Access;
  WebURL url(const char *text)
  {
    WebURL value;
    LOKA_VERIFY(WebURL::parse(String(text), value));
    return value;
  }
  Focus<URLField> *declaredFocus = 0;
  class Root;
  Root *declaredRoot = 0;
  class Root : public BoundaryNodeFor<Root>
  {
  public:
    explicit Root(const BoundaryPropsFor<Root> &p)
        : BoundaryNodeFor<Root>(p)
    {
      this->state(*declaredFocus);
      this->state(this->dialog, false);
      this->state(this->dialogResult, ConfirmResult::none());
      declaredRoot = this;
    }
    NodeState<bool> dialog;
    NodeState<ConfirmResult> dialogResult;
    virtual void composeNode(NodeComposition &c)
    {
      c.declare(Column()
                << EditText(EditTextProps().focusedAs(*declaredFocus, URL_FIELD))
                << (Show(*this->dialog.state())
                    << ConfirmDialog(ConfirmDialogOptions(String::Literal("URL result"))).result(this->dialogResult)));
    }
  };
  /** Implements the required platform surface, deliberately inheriting openURL. */
  class DecliningContext : public PlatformContext
  {
  public:
    virtual App *createApp(AppConfigurable *, NativeModuleHandle, int) const
    {
      return 0;
    }
    virtual Window *createWindow(const WindowProps &props)
    {
      return new NullWindow(this, props);
    }
    virtual NodeContext *createNodeContext(Node *) const
    {
      return 0;
    }
    virtual bool openFile(const loka::file::File &, loka::platform::file::FileHandle &) const
    {
      return false;
    }
    virtual bool createImageFromBlob(const resource::Blob &, std::size_t, std::size_t, resource::Image &)
    {
      return false;
    }
  };
  struct Config : AppConfigurable
  {
    URLOpener<3> opener;
    Focus<URLField> focus;
    explicit Config(PlatformContext &context)
        : AppConfigurable(&context)
    {
    }
    virtual void compose(AppComposition &c)
    {
      declaredFocus = &this->focus;
      c << OpenURLs(this->opener);
      c << WindowDef(WindowProps().scene(new Scene(Boundary<Root>())));
    }
  };
  class ProbeApp : public NullApp
  {
  public:
    std::vector<int> events;
    explicit ProbeApp(Config &config)
        : NullApp(&config)
    {
    }
    using App::admitAndApplyWindows;
    using App::reclaimWindows;
    virtual void projectMenu(Window *window, const MenuSources &)
    {
      if (window)
        this->events.push_back(1);
    }
  };
  class RunController : public NullScenePlatformController
  {
  public:
    void (*probe)(void *);
    void *user;
    RunController()
        : probe(0),
          user(0)
    {
    }
    virtual bool canSkipGlobalChangeForBoundaryLocalPaint() const
    {
      return false;
    }
    virtual void onChange(Node *root, NodeDirtyFlags flags, bool full)
    {
      NullScenePlatformController::onChange(root, flags, full);
      if (this->probe)
        this->probe(this->user);
    }
  };
  class TestContext : public NullPlatformContext
  {
  public:
    RunController controller;
    virtual Window *createWindow(const WindowProps &props)
    {
      return new NullWindow(this, props, &this->controller);
    }
  };
  struct Fixture
  {
    TestContext context;
    Config config;
    ProbeApp app;
    Fixture()
        : config(context),
          app(config)
    {
      this->app.run();
      Access::completeTurn(this->app);
      LayoutState bounds;
      bounds.width = 240;
      bounds.height = 320;
      this->context.controller.projectLayoutForTesting(declaredRoot, bounds);
      this->app.events.clear();
    }
    NullWindow &window()
    {
      return *static_cast<NullWindow *>(this->app.adoptedWindows()[0]);
    }
    void post(const char *text)
    {
      LOKA_VERIFY(this->config.opener.post(url(text)) == POST_ACCEPTED);
    }
  };
  struct Watch
  {
    Fixture &f;
    PushStateTracker closeLedger;
    enum Action
    {
      NONE,
      REPOST,
      CLOSE,
      QUIT,
      REENTER,
      DIALOG
    } action;
    explicit Watch(Fixture &fixture, Action a = NONE)
        : f(fixture),
          action(a)
    {
      const_cast<State<URLOpenOutcome> &>(f.config.opener.outcome()).bind(&observed, this, false);
    }
    ~Watch()
    {
      const_cast<State<URLOpenOutcome> &>(f.config.opener.outcome()).unbind(&observed, this);
    }
    static void closed(void *data)
    {
      static_cast<Watch *>(data)->f.app.events.push_back(5);
    }
    static void observed(void *data)
    {
      Watch &self = *static_cast<Watch *>(data);
      LOKA_VERIFY(Operation::hasActive());
      self.f.app.events.push_back(4);
      StateTrackerGuard guard(&self.closeLedger);
      loka::core::testing::PushStateTrackerTestAccess::defer(self.closeLedger, &closed, &self);
      switch (self.action)
      {
      case NONE:
        break;
      case REPOST:
        self.f.post("https://repost/");
        break;
      case CLOSE:
        self.f.app.requestWindowClose(&self.f.window());
        break;
      case QUIT:
        self.f.app.requestQuitAtCompletion();
        break;
      case DIALOG:
        declaredRoot->dialog.set(true);
        break;
      case REENTER:
        LOKA_VERIFY(!self.f.app.serveURLsAtCompletion(App::COMPLETION_OUTERMOST));
        break;
      }
    }
  };
  void focusChanged(void *data)
  {
    static_cast<Fixture *>(data)->app.events.push_back(2);
  }
  URLOpenKind opened(const WebURL &, void *data)
  {
    static_cast<Fixture *>(data)->app.events.push_back(3);
    return URL_OPENED;
  }
  URLOpenKind reenter(const WebURL &, void *data)
  {
    Fixture &f = *static_cast<Fixture *>(data);
    LOKA_VERIFY(!f.app.serveURLsAtCompletion(App::COMPLETION_OUTERMOST));
    LOKA_VERIFY(!Access::completeTurn(f.app));
    LOKA_VERIFY(f.context.openedURLs().size() == 1);
    return URL_NOT_OPENED;
  }
  URLOpenKind launchQuit(const WebURL &, void *data)
  {
    static_cast<Fixture *>(data)->app.requestQuit();
    return URL_OPENED;
  }
  URLOpenKind nullOpen(const WebURL &value, void *data)
  {
    return static_cast<NullPlatformContext *>(data)->openURL(value);
  }
  unsigned runProbes = 0;
  void busy(void *data)
  {
    Fixture &f = *static_cast<Fixture *>(data);
    LOKA_VERIFY(f.window().scene()->isBusy());
    LOKA_VERIFY(!f.app.serveURLsAtCompletion(App::COMPLETION_OUTERMOST));
    LOKA_VERIFY(f.context.openedURLs().empty());
  }
  void running(void *data)
  {
    Fixture &f = *static_cast<Fixture *>(data);
    LOKA_VERIFY(f.window().scene()->isRunInProgress());
    ++runProbes;
    busy(data);
  }
  unsigned dialogs(Node *node)
  {
    if (!node)
      return 0;
    unsigned count = node->asConfirmDialogNode() ? 1 : 0;
    INestable *nest = node->asNestable();
    if (!nest)
      return count;
    loka::dsl::CompositionCursor<Node> it(nest->childrenHead(), nest->childrenCount());
    for (Node *child = it.next(); child; child = it.next())
      count += dialogs(child);
    return count;
  }
} // namespace

void testURLOpenerAppOrder()
{
  Fixture f;
  Watch watch(f);
  f.context.setURLScript(&opened, &f);
  f.config.focus.state()->bind(&focusChanged, &f, false);
  f.config.focus.post(URL_FIELD);
  f.post("https://first/");
  f.post("https://second/");
  Operation::Regime regime;
  LOKA_VERIFY(Access::completeTurn(f.app));
  const int expected[] = {1, 2, 3, 4, 1, 5};
  LOKA_VERIFY(f.app.events == std::vector<int>(expected, expected + 6));
  LOKA_VERIFY(f.context.openedURLs().size() == 1);
  LOKA_VERIFY(f.config.opener.pending() == 1);
  LOKA_VERIFY(f.config.opener.outcome().get().url == url("https://first/"));
  LOKA_VERIFY(Access::completeTurn(f.app));
  LOKA_VERIFY(f.context.openedURLs().size() == 2);
  LOKA_VERIFY(f.config.opener.outcome().get().url == url("https://second/"));
  f.config.focus.state()->unbind(&focusChanged, &f);
  f.context.setURLScript(0, 0);
}

void testURLOpenerAppInheritedClock()
{
  Fixture f;
  f.post("https://first/");
  {
    Operation outer;
    LOKA_VERIFY(!Access::completeTurn(f.app));
  }
  LOKA_VERIFY(f.context.openedURLs().empty());
  LOKA_VERIFY(Access::completeTurn(f.app));
  LOKA_VERIFY(f.context.openedURLs().size() == 1);
}

void testURLOpenerAppPhases()
{
  Fixture f;
  f.post("https://first/");
  f.post("https://second/");
  f.context.setURLScript(&reenter, &f);
  Watch watch(f, Watch::REENTER);
  LOKA_VERIFY(Access::completeTurn(f.app));
  LOKA_VERIFY(f.context.openedURLs().size() == 1);
  f.context.setURLScript(0, 0);
  watch.action = Watch::NONE;
  {
    Operation turn;
    LOKA_VERIFY(loka::app::testing::URLOpenerAccess::launchNext(f.config.opener, &nullOpen, &f.context));
    f.post("https://third/");
    LOKA_VERIFY(!f.app.serveURLsAtCompletion(App::COMPLETION_OUTERMOST));
    LOKA_VERIFY(f.context.openedURLs().size() == 2);
    f.app.admitAndApplyWindows();
    turn.close();
    f.app.reclaimWindows();
  }
}

void testURLOpenerAppBusy()
{
  Fixture f;
  f.post("https://busy/");
  Operation turn;
  {
    BorrowScope borrow(*f.window().scenePlatformController());
    busy(&f);
  }
  SceneFocusTestAccess::duringPublication(f.window().scene()->focus(), &busy, &f);
  runProbes = 0;
  f.context.controller.probe = &running;
  f.context.controller.user = &f;
  f.window().scene()->invalidate(NODE_DIRTY_MYSELF);
  f.app.admitAndApplyWindows();
  f.context.controller.probe = 0;
  f.context.controller.user = 0;
  LOKA_VERIFY(runProbes > 0);
  turn.close();
  f.app.reclaimWindows();
  LOKA_VERIFY(Access::completeTurn(f.app));
}

void testURLOpenerAppObserverEffects()
{
  {
    Fixture f;
    Watch watch(f, Watch::DIALOG);
    f.post("https://dialog/");
    LOKA_VERIFY(Access::completeTurn(f.app));
    LOKA_VERIFY(dialogs(declaredRoot) == 0);
    watch.action = Watch::NONE;
    LOKA_VERIFY(!Access::completeTurn(f.app));
    LOKA_VERIFY(dialogs(declaredRoot) == 1);
  }
  {
    Fixture f;
    Watch watch(f, Watch::REPOST);
    f.post("https://first/");
    LOKA_VERIFY(Access::completeTurn(f.app));
    LOKA_VERIFY(f.context.openedURLs().size() == 1 && f.config.opener.pending() == 1);
    watch.action = Watch::NONE;
    LOKA_VERIFY(Access::completeTurn(f.app));
    LOKA_VERIFY(f.context.openedURLs().size() == 2);
  }
  {
    Fixture f;
    Watch watch(f, Watch::QUIT);
    f.post("https://quit/");
    LOKA_VERIFY(f.app.quitCalls() == 0);
    LOKA_VERIFY(Access::completeTurn(f.app));
    LOKA_VERIFY(f.app.quitCalls() == 1);
  }
  {
    Fixture f;
    Watch watch(f, Watch::CLOSE);
    f.post("https://close/");
    LOKA_VERIFY(Access::completeTurn(f.app));
    LOKA_VERIFY(Access::pendingWindowCloseCount(f.app) == 1);
    watch.action = Watch::NONE;
    LOKA_VERIFY(!Access::completeTurn(f.app));
    LOKA_VERIFY(Access::pendingWindowCloseCount(f.app) == 0);
  }
  {
    Fixture f;
    Watch watch(f);
    f.post("https://quit-launch/");
    f.context.setURLScript(&launchQuit, &f);
    LOKA_VERIFY(Access::completeTurn(f.app));
    LOKA_VERIFY(!f.config.opener.outcome().get().isNone());
    f.context.setURLScript(0, 0);
  }
}

void testURLOpenerAppRegistration()
{
  NullPlatformContext context;
  URLOpener<1> first, second;
  AppComposition composition(&context);
  composition << OpenURLs(first);
#ifdef NDEBUG
  composition << OpenURLs(second);
#elif defined(__linux__) && !defined(__SANITIZE_ADDRESS__)
  const pid_t child = fork();
  LOKA_VERIFY(child >= 0);
  if (child == 0)
  {
    composition << OpenURLs(second);
    _exit(0);
  }
  int status = 0;
  LOKA_VERIFY(waitpid(child, &status, 0) == child);
  LOKA_VERIFY(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
#else
  std::puts("[skip] duplicate registration death pin requires Linux debug without ASan");
#endif
  LOKA_VERIFY(composition.takeURLOpener() == &first);
  LOKA_VERIFY(composition.takeURLOpener() == 0);
  NullApp absent(0);
  LOKA_VERIFY(!Access::completeTurn(absent));
  DecliningContext declining;
  Config config(declining);
  ProbeApp app(config);
  app.run();
  LOKA_VERIFY(config.opener.post(url("https://default/")) == POST_ACCEPTED);
  LOKA_VERIFY(Access::completeTurn(app));
  LOKA_VERIFY(config.opener.outcome().get() == URLOpenOutcome(URL_UNAVAILABLE, url("https://default/")));
}

namespace
{
  void countOutcome(void *data)
  {
    ++*static_cast<unsigned *>(data);
  }
} // namespace
void testURLOpenerAppLeftovers()
{
  NullPlatformContext context;
  unsigned delivered = 0;
  {
    Config config(context);
    State<URLOpenOutcome> &fact = const_cast<State<URLOpenOutcome> &>(config.opener.outcome());
    fact.bind(&countOutcome, &delivered, false);
    {
      ProbeApp app(config);
      app.run();
      LOKA_VERIFY(config.opener.post(url("https://first/")) == POST_ACCEPTED);
      LOKA_VERIFY(config.opener.post(url("https://leftover/")) == POST_ACCEPTED);
      LOKA_VERIFY(Access::completeTurn(app));
    }
    LOKA_VERIFY(config.opener.pending() == 1);
    LOKA_VERIFY(delivered == 1);
    // The observer remains bound through opener destruction to detect a write.
  }
  LOKA_VERIFY(delivered == 1 && context.openedURLs().size() == 1);
}
