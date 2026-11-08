#include "WebIdle.hpp"
#include "WebSession.hpp"
namespace
{
  void WakeCallback(void *owner, unsigned handle, int kind, double now)
  {
    if (kind >= 0 && kind < loka::web::WAKE_COUNT)
      static_cast<WebSession *>(owner)->callback(static_cast<loka::web::Wake>(kind), handle, now);
  }
} // namespace
void WebIdle::Slot::cancel()
{
  if (this->handle)
    web_cancel(this->handle);
  this->handle = 0;
  this->kind = loka::web::WAKE_COUNT;
}
void WebIdle::Slot::arm(loka::web::Wake next, double ms, WebSession &session)
{
  if (this->handle && this->kind == next)
    return;
  this->cancel();
  this->kind = next;
  this->handle = next == loka::web::FRAME ? web_arm_frame(&WakeCallback, &session)
                                          : web_arm_task(&WakeCallback, &session, next, ms);
}
bool WebIdle::Slot::consume(loka::web::Wake next, unsigned value)
{
  if (!value || this->handle != value || this->kind != next)
    return false;
  this->handle = 0;
  this->kind = loka::web::WAKE_COUNT;
  return true;
}
WebIdle::WebIdle(double now)
    : policy_(),
      lastSample_(now),
      deadline_(now),
      frameElapsed_(0)
{
}
WebIdle::~WebIdle()
{
  this->cancel();
}
void WebIdle::cancel()
{
  this->frame_.cancel();
  this->task_.cancel();
}
void WebIdle::synchronize(const loka::app::IdlePolicy &policy, double now)
{
  if (this->policy_.mode == policy.mode && this->policy_.intervalSeconds == policy.intervalSeconds)
    return;
  this->policy_ = policy;
  this->deadline_ = now + policy.intervalSeconds * 1000;
  this->frameElapsed_ = 0;
  this->cancel();
}
bool WebIdle::sample(App &app, double now, loka::web::Wake provenance, double &elapsed)
{
  this->synchronize(app.idlePolicy(), now);
  const double delta = now >= this->lastSample_ ? (now - this->lastSample_) / 1000 : 0;
  this->lastSample_ = now;
  if (this->policy_.mode == loka::app::IDLE_MODE_NONE)
    return false;
  double candidate = 0;
  if (!app.consumeIdle(delta, candidate))
    return false;
  switch (this->policy_.mode)
  {
  case loka::app::IDLE_MODE_NONE:
    return false;
  case loka::app::IDLE_MODE_EVERY_TICK:
    this->frameElapsed_ += candidate;
    if (provenance != loka::web::FRAME)
      return false;
    elapsed = this->frameElapsed_;
    this->frameElapsed_ = 0;
    return true;
  case loka::app::IDLE_MODE_INTERVAL:
    elapsed = candidate;
    this->deadline_ = now + this->policy_.intervalSeconds * 1000;
    this->task_.cancel();
    return true;
  }
  return false;
}
void WebIdle::schedule(
    const loka::app::IdlePolicy &policy, double now, bool pending, bool quitting, WebSession &session)
{
  if (quitting)
  {
    this->frame_.cancel();
    this->task_.arm(loka::web::SHUTDOWN, 0, session);
    return;
  }
  this->synchronize(policy, now);
  if (policy.mode == loka::app::IDLE_MODE_EVERY_TICK)
    this->frame_.arm(loka::web::FRAME, 0, session);
  else
    this->frame_.cancel();
  if (pending)
    this->task_.arm(loka::web::IMMEDIATE, 0, session);
  else if (policy.mode == loka::app::IDLE_MODE_INTERVAL)
    this->task_.arm(loka::web::TIMEOUT, now < this->deadline_ ? this->deadline_ - now : 0, session);
  else
    this->task_.cancel();
}
bool WebIdle::consume(loka::web::Wake kind, unsigned handle)
{
  return kind == loka::web::FRAME ? this->frame_.consume(kind, handle) : this->task_.consume(kind, handle);
}
