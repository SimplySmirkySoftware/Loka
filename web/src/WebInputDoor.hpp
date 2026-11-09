#ifndef LOKA_WEB_INPUT_DOOR_HPP
#define LOKA_WEB_INPUT_DOOR_HPP
#include "WebWindow.hpp"
#include "WebScrollContext.hpp"
/** Native input borrow, deliberate twin of Win32InputDoor. Cost: one invocation
    per accepted click/resize, one receiver, no rows. Restores without pumping. */
class WebInputDoor
{
  typedef loka::app::scene::detail::InputInvocation Invocation;

public:
  static bool buttonClick(WebElementContext &context)
  {
    return Invocation(context.controller())(context, &WebElementContext::click);
  }
  /** Cost: accepted scroll once; one receiver under the existing input borrow. */
  static void scroll(WebScrollContext &context, int x, int y)
  {
    Invocation(context.controller())(context, &WebScrollContext::acceptScroll, x, y);
  }
  static void resize(WebWindow &window, int width, int height)
  {
    Invocation (*window.controller())(window, &WebWindow::handleNativeSize, width, height);
  }
};
#endif
