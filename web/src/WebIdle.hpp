#ifndef LOKA_WEB_IDLE_HPP
#define LOKA_WEB_IDLE_HPP
#include "WebBoundary.hpp"
#include "app/core/App.hpp"
class WebSession;
/** Sole owner of runtime timing and two independent registrations. Cost:
    outer entry samples/schedules once, O(1), only this owner's timing/slots. */
class WebIdle
{
public:
  explicit WebIdle(double now);
  ~WebIdle();
  /** Bootstrap calls once when runtime timing begins; O(1), own timestamp. */
  void begin(double now)
  {
    this->lastSample_ = now;
  }
  /** Cost: once per outer entry; one consumeIdle call and own timing fields, no rows. */
  bool sample(App &app, double now, loka::web::Wake provenance, double &elapsed);
  /** Cost: once per outer entry; two own registrations, no foreign rows. */
  void schedule(const loka::app::IdlePolicy &policy, double now, bool pending, bool quitting, WebSession &session);
  /** Cost: one callback entrance; one own slot identity check and consumption. */
  bool consume(loka::web::Wake kind, unsigned handle);
  /** Cost: shutdown/policy transition once; at most two own registrations. */
  void cancel();

private:
  struct Slot
  {
    Slot()
        : handle(0),
          kind(loka::web::WAKE_COUNT)
    {
    }
    unsigned handle;
    loka::web::Wake kind;
    void cancel();
    void arm(loka::web::Wake next, double ms, WebSession &session);
    bool consume(loka::web::Wake next, unsigned value);
  };
  /** Cost: sample/schedule once each; compare own policy and reset only on change. */
  void synchronize(const loka::app::IdlePolicy &policy, double now);
  loka::app::IdlePolicy policy_;
  double lastSample_;
  double deadline_;
  double frameElapsed_;
  Slot frame_;
  Slot task_;
  WebIdle(const WebIdle &);
  WebIdle &operator=(const WebIdle &);
};
#endif
