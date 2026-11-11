#include "WebAppEntry.hpp"
#include "WebSession.hpp"
#include "WebFixture.hpp"
#include "ScrollFixture.hpp"
#include "LayoutFixture.hpp"
#include "WebTestHooks.hpp"
#include "WebTestAccess.hpp"
#include "WebWindow.hpp"
#include "WebTextLayout.hpp"
#include <emscripten.h>
namespace webtest
{
  Fixture *fixture = 0;
  struct Staged
  {
    int roots;
    bool destroy;
    Staged()
        : roots(1),
          destroy(false)
    {
    }
  };
  Staged staged;
} // namespace webtest
namespace
{
  webtest::Fixture vehicle;
  WebSession *session = 0;
} // namespace
namespace loka
{
  namespace web
  {
    AppConfigurable *CreateAppConfig(PlatformContext *platform)
    {
      webtest::fixture = &vehicle;
      vehicle.roots = webtest::staged.roots;
      vehicle.destroy = webtest::staged.destroy;
      session = &testing::Access::session(static_cast<WebPlatformContext &>(*platform));
      if (webtest::staged.roots <= -10)
      {
        webtest::layoutCell() = static_cast<webtest::LayoutCell>(-10 - webtest::staged.roots);
        webtest::layoutVariant() = 0;
        return webtest::CreateLayout(platform);
      }
      if (webtest::staged.roots < 0)
        vehicle.roots = -webtest::staged.roots;
      return webtest::staged.roots < 0 ? webtest::CreateScroll(platform) : webtest::CreateCounter(platform);
    }
    namespace testing
    {
      void beforeClick(WebElementContext &) {}
      void routeProbe() {}
      bool refuseCommit()
      {
        return false;
      }
      bool refuseInstallation()
      {
        return false;
      }
      void tailCompleted() {}
    } // namespace testing
  } // namespace web
} // namespace loka
extern "C" int web_wake(int kind, double now);
extern "C"
{
  /** Cost: driver once per transition; one fixture state or its existing Scene request. */
  EMSCRIPTEN_KEEPALIVE void fixture_layout_command(int command, int value)
  {
    if (!session || !webtest::layoutFixture())
      return;
    {
      loka::core::Operation turn;
      webtest::LayoutFixture &body = *webtest::layoutFixture();
      if (command == 0)
        body.axis.set(value ? loka::app::STACK_AXIS_COLUMN : loka::app::STACK_AXIS_ROW);
      if (command == 1)
        body.shown.set(value != 0);
      if (command == 2)
        body.request.set(loka::app::ScrollTarget::Origin(value, 0));
      if (command == 4)
        body.request.set(loka::app::ScrollTarget::Origin(0, value));
      if (command == 3)
        body.scene()->requestLayoutAfterRun();
    }
    web_wake(loka::web::IMMEDIATE, emscripten_get_now());
  }
  /** Cost: driver observation once; copies one fixture fact, no tree walk. */
  EMSCRIPTEN_KEEPALIVE int fixture_layout_fact(int field)
  {
    if (!webtest::layoutFixture())
      return -999;
    const loka::core::Frame value = webtest::layoutFixture()->viewport.get();
    return field == 0 ? value.x : field == 1 ? value.y : field == 2 ? value.width : value.height;
  }
  EMSCRIPTEN_KEEPALIVE int fixture_scroll_close(uint32_t root, uint32_t token)
  {
    loka::core::Operation turn;
    WebResource *row = session->routes().find(root);
    if (!row || !row->root)
      return -1;
    loka::web::testing::Access::close(*session, row->root);
    return session->scroll(token, 0, 0, emscripten_get_now());
  }
  EMSCRIPTEN_KEEPALIVE int fixture_scroll_fact(int field)
  {
    if (!webtest::scrollFixture())
      return -999;
    const loka::core::Frame value = webtest::scrollFixture()->viewport.get();
    return field == 0 ? value.x : field == 1 ? value.y : field == 2 ? value.width : value.height;
  }
  EMSCRIPTEN_KEEPALIVE void fixture_scroll_command(int command, int value, uint32_t root)
  {
    if (!session || !webtest::scrollFixture())
      return;
    {
      loka::core::Operation turn;
      webtest::ScrollFixture &body = *webtest::scrollFixture();
      if (command == 0)
        body.request.set(loka::app::ScrollTarget::Top());
      if (command == 1)
        body.width.set(value);
      if (command == 2)
        body.shown.set(value != 0);
      if (command == 3)
      {
        WebResource *row = session->routes().find(root);
        if (row && row->root)
          row->root->visibilityState().set(value != 0);
      }
      if (command == 4)
        body.text.set(loka::core::String::FromInt(value));
    }
    web_wake(loka::web::IMMEDIATE, emscripten_get_now());
  }
  /** Fixture-only projection exercise. Cost: driver once per cell, its own temporary rows. */
  EMSCRIPTEN_KEEPALIVE int fixture_text_probe(int kind, int width, uint32_t token)
  {
    using namespace loka::app;
    const std::string prose = "The quick brown fox jumps over the lazy dog. ";
    const std::string value = kind == 0   ? "AVx"
                              : kind == 1 ? "a\nb"
                              : kind == 2 ? "漢字😀"
                              : kind == 3 ? (prose + prose + prose + prose + prose).substr(0, 200)
                              : kind == 4 ? "x " + std::string(198, 'a')
                                          : "";
    const loka::core::String text(value);
    const BlockStyle block = BlockStyle().wrap(TEXT_WRAP_WORD);
    WebTextSnapshot *snapshot = 0;
    const int status =
        WebTextLayout::build(text, WebTextConstraint(16, block, static_cast<short>(width)), false, snapshot);
    if (status != WEB_MEASURE_OK)
      return -1;
    WebTextSlot slot;
    slot.replace(snapshot);
    const WebTextDescriptor d = WebMetricDescriptor(16);
    web_set_text_style(token, d.data(), d.count());
    web_set_line_height(token, snapshot->metrics().ascent + snapshot->metrics().descent);
    web_set_block(token, TEXT_ALIGN_LEFT, TEXT_TRUNCATION_CLIP);
    web_set_text(token, snapshot->joined(), snapshot->joinedLength());
    web_set_rect(token, 0, 0, width, snapshot->extent().height);
    return snapshot->extent().height;
  }
  EMSCRIPTEN_KEEPALIVE void fixture_configure(int roots, int destroy)
  {
    webtest::staged.roots = roots;
    webtest::staged.destroy = destroy != 0;
  }
  EMSCRIPTEN_KEEPALIVE int fixture_configured()
  {
    return vehicle.roots * 10 + vehicle.destroy;
  }
  /** Fixture-only control; not a Web rail export. Own Boundary state through a turn. */
  EMSCRIPTEN_KEEPALIVE void fixture_command(int command, int value)
  {
    if (!session || vehicle.nodes.empty())
      return;
    {
      loka::core::Operation turn;
      if (command == 0)
        vehicle.nodes[0]->show(value != 0);
      if (command == 1)
        vehicle.nodes[0]->enable(value != 0);
      if (command == 2)
      {
        WebResource *row = session->routes().find(static_cast<uint32_t>(value));
        if (row && row->root)
          loka::web::testing::Access::close(*session, row->root);
      }
    }
    web_wake(loka::web::IMMEDIATE, emscripten_get_now());
  }
}
