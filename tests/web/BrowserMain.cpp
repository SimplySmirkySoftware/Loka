#include "WebSession.hpp"
#include "WebFixture.hpp"
#include "WebTestHooks.hpp"
#include "WebTestAccess.hpp"
#include "WebWindow.hpp"
#include "WebTextLayout.hpp"
#include <emscripten.h>
namespace webtest
{
  Fixture *fixture = 0;
}
namespace
{
  webtest::Fixture vehicle;
  WebSession *session = 0;
} // namespace
namespace loka
{
  namespace web
  {
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
extern "C"
{
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
  EMSCRIPTEN_KEEPALIVE int web_start(int config)
  {
    if (!session)
      return WebSession::FAILED;
    vehicle.roots = config == 2 ? 2 : config == 0 ? 0 : 1;
    vehicle.destroy = config == 3;
    return session->start(&webtest::CreateCounter, emscripten_get_now());
  }
  EMSCRIPTEN_KEEPALIVE int web_click(uint32_t token)
  {
    return session->click(token, emscripten_get_now());
  }
  EMSCRIPTEN_KEEPALIVE int web_resize(uint32_t token, int width, int height)
  {
    return session->resize(token, width, height, emscripten_get_now());
  }
  EMSCRIPTEN_KEEPALIVE int web_wake(int kind, double now)
  {
    if (kind < 0 || kind >= loka::web::WAKE_COUNT)
      return WebSession::REFUSED_CLOSED;
    return session->wake(static_cast<loka::web::Wake>(kind), now);
  }
  EMSCRIPTEN_KEEPALIVE void web_quit()
  {
    session->requestQuit(emscripten_get_now());
  }
  EMSCRIPTEN_KEEPALIVE void web_dispose()
  {
    session->dispose();
  }
  /** Fixture-only control; not a Web rail export. Own Boundary state through a turn. */
  EMSCRIPTEN_KEEPALIVE void fixture_command(int command, int value)
  {
    if (vehicle.nodes.empty())
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
    session->wake(loka::web::IMMEDIATE, emscripten_get_now());
  }
}
int main()
{
  webtest::fixture = &vehicle;
  session = new WebSession(emscripten_get_now());
  return 0;
}
