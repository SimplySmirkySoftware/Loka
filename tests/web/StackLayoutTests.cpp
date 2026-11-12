#include "LayoutFixture.hpp"
#include "FakeBoundary.hpp"
#include "WebTestAccess.hpp"
#include "WebScrollContext.hpp"
#include "app/layout/PlatformBuiltinLayoutHandlers.hpp"
#include "support/TestVerify.hpp"
#include "testing/scene/SceneTestFlow.hpp"
#include <cstdio>
#include <climits>
#include <cstdlib>
#include <cstring>
using namespace loka::app;
using namespace loka::app::scene;
using namespace loka::web;
namespace
{
  const webtest::Resource &Label(const char *text)
  {
    for (std::map<uint32_t, webtest::Resource>::const_iterator i = webtest::resources.begin();
         i != webtest::resources.end();
         ++i)
      if (i->second.text == text)
        return i->second;
    for (std::map<uint32_t, webtest::Resource>::const_iterator i = webtest::resources.begin();
         i != webtest::resources.end();
         ++i)
      std::fprintf(stderr,
                   "resource text=[%s] xywh=%d,%d,%d,%d\n",
                   i->second.text.c_str(),
                   i->second.x,
                   i->second.y,
                   i->second.width,
                   i->second.height);
    LOKA_VERIFY(false && "label must exist");
    return webtest::resources.begin()->second;
  }
  class CountingController : public WebScenePlatformController
  {
  public:
    std::vector<LayoutState> refusedSeats;
    CountingController(WebSession &session, int width, int height)
        : WebScenePlatformController(session, 0, width, height)
    {
    }
    virtual bool prepareProjectedLayout(Node *node, LayoutState &state)
    {
      if (node && !node->asTextNode() && !node->asButtonNode() && !node->asCellNode())
        this->refusedSeats.push_back(state);
      return WebScenePlatformController::prepareProjectedLayout(node, state);
    }
  };
  class Rig
  {
  public:
    WebSession session;
    WebElement root;
    CountingController controller;
    Scene scene;
    Rig(webtest::LayoutCell cell, int width = 220, int height = 120)
        : controller(this->session, width, height),
          scene(Prepare(cell))
    {
      LOKA_VERIFY(this->scene.mount(&this->controller));
      loka::dsl::testing::SceneTestAccess::updateAttached(this->scene, true);
    }
    ~Rig()
    {
      loka::dsl::testing::SceneTestAccess::unmount(this->scene);
      this->controller.destroy();
    }
    void flush()
    {
      this->scene.flushInvalidation();
    }
    static BoundaryDefinition<BoundaryPropsFor<webtest::LayoutFixture>, webtest::LayoutFixture>
    Prepare(webtest::LayoutCell cell)
    {
      webtest::layoutCell() = cell;
      return Boundary<webtest::LayoutFixture>();
    }
  };
  void Rows()
  {
    webtest::reset();
    {
      Rig rig(webtest::ROW_SHARED);
      rig.flush();
      const webtest::Resource &a = Label("a"), &b = Label("long label");
      LOKA_VERIFY(a.x == 0 && a.y == 0 && a.width == 94);
      LOKA_VERIFY(b.x == 100 && b.y == 0 && b.width == 94);
      std::puts("RowShared passed");
    }
    webtest::reset();
    {
      Rig rig(webtest::ROW_NATURAL);
      rig.flush();
      const webtest::Resource &a = Label("a"), &b = Label("long label");
      LOKA_VERIFY(a.width == 7 && b.width == 70 && b.x == 13 && b.y == 0);
      std::puts("RowNatural passed");
    }
    webtest::reset();
    {
      Rig rig(webtest::TOOLBAR_LABELS);
      rig.flush();
      LOKA_VERIFY(Label("Go").width == 32 && Label("label").y == 5);
      LOKA_VERIFY(Label("Go").x == Label("label").width + 8);
      std::puts("ToolbarLabels passed");
    }
  }
  void AlignedTextButton()
  {
    for (int height = 13; height <= 23; height += 10)
    {
      webtest::reset();
      webtest::metricAscent = height - 3;
      Rig rig(webtest::ALIGNED_TEXT_BUTTON);
      rig.flush();
      LOKA_VERIFY(Label("center").y == 5 && Label("center").height == height);
      LOKA_VERIFY(Label("bottom").y == height + 24);
      LOKA_VERIFY(Label("after").y == 2 * (height + 14));
      const WebTextDescriptor expected = WebMetricDescriptor(WebFontKey(TextStyle()));
      LOKA_VERIFY(webtest::metricDescriptors.front()
                  == std::vector<int>(expected.data(), expected.data() + expected.count()));
    }
    std::puts("AlignedTextButton passed");
  }
  void AlignedRowEstimateDegradeAndRecovery()
  {
    webtest::reset();
    Rig rig(webtest::ALIGNED_TEXT_BUTTON);
    rig.flush();
    const unsigned builds = webtest::textBuilds;
    // Refuse only the first estimate; natural Text answers and snapshots survive.
    webtest::failMetrics = 1;
    rig.controller.requestRelayout();
    rig.flush();
    LOKA_VERIFY(Label("center").y == 0 && Label("Go").y == 6);
    LOKA_VERIFY(Label("bottom").y == 43 && Label("after").y == 60);
    LOKA_VERIFY(webtest::textBuilds == builds && rig.scene.hasPendingInvalidation());
    rig.flush();
    LOKA_VERIFY(Label("center").y == 5 && Label("after").y == 54);
    LOKA_VERIFY(!rig.scene.hasPendingInvalidation());
    webtest::failMetrics = -1;
    rig.controller.requestRelayout();
    for (unsigned i = 0; i < 3; ++i)
    {
      const unsigned before = webtest::metricCalls;
      rig.flush();
      LOKA_VERIFY(webtest::metricCalls == before + 6);
      LOKA_VERIFY(Label("center").y == 0 && Label("bottom").y == 27);
      LOKA_VERIFY(Label("after").y == 54 && rig.scene.hasPendingInvalidation());
      LOKA_VERIFY(webtest::textBuilds == builds);
    }
    webtest::failMetrics = 0;
    rig.flush();
    LOKA_VERIFY(Label("center").y == 5 && !rig.scene.hasPendingInvalidation());
#ifdef NDEBUG
    webtest::measureStatus = WEB_MEASURE_PROTOCOL;
    webtest::failMetrics = 1;
    rig.controller.requestRelayout();
    rig.flush();
    LOKA_VERIFY(Label("center").y == 0 && !rig.scene.hasPendingInvalidation());
#else
    std::puts("[skip] estimate PROTOCOL release: requires NDEBUG; debug death tested separately");
#endif
    std::puts("AlignedRowEstimateDegradeAndRecovery passed");
  }
  void RowEstimateLimits()
  {
    webtest::reset();
    short line = 111, button = 222;
    webtest::metricAscent = 32754;
    LOKA_VERIFY(WebTextLayout::rowEstimate(line, button) == WEB_MEASURE_OK && line == 32757 && button == 32767);
    webtest::metricAscent = 32755;
    line = 111;
    button = 222;
    LOKA_VERIFY(WebTextLayout::rowEstimate(line, button) == WEB_MEASURE_REFUSED && line == 111 && button == 222);
    webtest::metricAscent = 32765;
    LOKA_VERIFY(WebTextLayout::rowEstimate(line, button) == WEB_MEASURE_REFUSED && line == 111 && button == 222);
    webtest::metricAscent = -3;
    LOKA_VERIFY(WebTextLayout::rowEstimate(line, button) == WEB_MEASURE_REFUSED && line == 111 && button == 222);
    std::puts("RowEstimateLimits passed");
  }
  void Shutdown(WebSession &session)
  {
    session.dispose();
    LOKA_VERIFY(webtest::task(SHUTDOWN));
    webtest::fire(webtest::task(SHUTDOWN), 1000);
    LOKA_VERIFY(webtest::resources.empty());
  }
  void SkinConsumers()
  {
    const int gaps[] = {12, 6, 6, 8};
    const int horizontal[] = {4, 6, 6, 8};
    const int margins[] = {9, 10, 16, 12};
    for (int skin = 0; skin != 4; ++skin)
    {
      for (int cell = webtest::SKIN_STACK; cell <= webtest::SKIN_OVERFLOW; ++cell)
      {
        webtest::reset();
        webtest::skinSelection = skin;
        webtest::layoutCell() = static_cast<webtest::LayoutCell>(cell);
        WebSession session;
        LOKA_VERIFY(session.start(&webtest::CreateLayout, 0) == WebSession::STARTED);
        LOKA_VERIFY(webtest::skinPulls == 1);
        webtest::skinSelection = (skin + 1) % 4;
        const uint32_t root = webtest::token(ROOT);
        LOKA_VERIFY(session.resize(root, 215, 120, 1) == WebSession::ACCEPTED);
        LOKA_VERIFY(webtest::skinPulls == 1);
        if (cell == webtest::SKIN_STACK)
        {
          LOKA_VERIFY(Label("b").x - Label("a").width == horizontal[skin]);
          LOKA_VERIFY(Label("c").y - Label("a").height == gaps[skin]);
          LOKA_VERIFY(Label("d").x - Label("c").width == 3);
        }
        else if (cell == webtest::SKIN_MARGIN)
        {
          LOKA_VERIFY(Label("margin").x == margins[skin] && Label("margin").y == margins[skin]);
          LOKA_VERIFY(Label("margin").width == 215 - 2 * margins[skin]);
        }
        else if (cell == webtest::SKIN_INHERITED)
          LOKA_VERIFY(Label("second").y - Label("first").height == gaps[skin]);
        else
        {
          const uint32_t scroll = webtest::token(SCROLL);
          LOKA_VERIFY(webtest::resources[scroll].documentWidth == (skin == 0 ? 215 : 210 + horizontal[skin]));
          {
            loka::core::Operation turn;
            webtest::layoutFixture()->request.set(ScrollTarget::Origin(100, 0));
          }
          session.wake(IMMEDIATE, 2);
          LOKA_VERIFY(webtest::resources[scroll].originX == (skin == 0 ? 0 : horizontal[skin] - 5));
        }
        Shutdown(session);
      }
    }
    std::puts("SkinConsumers stack/override/box/inherited/overflow/clamp passed");
  }
  void SessionDegrade()
  {
    webtest::reset();
    webtest::layoutCell() = webtest::DEGRADE_SCROLL;
    WebSession session;
    LOKA_VERIFY(session.start(&webtest::CreateLayout, 0) == WebSession::STARTED);
    const uint32_t root = webtest::token(ROOT), scroll = webtest::token(SCROLL);
    LOKA_VERIFY(scroll && !webtest::resources[scroll].hidden);
    LOKA_VERIFY(session.resize(root, 220, 20, 0) == WebSession::ACCEPTED);
    webtest::LayoutFixture &body = *webtest::layoutFixture();
    {
      loka::core::Operation turn;
      body.request.set(ScrollTarget::Origin(0, 19));
    }
    session.wake(IMMEDIATE, 1);
    LOKA_VERIFY(webtest::resources[scroll].extent == 39 && body.viewport.get().y == 19);
    const unsigned builds = webtest::textBuilds;
    webtest::failMetrics = -1;
    session.routes().find(root)->root->requestSceneRelayout();
    for (int i = 0; i < 3; ++i)
    {
      session.wake(IMMEDIATE, 2 + i);
      LOKA_VERIFY(webtest::resources[scroll].extent == 13 && body.viewport.get().y == 0);
      LOKA_VERIFY(Label("after").y == 0 && webtest::textBuilds == builds);
      LOKA_VERIFY(webtest::task(IMMEDIATE));
    }
    webtest::failMetrics = 0;
    webtest::fire(webtest::task(IMMEDIATE), 10);
    LOKA_VERIFY(webtest::resources[scroll].extent == 39 && body.viewport.get().y == 0);
    LOKA_VERIFY(!webtest::task(IMMEDIATE));
    Shutdown(session);
    std::puts("AlignedRowEstimateDegradeAndRecovery ScrollView extent/origin passed");
    webtest::reset();
    webtest::layoutCell() = webtest::ALIGNED_TEXT_BUTTON;
    {
      WebSession rootSession;
      LOKA_VERIFY(rootSession.start(&webtest::CreateLayout, 0) == WebSession::STARTED);
      const unsigned built = webtest::textBuilds;
      webtest::failMetrics = -1;
      rootSession.routes().find(webtest::token(ROOT))->root->requestSceneRelayout();
      rootSession.wake(IMMEDIATE, 1);
      LOKA_VERIFY(Label("center").y == 0 && Label("bottom").y == 27);
      LOKA_VERIFY(Label("after").y == 54 && webtest::textBuilds == built);
      LOKA_VERIFY(webtest::task(IMMEDIATE));
      webtest::failMetrics = 0;
      webtest::fire(webtest::task(IMMEDIATE), 2);
      LOKA_VERIFY(Label("center").y == 5 && !webtest::task(IMMEDIATE));
      Shutdown(rootSession);
    }
    std::puts("AlignedRowEstimateDegradeAndRecovery started root passed");
    // Finite callback batches demonstrate persistent retries, not a turn budget.
    for (int path = 0; path < 2; ++path)
    {
      webtest::reset();
      webtest::layoutCell() = path ? webtest::TEXT_ONLY : webtest::REFUSED_ROW;
      webtest::failMetrics = path ? -1 : 0;
      WebSession persistent;
      LOKA_VERIFY(persistent.start(&webtest::CreateLayout, 0) == WebSession::STARTED);
      if (!path)
      {
        // Warm the after Text: only Stack estimates can schedule these retries.
        webtest::failMetrics = -1;
        persistent.routes().find(webtest::token(ROOT))->root->requestSceneRelayout();
        persistent.wake(IMMEDIATE, 0);
      }
      const unsigned buildsBeforeBatches = webtest::textBuilds;
      for (int batch = 0; batch < 3; ++batch)
      {
        const unsigned before = webtest::metricCalls;
        LOKA_VERIFY(webtest::task(IMMEDIATE));
        webtest::fire(webtest::task(IMMEDIATE), batch + 1);
        LOKA_VERIFY(webtest::metricCalls > before && webtest::task(IMMEDIATE));
        if (!path)
          LOKA_VERIFY(webtest::textBuilds == buildsBeforeBatches);
      }
      webtest::failMetrics = 0;
      webtest::fire(webtest::task(IMMEDIATE), 10);
      LOKA_VERIFY(!webtest::task(IMMEDIATE));
      Shutdown(persistent);
    }
    std::puts("PersistentStackAndTextRetry passed");
  }
  void InitialLeafRefusal()
  {
    for (int scroll = 0; scroll < 2; ++scroll)
    {
      webtest::reset();
      webtest::layoutCell() = scroll ? webtest::LEAF_FAILURE_SCROLL : webtest::LEAF_FAILURE_ROOT;
      webtest::failAdvance = -1;
      WebSession session;
      LOKA_VERIFY(session.start(&webtest::CreateLayout, 0) == WebSession::STARTED);
      unsigned leafVisits = 0;
      for (std::map<uint32_t, webtest::Resource>::const_iterator i = webtest::resources.begin();
           i != webtest::resources.end();
           ++i)
        if (i->second.kind == TEXT || i->second.kind == BUTTON)
        {
          ++leafVisits;
          LOKA_VERIFY(i->second.text.empty() && i->second.height == 0);
        }
      LOKA_VERIFY(leafVisits == (scroll ? 1u : 2u));
      if (scroll)
        LOKA_VERIFY(webtest::layoutFixture()->viewport.get() == loka::core::Frame());
      LOKA_VERIFY(webtest::task(IMMEDIATE));
      webtest::failAdvance = 0;
      webtest::fire(webtest::task(IMMEDIATE), 1);
      LOKA_VERIFY(Label("after").height == 13);
      Shutdown(session);
    }
    for (int height = 32757; height <= 32758; ++height)
    {
      webtest::reset();
      webtest::layoutCell() = webtest::TEXT_ONLY;
      webtest::metricAscent = height - 3;
      WebSession session;
      LOKA_VERIFY(session.start(&webtest::CreateLayout, 0) == WebSession::STARTED);
      Shutdown(session);
    }
    std::puts("InitialLeafRefusal root/ScrollView and boundary startup passed");
  }
  void UnalignedNeverAsks()
  {
    const webtest::LayoutCell cells[] = {webtest::ROW_SHARED, webtest::ROW_NATURAL, webtest::UNALIGNED_NESTED};
    for (unsigned i = 0; i < 3; ++i)
    {
      webtest::reset();
      Rig rig(cells[i]);
      rig.flush();
      // Width consultations use advance, never metrics. Only two leaf builds.
      LOKA_VERIFY(webtest::metricCalls == 2);
      const unsigned before = webtest::metricCalls;
      rig.controller.requestRelayout();
      rig.flush();
      LOKA_VERIFY(webtest::metricCalls == before);
    }
    webtest::reset();
    webtest::layoutVariant() = 3;
    webtest::layoutCell() = webtest::ROW_SCROLL;
    {
      WebSession session;
      LOKA_VERIFY(session.start(&webtest::CreateLayout, 0) == WebSession::STARTED);
      LOKA_VERIFY(webtest::metricCalls == 2);
      const unsigned calls = webtest::metricCalls;
      session.routes().find(webtest::token(ROOT))->root->requestSceneRelayout();
      session.wake(IMMEDIATE, 1);
      LOKA_VERIFY(webtest::metricCalls == calls);
      Shutdown(session);
    }
    webtest::layoutVariant() = 0;
    std::puts("UnalignedNeverAsks passed");
  }
  void WrappedRowResize()
  {
    webtest::reset();
    Rig rig(webtest::WRAPPED_ROW, 220);
    rig.flush();
    const webtest::Resource initial = Label("abcdefghijabcde\nfghij");
    LOKA_VERIFY(initial.y == 5 && initial.height == 26 && Label("after").y == 35);
    rig.controller.resize(120, 120);
    rig.flush();
    const webtest::Resource narrow = Label("abcdefgh\nijabcdef\nghij");
    LOKA_VERIFY(narrow.y == 5 && narrow.height == 39 && Label("after").y == 48);
    std::puts("WrappedRowResize passed");
  }
  void ColumnRemainingHeight()
  {
    for (int height = 33; height <= 35; ++height)
    {
      webtest::reset();
      webtest::layoutCell() = webtest::COLUMN_REMAINING;
      WebSession session;
      LOKA_VERIFY(session.start(&webtest::CreateLayout, 0) == WebSession::STARTED);
      LOKA_VERIFY(session.resize(webtest::token(ROOT), 220, height, 1) == WebSession::ACCEPTED);
      LOKA_VERIFY(webtest::layoutFixture()->viewport.get().height == (height > 34 ? height - 34 : 0));
      LOKA_VERIFY(webtest::resources[webtest::token(SCROLL)].y == 34);
      Shutdown(session);
    }
    for (int variant = 1; variant <= 2; ++variant)
    {
      webtest::reset();
      webtest::layoutVariant() = variant;
      webtest::layoutCell() = webtest::COLUMN_REMAINING;
      WebSession session;
      LOKA_VERIFY(session.start(&webtest::CreateLayout, 0) == WebSession::STARTED);
      LOKA_VERIFY(webtest::layoutFixture()->viewport.get().height == (variant == 1 ? 0 : 17));
      Shutdown(session);
    }
    webtest::layoutVariant() = 0;
    std::puts("ColumnRemainingHeight passed");
  }
  void RowScrollReveal()
  {
    webtest::reset();
    webtest::layoutCell() = webtest::ROW_SCROLL;
    WebSession session;
    LOKA_VERIFY(session.start(&webtest::CreateLayout, 0) == WebSession::STARTED);
    webtest::LayoutFixture &body = *webtest::layoutFixture();
    const uint32_t scroll = webtest::token(SCROLL);
    LOKA_VERIFY(webtest::resources[scroll].documentWidth > body.viewport.get().width);
    {
      loka::core::Operation turn;
      body.shown.set(false);
    }
    session.wake(IMMEDIATE, 1);
    {
      loka::core::Operation turn;
      body.request.set(ScrollTarget::Origin(20, 0));
    }
    session.wake(IMMEDIATE, 2);
    LOKA_VERIFY(!body.request.get().isNone());
    {
      loka::core::Operation turn;
      body.shown.set(true);
    }
    session.wake(IMMEDIATE, 3);
    LOKA_VERIFY(body.request.get().isNone() && body.viewport.get().x == 20);
    LOKA_VERIFY(session.resize(webtest::token(ROOT), 400, 120, 4) == WebSession::ACCEPTED);
    LOKA_VERIFY(body.viewport.get().x == 0);
    Shutdown(session);
    for (int variant = 1; variant <= 2; ++variant)
    {
      webtest::reset();
      webtest::layoutVariant() = variant;
      webtest::layoutCell() = webtest::ROW_SCROLL;
      WebSession other;
      LOKA_VERIFY(other.start(&webtest::CreateLayout, 0) == WebSession::STARTED);
      const webtest::Resource &row = webtest::resources[webtest::token(SCROLL)];
      LOKA_VERIFY(row.documentWidth == webtest::layoutFixture()->viewport.get().width);
      Shutdown(other);
    }
    webtest::layoutVariant() = 0;
    std::puts("RowScrollReveal passed");
  }
  void RowMidFailure()
  {
    for (int inScroll = 0; inScroll < 2; ++inScroll)
    {
      webtest::reset();
      webtest::layoutCell() = inScroll ? webtest::MID_FAILURE_SCROLL : webtest::MID_FAILURE;
      WebSession session;
      LOKA_VERIFY(session.start(&webtest::CreateLayout, 0) == WebSession::STARTED);
      bool after = false, unreached = false;
      for (std::map<uint32_t, webtest::Resource>::const_iterator i = webtest::resources.begin();
           i != webtest::resources.end();
           ++i)
      {
        after = after || i->second.text == "after";
        unreached = unreached || i->second.text == "unreached";
      }
      LOKA_VERIFY(Label("first").x == 0 && Label("edge").x == 32760);
      LOKA_VERIFY(!unreached && after == !inScroll);
      if (inScroll)
        LOKA_VERIFY(webtest::layoutFixture()->viewport.get() == loka::core::Frame());
      {
        loka::core::Operation turn;
        webtest::layoutFixture()->wallWidth.set(20);
      }
      session.wake(IMMEDIATE, 1);
      LOKA_VERIFY(Label("unreached").x == 27 && Label("after").y == 13);
      if (inScroll)
        LOKA_VERIFY(webtest::layoutFixture()->viewport.get().width > 0);
      Shutdown(session);
      std::puts(inScroll ? "RowMidFailure ScrollView passed" : "RowMidFailure root passed");
    }
  }
  Node *FirstBox(Node *node)
  {
    if (node->asBoxNode())
      return node;
    INestable *nestable = node->asNestable();
    if (nestable)
      for (Node *child = nestable->childrenHead(); child; child = child->nextInComposition)
      {
        Node *found = FirstBox(child);
        if (found)
          return found;
      }
    return 0;
  }
  void RowMidFailureY()
  {
    for (int scroll = 0; scroll < 2; ++scroll)
    {
      webtest::reset();
      webtest::layoutCell() = scroll ? webtest::MID_FAILURE_Y_SCROLL : webtest::MID_FAILURE_Y;
      WebSession session;
      LOKA_VERIFY(session.start(&webtest::CreateLayout, 0) == WebSession::STARTED);
      LOKA_VERIFY(Label("last").y == 32744 && Label("first").y == 32749);
      const loka::core::Frame fact = webtest::layoutFixture()->viewport.get();
      const uint32_t token = webtest::token(SCROLL);
      const int extent = scroll ? webtest::resources[token].extent : 0;
      {
        loka::core::Operation turn;
        LOKA_VERIFY(Box().height(32745).applyPropsToNode(FirstBox(webtest::layoutFixture())));
        session.routes().find(webtest::token(ROOT))->root->requestSceneRelayout();
        if (scroll)
          webtest::layoutFixture()->request.set(ScrollTarget::Top());
      }
      session.wake(IMMEDIATE, 1);
      LOKA_VERIFY(Label("first").y == 32750 && Label("last").y == 32744);
      if (scroll)
      {
        LOKA_VERIFY(webtest::layoutFixture()->viewport.get() == fact);
        LOKA_VERIFY(webtest::resources[token].extent == extent);
        LOKA_VERIFY(webtest::layoutFixture()->request.get().isNone());
      }
      {
        loka::core::Operation turn;
        LOKA_VERIFY(Box().height(32744).applyPropsToNode(FirstBox(webtest::layoutFixture())));
        session.routes().find(webtest::token(ROOT))->root->requestSceneRelayout();
      }
      session.wake(IMMEDIATE, 2);
      LOKA_VERIFY(Label("first").y == 32749 && Label("last").y == 32744);
      Shutdown(session);
    }
    std::puts("RowMidFailure y 32767/32768 root/ScrollView passed");
  }
  class SpanTraversal : public IPlatformLayoutTraversal
  {
  public:
    unsigned calls;
    int firstY;
    SpanTraversal()
        : calls(0),
          firstY(-1)
    {
    }
    virtual int layoutChild(Node *, const LayoutState &state)
    {
      if (!this->calls++)
        this->firstY = state.y;
      return state.y + 10;
    }
    virtual void setLayoutResultY(short) {}
    virtual short layoutResultY() const
    {
      return 0;
    }
  };
  void CommonStackSpans()
  {
    layout::RowLayoutMetrics metrics;
    layout::StackPlatformLayoutHandler handler(metrics, layout::RailSpacing(12, 4, 9));
    StackProps props(STACK_AXIS_COLUMN);
    props.gap(3);
    StackNode column(props);
    for (int i = 0; i < 4; ++i)
      column.addChild(Text("span").create());
    LayoutState state;
    state.width = 100;
    state.y = 7;
    layout::StackSpans spans;
    SpanTraversal traversal;
    LOKA_VERIFY(handler.layoutNode(&column, state, &traversal, 0, &spans) == 56);
    LOKA_VERIFY(spans.valid() && spans.size() == 4 && traversal.calls == 4);
    const layout::LazyWindow range = {1, 2};
    traversal.calls = 0;
    LOKA_VERIFY(handler.layoutNode(&column, state, &traversal, &range, &spans) == 56);
    LOKA_VERIFY(traversal.calls == 2 && traversal.firstY == 20);
    LOKA_VERIFY(handler.layoutNode(0, state, &traversal) == 7);
    LOKA_VERIFY(handler.layoutNode(&column, state, 0) == 7);
    std::puts("CommonStackSpans passed");
  }
  void RefusedOnlyRow()
  {
    const int heights[] = {13, 0, 13, 40, 40, 13, 13};
    for (int variant = 0; variant < 7; ++variant)
    {
      webtest::reset();
      webtest::layoutVariant() = variant;
      Rig rig(webtest::REFUSED_ROW);
      rig.flush();
      LOKA_VERIFY(Label("after").y == heights[variant]);
      LOKA_VERIFY(webtest::resources.size() == 1);
      const unsigned visits = static_cast<unsigned>(rig.controller.refusedSeats.size());
      rig.controller.requestRelayout();
      rig.flush();
      // RectSurface has no IProjectedLayoutNode door; its generic leaf visit
      // returns y without entering the registered refusal handler.
      LOKA_VERIFY(rig.controller.refusedSeats.size() - visits == (variant == 4 ? 0u : variant == 2 ? 2u : 1u));
      if (variant != 4)
      {
        const LayoutState &seat = rig.controller.refusedSeats.back();
        LOKA_VERIFY(seat.width == (variant == 2 ? 107 : variant == 3 ? 20 : 220));
        LOKA_VERIFY(seat.x == (variant == 2 ? 113 : 0));
      }
    }
    webtest::layoutVariant() = 0;
    std::puts("RefusedOnlyRow passed");
  }
  void ApproximateAlignment()
  {
    webtest::reset();
    {
      Rig rig(webtest::DEGRADE_WRAPPED);
      rig.flush();
      const unsigned builds = webtest::textBuilds;
      webtest::failMetrics = -1;
      rig.controller.requestRelayout();
      rig.flush();
      LOKA_VERIFY(Label("top").y == 0 && Label("top button").y == 0);
      LOKA_VERIFY(Label("center").y == 23 && Label("center button").y == 23);
      LOKA_VERIFY(Label("bottom").y == 46 && Label("bottom button").y == 46);
      LOKA_VERIFY(Label("after").y == 69 && webtest::textBuilds == builds);
      webtest::failMetrics = 0;
      rig.flush();
      LOKA_VERIFY(Label("center").y == 28 && Label("bottom").y == 56);
    }
    webtest::reset();
    {
      Rig rig(webtest::DEGRADE_EXPLICIT);
      rig.flush();
      webtest::failMetrics = -1;
      rig.controller.requestRelayout();
      rig.flush();
      LOKA_VERIFY(Label("Go").y == 20 && Label("after").y == 43);
      webtest::failMetrics = 0;
      rig.flush();
      LOKA_VERIFY(Label("Go").y == 8 && Label("after").y == 40);
    }
    webtest::reset();
    {
      Rig rig(webtest::TOOLBAR_LABELS);
      rig.flush();
      webtest::failMetrics = 1;
      rig.controller.requestRelayout();
      rig.flush();
      LOKA_VERIFY(Label("label").y == 0 && Label("Go").y == 6);
      rig.flush();
      LOKA_VERIFY(Label("label").y == 5 && Label("Go").y == 0);
    }
    std::puts("ApproximateAlignment TOP/CENTER/BOTTOM explicit Toolbar passed");
  }
  void AxisRoundTrip()
  {
    webtest::reset();
    Rig rig(webtest::AXIS_ROUND_TRIP);
    rig.flush();
    const webtest::Resource initial = Label("long label");
    LOKA_VERIFY(initial.x > 0 && initial.y == 0);
    {
      loka::core::Operation turn;
      webtest::layoutFixture()->axis.set(STACK_AXIS_COLUMN);
    }
    rig.flush();
    LOKA_VERIFY(Label("long label").x == 0 && Label("long label").y == 19);
    {
      loka::core::Operation turn;
      webtest::layoutFixture()->axis.set(STACK_AXIS_ROW);
    }
    rig.flush();
    LOKA_VERIFY(Label("long label").x == initial.x && Label("long label").y == initial.y);
    std::puts("AxisRoundTrip passed");
  }
} // namespace
void WebStackLayoutTests()
{
  const char *cell = std::getenv("LOKA_WEB_STACK_CELL");
  if (cell && std::strcmp(cell, "AlignedTextButton") == 0)
  {
    AlignedTextButton();
    return;
  }
  if (cell && std::strcmp(cell, "RowMidFailure") == 0)
  {
    RowMidFailure();
    RowMidFailureY();
    return;
  }
  Rows();
  AlignedTextButton();
  AlignedRowEstimateDegradeAndRecovery();
  RowEstimateLimits();
  AxisRoundTrip();
  UnalignedNeverAsks();
  WrappedRowResize();
  SessionDegrade();
  InitialLeafRefusal();
  SkinConsumers();
  ColumnRemainingHeight();
  RowScrollReveal();
  RowMidFailure();
  RowMidFailureY();
  CommonStackSpans();
  RefusedOnlyRow();
  ApproximateAlignment();
}

void WebStackProtocolDeath()
{
  webtest::reset();
  webtest::measureStatus = WEB_MEASURE_PROTOCOL;
  webtest::failMetrics = 1;
  short line = 1, button = 2;
  WebTextLayout::rowEstimate(line, button);
}
