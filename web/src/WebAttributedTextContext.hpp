#ifndef LOKA_WEB_ATTRIBUTED_TEXT_CONTEXT_HPP
#define LOKA_WEB_ATTRIBUTED_TEXT_CONTEXT_HPP
#include "WebScenePlatformController.hpp"
#include "WebAttributedSlot.hpp"

/** Retained consumer of completed PER_RUN snapshots; deliberate WebElementContext twin. */
class WebAttributedTextContext : public WebResourceContext
{
public:
  /** Cost: handler once per admission; initializes only this context's empty slot. */
  WebAttributedTextContext(loka::app::scene::Node &node, WebScenePlatformController &controller)
      : WebResourceContext(node, controller)
  {
  }
  /** Cost: controller once per visit; compares own source segments, builds only on a miss.
      Builder walks its own value; WORD lookahead is worst-case quadratic in bytes. */
  virtual short layout(loka::app::scene::IPlatformController *, loka::app::scene::LayoutState &);
  /** Cost: controller once per visit; two imports with a snapshot, one without; own rows only. */
  virtual void render(loka::app::scene::IPlatformController *);
  /** Cost: lifecycle once per transition; retire clears own buffers, park preserves them. */
  virtual void onFactChanged(loka::app::scene::NodeLifecycleFact, loka::app::scene::NodeLifecycleFact);

private:
  WebAttributedSlot projection_;
};
#endif
