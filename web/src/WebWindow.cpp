#include "WebWindow.hpp"
#include "WebSession.hpp"
#include <new>
WebRoot::~WebRoot()
{
  WebResource *row = this->element_.revoke(this->session_.routes());
  if (row)
  {
    web_release(row->token);
    delete row;
  }
}
bool WebRoot::create(WebWindow &window, int &width, int &height)
{
  if (!this->element_.create(this->session_.routes(), this->session_.tokens(), loka::web::ROOT, 0, width, height))
    return false;
  this->session_.routes().find(this->token())->root = &window;
  // Creation commits a hidden element; later visibility failures preserve it.
  return this->park(true);
}
bool WebRoot::park(bool hidden)
{
  if (!this->token() || !web_set_hidden(this->token(), hidden))
    return false;
  this->visibility_ = hidden ? HIDDEN : SHOWN;
  return true;
}
WebWindow::WebWindow(PlatformContext *platform, const WindowProps &props, WebSession &session)
    : Window(platform, props),
      session_(session),
      root_(session),
      controller_(0)
{
}
WebWindow::~WebWindow()
{
  this->detachNativeStateObservers();
  if (this->scene() && this->controller_)
    this->unmountSceneForTeardown(*this->scene());
  if (this->controller_)
    this->controller_->destroy();
  delete this->controller_;
  this->controller_ = 0;
  // WebRoot extracts its route and releases after child drain, the deliberate
  // twin of Win32Window::destroyNativeWindow and NullWindow::destroyScenePlatform.
}
bool WebWindow::initialize()
{
  int width = this->width() > 0 ? this->width() : Window::defaultFrame().width;
  int height = this->height() > 0 ? this->height() : Window::defaultFrame().height;
  if (!this->root_.create(*this, width, height))
    return false;
  this->storeNativeFrame(loka::core::Frame(0, 0, width, height));
  this->controller_ = new (std::nothrow) WebScenePlatformController(this->session_, this->root_.token(), width, height);
  if (!this->controller_)
    return false;
  if (this->scene() && !this->mountReplacementScene(this->scene()))
    return false;
  return true;
}
bool WebWindow::live() const
{
  return this->controller_ && (!this->scene() || this->scene()->getAttachedState()->get());
}
bool WebWindow::mountReplacementScene(loka::app::scene::Scene *next)
{
  return this->controller_ && next->mount(this->controller_);
}
bool WebWindow::hasLiveScenePlatform() const
{
  return this->controller_ != 0;
}
bool WebWindow::hasPendingNativeVisibility() const
{
  // Win32Window compares intent with hwnd_ != NULL; NullWindow compares it
  // with controller presence. Web park retains both: compare applied visibility.
  return this->visibility_->get() != this->root_.isShown();
}
void WebWindow::applyNativeVisibility()
{
  if (this->hasPendingNativeVisibility() && this->root_.park(!this->visibility_->get()) && this->rootShown())
    this->requestSceneRelayout();
}
void WebWindow::handleNativeSize(int width, int height)
{
  this->storeNativeFrame(loka::core::Frame(0, 0, width, height));
  this->controller_->resize(width, height);
}
bool WebWindow::hasPendingScenePlatformSync() const
{
  return this->controller_ && this->controller_->hasPendingSync();
}
void WebWindow::synchronizeScenePlatform()
{
  if (this->controller_)
    this->controller_->synchronize();
}
void WebWindow::drainNativeRetirements()
{
  if (this->controller_)
    this->controller_->drainNativeRetirements();
}

void WebWindow::requestSceneRelayout()
{
  if (this->scene())
    this->scene()->requestLayoutAfterRun();
}
