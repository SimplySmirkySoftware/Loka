#ifndef LOKA_WIN32_INPUT_DOOR_HPP
#define LOKA_WIN32_INPUT_DOOR_HPP

#include "Win32ScenePlatformController.hpp"
#include "Win32Window.hpp"
#include "context/Win32ButtonContext.hpp"
#include "context/Win32EditTextContext.hpp"
#include "context/Win32PopupMenuContext.hpp"
#include "context/Win32ScrollViewContext.hpp"
#include "context/Win32CellContext.hpp"
#include "context/Win32TextEditorContext.hpp"

/** Complete synchronous Win32 input operations. The stack borrow restores
    without pumping; Win32App::flushIterationTail remains the completion.
    Adapters supply a live receiver and never borrow it after invocation. */
class Win32InputDoor
{
  typedef loka::app::scene::detail::InputInvocation Invocation;

public:
  static bool buttonCommand(Win32ButtonContext &c, WPARAM wp, LPARAM lp)
  {
    return Invocation(*c.controller())(c, &Win32ButtonContext::handleCommand, wp, lp);
  }
  static bool editTextCommand(Win32EditTextContext &c, WPARAM wp, LPARAM lp)
  {
    return Invocation(*c.controller())(c, &Win32EditTextContext::handleCommand, wp, lp);
  }
  static bool popupMenuCommand(Win32PopupMenuContext &c, WPARAM wp, LPARAM lp)
  {
    return Invocation(*c.controller())(c, &Win32PopupMenuContext::handleCommand, wp, lp);
  }
  static bool textEditorCommand(Win32TextEditorContext &c, WPARAM wp, LPARAM lp)
  {
    return Invocation(*c.controller())(c, &Win32TextEditorContext::handleCommand, wp, lp);
  }
  static bool scroll(Win32ScrollViewContext &c, int bar, int command, int position)
  {
    return Invocation(*c.controller())(c, &Win32ScrollViewContext::handleScroll, bar, command, position);
  }
  static void mouseWheel(Win32ScrollViewContext &c, int bar, WPARAM wp)
  {
    Invocation (*c.controller())(c, &Win32ScrollViewContext::handleMouseWheel, bar, wp);
  }
  static void cellClick(Win32CellContext &c)
  {
    Invocation(*c.controller())(c, &Win32CellContext::handleClick);
  }
  static LRESULT textEditorInput(Win32TextEditorContext &c, UINT message, WPARAM wp, LPARAM lp)
  {
    return Invocation(*c.controller())(c, &Win32TextEditorContext::handleInputMessage, message, wp, lp);
  }
  static void textEditorRetry(Win32TextEditorContext &c)
  {
    Invocation(*c.controller())(c, &Win32TextEditorContext::handleRestoreTimer);
  }
  static bool editCommand(Win32Window &w, loka::app::EditVerb verb)
  {
    if (!w.scenePlatformController_)
      return false;
    return Invocation(*w.scenePlatformController_)(w, &Win32Window::handleEditCommandInput, verb);
  }
  static void windowSize(Win32Window &w, WPARAM wp, LPARAM lp)
  {
    Invocation(*w.scenePlatformController_)(w, &Win32Window::handleNativeSize, wp, lp);
  }
  static void windowDpi(Win32Window &w, WPARAM wp, LPARAM lp)
  {
    Invocation(*w.scenePlatformController_)(w, &Win32Window::handleNativeDpi, wp, lp);
  }
};
#endif
