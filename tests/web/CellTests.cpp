#include "CellFixture.hpp"
#include "FakeBoundary.hpp"
#include "WebTestAccess.hpp"
#include "support/TestVerify.hpp"
#include "support/LifecycleFactTestAccess.hpp"
#include "../../example/MineSweeper/src/ProductionAppConfig.hpp"
#include <climits>
#include <cstdio>
#include <sstream>
using namespace loka::app;
using namespace loka::app::scene;
using namespace loka::web;
namespace
{
  typedef std::vector<Token> Tokens;
  Tokens Cells(WebSession &session)
  {
    Tokens result;
    for (std::map<Token, webtest::Resource>::const_iterator i = webtest::resources.begin();
         i != webtest::resources.end();
         ++i)
    {
      WebResource *row = session.routes().find(i->first);
      if (row && row->leaf() && row->leaf()->owner()->asCellNode())
        result.push_back(i->first);
    }
    return result;
  }
  Token ButtonToken(WebSession &session)
  {
    for (std::map<Token, webtest::Resource>::const_iterator i = webtest::resources.begin();
         i != webtest::resources.end();
         ++i)
    {
      WebResource *row = session.routes().find(i->first);
      if (row && row->leaf() && row->leaf()->owner()->asButtonNode())
        return i->first;
    }
    return 0;
  }
  struct Rig
  {
    WebSession session;
    explicit Rig(int mode = 0)
    {
      webtest::reset();
      webtest::clickProbe = 0;
      webtest::cellMode() = mode;
      LOKA_VERIFY(this->session.start(&webtest::CreateCells, 0) == WebSession::STARTED);
    }
    ~Rig()
    {
      webtest::clickProbe = 0;
      this->session.dispose();
      webtest::fire(webtest::task(SHUTDOWN), 1000);
      LOKA_VERIFY(webtest::resources.empty());
    }
    void flush()
    {
      this->session.wake(IMMEDIATE, 0);
    }
  };
  void CellPolicy()
  {
    Rig rig;
    const Tokens cells = Cells(rig.session);
    LOKA_VERIFY(cells.size() == 3);
    const Token button = ButtonToken(rig.session);
    LOKA_VERIFY(button != 0 && webtest::resources.size() == 5);
    LOKA_VERIFY(webtest::resources[cells[0]].kind == CELL);
    LOKA_VERIFY(webtest::resources[cells[0]].text == "a b");
    LOKA_VERIFY(webtest::resources[cells[1]].text == "owned");
    LOKA_VERIFY(webtest::resources[cells[0]].enabled && !webtest::resources[button].enabled);
    rig.session.click(button, 0);
    rig.session.click(cells[0], 0);
    rig.session.click(cells[1], 0);
    rig.session.click(cells[2], 0);
    LOKA_VERIFY(webtest::cellFixture()->buttonHits == 0);
    LOKA_VERIFY(webtest::cellFixture()->hits[0].count == 1 && webtest::cellFixture()->hits[1].count == 1);
    WebElementContext *context = rig.session.routes().find(cells[0])->leaf();
    short out = 123;
    LOKA_VERIFY(!context->controller().queryNaturalWidth(context->owner(), out) && out == 123);
    LOKA_VERIFY(!context->controller().queryNaturalHeight(context->owner(), out) && out == 123);
    const short heights[] = {-1, 0, 1, SHRT_MAX};
    for (unsigned i = 0; i < 4; ++i)
    {
      LayoutState state;
      state.width = 27;
      state.height = heights[i];
      context->layout(&context->controller(), state);
      const int expected = heights[i] <= 0 ? 20 : heights[i];
      LOKA_VERIFY(webtest::resources[cells[0]].height == expected);
      LOKA_VERIFY(webtest::resources[cells[0]].width == 27);
    }
    LayoutState state;
    state.y = 32760;
    state.width = 27;
    state.height = 0;
    LOKA_VERIFY(context->layout(&context->controller(), state) == SHRT_MAX);
    LOKA_VERIFY(webtest::resources[cells[0]].y == 32760 && webtest::resources[cells[0]].height == 20);
    state.height = SHRT_MAX;
    LOKA_VERIFY(context->layout(&context->controller(), state) == SHRT_MAX);
    LOKA_VERIFY(webtest::resources[cells[0]].height == SHRT_MAX);
    const short wallHeights[] = {0, SHRT_MAX};
    for (unsigned i = 0; i < 2; ++i)
    {
      state.height = wallHeights[i];
      const webtest::Resource before = webtest::resources[cells[0]];
      LOKA_VERIFY(loka::web::testing::Access::cellViewportVisit(*context, state));
      LOKA_VERIFY(webtest::resources[cells[0]].height == before.height && webtest::resources[cells[0]].y == before.y);
    }
    {
      loka::core::Operation turn;
      webtest::cellFixture()->text.set(loka::core::String::Literal("changed"));
    }
    rig.flush();
    LOKA_VERIFY(webtest::resources[cells[0]].text == "changed");
    std::puts(
        "CellPolicy resource, identity, emitters, copy/live text, natural refusal, height and root bottom passed");
  }
  void Invalidate(WebElementContext &context)
  {
    LifecycleFactTestAccess::MarkSubtreeRetired(context.owner());
  }
  void CellDispatchRecheck()
  {
    Rig rig;
    const Tokens cells = Cells(rig.session);
    webtest::clickProbe = &Invalidate;
    rig.session.click(cells[0], 0);
    LOKA_VERIFY(webtest::cellFixture()->hits[0].count == 0);
    webtest::clickProbe = 0;
    std::puts("CellDispatchRecheck hook invalidates admitted Cell before emission passed");
  }
  void CellPark()
  {
    Rig rig;
    const Tokens cells = Cells(rig.session);
    {
      loka::core::Operation turn;
      webtest::cellFixture()->shown.set(false);
    }
    rig.flush();
    rig.session.click(cells[0], 0);
    LOKA_VERIFY(webtest::cellFixture()->hits[0].count == 0);
    {
      loka::core::Operation turn;
      webtest::cellFixture()->shown.set(true);
    }
    rig.flush();
    LOKA_VERIFY(Cells(rig.session) == cells);
    rig.session.click(cells[0], 0);
    LOKA_VERIFY(webtest::cellFixture()->hits[0].count == 1);
    std::puts("CellPark retained identity and restored admission passed");
  }
  std::string Call(const char *name, Token token)
  {
    std::ostringstream out;
    out << name << ':' << token;
    return out.str();
  }
  void CellGeneration()
  {
    Rig rig(1);
    const Tokens old = Cells(rig.session);
    LOKA_VERIFY(old.size() == 64);
    const Token button = ButtonToken(rig.session);
    rig.session.click(button, 0);
    const Tokens fresh = Cells(rig.session);
    LOKA_VERIFY(fresh.size() == 64 && fresh[0] > old[63]);
    LOKA_VERIFY(ButtonToken(rig.session) == button);
    for (unsigned i = 0; i < 64; ++i)
    {
      LOKA_VERIFY(webtest::resources[fresh[i]].text == ".");
      rig.session.click(old[i], 0);
      LOKA_VERIFY(webtest::cellFixture()->hits[i].count == 0);
      unsigned revokes = 0, releases = 0;
      for (unsigned j = 0; j < webtest::calls.size(); ++j)
      {
        if (webtest::calls[j] == Call("revoke", old[i]))
          ++revokes;
        if (webtest::calls[j] == Call("release", old[i]))
        {
          LOKA_VERIFY(revokes == 1);
          ++releases;
        }
      }
      LOKA_VERIFY(revokes == 1 && releases == 1);
    }
    const int buttonHeight = webtest::resources[button].height;
    // The zero-gap Column passes exactly three positive pixels beyond the Button to Grid.
    rig.session.resize(webtest::token(ROOT), 220, buttonHeight + 3, 0);
    LOKA_VERIFY(webtest::resources[button].height == buttonHeight);
    for (unsigned i = 0; i < 64; ++i)
      LOKA_VERIFY(webtest::resources[fresh[i]].y == buttonHeight);
    rig.session.resize(webtest::token(ROOT), 220, 3, 0);
    for (unsigned i = 0; i < 64; ++i)
      LOKA_VERIFY(webtest::resources[fresh[i]].height == 20);
    std::puts("CellGeneration 64 replacements, retained New Game, stale ingress, revoke-before-release once, cramped "
              "fallback passed");
  }
  void CellMeasurement(bool protocolDeath = false)
  {
    webtest::reset();
    WebSession session;
    WebElement root;
    int width = 100, height = 80;
    LOKA_VERIFY(root.create(session.routes(), session.tokens(), ROOT, 0, width, height));
    {
      WebScenePlatformController controller(session, root.token(), width, height, 0);
      CellNode node(Cell("old").props);
      node.setPropsTypeId(CellProps::staticTypeId());
      LayoutState state;
      state.width = 27;
      state.height = 41;
      LOKA_VERIFY(controller.prepareProjectedLayout(&node, state));
      WebElementContext *context = static_cast<WebElementContext *>(node.getContext());
      const Token token = context->token();
      webtest::failMeasure = -1;
      if (protocolDeath)
        webtest::measureStatus = WEB_MEASURE_PROTOCOL;
      context->layout(&controller, state);
      context->render(&controller);
      LOKA_VERIFY(!webtest::resources[token].projected && webtest::resources[token].height == 41);
      webtest::failMeasure = 0;
      context->layout(&controller, state);
      context->render(&controller);
      LOKA_VERIFY(webtest::resources[token].text == "old");
      webtest::failMeasure = -1;
      const unsigned calls = webtest::measureCalls;
      context->layout(&controller, state);
      context->render(&controller);
      LOKA_VERIFY(webtest::measureCalls == calls && webtest::resources[token].text == "old");
      LOKA_VERIFY(Cell("new").applyPropsToNode(&node));
      context->layout(&controller, state);
      context->render(&controller);
      LOKA_VERIFY(webtest::resources[token].text == "old" && webtest::resources[token].height == 41);
      webtest::failMeasure = 0;
      context->layout(&controller, state);
      context->render(&controller);
      LOKA_VERIFY(webtest::resources[token].text == "new");
#ifdef NDEBUG
      LOKA_VERIFY(Cell("protocol").applyPropsToNode(&node));
      webtest::measureStatus = WEB_MEASURE_PROTOCOL;
      webtest::failMeasure = -1;
      context->layout(&controller, state);
      context->render(&controller);
      LOKA_VERIFY(webtest::resources[token].text == "new");
      std::puts("CellMeasurement NDEBUG PROTOCOL preserves snapshot passed");
#else
      std::puts("[skip] CellMeasurement PROTOCOL release requires NDEBUG");
#endif
      LifecycleFactTestAccess::MarkSubtreeRetired(&node);
      node.setContext(0);
    }
    WebResource *row = root.revoke(session.routes());
    LOKA_VERIFY(web_release(row->token) == CONSUMED);
    delete row;
    std::puts("CellMeasurement initial refusal, retained refusal, reuse during outage and recovery passed");
  }
  void CellArrangements()
  {
    for (int mode = 2; mode <= 7; ++mode)
    {
      Rig rig(mode);
      if (mode == 6)
        rig.session.resize(webtest::token(ROOT), 220, 0, 0);
      const Tokens cells = Cells(rig.session);
      LOKA_VERIFY(!cells.empty());
      const webtest::Resource first = webtest::resources[cells[0]];
      if (mode == 2)
      {
        LOKA_VERIFY(first.height == 20 && webtest::resources[cells[1]].y == 20);
      }
      if (mode == 3 || mode == 4)
      {
        LOKA_VERIFY(first.height > 0);
        if (mode == 3)
          LOKA_VERIFY(first.height == webtest::resources[cells[1]].height);
        webtest::failMetrics = -1;
        rig.session.routes().find(cells[0])->leaf()->controller().requestRelayout();
        rig.flush();
        LOKA_VERIFY(webtest::resources[cells[0]].height == 20);
        webtest::failMetrics = 0;
        rig.flush();
        LOKA_VERIFY(webtest::resources[cells[0]].height == first.height);
      }
      if (mode == 5 || mode == 6)
      {
        LOKA_VERIFY(first.height == (mode == 5 ? 240 : 20));
        const Token scroll = webtest::token(SCROLL);
        const webtest::Resource before = webtest::resources[scroll];
        LayoutState seat;
        seat.width = 220;
        seat.y = 32760;
        seat.height = 0;
        WebResourceContext *scrollContext = rig.session.routes().find(scroll)->receiver;
        loka::web::testing::Access::scrollVisit(*scrollContext, seat);
        LOKA_VERIFY(webtest::resources[cells[0]].y == first.y && webtest::resources[cells[0]].height == first.height);
        LOKA_VERIFY(webtest::resources[scroll].extent == before.extent);
        LOKA_VERIFY(webtest::resources[scroll].originY == before.originY);
      }
      if (mode == 7)
        LOKA_VERIFY(first.height == 240 && webtest::resources[cells[1]].height == 20
                    && webtest::resources[cells[1]].y == 240);
    }
    std::puts("CellArrangements natural/remaining Column, aligned/mixed Row degrade/recover, positive/zero ScrollView "
              "passed");
  }
  void CellScrollMeasurement()
  {
    Rig rig(5);
    const Tokens cells = Cells(rig.session);
    const Token scroll = webtest::token(SCROLL);
    const webtest::Resource before = webtest::resources[scroll];
    const loka::core::Frame viewport = webtest::cellFixture()->viewport.get();
    webtest::failMeasure = -1;
    {
      loka::core::Operation turn;
      webtest::cellFixture()->text.set(loka::core::String::Literal("scroll changed"));
    }
    rig.session.resize(webtest::token(ROOT), 220, 80, 0);
    LOKA_VERIFY(webtest::resources[cells[0]].text == "a b");
    LOKA_VERIFY(webtest::resources[scroll].extent == before.extent);
    LOKA_VERIFY(webtest::resources[scroll].originX == before.originX
                && webtest::resources[scroll].originY == before.originY);
    LOKA_VERIFY(webtest::cellFixture()->viewport.get() == viewport);
    // Positive control for the PROTOCOL pin below: REFUSED leaves a retry armed.
    LOKA_VERIFY(webtest::task(IMMEDIATE));
    webtest::failMeasure = 0;
    rig.flush();
    LOKA_VERIFY(webtest::resources[cells[0]].text == "scroll changed");
    LOKA_VERIFY(webtest::cellFixture()->viewport.get().height == 80);
    LOKA_VERIFY(webtest::resources[scroll].extent == 80);
    LOKA_VERIFY(!webtest::task(IMMEDIATE));
#ifdef NDEBUG
    webtest::measureStatus = WEB_MEASURE_PROTOCOL;
    webtest::failMeasure = -1;
    {
      loka::core::Operation turn;
      webtest::cellFixture()->text.set(loka::core::String::Literal("protocol"));
    }
    rig.flush();
    LOKA_VERIFY(webtest::resources[cells[0]].text == "scroll changed" && !webtest::task(IMMEDIATE));
    webtest::measureStatus = WEB_MEASURE_REFUSED;
    webtest::failMeasure = 0;
    std::puts("CellScrollMeasurement NDEBUG PROTOCOL keeps the snapshot and arms no retry passed");
#else
    std::puts("[skip] CellScrollMeasurement PROTOCOL no-retry requires NDEBUG");
#endif
    std::puts("CellScrollMeasurement refusal retains snapshot/extent/origin/viewport then recovery commits passed");
  }
  void CellConstructionRefusals()
  {
    for (int failure = 0; failure < 2; ++failure)
    {
      webtest::reset();
      WebSession session;
      WebElement root;
      int width = 100, height = 80;
      LOKA_VERIFY(root.create(session.routes(), session.tokens(), ROOT, 0, width, height));
      {
        WebScenePlatformController controller(session, root.token(), width, height, 0);
        CellNode cell(Cell("refused").props);
        LayoutState seat;
        if (failure == 0)
          webtest::failCreate = 1;
        else
          webtest::installationsRefused = 1;
        LOKA_VERIFY(!controller.prepareProjectedLayout(&cell, seat));
        LOKA_VERIFY(!cell.getContext());
        controller.drainNativeRetirements();
        LOKA_VERIFY(webtest::resources.size() == 1);
      }
      WebResource *row = root.revoke(session.routes());
      LOKA_VERIFY(web_release(row->token) == CONSUMED);
      delete row;
    }
    std::puts("CellConstructionRefusals create and post-commit adoption passed");
  }
  class SeedAccess : public MineSweeperProductionAppConfig
  {
  public:
    explicit SeedAccess(PlatformContext *platform)
        : MineSweeperProductionAppConfig(platform)
    {
    }
    unsigned long seed() const
    {
      return this->mainProps().seed_;
    }
  };
  void ProductionSeed()
  {
    WebSession session;
    WebPlatformContext platform(session);
    const unsigned long before = static_cast<unsigned long>(std::time(0));
    const SeedAccess config(&platform);
    const unsigned long after = static_cast<unsigned long>(std::time(0));
    LOKA_VERIFY(config.seed() >= before && config.seed() <= after);
    std::puts("ProductionSeed construction-time unsigned-long interval passed");
  }
} // namespace
void WebCellTests()
{
  CellPolicy();
  CellDispatchRecheck();
  CellPark();
  CellGeneration();
  CellMeasurement();
  CellArrangements();
  CellScrollMeasurement();
  CellConstructionRefusals();
  ProductionSeed();
}

void WebCellProtocolDeath()
{
  CellMeasurement(true);
}
