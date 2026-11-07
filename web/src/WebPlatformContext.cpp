#include "WebPlatformContext.hpp"
#include "WebSession.hpp"
#include "WebTestHooks.hpp"
#include "WebWindow.hpp"
#include <new>
App *WebPlatformContext::createApp(AppConfigurable *config, NativeModuleHandle, int) const
{
  return new (std::nothrow) WebApp(config, this->session_);
}
Window *WebPlatformContext::createWindow(const WindowProps &props)
{
  WebWindow *window = new (std::nothrow) WebWindow(this, props, this->session_);
  if (window && !window->initialize())
  {
    delete window;
    return 0;
  }
  return window;
}
loka::app::scene::NodeContext *WebPlatformContext::createNodeContext(loka::app::scene::Node *) const
{
  return 0;
}
void WebApp::quit()
{
  this->session_.quit();
}
void WebApp::tail(loka::core::Operation &turn, CompletionEntry entry)
{
  // Deliberate Web-local twin of Win32App::flushIterationTail and
  // AppTestAccess::completeTurn. The second admission is unconditional (Win32).
  turn.settle();
  this->admitAndApplyWindows();
  this->reconcileAtCompletion();
  (void)this->serveURLsAtCompletion(entry);
  this->admitAndApplyWindows();
  turn.close();
  this->reclaimWindows();
#ifdef TEST_BUILD
  loka::web::testing::tailCompleted();
#endif
}
