#include "WebSession.hpp"
#include "WebWindow.hpp"
#include "WebTestAccess.hpp"
#include "FakeBoundary.hpp"
#include "WebFixture.hpp"
#include "support/TestVerify.hpp"
#include "support/LifecycleFactTestAccess.hpp"
#include "testing/scene/SceneTestFlow.hpp"
#include "app/nodes/nestable/ScrollView.hpp"
#include "app/nodes/controls/PopupMenu.hpp"
#include "app/nodes/controls/Card.hpp"
#include "app/nodes/controls/ScrollBar.hpp"
#include "app/nodes/controls/TextEditor.hpp"
#include "app/nodes/ImageView.hpp"
#include "app/RectSurface.hpp"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <cstdlib>
namespace webtest
{
  Fixture *fixture = 0;
}
namespace
{
  using namespace loka::web;
  using loka::web::testing::Access;
  WebSession *current = 0;
  uint32_t eventToken = 0;
  unsigned nestedCalls = 0, completingCalls = 0, retirementChecks = 0;
  void Reset(webtest::Fixture &fixture)
  {
    webtest::reset();
    webtest::fixture = &fixture;
    webtest::routeProbes = webtest::commitsRefused = webtest::installationsRefused = webtest::tails = 0;
    webtest::clickProbe = 0;
    webtest::tailProbe = 0;
  }
  void Shutdown(WebSession &session)
  {
    session.dispose();
    LOKA_VERIFY(webtest::task(SHUTDOWN) != 0);
    webtest::fire(webtest::task(SHUTDOWN), 1000);
    LOKA_VERIFY(webtest::resources.empty());
    LOKA_VERIFY(Access::routesEmpty(session));
    LOKA_VERIFY(Access::disposed(session));
  }
  void CounterTest()
  {
    webtest::Fixture fixture;
    Reset(fixture);
    WebSession session;
    LOKA_VERIFY(session.start(&webtest::CreateCounter, 0) == WebSession::STARTED);
    uint32_t button = webtest::token(BUTTON), text = webtest::token(TEXT);
    LOKA_VERIFY(button && text);
    LOKA_VERIFY(webtest::resources[text].text == "Count: 0");
    if (std::getenv("LOKA_WEB_CAPTURE_GOLDEN"))
    {
      for (unsigned i = 0; i < webtest::calls.size(); ++i)
        std::printf("[golden] %s\n", webtest::calls[i].c_str());
    }
    else
    {
      const std::string source(__FILE__);
      const std::string path = source.substr(0, source.find_last_of("/\\") + 1) + "counter.golden";
      FILE *file = std::fopen(path.c_str(), "rb");
      LOKA_VERIFY(file != 0);
      std::string expected;
      char buffer[256];
      std::size_t n = 0;
      while ((n = std::fread(buffer, 1, sizeof(buffer), file)) != 0)
        expected.append(buffer, n);
      LOKA_VERIFY(std::fclose(file) == 0);
      std::string actual;
      for (unsigned i = 0; i < webtest::calls.size(); ++i)
        actual += webtest::calls[i] + "\n";
      LOKA_VERIFY(actual == expected);
    }
    webtest::calls.clear();
    const unsigned tails = webtest::tails;
    LOKA_VERIFY(session.click(button, 1) == WebSession::ACCEPTED);
    LOKA_VERIFY(webtest::resources[text].text == "Count: 1");
    unsigned writes = 0;
    char expected[40];
    std::sprintf(expected, "text:%u=Count: 1", text);
    for (unsigned i = 0; i < webtest::calls.size(); ++i)
      if (webtest::calls[i] == expected)
        ++writes;
    LOKA_VERIFY(writes == 1 && webtest::tails == tails + 1 && fixture.bodyCalls == 1);
    Shutdown(session);
    session.dispose();
    LOKA_VERIFY(webtest::completed == 1);
  }
  void ParkTest()
  {
    webtest::Fixture fixture;
    Reset(fixture);
    WebSession session;
    LOKA_VERIFY(session.start(&webtest::CreateCounter, 0) == WebSession::STARTED);
    const uint32_t button = webtest::token(BUTTON);
    {
      loka::core::Operation turn;
      fixture.nodes[0]->enable(false);
    }
    session.wake(IMMEDIATE, 1);
    LOKA_VERIFY(session.click(button, 2) == WebSession::REFUSED_TOKEN);
    {
      loka::core::Operation turn;
      fixture.nodes[0]->enable(true);
      fixture.nodes[0]->show(false);
    }
    session.wake(IMMEDIATE, 3);
    LOKA_VERIFY(session.routes().find(button) != 0 && webtest::resources[button].hidden);
    LOKA_VERIFY(session.click(button, 4) == WebSession::REFUSED_TOKEN);
    {
      loka::core::Operation turn;
      fixture.nodes[0]->show(true);
    }
    session.wake(IMMEDIATE, 5);
    LOKA_VERIFY(webtest::token(BUTTON) == button && !webtest::resources[button].hidden);
    LOKA_VERIFY(session.click(button, 6) == WebSession::ACCEPTED);
    Shutdown(session);
  }
  void RevokeProbe(const char *call, uint32_t token)
  {
    if (std::strcmp(call, "revoke") != 0)
      return;
    LOKA_VERIFY(current->routes().find(token) == 0);
    LOKA_VERIFY(webtest::resources.find(token) != webtest::resources.end());
    LOKA_VERIFY(webtest::resources[token].hidden);
    LOKA_VERIFY(current->click(token, 1) != WebSession::ACCEPTED);
    ++retirementChecks;
  }
  void RetirementTest()
  {
    webtest::Fixture fixture;
    fixture.destroy = true;
    Reset(fixture);
    WebSession session;
    current = &session;
    LOKA_VERIFY(session.start(&webtest::CreateCounter, 0) == WebSession::STARTED);
    const uint32_t old = webtest::token(BUTTON);
    retirementChecks = 0;
    webtest::probe = &RevokeProbe;
    {
      loka::core::Operation turn;
      fixture.nodes[0]->show(false);
    }
    // Admission delivers terminal facts; RevokeProbe observes the pre-drain interval.
    LOKA_VERIFY(webtest::resources.find(old) != webtest::resources.end());
    session.wake(IMMEDIATE, 2);
    LOKA_VERIFY(webtest::resources.find(old) == webtest::resources.end());
    {
      loka::core::Operation turn;
      fixture.nodes[0]->show(true);
    }
    session.wake(IMMEDIATE, 3);
    const uint32_t fresh = webtest::token(BUTTON);
    LOKA_VERIFY(fresh != 0);
    // B is installed and its current callback works before replaying A's saved
    // token callback. Under mutation 3 both retired leaf slots are reused, so
    // this fails on stale delivery into live B, not on an allocator collision.
    LOKA_VERIFY(session.click(fresh, 4) == WebSession::ACCEPTED);
    const unsigned beforeStale = fixture.bodyCalls;
    const WebSession::Ingress stale = session.click(old, 5);
    LOKA_VERIFY(fixture.bodyCalls == beforeStale);
    LOKA_VERIFY(stale == WebSession::REFUSED_TOKEN);
    LOKA_VERIFY(fresh != old);
    LOKA_VERIFY(retirementChecks >= 2);
    webtest::probe = 0;
    // Pending retirement is also discharged by teardown without another ordinary tail.
    {
      loka::core::Operation turn;
      fixture.nodes[0]->show(false);
    }
    Shutdown(session);
  }
  void SeatsTest()
  {
    for (unsigned seat = 1; seat <= 2; ++seat)
    {
      webtest::Fixture fixture;
      fixture.seat = seat;
      Reset(fixture);
      WebSession session;
      LOKA_VERIFY(session.start(&webtest::CreateCounter, 0) == WebSession::STARTED);
      const uint32_t old = webtest::token(BUTTON);
      {
        loka::core::Operation turn;
        if (seat == 1)
          fixture.nodes[0]->show(false);
        else
          fixture.nodes[0]->key(1);
      }
      session.wake(IMMEDIATE, 1);
      LOKA_VERIFY(session.click(old, 2) == WebSession::REFUSED_TOKEN);
      {
        loka::core::Operation turn;
        if (seat == 1)
          fixture.nodes[0]->show(true);
        else
          fixture.nodes[0]->key(0);
      }
      session.wake(IMMEDIATE, 3);
      const uint32_t next = webtest::token(BUTTON);
      LOKA_VERIFY(next != 0 && (seat == 1 ? next == old : next != old));
      LOKA_VERIFY(session.click(next, 4) == WebSession::ACCEPTED);
      Shutdown(session);
    }
  }
  class RefusedScrollRoot : public loka::app::scene::BoundaryNodeFor<RefusedScrollRoot>
  {
  public:
    loka::app::scene::NodeState<loka::core::Frame> viewport;
    loka::app::scene::Request<loka::app::ScrollTarget> request;
    explicit RefusedScrollRoot(const loka::app::scene::BoundaryPropsFor<RefusedScrollRoot> &props)
        : loka::app::scene::BoundaryNodeFor<RefusedScrollRoot>(props)
    {
      this->state(this->viewport, loka::core::Frame(1, 2, 80, 30));
      this->state(this->request, loka::app::ScrollTarget::Origin(7, 8));
    }
    virtual void composeNode(loka::app::scene::NodeComposition &c)
    {
      c.declare(loka::app::ScrollView(this->viewport).scrollTo(this->request) << loka::app::Text("must not project"));
    }
  };
  void RefusalTest()
  {
    using namespace loka::app;
    using namespace loka::app::scene;
    webtest::Fixture fixture;
    Reset(fixture);
    WebSession session;
    WebElement root;
    int width = 320, height = 200;
    LOKA_VERIFY(root.create(session.routes(), session.tokens(), ROOT, 0, width, height));
    {
      WebScenePlatformController controller(session, root.token(), width, height);
      EditTextNode edit((EditTextProps()));
      TextEditorNode editor((TextEditorProps()));
      PopupMenuNode popup((PopupMenuProps()));
      ImageViewNode image((ImageViewProps()));
      RectSurfaceNode surface((RectSurfaceProps()));
      ScrollBarNode bar((ScrollBarProps()));
      CellNode cell((CellProps()));
      CardBandNode band((CardBandProps()));
      Node *nodes[] = {&edit, &editor, &popup, &image, &surface, &bar, &cell, &band};
      LayoutState layout;
      for (unsigned i = 0; i < sizeof(nodes) / sizeof(nodes[0]); ++i)
      {
        LOKA_VERIFY(!controller.prepareProjectedLayout(nodes[i], layout));
        LOKA_VERIFY(!nodes[i]->getContext());
      }
      Scene scene((Boundary<RefusedScrollRoot>()));
      LOKA_VERIFY(scene.mount(&controller));
      RefusedScrollRoot *boundary =
          static_cast<RefusedScrollRoot *>(loka::dsl::testing::SceneTestAccess::rootBoundary(scene));
      loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);
      for (unsigned i = 0; scene.hasPendingInvalidation() && i < 12; ++i)
        LOKA_VERIFY(scene.flushInvalidation());
      LOKA_VERIFY(!scene.hasPendingInvalidation());
      LOKA_VERIFY(boundary->request.get().isNone());
      LOKA_VERIFY(boundary->viewport.get() == loka::core::Frame(1, 2, 80, 30));
      LOKA_VERIFY(webtest::resources.size() == 1);
      loka::dsl::testing::SceneTestAccess::unmount(scene);
      controller.destroy();
    }
    WebResource *row = root.revoke(session.routes());
    LOKA_VERIFY(web_release(row->token) == CONSUMED);
    delete row;
  }
  void ResourceTest()
  {
    webtest::Fixture fixture;
    Reset(fixture);
    WebSession session;
    WebElement root;
    int width = 320, height = 200;
    LOKA_VERIFY(root.create(session.routes(), session.tokens(), ROOT, 0, width, height));
    {
      WebScenePlatformController controller(session, root.token(), width, height);
      loka::app::ButtonNode node(loka::app::Button("Test").props);
      loka::app::scene::LayoutState state;
      webtest::failCreate = 1;
      LOKA_VERIFY(!controller.prepareProjectedLayout(&node, state));
      LOKA_VERIFY(webtest::resources.size() == 1 && !controller.hasPendingSync());
      webtest::commitsRefused = 1;
      LOKA_VERIFY(!controller.prepareProjectedLayout(&node, state));
      LOKA_VERIFY(webtest::resources.size() == 1);
      webtest::installationsRefused = 1;
      LOKA_VERIFY(!controller.prepareProjectedLayout(&node, state));
      LOKA_VERIFY(controller.hasPendingSync());
      LOKA_VERIFY(webtest::resources.size() == 2);
      controller.drainNativeRetirements();
      LOKA_VERIFY(webtest::resources.size() == 1);
      LOKA_VERIFY(controller.prepareProjectedLayout(&node, state));
      const uint32_t button = webtest::token(BUTTON);
      loka::app::scene::LifecycleFactTestAccess::MarkSubtreeRetired(&node);
      node.setContext(0);
      LOKA_VERIFY(!session.routes().find(button));
      LOKA_VERIFY(webtest::resources[button].hidden);
      LOKA_VERIFY(controller.hasPendingSync());
      controller.drainNativeRetirements();
      LOKA_VERIFY(web_release(button) == ABSENT);
    }
    WebResource *row = root.revoke(session.routes());
    LOKA_VERIFY(web_release(row->token) == CONSUMED);
    delete row;
    LOKA_VERIFY(web_release(0) == NO_OWNERSHIP);
    Access::nextToken(session, UINT32_MAX - 1);
    LOKA_VERIFY(session.tokens().take() == UINT32_MAX - 1);
    LOKA_VERIFY(session.tokens().take() == UINT32_MAX);
    LOKA_VERIFY(session.tokens().take() == 0 && session.tokens().take() == 0);
  }
  void RouteBoundTest()
  {
    WebInputRoutes routes;
    for (unsigned i = 1; i <= 4096; ++i)
      LOKA_VERIFY(routes.reserve(i, BUTTON) != 0);
    for (unsigned i = 1; i <= 4096; ++i)
    {
      webtest::routeProbes = 0;
      LOKA_VERIFY(routes.find(i) != 0);
      LOKA_VERIFY(webtest::routeProbes <= 32);
    }
    for (unsigned i = 1; i <= 4096; i += 2)
      delete routes.extract(i);
    for (unsigned i = 2; i <= 4096; i += 2)
    {
      LOKA_VERIFY(routes.find(i) != 0);
      delete routes.extract(i);
    }
  }
  void DeepRouteTest()
  {
    webtest::Fixture fixture;
    fixture.depth = 128;
    Reset(fixture);
    WebSession session;
    LOKA_VERIFY(session.start(&webtest::CreateCounter, 0) == WebSession::STARTED);
    const uint32_t button = webtest::token(BUTTON);
    webtest::routeProbes = 0;
    LOKA_VERIFY(session.click(button, 1) == WebSession::ACCEPTED);
    LOKA_VERIFY(webtest::routeProbes <= 66);
    Shutdown(session);
  }
  void Nested(WebElementContext &context)
  {
    webtest::clickProbe = 0;
    LOKA_VERIFY(context.controller().borrowPhase().open());
    LOKA_VERIFY(current->click(eventToken, 2) == WebSession::NESTED);
    LOKA_VERIFY(context.controller().borrowPhase().open());
    ++nestedCalls;
  }
  void CompletingProbe(const char *call, uint32_t)
  {
    if (std::strcmp(call, "text") != 0)
      return;
    const unsigned before = webtest::fixture->bodyCalls;
    const WebSession::Ingress result = current->click(eventToken, 3);
    LOKA_VERIFY(webtest::fixture->bodyCalls == before);
    LOKA_VERIFY(result == WebSession::REFUSED_COMPLETING);
    ++completingCalls;
  }
  void ReentryTest()
  {
    webtest::Fixture fixture;
    Reset(fixture);
    WebSession session;
    current = &session;
    LOKA_VERIFY(session.start(&webtest::CreateCounter, 0) == WebSession::STARTED);
    eventToken = webtest::token(BUTTON);
    nestedCalls = completingCalls = 0;
    const unsigned tails = webtest::tails;
    webtest::clickProbe = &Nested;
    webtest::probe = &CompletingProbe;
    LOKA_VERIFY(session.click(eventToken, 1) == WebSession::ACCEPTED);
    LOKA_VERIFY(nestedCalls == 1 && completingCalls > 0 && webtest::tails == tails + 1);
    webtest::probe = 0;
    Shutdown(session);
  }
  void DisposeInInput(WebElementContext &context)
  {
    webtest::clickProbe = 0;
    current->dispose();
    // Load-bearing lifetime pin: synchronous shutdown leaves this receiver freed.
    LOKA_VERIFY(context.owner()->lifecycleFact() != loka::app::scene::NODE_FACT_RETIRED);
    LOKA_VERIFY(context.controller().borrowPhase().open());
    LOKA_VERIFY(!Access::disposed(*current));
  }
  void DisposeProbe(const char *, uint32_t)
  {
    webtest::probe = 0;
    current->dispose();
  }
  void BootstrapProbe(const char *call, uint32_t token)
  {
    if (std::strcmp(call, "create") == 0)
      LOKA_VERIFY(current->click(token, 0) == WebSession::REFUSED_BOOTSTRAP);
  }
  void DisposeTest()
  {
    for (unsigned location = 0; location < 4; ++location)
    {
      webtest::Fixture fixture;
      Reset(fixture);
      WebSession session;
      current = &session;
      webtest::probe = location == 0 ? &DisposeProbe : &BootstrapProbe;
      LOKA_VERIFY(session.start(&webtest::CreateCounter, 0) == WebSession::STARTED);
      webtest::probe = 0;
      if (location == 1)
      {
        webtest::clickProbe = &DisposeInInput;
        session.click(webtest::token(BUTTON), 1);
      }
      if (location == 2)
      {
        webtest::probe = &DisposeProbe;
        session.click(webtest::token(BUTTON), 1);
      }
      LOKA_VERIFY(!Access::disposed(session));
      webtest::probe = 0;
      Shutdown(session);
    }
  }
  struct Document : loka::app::DocumentFacts
  {
    explicit Document(unsigned n = 0)
        : value(n)
    {
    }
    unsigned value;
  };
  class RosterConfig : public AppConfigurable
  {
  public:
    loka::app::DocumentRoster<Document, 16> roster;
    unsigned limit;
    bool decline;
    explicit RosterConfig(PlatformContext *p)
        : AppConfigurable(p),
          limit(11),
          decline(false)
    {
      this->roster.open(Document());
      this->roster.closeAsked().deferBind(&RosterConfig::Asked, this);
    }
    virtual ~RosterConfig()
    {
      this->roster.closeAsked().deferUnbind(&RosterConfig::Asked, this);
    }
    virtual void compose(AppComposition &c)
    {
      c << loka::app::DocumentWindows(this->roster, &RosterConfig::MakeWindow, this);
    }
    virtual loka::app::IdlePolicy idlePolicy() const
    {
      return webtest::fixture->policy;
    }
    static WindowProps MakeWindow(loka::core::ItemId, const Document &document, void *data)
    {
      RosterConfig &self = *static_cast<RosterConfig *>(data);
      if (document.value < self.limit)
        self.roster.open(Document(document.value + 1));
      return WindowProps().size(320, 200).scene(loka::app::scene::Boundary<webtest::Counter>());
    }
    static void Asked(void *data)
    {
      RosterConfig &self = *static_cast<RosterConfig *>(data);
      const loka::app::CloseAsk ask = self.roster.closeAsked().get();
      if (ask.id.isNone())
        return;
      if (self.decline)
        self.roster.declineClose(ask);
      else
        self.roster.close(ask.id);
    }
  };
  RosterConfig *rosterConfig = 0;
  AppConfigurable *CreateRoster(PlatformContext *p)
  {
    rosterConfig = new RosterConfig(p);
    return rosterConfig;
  }
  void DisposeInputOnlyTest()
  {
    webtest::Fixture fixture;
    Reset(fixture);
    WebSession session;
    current = &session;
    LOKA_VERIFY(session.start(&webtest::CreateCounter, 0) == WebSession::STARTED);
    webtest::clickProbe = &DisposeInInput;
    session.click(webtest::token(BUTTON), 1);
    Shutdown(session);
  }
  void ContinuationTest()
  {
    for (unsigned order = 0; order < 2; ++order)
    {
      webtest::Fixture fixture;
      fixture.policy = loka::app::IdlePolicy::everyTick();
      Reset(fixture);
      WebSession session;
      LOKA_VERIFY(session.start(&CreateRoster, 0) == WebSession::STARTED);
      LOKA_VERIFY(webtest::tails == 5);
      LOKA_VERIFY(webtest::task(IMMEDIATE) != 0 && webtest::task(FRAME) != 0);
      if (order == 0)
        session.click(webtest::token(BUTTON), 1);
      else
        webtest::fire(webtest::task(IMMEDIATE), 1);
      LOKA_VERIFY(webtest::tails == 6);
      Shutdown(session);
    }
  }
  void QuitTest()
  {
    webtest::Fixture fixture;
    Reset(fixture);
    WebSession session;
    LOKA_VERIFY(session.start(&CreateRoster, 0) == WebSession::STARTED);
    session.wake(IMMEDIATE, 1);
    rosterConfig->decline = true;
    Access::requestQuit(session);
    session.wake(IMMEDIATE, 2);
    LOKA_VERIFY(webtest::task(SHUTDOWN) == 0 && !Access::disposed(session));
    rosterConfig->decline = false;
    Access::requestQuit(session);
    for (unsigned i = 0; i < 20 && !webtest::task(SHUTDOWN); ++i)
      session.wake(IMMEDIATE, 3 + i);
    LOKA_VERIFY(webtest::task(SHUTDOWN) != 0);
    webtest::fire(webtest::task(SHUTDOWN), 30);
    LOKA_VERIFY(Access::disposed(session) && webtest::resources.empty());
  }
  void IdleTest()
  {
    webtest::Fixture fixture;
    Reset(fixture);
    WebSession session;
    LOKA_VERIFY(session.start(&webtest::CreateCounter, 0) == WebSession::STARTED);
    LOKA_VERIFY(webtest::tasks.empty());
    fixture.policy = loka::app::IdlePolicy::everyTick();
    session.wake(IMMEDIATE, 0);
    const unsigned first = webtest::task(FRAME);
    session.click(webtest::token(BUTTON), 4);
    session.click(webtest::token(BUTTON), 9);
    LOKA_VERIFY(fixture.idleCalls == 0);
    webtest::fire(first, 16);
    LOKA_VERIFY(fixture.idleCalls == 1 && std::fabs(fixture.idleElapsed - 0.016) < 0.000001);
    const unsigned second = webtest::task(FRAME);
    LOKA_VERIFY(second != 0 && second != first);
    const unsigned tails = webtest::tails;
    webtest::fire(first, 17);
    LOKA_VERIFY(webtest::tails == tails);
    fixture.policy = loka::app::IdlePolicy::interval(0.1);
    session.wake(IMMEDIATE, 16);
    webtest::fire(second, 17);
    LOKA_VERIFY(fixture.idleCalls == 1);
    webtest::fire(webtest::task(TIMEOUT), 50);
    LOKA_VERIFY(fixture.idleCalls == 1);
    webtest::fire(webtest::task(TIMEOUT), 116);
    LOKA_VERIFY(fixture.idleCalls == 2);
    LOKA_VERIFY(std::fabs(webtest::tasks[webtest::task(TIMEOUT)].ms - 100) < 0.000001);
    webtest::fire(webtest::task(TIMEOUT), 216);
    LOKA_VERIFY(fixture.idleCalls == 3);
    fixture.policy = loka::app::IdlePolicy::none();
    session.wake(IMMEDIATE, 217);
    LOKA_VERIFY(webtest::task(TIMEOUT) == 0 && webtest::task(FRAME) == 0);
    Shutdown(session);
  }
  AppConfigurable *RefuseConfig(PlatformContext *)
  {
    return 0;
  }
  void BootstrapTest()
  {
    webtest::Fixture fixture;
    fixture.roots = 0;
    Reset(fixture);
    WebSession session;
    LOKA_VERIFY(session.start(&webtest::CreateCounter, 0) == WebSession::WINDOWLESS);
    LOKA_VERIFY(!Access::regime(session));
    LOKA_VERIFY(session.click(1, 0) == WebSession::REFUSED_CLOSED);
    Shutdown(session);
    WebSession failed;
    LOKA_VERIFY(failed.start(&RefuseConfig, 0) == WebSession::FAILED);
    Shutdown(failed);
    LOKA_VERIFY(failed.start(&webtest::CreateCounter, 1) == WebSession::FAILED);
  }
  void OldModuleTest()
  {
    webtest::Fixture fixture;
    Reset(fixture);
    WebSession oldSession;
    LOKA_VERIFY(oldSession.start(&webtest::CreateCounter, 0) == WebSession::STARTED);
    const uint32_t oldButton = webtest::token(BUTTON);
    oldSession.dispose();
    const webtest::Task callback = webtest::tasks[webtest::task(SHUTDOWN)];
    Shutdown(oldSession);
    WebSession newSession;
    LOKA_VERIFY(newSession.start(&webtest::CreateCounter, 0) == WebSession::STARTED);
    LOKA_VERIFY(webtest::token(BUTTON) == oldButton);
    LOKA_VERIFY(oldSession.click(oldButton, 1) == WebSession::REFUSED_CLOSED);
    callback.callback(callback.session, 1, callback.kind, 1);
    LOKA_VERIFY(newSession.click(oldButton, 2) == WebSession::ACCEPTED);
    Shutdown(newSession);
  }
  unsigned closeTail = 0;
  uint32_t closingRoot = 0;
  void CloseProbe(const char *call, uint32_t token)
  {
    if (std::strcmp(call, "text") == 0 && closingRoot)
    {
      WebWindow *window = current->routes().find(closingRoot)->root;
      Access::close(*current, window);
    }
    if (std::strcmp(call, "release") == 0 && token == closingRoot)
      LOKA_VERIFY(webtest::tails == closeTail + 1);
  }
  void CloseDuringTailTest()
  {
    webtest::Fixture fixture;
    fixture.roots = 2;
    Reset(fixture);
    WebSession session;
    current = &session;
    LOKA_VERIFY(session.start(&webtest::CreateCounter, 0) == WebSession::STARTED);
    closingRoot = webtest::token(ROOT);
    closeTail = webtest::tails;
    const uint32_t button = webtest::token(BUTTON);
    webtest::probe = &CloseProbe;
    LOKA_VERIFY(session.click(button, 1) == WebSession::ACCEPTED);
    webtest::probe = 0;
    LOKA_VERIFY(webtest::tails == closeTail + 2);
    LOKA_VERIFY(session.resize(closingRoot, 10, 10, 2) == WebSession::REFUSED_TOKEN);
    LOKA_VERIFY(session.click(button, 2) == WebSession::REFUSED_TOKEN);
    closingRoot = 0;
    Shutdown(session);
  }
  void RootVisibilityTest()
  {
    webtest::Fixture fixture;
    Reset(fixture);
    WebSession session;
    LOKA_VERIFY(session.start(&webtest::CreateCounter, 0) == WebSession::STARTED);
    const uint32_t root = webtest::token(ROOT), button = webtest::token(BUTTON);
    WebWindow *window = session.routes().find(root)->root;
    {
      loka::core::Operation turn;
      loka::core::StateTrackerGuard guard(window->getTracker());
      window->visibilityState().set(false);
    }
    webtest::failHidden = 100;
    session.wake(IMMEDIATE, 1);
    LOKA_VERIFY(!webtest::resources[root].hidden);
    LOKA_VERIFY(session.click(button, 2) == WebSession::REFUSED_TOKEN);
    webtest::failHidden = 0;
    session.wake(IMMEDIATE, 3);
    LOKA_VERIFY(webtest::resources[root].hidden);
    {
      loka::core::Operation turn;
      loka::core::StateTrackerGuard guard(window->getTracker());
      window->visibilityState().set(true);
    }
    session.wake(IMMEDIATE, 4);
    LOKA_VERIFY(!webtest::resources[root].hidden && webtest::token(ROOT) == root);
    LOKA_VERIFY(session.click(button, 5) == WebSession::ACCEPTED);
    Shutdown(session);
  }
  void TwoRootTest()
  {
    webtest::Fixture fixture;
    fixture.roots = 2;
    Reset(fixture);
    WebSession session;
    LOKA_VERIFY(session.start(&webtest::CreateCounter, 0) == WebSession::STARTED);
    std::vector<uint32_t> roots;
    for (std::map<uint32_t, webtest::Resource>::iterator i = webtest::resources.begin(); i != webtest::resources.end();
         ++i)
      if (i->second.kind == ROOT)
        roots.push_back(i->first);
    LOKA_VERIFY(roots.size() == 2);
    LOKA_VERIFY(session.resize(roots[0], 201, 101, 1) == WebSession::ACCEPTED);
    LOKA_VERIFY(session.resize(roots[1], 302, 202, 2) == WebSession::ACCEPTED);
    LOKA_VERIFY(session.routes().find(roots[0])->root->nativeFrame().get().width == 201);
    LOKA_VERIFY(session.routes().find(roots[1])->root->nativeFrame().get().width == 302);
    {
      loka::core::Operation turn;
      Access::close(session, session.routes().find(roots[0])->root);
    }
    LOKA_VERIFY(session.resize(roots[0], 10, 10, 3) == WebSession::REFUSED_TOKEN);
    session.wake(IMMEDIATE, 3);
    LOKA_VERIFY(!session.routes().find(roots[0]));
    LOKA_VERIFY(session.resize(roots[1], 300, 200, 4) == WebSession::ACCEPTED);
    Shutdown(session);
    LOKA_VERIFY(session.resize(roots[0], 10, 10, 3) == WebSession::REFUSED_CLOSED);
  }
} // namespace
int main(int argc, char **argv)
{
  if (argc == 2 && std::strcmp(argv[1], "dispose-input") == 0)
  {
    DisposeInputOnlyTest();
    return 0;
  }
#define RUN(name)                                                                                                      \
  do                                                                                                                   \
  {                                                                                                                    \
    std::printf("%s\n", #name);                                                                                        \
    std::fflush(stdout);                                                                                               \
    name();                                                                                                            \
  } while (0)
  RUN(CounterTest);
  RUN(ParkTest);
  RUN(RetirementTest);
  RUN(SeatsTest);
  RUN(ResourceTest);
  RUN(RefusalTest);
  RUN(RouteBoundTest);
  RUN(DeepRouteTest);
  RUN(ReentryTest);
  RUN(DisposeTest);
  RUN(ContinuationTest);
  RUN(QuitTest);
  RUN(IdleTest);
  RUN(BootstrapTest);
  RUN(OldModuleTest);
  RUN(CloseDuringTailTest);
  RUN(RootVisibilityTest);
  RUN(TwoRootTest);
#undef RUN
  std::puts("18 Web tests passed");
  return 0;
}
