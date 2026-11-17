#include "WebRectSurfaceContext.hpp"
#include "app/RectSurface.hpp"
using namespace loka::app;
using namespace loka::app::scene;
int WebRectSurfaceContext::place(const LayoutState &state)
{
  const RectSurfaceProps &props = this->owner()->asRectSurfaceNode()->props;
  const int width = props.width_ > 0 ? props.width_ : state.width;
  const int height = props.height_ > 0 ? props.height_ : state.height;
  const int right = state.x + width;
  const int bottom = state.y + height;
  // Deliberate refuse-then-hide twin of WebPopupContext::layout.
  if (width <= 0 || height <= 0 || !this->controller_.checkCoordinate(state.x)
      || !this->controller_.checkCoordinate(state.y) || !this->controller_.checkCoordinate(right)
      || !this->controller_.checkCoordinate(bottom))
  {
    web_set_projected(this->token(), 0);
    return state.y;
  }
  LayoutState resolved = state;
  resolved.width = static_cast<short>(width);
  resolved.height = static_cast<short>(height);
  LayoutState projected;
  if (!this->controller_.project(resolved, projected))
  {
    web_set_projected(this->token(), 0);
    return state.y;
  }
  web_set_rect(this->token(), projected.x, projected.y, width, height);
  web_set_projected(this->token(), 1);
  return bottom;
}
