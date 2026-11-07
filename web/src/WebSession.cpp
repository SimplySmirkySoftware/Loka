#include "WebSession.hpp"
#include "app/bootstrap/PlatformBootstrap.hpp"
#include "WebPlatformContext.hpp"
#include "WebInputDoor.hpp"
#include <new>
#include <cassert>
WebSession::WebSession(double now)
    : platform_(0),
      config_(0),
      app_(0),
      regime_(0),
      execution_(IDLE),
      lifecycle_(STARTING),
      idle_(now)
{
}
WebSession::~WebSession()
{
  assert(!this->app_ && !this->config_ && !this->platform_ && !this->regime_);
}
WebSession::Ingress WebSession::ingress() const
{
  if (this->lifecycle_ >= QUITTING)
    return REFUSED_CLOSED;
  switch (this->execution_)
  {
  case IDLE:
    return this->lifecycle_ == RUNNING ? ACCEPTED : REFUSED_BOOTSTRAP;
  case BOOTSTRAP:
    return REFUSED_BOOTSTRAP;
  case DISPATCHING:
    return NESTED;
  case COMPLETING:
    return REFUSED_COMPLETING;
  }
  return REFUSED_CLOSED;
}
WebSession::StartResult WebSession::start(ConfigFactory factory, double now)
{
  if (this->lifecycle_ != STARTING || this->execution_ != IDLE)
    return FAILED;
  this->execution_ = BOOTSTRAP;
  loka::platform::InitPlatformRuntime();
  this->platform_ = new (std::nothrow) WebPlatformContext(*this);
  if (this->platform_ && factory)
    this->config_ = factory(this->platform_);
  if (this->config_)
    this->app_ = static_cast<WebApp *>(this->platform_->createApp(this->config_, 0, 0));
  if (!this->app_)
  {
    // Pre-commit construction rollback, not runtime shutdown.
    delete this->config_;
    this->config_ = 0;
    delete this->platform_;
    this->platform_ = 0;
    this->execution_ = IDLE;
    this->lifecycle_ = QUITTING;
    this->schedule(now);
    return FAILED;
  }
  this->app_->run();
  const bool windowless = this->app_->windowless();
  this->execution_ = IDLE;
  if (this->lifecycle_ == QUITTING)
  {
    this->schedule(now);
    return windowless ? WINDOWLESS : STARTED;
  }
  this->regime_ = new (std::nothrow) loka::core::Operation::Regime();
  if (!this->regime_)
  {
    this->quit();
    return FAILED;
  }
  this->lifecycle_ = RUNNING;
  this->idle_.begin(now);
  this->wake(loka::web::IMMEDIATE, now);
  return STARTED;
}
void WebSession::quit()
{
  if (this->lifecycle_ == DISPOSED)
    return;
  this->lifecycle_ = QUITTING;
  if (this->execution_ == IDLE)
    this->schedule(0);
}
void WebSession::dispose()
{
  this->quit();
}
void WebSession::schedule(double now)
{
  this->idle_.schedule(this->app_ ? this->app_->idlePolicy() : loka::app::IdlePolicy::none(),
                       now,
                       this->app_ && this->app_->pending(),
                       this->lifecycle_ == QUITTING,
                       *this);
}
void WebSession::shutdown()
{
  if (this->execution_ != IDLE || this->lifecycle_ != QUITTING)
    return;
  this->idle_.cancel();
  delete this->regime_;
  this->regime_ = 0;
  delete this->app_;
  this->app_ = 0;
  delete this->config_;
  this->config_ = 0;
  delete this->platform_;
  this->platform_ = 0;
  assert(this->routes_.empty());
  this->lifecycle_ = DISPOSED;
  web_completed();
}
void WebSession::callback(loka::web::Wake kind, unsigned handle, double now)
{
  if (!this->idle_.consume(kind, handle))
    return;
  if (kind == loka::web::SHUTDOWN)
  {
    this->shutdown();
    return;
  }
  this->wake(kind, now);
}
WebSession::Ingress WebSession::click(loka::web::Token token, double now)
{
  const Ingress entry = this->ingress();
  if (entry != ACCEPTED && entry != NESTED)
    return entry;
  WebResource *row = this->routes_.find(token);
  if (!row || row->kind != loka::web::BUTTON || !row->leaf || !row->leaf->active())
    return REFUSED_TOKEN;
  return WebEntry::run(*this, entry, loka::web::BUTTON, token, 0, 0, loka::web::WAKE_COUNT, now);
}
WebSession::Ingress WebSession::resize(loka::web::Token token, int width, int height, double now)
{
  const Ingress entry = this->ingress();
  if (entry != ACCEPTED && entry != NESTED)
    return entry;
  WebResource *row = this->routes_.find(token);
  if (!row || row->kind != loka::web::ROOT || !row->root || !row->root->live() || this->app_->closing(row->root)
      || width < 0 || height < 0 || width > 32767 || height > 32767)
    return REFUSED_TOKEN;
  return WebEntry::run(*this, entry, loka::web::ROOT, token, width, height, loka::web::WAKE_COUNT, now);
}
WebSession::Ingress WebSession::wake(loka::web::Wake kind, double now)
{
  const Ingress entry = this->ingress();
  if (entry != ACCEPTED && entry != NESTED)
    return entry;
  return WebEntry::run(*this, entry, loka::web::KIND_COUNT, 0, 0, 0, kind, now);
}
namespace
{
  void Dispatch(WebSession &session, loka::web::Kind kind, loka::web::Token token, int width, int height)
  {
    WebResource *row = session.routes().find(token);
    if (!row)
      return;
    switch (kind)
    {
    case loka::web::BUTTON:
      WebInputDoor::buttonClick(*row->leaf);
      break;
    case loka::web::ROOT:
      WebInputDoor::resize(*row->root, width, height);
      break;
    case loka::web::TEXT:
      break;
    case loka::web::KIND_COUNT:
      break;
    }
  }
} // namespace
WebSession::Ingress WebEntry::run(WebSession &session,
                                  WebSession::Ingress result,
                                  loka::web::Kind kind,
                                  loka::web::Token token,
                                  int width,
                                  int height,
                                  loka::web::Wake wake,
                                  double now)
{
  const App::CompletionEntry entry =
      loka::core::Operation::hasActive() ? App::COMPLETION_INHERITED_CLOCK : App::COMPLETION_OUTERMOST;
  {
    loka::core::Operation turn;
    if (result == WebSession::NESTED)
    {
      Dispatch(session, kind, token, width, height);
      return result;
    }
    session.execution_ = WebSession::DISPATCHING;
    Dispatch(session, kind, token, width, height);
    double elapsed = 0;
    if (session.idle_.sample(*session.app_, now, wake, elapsed))
      session.app_->handleIdle(elapsed);
    session.execution_ = WebSession::COMPLETING;
    session.app_->tail(turn, entry);
  }
  // Win32 drains messages before each continuation. Browsers cannot do that:
  // four additional tails bound this batch; immediate permits (not guarantees) input interleaving.
  for (unsigned extra = 0; extra < 4 && session.app_->pending() && session.lifecycle_ == WebSession::RUNNING; ++extra)
  {
    loka::core::Operation turn;
    session.app_->tail(turn, App::COMPLETION_OUTERMOST);
  }
  session.execution_ = WebSession::IDLE;
  session.schedule(now);
  return result;
}

bool WebSession::acceptsRoot(loka::web::Token token) const
{
  WebResource *row = this->routes_.find(token);
  return this->lifecycle_ == RUNNING && row && row->root && row->root->live() && row->root->visibilityState().get()
         && !this->app_->closing(row->root);
}
