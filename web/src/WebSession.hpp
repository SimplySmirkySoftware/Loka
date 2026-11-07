#ifndef LOKA_WEB_SESSION_HPP
#define LOKA_WEB_SESSION_HPP
#include "WebResources.hpp"
#include "WebIdle.hpp"
#include "core/Operation.hpp"
class WebPlatformContext;
class WebApp;
class WebEntry;
/** Module-lifetime owner. Destruction of the App chain occurs only in a consumed
    shutdown task at IDLE. Cost: ingress O(1); shutdown once over owned services,
    each service destroys its own rows. No Operation is stored in this owner. */
class WebSession
{
public:
  enum Ingress
  {
    ACCEPTED,
    NESTED,
    REFUSED_COMPLETING,
    REFUSED_BOOTSTRAP,
    REFUSED_CLOSED,
    REFUSED_TOKEN
  };
  enum StartResult
  {
    STARTED,
    WINDOWLESS,
    FAILED
  };
  typedef AppConfigurable *(*ConfigFactory)(PlatformContext *);
  explicit WebSession(double now = 0);
  ~WebSession();
  /** Cost: module wrapper once; constructs its owner chain, App visits its own composition. */
  StartResult start(ConfigFactory factory, double now);
  /** Cost: one click; two button and two root lookups (each <=33 probes).
      App checks its pending-close rows twice; one receiver, then completion. */
  Ingress click(loka::web::Token token, double now);
  /** Cost: one resize; two bounded root lookups; App checks its pending-close
      rows once, then owner layout scheduling and completion. */
  Ingress resize(loka::web::Token root, int width, int height, double now);
  /** Cost: wake wrapper once per valid callback; no dispatch rows, one bounded entry batch. */
  Ingress wake(loka::web::Wake kind, double now);
  /** Cost: adapter callback once; consumes one own slot before ingress or shutdown. */
  void callback(loka::web::Wake kind, unsigned handle, double now);
  /** Cost: controller check twice per click; <=33 route probes and one App-owned
      pending-close query, O(pending closures). No caller walks App rows. */
  bool acceptsRoot(loka::web::Token token) const;
  /** Cost: App or wrapper once per request; O(1) own lifecycle and task slot. */
  void quit();
  /** Cost: wrapper once per request; O(1), repeated disposal has no work. */
  void dispose();
  WebInputRoutes &routes()
  {
    return this->routes_;
  }
  WebTokens &tokens()
  {
    return this->tokens_;
  }

private:
  friend class WebEntry;
  friend struct loka::web::testing::Access;
  enum Execution
  {
    IDLE,
    BOOTSTRAP,
    DISPATCHING,
    COMPLETING
  };
  enum Lifecycle
  {
    STARTING,
    RUNNING,
    QUITTING,
    DISPOSED
  };
  /** Cost: each export once before lookup/borrow; two own phase reads, no rows. */
  Ingress ingress() const;
  /** Cost: once after each outer batch; App checks its component rows, idle reconciles two slots. */
  void schedule(double now);
  /** Cost: one consumed shutdown task; each owner tears down its own rows in reverse order. */
  void shutdown();
  WebInputRoutes routes_;
  WebTokens tokens_;
  WebPlatformContext *platform_;
  AppConfigurable *config_;
  WebApp *app_;
  loka::core::Operation::Regime *regime_;
  Execution execution_;
  Lifecycle lifecycle_;
  WebIdle idle_;
  WebSession(const WebSession &);
  WebSession &operator=(const WebSession &);
};
/** Stack entry owns each lexical turn. Cost: one dispatch and tail per accepted
    export, at most four extra tails; App doors walk App-owned rows. */
class WebEntry
{
public:
  /** Cost: accepted export once; one tail plus at most four continuations, App-owned rows only. */
  static WebSession::Ingress
  run(WebSession &, WebSession::Ingress, loka::web::Kind, loka::web::Token, int, int, loka::web::Wake, double);
};
#endif
