#include "RectSurfaceFixture.hpp"
#include "FakeBoundary.hpp"
#include "WebTestAccess.hpp"
#include "WebRectSurfaceContext.hpp"
#include "support/TestVerify.hpp"
#include "testing/scene/SceneTestFlow.hpp"
#include <cstdio>
#include <cstring>
#include <sstream>
using namespace loka::app;
using namespace loka::app::scene;
using namespace loka::web;
namespace
{
  struct Rig
  {
    WebSession session;
    /** Cost: each test case once; starts one fixture session. */
    explicit Rig(webtest::SurfaceParent parent, short w = 0, short h = 0, bool destroy = false)
    {
      webtest::reset();
      webtest::surfaceSetup().parent = parent;
      webtest::surfaceSetup().width = w;
      webtest::surfaceSetup().height = h;
      webtest::surfaceSetup().destroy = destroy;
      LOKA_VERIFY(this->session.start(&webtest::CreateSurfaces, 0) == WebSession::STARTED);
    }
    /** Cost: test teardown once; existing session shutdown owns its resource rows. */
    ~Rig()
    {
      webtest::probe = 0;
      this->session.dispose();
      webtest::fire(webtest::task(SHUTDOWN), 1000);
      LOKA_VERIFY(webtest::resources.empty());
    }
    /** Cost: assertion once; scans only the fake adapter observations. */
    Token token() const
    {
      return webtest::token(RECT_SURFACE);
    }
    /** Cost: assertion/command once; one test token lookup and owned route lookup. */
    WebRectSurfaceContext &context()
    {
      LOKA_VERIFY(this->token());
      return *static_cast<WebRectSurfaceContext *>(this->session.routes().find(this->token())->receiver);
    }
    /** Cost: assertion once; reads one fake adapter observation. */
    webtest::Resource &row()
    {
      return webtest::resources[this->token()];
    }
    /** Cost: test transition once; invokes the existing session completion. */
    void flush()
    {
      this->session.wake(IMMEDIATE, 0);
    }
    /** Cost: test transition once; fixture-owned tree search and one props apply. */
    void apply(short w, short h)
    {
      {
        loka::core::Operation turn;
        webtest::SurfaceFixture &f = *webtest::surfaceFixture();
        LOKA_VERIFY(RectSurface()
                        .size(w, h)
                        .model(f.model.state())
                        .laidOutExtent(f.extent)
                        .applyPropsToNode(webtest::FindSurfaceNode(&f)));
        f.scene()->requestLayoutAfterRun();
      }
      this->flush();
    }
    /** Cost: test transition once; one existing root resize entry. */
    void resize(int w, int h)
    {
      LOKA_VERIFY(this->session.resize(webtest::token(ROOT), w, h, 0) == WebSession::ACCEPTED);
    }
    /** Cost: assertion once; reads the fixture sentinel and notification observation. */
    void sentinel(unsigned notifications)
    {
      LOKA_VERIFY(webtest::surfaceFixture()->extent.get() == loka::core::Frame(7, 11, 17, 19));
      LOKA_VERIFY(webtest::surfaceFixture()->notifications() == notifications);
    }
  };
  /** Cost: assertion once; walks the test-owned import history. */
  unsigned Calls(const char *name, Token token)
  {
    std::ostringstream prefix;
    prefix << name << ':' << token;
    unsigned count = 0;
    for (unsigned i = 0; i < webtest::calls.size(); ++i)
      if (webtest::calls[i].find(prefix.str()) == 0)
        ++count;
    return count;
  }
  /** Cost: test group once; only its mounted fixture and fake-boundary observations. */
  void Matrix()
  {
    for (int parent = webtest::SURFACE_COLUMN; parent <= webtest::SURFACE_SCROLL; ++parent)
      for (int axes = 0; axes < 4; ++axes)
      {
        Rig rig(static_cast<webtest::SurfaceParent>(parent), axes & 1 ? 130 : 0, axes & 2 ? 50 : 0);
        const unsigned notifications = webtest::surfaceFixture()->notifications();
        LOKA_VERIFY(notifications == 0);
        const webtest::Resource &r = rig.row();
        std::printf(
            "surface matrix %d/%d: %d,%d %dx%d projected=%d\n", parent, axes, r.x, r.y, r.width, r.height, r.projected);
        LOKA_VERIFY(r.kind == RECT_SURFACE);
        const int offered[] = {60, 0, 80, 13, 0, 40, 80};
        const int height = axes & 2 ? 50 : offered[parent];
        LOKA_VERIFY(r.projected == (height > 0));
        if (height > 0)
        {
          LOKA_VERIFY(r.width == (axes & 1 ? 130 : 100));
          LOKA_VERIFY(r.height == height);
        }
        rig.sentinel(notifications);
        rig.resize(140, 100);
        const int resizedOffers[] = {80, 0, 100, 13, 0, 50, 100};
        const int resizedHeight = axes & 2 ? 50 : resizedOffers[parent];
        LOKA_VERIFY(rig.row().projected == (resizedHeight > 0));
        if (resizedHeight > 0)
        {
          LOKA_VERIFY(rig.row().height == resizedHeight);
          LOKA_VERIFY(rig.row().width == (axes & 1 ? 130 : 140));
        }
        rig.sentinel(notifications);
        LOKA_VERIFY(rig.token() && rig.session.routes().find(rig.token())->leaf() == 0);
      }
    Rig rig(webtest::SURFACE_DIRECT, 0, 20);
    const unsigned notifications = webtest::surfaceFixture()->notifications();
    LOKA_VERIFY(notifications == 0);
    const Token token = rig.token();
    LOKA_VERIFY(rig.row().projected);
    rig.resize(0, 80);
    LOKA_VERIFY(!rig.row().projected);
    rig.resize(100, 80);
    LOKA_VERIFY(rig.row().projected && rig.token() == token);
    rig.apply(0, 0);
    rig.resize(100, 0);
    LOKA_VERIFY(!rig.row().projected);
    rig.resize(100, 80);
    LOKA_VERIFY(rig.row().projected && rig.row().height == 80);
    rig.apply(0, 20);
    LayoutState state;
    LOKA_VERIFY(!rig.context().controller().prepareProjectedLayout(rig.context().owner(), state));
    LOKA_VERIFY(rig.context().owner()->getContext() == &rig.context());
    webtest::calls.clear();
    {
      loka::core::Operation turn;
      RectSurfaceModel model;
      webtest::surfaceFixture()->model.set(model, true);
    }
    rig.flush();
    LOKA_VERIFY(Calls("rect", token) > 0 && Calls("projected", token) == Calls("rect", token));
    LOKA_VERIFY(!Calls("text", token) && !Calls("create", token));
    rig.sentinel(notifications);
    state.y = 32760;
    state.width = 100;
    state.height = 80;
    LOKA_VERIFY(rig.context().place(state) == 32780);
    LOKA_VERIFY(state.height == 80 && state.y == 32760);
    // Positive control: the observer must detect even a restored sentinel.
    {
      loka::core::Operation turn;
      webtest::surfaceFixture()->extent.set(loka::core::Frame(1, 2, 3, 4));
    }
    rig.flush();
    LOKA_VERIFY(webtest::surfaceFixture()->notifications() == 1);
    {
      loka::core::Operation turn;
      webtest::surfaceFixture()->extent.set(loka::core::Frame(7, 11, 17, 19));
    }
    rig.flush();
    rig.sentinel(2);
  }
  /** Cost: test group once; only its mounted fixture and fake-boundary observations. */
  void Lifetime()
  {
    for (int destroy = 0; destroy < 2; ++destroy)
    {
      Rig rig(webtest::SURFACE_DIRECT, 20, 30, destroy != 0);
      const Token token = rig.token();
      const unsigned notifications = webtest::surfaceFixture()->notifications();
      LOKA_VERIFY(notifications == 0);
      webtest::calls.clear();
      {
        loka::core::Operation turn;
        webtest::surfaceFixture()->shown.set(false);
      }
      rig.flush();
      rig.sentinel(notifications);
      if (destroy)
      {
        LOKA_VERIFY(!rig.session.routes().find(token));
        LOKA_VERIFY(Calls("revoke", token) == 1 && Calls("release", token) == 1);
        std::ostringstream revoke, release;
        revoke << "revoke:" << token;
        release << "release:" << token;
        unsigned ri = 0, li = 0;
        for (unsigned i = 0; i < webtest::calls.size(); ++i)
        {
          if (webtest::calls[i] == revoke.str())
            ri = i;
          if (webtest::calls[i].find(release.str()) == 0)
            li = i;
        }
        LOKA_VERIFY(ri < li);
      }
      else
        LOKA_VERIFY(rig.row().hidden && rig.token() == token && !Calls("revoke", token));
      {
        loka::core::Operation turn;
        webtest::surfaceFixture()->shown.set(true);
      }
      rig.flush();
      LOKA_VERIFY(rig.row().projected && !rig.row().hidden);
      LOKA_VERIFY(destroy ? rig.token() != token : rig.token() == token);
      rig.sentinel(notifications);
    }
  }
  /** Cost: test group once; only its mounted fixture and fake-boundary observations. */
  void Failures()
  {
    for (int failure = 0; failure < 3; ++failure)
    {
      webtest::reset();
      webtest::surfaceSetup().parent = webtest::SURFACE_DIRECT;
      webtest::surfaceSetup().width = 20;
      webtest::surfaceSetup().height = 30;
      webtest::surfaceSetup().destroy = false;
      WebSession session;
      WebScenePlatformController controller(session, 0, 100, 80, 0);
      Scene scene((Boundary<webtest::SurfaceFixture>()));
      if (failure == 0)
        webtest::failCreate = 100;
      if (failure == 1)
        webtest::commitsRefused = 100;
      if (failure == 2)
        webtest::installationsRefused = 100;
      LOKA_VERIFY(scene.mount(&controller));
      loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);
      scene.flushInvalidation();
      Node *surface = webtest::FindSurfaceNode(webtest::surfaceFixture());
      LOKA_VERIFY(!surface->getContext());
      LOKA_VERIFY(controller.hasPendingSync() == (failure == 2));
      LOKA_VERIFY(webtest::resources.empty() == (failure != 2));
      if (failure == 2)
        LOKA_VERIFY(webtest::resources.begin()->second.revoked);
      controller.drainNativeRetirements();
      LOKA_VERIFY(webtest::resources.empty());
      webtest::failCreate = webtest::commitsRefused = webtest::installationsRefused = 0;
      controller.requestRelayout();
      scene.flushInvalidation();
      LOKA_VERIFY(surface->getContext() && webtest::token(RECT_SURFACE));
      LOKA_VERIFY(webtest::resources[webtest::token(RECT_SURFACE)].projected);
      loka::dsl::testing::SceneTestAccess::unmount(scene);
      controller.destroy();
      LOKA_VERIFY(webtest::resources.empty());
    }
  }
  /** Cost: test group once; only its mounted fixture and fake-boundary observations. */
  void GridAndScrollOffers()
  {
    {
      Rig rig(webtest::SURFACE_GRID, 130, 50);
      unsigned count = 0;
      for (std::map<uint32_t, webtest::Resource>::const_iterator i = webtest::resources.begin();
           i != webtest::resources.end();
           ++i)
        if (i->second.kind == RECT_SURFACE)
        {
          LOKA_VERIFY(i->second.width == 130 && i->second.height == 50);
          LOKA_VERIFY(i->second.y == static_cast<int>(count) * 40);
          ++count;
        }
      LOKA_VERIFY(count == 2);
      rig.resize(100, 0);
      for (std::map<uint32_t, webtest::Resource>::const_iterator i = webtest::resources.begin();
           i != webtest::resources.end();
           ++i)
        if (i->second.kind == RECT_SURFACE)
          LOKA_VERIFY(i->second.projected && i->second.height == 50 && i->second.y == 0);
    }
    for (int horizontal = 0; horizontal < 2; ++horizontal)
    {
      Rig rig(webtest::SURFACE_SCROLL, horizontal ? 130 : 0, 0);
      webtest::gutterVertical = 7;
      webtest::gutterHorizontal = 9;
      rig.resize(100, 80);
      unsigned count = 0;
      for (std::map<uint32_t, webtest::Resource>::const_iterator i = webtest::resources.begin();
           i != webtest::resources.end();
           ++i)
        if (i->second.kind == RECT_SURFACE)
        {
          const webtest::Resource &r = i->second;
          std::printf("surface scroll gutter %d/%u: %d,%d %dx%d\n", horizontal, count, r.x, r.y, r.width, r.height);
          LOKA_VERIFY(r.projected && r.height == (horizontal ? 71 : 80));
          LOKA_VERIFY(r.width == (horizontal ? 130 : 93));
          LOKA_VERIFY(r.y == static_cast<int>(count) * (horizontal ? 83 : 92));
          ++count;
        }
      LOKA_VERIFY(count == 2);
    }
  }
  /** Cost: test group once; only its mounted fixture and fake-boundary observations. */
  void ProjectionAndRightEdge()
  {
    {
      Rig root(webtest::SURFACE_WALL_ROOT, 20, 20);
      Node *box = webtest::FindSurfaceNode(webtest::surfaceFixture(), true)->nextInComposition;
      {
        loka::core::Operation turn;
        LOKA_VERIFY(Box().padding(1).applyPropsToNode(box));
      }
      root.apply(32766, 20);
      LOKA_VERIFY(root.row().projected && root.row().x == 1 && root.row().width == 32766);
      root.apply(32767, 20);
      LOKA_VERIFY(root.row().projected && root.row().x == 1 && root.row().width == 32767);
      root.sentinel(0);
    }
    Rig rig(webtest::SURFACE_WALL_SCROLL, 20, 20);
    const unsigned notifications = webtest::surfaceFixture()->notifications();
    LOKA_VERIFY(notifications == 0);
    Node *scroll = rig.session.routes().find(webtest::token(SCROLL))->receiver->owner();
    const int insets[] = {32766, 32767, 0};
    for (unsigned i = 0; i < 3; ++i)
    {
      {
        loka::core::Operation turn;
        LOKA_VERIFY(
            ScrollView(webtest::surfaceFixture()->viewport).contentInsets(insets[i], 0).applyPropsToNode(scroll));
        webtest::surfaceFixture()->scene()->requestLayoutAfterRun();
      }
      rig.flush();
      LOKA_VERIFY(rig.row().projected == (i != 1));
      if (i != 1)
        LOKA_VERIFY(rig.row().y == 1 + insets[i]);
      rig.sentinel(notifications);
    }
    Node *outer = webtest::FindSurfaceNode(webtest::surfaceFixture(), true)->nextInComposition;
    {
      loka::core::Operation turn;
      LOKA_VERIFY(Box().padding(1).applyPropsToNode(outer));
    }
    rig.apply(32766, 20);
    LOKA_VERIFY(rig.row().projected && rig.row().x == 1 && rig.row().width == 32766);
    const int extent = webtest::resources[webtest::token(SCROLL)].extent;
    rig.apply(32767, 20);
    LOKA_VERIFY(!rig.row().projected && webtest::resources[webtest::token(SCROLL)].extent == extent);
    rig.apply(20, 20);
    LOKA_VERIFY(rig.row().projected && rig.row().x == 1 && rig.row().y == 2);
    rig.sentinel(notifications);
  }
  /** Cost: test group once; only its mounted fixture and fake-boundary observations. */
  void Walls()
  {
    for (int scroll = 0; scroll < 2; ++scroll)
    {
      Rig rig(scroll ? webtest::SURFACE_WALL_SCROLL : webtest::SURFACE_WALL_ROOT, 20, 20);
      const unsigned notifications = webtest::surfaceFixture()->notifications();
      LOKA_VERIFY(notifications == 0);
      const Token token = rig.token();
      // Scroll extent is retained when the reached leaf rejects its far edge.
      Node *box = webtest::FindSurfaceNode(webtest::surfaceFixture(), true);
      const int tops[] = {32747, 32748, 32760, 1};
      for (unsigned i = 0; i < 4; ++i)
      {
        {
          loka::core::Operation turn;
          LOKA_VERIFY(Box().height(static_cast<short>(tops[i])).applyPropsToNode(box));
          webtest::surfaceFixture()->scene()->requestLayoutAfterRun();
        }
        rig.flush();
        std::printf("surface wall %d/%d: %d,%d %dx%d projected=%d\n",
                    scroll,
                    tops[i],
                    rig.row().x,
                    rig.row().y,
                    rig.row().width,
                    rig.row().height,
                    rig.row().projected);
        LOKA_VERIFY(rig.row().projected == (!scroll || i == 0 || i == 3));
        if (scroll && (i == 1 || i == 2))
          LOKA_VERIFY(webtest::resources[webtest::token(SCROLL)].extent == 32767);
        rig.sentinel(notifications);
      }
      LOKA_VERIFY(rig.token() == token);
    }
  }
} // namespace
/** Cost: test runner once; runs the six fixture groups, no production row traversal. */
void WebRectSurfaceTests()
{
  LOKA_VERIFY(RECT_SURFACE == 6);
  Matrix();
  Lifetime();
  GridAndScrollOffers();
  ProjectionAndRightEdge();
  Walls();
  Failures();
  std::puts("RectSurface mounted sizing, visibility, model, lifetime, extent and int return passed");
}
