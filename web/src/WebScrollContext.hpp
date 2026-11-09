#ifndef LOKA_WEB_SCROLL_CONTEXT_HPP
#define LOKA_WEB_SCROLL_CONTEXT_HPP
#include "WebScenePlatformController.hpp"
/** Completed geometry, replaced only after a successful native commit. */
class WebScrollMetrics
{
public:
  WebScrollMetrics(const loka::app::scene::LayoutState &seat,
                   int documentWidth,
                   int extent,
                   int viewportWidth,
                   int viewportHeight,
                   const loka::app::ScrollViewVisit &axes)
      : rect_(seat.x, seat.y, seat.width, seat.height),
        documentWidth_(documentWidth),
        extent_(extent),
        viewportWidth_(viewportWidth),
        viewportHeight_(viewportHeight),
        axes_(axes)
  {
  }
  /** Cost: layout/request/input once; clamps two coordinates against this value. */
  loka::core::Frame clamp(int x, int y) const;
  /** Cost: successful traversal or request once; one adapter row, one synchronous commit. */
  int commit(loka::web::Token token, int x, int y, int &appliedX, int &appliedY) const;
  int width() const
  {
    return this->viewportWidth_;
  }
  int height() const
  {
    return this->viewportHeight_;
  }

private:
  const loka::core::Frame rect_;
  const int documentWidth_, extent_, viewportWidth_, viewportHeight_;
  const loka::app::ScrollViewVisit axes_;
};
/** Nestable receiver. Native scroll input reports facts without writing the DOM. */
class WebScrollContext : public WebResourceContext
{
public:
  WebScrollContext(loka::app::scene::Node &node, WebScenePlatformController &controller)
      : WebResourceContext(node, controller),
        metrics_(0)
  {
  }
  virtual ~WebScrollContext();
  /** Layout once per successful traversal; one commit, then one publication. */
  bool commit(WebScrollMetrics *candidate);
  /** Request once per taken target; replays only completed geometry. */
  bool apply(const loka::core::Frame &target);
  /** Input ingress/dispatch once each; own facts plus owning root admission. */
  bool active() const;
  /** One native input; clamps against completed metrics and publishes once. */
  void acceptScroll(int x, int y);

private:
  WebScrollMetrics *metrics_;
};
#endif
