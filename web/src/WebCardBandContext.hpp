#ifndef LOKA_WEB_CARD_BAND_CONTEXT_HPP
#define LOKA_WEB_CARD_BAND_CONTEXT_HPP
#include "WebScenePlatformController.hpp"
/** A projected Card strip. WebResourceContext owns its lifetime. */
class WebCardBandContext : public WebResourceContext
{
public:
  /** Cost: retained handler once per creation; borrows the owner, no row walk. */
  WebCardBandContext(loka::app::scene::Node &node, WebScenePlatformController &controller)
      : WebResourceContext(node, controller)
  {
  }
  /** Cost: controller once per reached strip (four per Card); own props only.
      Three placement imports on success, one on refusal; no row walk. */
  virtual short layout(loka::app::scene::IPlatformController *, loka::app::scene::LayoutState &state);
};
#endif
