#include "GridFixture.hpp"
#include "FakeBoundary.hpp"
#include "WebTestAccess.hpp"
#include "WebGridLayoutHandler.hpp"
#include "support/TestVerify.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
using namespace loka::app;
using namespace loka::app::scene;
using namespace loka::web;
namespace
{
  // Wide-state-only: inputs outside short cannot enter the real Web handler.
  struct WideState
  {
    typedef int Coordinate;
    int x, y, width, height;
    WideState(int a = 0, int b = 0, int w = 0, int h = 0)
        : x(a),
          y(b),
          width(w),
          height(h)
    {
    }
  };
  struct Probe
  {
    std::vector<WideState> offers;
    std::vector<int> checks;
    int oversizedBottom;
    Probe()
        : oversizedBottom(0)
    {
    }
    static int Dispatch(void *context, Node *, const WideState &state)
    {
      Probe &self = *static_cast<Probe *>(context);
      self.offers.push_back(state);
      return self.oversizedBottom ? self.oversizedBottom : state.y + state.height;
    }
    static bool Accept(void *context, int coordinate)
    {
      Probe &self = *static_cast<Probe *>(context);
      self.checks.push_back(coordinate);
      return coordinate >= -32768 && coordinate <= 32767;
    }
  };
  void Children(GridNode &grid, unsigned count)
  {
    for (unsigned i = 0; i < count; ++i)
      grid.addChild(Button("probe").create());
  }
  int Compute(GridNode &grid,
              const WideState &state,
              Probe &probe,
              bool wall = true,
              const layout::GridLayoutMetrics &metrics = layout::GridLayoutMetrics())
  {
    return layout::computeGridLayoutResultY(&grid, state, metrics, &probe, &Probe::Dispatch, wall ? &Probe::Accept : 0);
  }
  void CommonEdges()
  {
    GridNode grid(GridProps(1, 1));
    Children(grid, 1);
    for (int edge = 0; edge < 4; ++edge)
      for (int value = 32767; value <= 32768; ++value)
      {
        // Origin tests use zero extent. Verify the entire ordered callback trace
        // so removing an origin check cannot hide behind the equal far edge.
        WideState state;
        if (edge == 0)
          state.x = value;
        if (edge == 1)
        {
          state.x = 32760;
          state.width = value - 32760;
        }
        if (edge == 2)
          state.y = value;
        if (edge == 3)
        {
          state.y = 32760;
          state.height = value - 32760;
        }
        Probe probe;
        Compute(grid, state, probe);
        const int expected[] = {state.x, state.x + state.width, state.y, state.y + state.height};
        const unsigned checks = value == 32767 ? 4 : static_cast<unsigned>(edge + 1);
        LOKA_VERIFY(probe.checks.size() == checks);
        for (unsigned i = 0; i < checks; ++i)
          LOKA_VERIFY(probe.checks[i] == expected[i]);
        LOKA_VERIFY(probe.offers.size() == (value == 32767 ? 1u : 0u));
      }
    // Wide-state-only lower bounds, independently for x and y.
    for (int axis = 0; axis < 2; ++axis)
      for (int value = -32769; value <= -32768; ++value)
      {
        Probe probe;
        Compute(grid, WideState(axis ? 0 : value, axis ? value : 0), probe);
        LOKA_VERIFY(probe.offers.size() == (value == -32768 ? 1u : 0u));
        LOKA_VERIFY(probe.checks.size() == (value == -32768 ? 4u : axis ? 3u : 1u));
      }
    Probe nullProbe;
    LOKA_VERIFY(Compute(grid, WideState(32768, -32769, 8, 9), nullProbe, false) == -32760);
    LOKA_VERIFY(nullProbe.checks.empty() && nullProbe.offers.size() == 1);
    LOKA_VERIFY(nullProbe.offers[0].x == 32768 && nullProbe.offers[0].y == -32769);
    std::puts("CommonEdges x/right/y/bottom and wide-state-only lower bounds passed");
  }
  void CommonStops()
  {
    GridNode grid(GridProps(3, 1));
    Children(grid, 3);
    Probe first;
    LOKA_VERIFY(Compute(grid, WideState(32768, 11, 0, 90), first) == 11);
    LOKA_VERIFY(first.offers.empty());
    Probe mid;
    mid.oversizedBottom = 32765;
    LOKA_VERIFY(Compute(grid, WideState(0, 32700, 10, 120), mid) == 32765);
    LOKA_VERIFY(mid.offers.size() == 1 && mid.checks.size() == 8);
    LOKA_VERIFY(mid.offers[0].y == 32700);
    std::puts("CommonStops first and mid refusal preserve maxY without later dispatch passed");
  }
  void CommonShapes()
  {
    const short dimensions[][2] = {{2, 2}, {0, 0}, {-2, -3}, {32767, 32767}};
    for (unsigned shape = 0; shape < 4; ++shape)
      for (unsigned count = 0; count <= 5; ++count)
      {
        GridNode grid(GridProps(dimensions[shape][0], dimensions[shape][1]));
        Children(grid, count);
        Probe probe;
        layout::GridLayoutMetrics metrics;
        if (!shape)
        {
          metrics.gapX = 2;
          metrics.gapY = 4;
        }
        const unsigned cap = shape == 0 ? 4 : shape == 3 ? count : 1;
        const unsigned visited = count < cap ? count : cap;
        const int bottom = Compute(grid, WideState(3, 7, 100, 80), probe, false, metrics);
        LOKA_VERIFY(probe.offers.size() == visited && probe.checks.empty());
        if (!visited)
          LOKA_VERIFY(bottom == 7);
        else if (shape == 0)
        {
          LOKA_VERIFY(probe.offers[0].width == 49 && probe.offers[0].height == 38);
          LOKA_VERIFY(bottom == (visited > 2 ? 87 : 45));
          if (visited > 2)
            LOKA_VERIFY(probe.offers[2].x == 3 && probe.offers[2].y == 49);
        }
        else if (shape == 3)
          LOKA_VERIFY(bottom == 7 && probe.offers.back().width == 0 && probe.offers.back().height == 0);
        else
          LOKA_VERIFY(bottom == 87 && probe.offers[0].width == 100 && probe.offers[0].height == 80);
      }
    std::puts("CommonShapes empty/ragged/nonpositive/max/capacity+1 null characterization passed");
  }
  class Traversal : public IPlatformLayoutTraversal
  {
  public:
    std::vector<LayoutState> offers;
    unsigned checks;
    Traversal()
        : checks(0)
    {
    }
    virtual int layoutChild(Node *, const LayoutState &state)
    {
      this->offers.push_back(state);
      return state.y;
    }
    virtual bool refuseLayoutResultY(int value)
    {
      ++this->checks;
      return IPlatformLayoutTraversal::refuseLayoutResultY(value);
    }
    virtual void setLayoutResultY(short) {}
    virtual short layoutResultY() const
    {
      return 0;
    }
  };
  void OfferedCells()
  {
    GridNode grid(GridProps(2, 2));
    Children(grid, 4);
    LayoutState state;
    state.x = state.y = 0;
    state.width = 100;
    state.height = 80;
    WebGridLayoutHandler handler;
    Traversal traversal;
    // This traversal reports row tops, unlike the real Buttons in OfferTable.
    LOKA_VERIFY(handler.layoutNode(&grid, state, &traversal) == 40);
    LOKA_VERIFY(traversal.offers.size() == 4 && traversal.checks == 16);
    for (unsigned i = 0; i < 4; ++i)
    {
      LOKA_VERIFY(traversal.offers[i].x == static_cast<int>(i % 2) * 50);
      LOKA_VERIFY(traversal.offers[i].y == static_cast<int>(i / 2) * 40);
      LOKA_VERIFY(traversal.offers[i].width == 50 && traversal.offers[i].height == 40);
    }
    std::puts("OfferedCells 50x40 cells distinct from 23-high painted Buttons passed");
  }
  void HandlerPolicies()
  {
    GridNode grid(GridProps(1, 1));
    Children(grid, 1);
    LayoutState state;
    state.x = state.y = 32767;
    state.width = state.height = 1;
    Traversal native;
    layout::GridLayoutMetrics metrics;
    layout::GridPlatformLayoutHandler common(metrics);
    LOKA_VERIFY(common.layoutNode(&grid, state, &native) == 32767);
    LOKA_VERIFY(native.checks == 0 && native.offers.size() == 1);
    WebGridLayoutHandler web;
    Traversal refused;
    LOKA_VERIFY(web.layoutNode(&grid, state, &refused) == 32767);
    LOKA_VERIFY(refused.checks == 2 && refused.offers.empty());
    const short negatives[] = {-1, -32768};
    const short origins[] = {-32768, 32767};
    for (unsigned axis = 0; axis < 2; ++axis)
      for (unsigned n = 0; n < 2; ++n)
        for (unsigned o = 0; o < 2; ++o)
        {
          state.x = state.y = origins[o];
          state.width = axis ? 0 : negatives[n];
          state.height = axis ? negatives[n] : 0;
          Traversal probe;
          LOKA_VERIFY(web.layoutNode(&grid, state, &probe) == state.y);
          LOKA_VERIFY(probe.checks == 4 && probe.offers.size() == 1);
          LOKA_VERIFY(probe.offers[0].width == 0 && probe.offers[0].height == 0);
          LOKA_VERIFY(probe.offers[0].x == state.x && probe.offers[0].y == state.y);
        }
    for (unsigned axis = 0; axis < 2; ++axis)
    {
      state.x = state.y = 1;
      state.width = axis ? 0 : 32767;
      state.height = axis ? 32767 : 0;
      Traversal probe;
      LOKA_VERIFY(web.layoutNode(&grid, state, &probe) == 1);
      LOKA_VERIFY(probe.offers.empty() && probe.checks == (axis ? 4u : 2u));
    }
    std::puts("HandlerPolicies common omits callback; Web short negative and maximum offers passed");
  }
  uint32_t Token(const char *label)
  {
    for (std::map<uint32_t, webtest::Resource>::const_iterator i = webtest::resources.begin();
         i != webtest::resources.end();
         ++i)
      if (i->second.text == label)
        return i->first;
    return 0;
  }
  const webtest::Resource &Label(const char *label)
  {
    const uint32_t token = Token(label);
    LOKA_VERIFY(token);
    return webtest::resources[token];
  }
  void Shutdown(WebSession &session)
  {
    session.dispose();
    LOKA_VERIFY(webtest::task(SHUTDOWN));
    webtest::fire(webtest::task(SHUTDOWN), 1000);
    LOKA_VERIFY(webtest::resources.empty());
  }
  void OfferTable()
  {
    const webtest::GridCell cells[] = {webtest::GRID_BOX,
                                       webtest::GRID_ROOT,
                                       webtest::GRID_REMAINING,
                                       webtest::GRID_NATURAL,
                                       webtest::GRID_SCROLL,
                                       webtest::GRID_PADDED};
    const int firstY[] = {0, 9, 23, 23, 0, 5};
    const int secondY[] = {40, 40, 51, 23, 40, 45};
    for (unsigned i = 0; i < 6; ++i)
    {
      webtest::reset();
      webtest::gridCell() = cells[i];
      WebSession session;
      LOKA_VERIFY(session.start(&webtest::CreateGrid, 0) == WebSession::STARTED);
      const int inset = i == 5 ? 5 : i == 1 ? 9 : 0;
      LOKA_VERIFY(Label("a").x == inset && Label("a").y == firstY[i]);
      LOKA_VERIFY(Label("b").x == inset + (i == 1 ? 41 : 50) && Label("b").y == firstY[i]);
      LOKA_VERIFY(Label("c").y == secondY[i] && Label("d").y == secondY[i]);
      LOKA_VERIFY(Label("a").width == (i == 1 ? 41 : 50) && Label("d").height == 23);
      if (i == 2 || i == 3)
        LOKA_VERIFY(Label("after").y == secondY[i] + 23);
      if (i == 4)
        LOKA_VERIFY(webtest::resources[webtest::token(SCROLL)].extent == 63);
      Shutdown(session);
    }
    std::puts("OfferTable fixed non-square/root/remaining/natural/ScrollView/padded passed");
  }
  void RefusedShapes()
  {
    const webtest::GridCell cells[] = {
        webtest::GRID_REFUSED, webtest::GRID_SPARSE, webtest::GRID_EMPTY, webtest::GRID_MIXED, webtest::GRID_CAPACITY};
    const int bottoms[] = {40, 63, 0, 63};
    const unsigned leaves[] = {1, 4, 1, 3, 4};
    for (unsigned i = 0; i < 5; ++i)
    {
      webtest::reset();
      webtest::gridCell() = cells[i];
      WebSession session;
      LOKA_VERIFY(session.start(&webtest::CreateGrid, 0) == WebSession::STARTED);
      unsigned buttons = 0;
      for (std::map<uint32_t, webtest::Resource>::const_iterator it = webtest::resources.begin();
           it != webtest::resources.end();
           ++it)
        if (it->second.kind == BUTTON)
          ++buttons;
      LOKA_VERIFY(buttons == leaves[i]);
      if (i < 4)
        LOKA_VERIFY(Label("after").y == bottoms[i]);
      else
        LOKA_VERIFY(!Token("excess"));
      Shutdown(session);
    }
    std::puts("RefusedShapes all-refused/sparse/empty/mixed/capacity+1 passed");
  }
  void CapacityHistory()
  {
    webtest::reset();
    webtest::gridCell() = webtest::GRID_BOX;
    WebSession session;
    LOKA_VERIFY(session.start(&webtest::CreateGrid, 0) == WebSession::STARTED);
    GridNode *grid = webtest::FindGrid(webtest::gridFixture());
    Node *first = grid->childrenHead();
    const uint32_t last = Token("d");
    const webtest::Resource old = Label("d");
    {
      loka::core::Operation turn;
      LOKA_VERIFY(Grid().rows(1).cols(1).applyPropsToNode(grid));
      webtest::gridFixture()->scene()->requestLayoutAfterRun();
    }
    session.wake(IMMEDIATE, 1);
    LOKA_VERIFY(grid->childrenHead() == first && Token("d") == last);
    LOKA_VERIFY(Label("a").width == 100 && Label("d").x == old.x && Label("d").y == old.y);
    LOKA_VERIFY(!Label("d").hidden && Label("d").projected && Label("d").enabled);
    const unsigned clicks = webtest::gridFixture()->clicks;
    LOKA_VERIFY(session.click(last, 2) == WebSession::ACCEPTED);
    LOKA_VERIFY(webtest::gridFixture()->clicks == clicks + 1);
    {
      loka::core::Operation turn;
      LOKA_VERIFY(Grid().rows(4).cols(1).applyPropsToNode(grid));
      webtest::gridFixture()->scene()->requestLayoutAfterRun();
    }
    session.wake(IMMEDIATE, 3);
    LOKA_VERIFY(grid->childrenHead() == first && Token("d") == last);
    LOKA_VERIFY(Label("d").x == 0 && Label("d").y == 60 && Label("d").width == 100);
    Shutdown(session);
    std::puts("CapacityHistory retained identity, stale rect/visibility/input, restore passed");
  }
  Node *OuterBox(Node *node)
  {
    if (node->asBoxNode())
      return node;
    if (INestable *nestable = node->asNestable())
      for (Node *child = nestable->childrenHead(); child; child = child->nextInComposition)
        if (Node *found = OuterBox(child))
          return found;
    return 0;
  }
  unsigned Calls(const char *kind, uint32_t token)
  {
    std::ostringstream prefix;
    prefix << kind << ':' << token;
    unsigned count = 0;
    for (unsigned i = 0; i < webtest::calls.size(); ++i)
      if (webtest::calls[i].find(prefix.str()) == 0)
        ++count;
    return count;
  }
  void WallHistory()
  {
    for (int scroll = 0; scroll < 2; ++scroll)
    {
      webtest::reset();
      webtest::gridCell() = scroll ? webtest::GRID_WALL_SCROLL : webtest::GRID_WALL_ROOT;
      WebSession session;
      LOKA_VERIFY(session.start(&webtest::CreateGrid, 0) == WebSession::STARTED);
      LOKA_VERIFY(Label("first").x == 0 && Label("last").x == 16383);
      const uint32_t last = Token("last"), after = Token("after"), viewportToken = webtest::token(SCROLL);
      if (scroll)
      {
        LOKA_VERIFY(session.resize(webtest::token(ROOT), 100, 20, 0) == WebSession::ACCEPTED);
        LOKA_VERIFY(!webtest::resources[viewportToken].hidden);
        loka::core::Operation turn;
        webtest::gridFixture()->request.set(ScrollTarget::Origin(0, 10));
      }
      session.wake(IMMEDIATE, 1);
      const loka::core::Frame fact = webtest::gridFixture()->viewport.get();
      if (scroll)
        LOKA_VERIFY(fact.y == 10);
      const webtest::Resource viewport = scroll ? webtest::resources[viewportToken] : webtest::Resource();
      const unsigned oldLast = Calls("rect", last), oldAfter = Calls("rect", after),
                     commits = Calls("commit-scroll", viewportToken);
      Node *box = OuterBox(webtest::gridFixture());
      GridNode *grid = webtest::FindGrid(webtest::gridFixture());
      Node *firstChild = grid->childrenHead();
      {
        loka::core::Operation turn;
        LOKA_VERIFY(Box().width(100).padding(5).applyPropsToNode(box));
        webtest::gridFixture()->scene()->requestLayoutAfterRun();
      }
      session.wake(IMMEDIATE, 2);
      LOKA_VERIFY(Label("first").x == 5 && Label("first").y == 5);
      LOKA_VERIFY(Token("last") == last && Label("last").x == 16383 && Label("last").y == 0);
      LOKA_VERIFY(grid->childrenHead() == firstChild && Calls("rect", last) == oldLast);
      LOKA_VERIFY(Calls("rect", after) == oldAfter + (scroll ? 0u : 1u));
      if (scroll)
      {
        LOKA_VERIFY(webtest::gridFixture()->viewport.get() == fact);
        LOKA_VERIFY(webtest::resources[viewportToken].extent == viewport.extent);
        LOKA_VERIFY(webtest::resources[viewportToken].originY == viewport.originY);
        LOKA_VERIFY(Calls("commit-scroll", viewportToken) == commits);
      }
      else
        LOKA_VERIFY(Label("after").y == 33);
      {
        loka::core::Operation turn;
        LOKA_VERIFY(Box().width(100).applyPropsToNode(box));
        webtest::gridFixture()->scene()->requestLayoutAfterRun();
      }
      session.wake(IMMEDIATE, 3);
      LOKA_VERIFY(Token("last") == last && grid->childrenHead() == firstChild);
      LOKA_VERIFY(Label("first").x == 0 && Label("last").x == 16383 && Label("after").y == 23);
      LOKA_VERIFY(Calls("rect", last) > oldLast);
      Shutdown(session);
    }
    std::puts("WallHistory Grid far-edge root/started shown ScrollView stop, retained writes, recovery passed");
  }
  void InitialWall()
  {
    for (int scroll = 0; scroll < 2; ++scroll)
    {
      webtest::reset();
      webtest::initialGridWall() = true;
      webtest::gridCell() = scroll ? webtest::GRID_WALL_SCROLL : webtest::GRID_WALL_ROOT;
      WebSession session;
      LOKA_VERIFY(session.start(&webtest::CreateGrid, 0) == WebSession::STARTED);
      LOKA_VERIFY(Label("first").x == 5 && Label("first").y == 5);
      LOKA_VERIFY(!Token("last") && (Token("after") != 0) == !scroll);
      if (scroll)
      {
        LOKA_VERIFY(!webtest::resources[webtest::token(SCROLL)].hidden);
        LOKA_VERIFY(webtest::gridFixture()->viewport.get() == loka::core::Frame());
        LOKA_VERIFY(Calls("commit-scroll", webtest::token(SCROLL)) == 0);
      }
      Shutdown(session);
      webtest::initialGridWall() = false;
    }
    std::puts("InitialWall offered far edge refuses before fitting leaf at root/shown ScrollView passed");
  }

} // namespace
void WebGridLayoutTests()
{
#define CELL(name)                                                                                                     \
  do                                                                                                                   \
  {                                                                                                                    \
    if (!std::getenv("LOKA_WEB_GRID_CELL") || std::strcmp(std::getenv("LOKA_WEB_GRID_CELL"), #name) == 0)              \
    {                                                                                                                  \
      std::puts(#name);                                                                                                \
      std::fflush(stdout);                                                                                             \
      name();                                                                                                          \
    }                                                                                                                  \
  } while (0)
  CELL(CommonEdges);
  CELL(CommonStops);
  CELL(CommonShapes);
  CELL(HandlerPolicies);
  CELL(OfferedCells);
  CELL(OfferTable);
  CELL(RefusedShapes);
  CELL(CapacityHistory);
  CELL(WallHistory);
  CELL(InitialWall);
#undef CELL
}
