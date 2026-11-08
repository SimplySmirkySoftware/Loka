#include "WebSession.hpp"
#include "WebFixture.hpp"
#include "WebTestHooks.hpp"
#include "WebTestAccess.hpp"
#include "WebWindow.hpp"
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
