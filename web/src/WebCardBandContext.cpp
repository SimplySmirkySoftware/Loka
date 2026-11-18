#include "WebCardBandContext.hpp"
#include "app/nodes/controls/Card.hpp"
#include <climits>
using namespace loka::app;
using namespace loka::app::scene;
short WebCardBandContext::layout(IPlatformController *, LayoutState &state)
{
  const int bottom = state.y + state.height;
  LayoutState projected;
  // Deliberate refuse-then-hide twin of WebRectSurfaceContext::place and WebPopupContext::layout.
  if (state.width <= 0 || state.height <= 0 || !this->controller_.checkCoordinate(state.x)
      || !this->controller_.checkCoordinate(state.y) || !this->controller_.checkCoordinate(state.x + state.width)
      || !this->controller_.checkCoordinate(bottom) || !this->controller_.project(state, projected))
  {
    web_set_projected(this->token(), 0);
    return state.y;
  }
  const CardBandProps &props = this->owner()->asCardBandNode()->props;
  const loka::core::Frame local(0, 0, state.width, state.height);
  const loka::core::Frame body = CardBodyInBand(local, props);
  web_set_rect(this->token(), projected.x, projected.y, projected.width, projected.height);
  web_set_card_body(this->token(), body.x, body.y, body.width, body.height, props.style.extent());
  web_set_projected(this->token(), 1);
  return static_cast<short>(bottom > SHRT_MAX ? SHRT_MAX : bottom);
}
