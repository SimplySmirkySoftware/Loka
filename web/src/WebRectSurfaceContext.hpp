#ifndef LOKA_WEB_RECT_SURFACE_CONTEXT_HPP
#define LOKA_WEB_RECT_SURFACE_CONTEXT_HPP
#include "WebScenePlatformController.hpp"
/** Diagnostic surface; resource lifetime belongs entirely to WebResourceContext. */
class WebRectSurfaceContext : public WebResourceContext
{
public:
  /** Cost: retained handler once per creation; initializes one owner borrow, no rows. */
  WebRectSurfaceContext(loka::app::scene::Node &node, WebScenePlatformController &controller)
      : WebResourceContext(node, controller)
  {
  }
  /** Cost: controller once per reached visit; own size props, no row walk.
      Success writes rect then visibility (two imports, five style assignments);
      refusal writes visibility once. Failed ensure never calls this door. */
  int place(const loka::app::scene::LayoutState &state);
};
#endif
