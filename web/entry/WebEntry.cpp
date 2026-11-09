#include "WebAppEntry.hpp"
#include "WebSession.hpp"
#include <emscripten.h>
namespace
{
  WebSession *session = 0;
}
extern "C"
{
  EMSCRIPTEN_KEEPALIVE int web_start()
  {
    if (!session)
      return WebSession::FAILED;
    return session->start(&loka::web::CreateAppConfig, emscripten_get_now());
  }
  EMSCRIPTEN_KEEPALIVE int web_click(uint32_t token)
  {
    return session->click(token, emscripten_get_now());
  }
  EMSCRIPTEN_KEEPALIVE int web_scroll(uint32_t token, int x, int y)
  {
    return session->scroll(token, x, y, emscripten_get_now());
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
}
int main()
{
  // Module lifetime, never deleted: only the shutdown task destroys the App
  // chain, independently of EXIT_RUNTIME and runtime destructor behavior.
  session = new WebSession(emscripten_get_now());
  return 0;
}
