#ifndef LOKA_TOOLBOX_INPUT_DOOR_HPP
#define LOKA_TOOLBOX_INPUT_DOOR_HPP

#include "ToolboxScenePlatformController.hpp"

/** Complete synchronous Toolbox operations, parallel to Win32InputDoor and
    MacInputDoor. The borrow restores without pumping; ToolboxApp::present
    remains the completion. Tracking and commit share one scope. */
class ToolboxInputDoor
{
  typedef loka::app::scene::detail::InputInvocation Invocation;

public:
  static bool syncNativeFocus(ToolboxScenePlatformController &c, NativeFocusMode mode)
  {
    return Invocation(c).operator()(c, &ToolboxScenePlatformController::syncNativeFocus, mode);
  }
  static void releaseNativeFocus(ToolboxScenePlatformController &c, loka::app::scene::NodeContext *context)
  {
    Invocation(c).operator()(c, &ToolboxScenePlatformController::releaseNativeFocus, context);
  }
  static bool mouseDown(ToolboxScenePlatformController &c, const Point &point, const EventRecord &event = EventRecord())
  {
    return Invocation(c).operator()<ToolboxScenePlatformController, bool, const Point &, const EventRecord &>(
        c, &ToolboxScenePlatformController::handleMouseDown, point, event);
  }
  static bool offerKeyEvent(ToolboxScenePlatformController &c, const EventRecord &event)
  {
    return Invocation(c).operator()<ToolboxScenePlatformController, bool, const EventRecord &>(
        c, &ToolboxScenePlatformController::offerKeyEvent, event);
  }
  static bool keyDown(ToolboxScenePlatformController &c, char key)
  {
    return Invocation(c).operator()(c, &ToolboxScenePlatformController::handleKeyDown, key);
  }
  static bool scrollKey(ToolboxScenePlatformController &c, ToolboxScrollKey key)
  {
    return Invocation(c).operator()(c, &ToolboxScenePlatformController::handleScrollKey, key);
  }
  static bool tabKey(ToolboxScenePlatformController &c, ToolboxTabDirection direction)
  {
    return Invocation(c).operator()(c, &ToolboxScenePlatformController::handleTabKey, direction);
  }
  /** A null presenter preserves caller-owned erase ordering for dirty replay. */
  static void render(ToolboxScenePlatformController &c, ToolboxFullDrawPresenter *presenter = 0)
  {
    Invocation(c).operator()(c, &ToolboxScenePlatformController::render, presenter);
  }
  static void renderDirty(ToolboxScenePlatformController &c, const Rect &rect)
  {
    Invocation(c).operator()<ToolboxScenePlatformController, void, const Rect &>(
        c, &ToolboxScenePlatformController::renderDirty, rect);
  }
  static void idleTextEdits(ToolboxScenePlatformController &c)
  {
    Invocation(c).operator()(c, &ToolboxScenePlatformController::idleTextEdits);
  }
};
#endif
