#ifndef LOKA_WEB_POPUP_CONTEXT_HPP
#define LOKA_WEB_POPUP_CONTEXT_HPP
#include "WebScenePlatformController.hpp"
/** Projects one PopupMenu; resource lifetime is entirely WebResourceContext's. */
class WebPopupContext : public WebResourceContext
{
public:
  /** Cost: handler creation once; initializes one owner borrow, no rows. */
  WebPopupContext(loka::app::scene::Node &node, WebScenePlatformController &controller)
      : WebResourceContext(node, controller)
  {
  }
  /** Cost: controller visit once; one memoized font estimate, no item walk. */
  virtual short layout(loka::app::scene::IPlatformController *, loka::app::scene::LayoutState &);
  /** Cost: controller visit once; walks this node's projected items once. */
  virtual void render(loka::app::scene::IPlatformController *);
  /** Cost: ingress and dispatch once each; reads this owner's admission and enabled fact. */
  bool active() const;
  /** Cost: input borrow once per selection; own props only, no DOM writes or row walk. */
  bool select(int index);

private:
  /** Cost: render/select once each; O(1) own items size, capped before narrowing. */
  int projectedCount() const;
};
#endif
