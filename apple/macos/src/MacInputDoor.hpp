#ifndef LOKA_MAC_INPUT_DOOR_HPP
#define LOKA_MAC_INPUT_DOOR_HPP

#include "MacScenePlatformController.hpp"
#include "core/Operation.hpp"
#include "MacWindow.hpp"
#include "context/MacButtonContext.hpp"
#include "context/MacEditTextContext.hpp"
#include "context/MacPopupMenuContext.hpp"
#include "context/MacScrollViewContext.hpp"
#include "context/MacCellContext.hpp"
#include "context/MacTextEditorContext.hpp"

/** Complete synchronous macOS input operations, parallel to Win32InputDoor.
    The borrow restores without pumping; MacApp::flushInvalidationsTick remains
    the completion. Each entry is a collection turn: collect, settle, close.
    Adapters never borrow a receiver after invocation. */
class MacInputDoor
{
  typedef loka::app::scene::detail::InputInvocation Invocation;

  /** One clock owner for every input arity, including controllerless N5 calls.
      invoke restores the borrow before close drives any ledger callbacks. */
  template<class C, class M, class A, class B>
  static void collect(loka::app::scene::IPlatformController *controller,
                      C &c, M body, A a, B b)
  {
    loka::core::Operation turn;
    invoke(controller, c, body, a, b);
    turn.close();
  }
  template<class C>
  static void invoke(loka::app::scene::IPlatformController *controller,
                     C &c, void (C::*body)(), int, int)
  {
    if (controller)
      Invocation(*controller).operator()(c, body);
    else
      (c.*body)();
  }
  template<class C, class A>
  static void invoke(loka::app::scene::IPlatformController *controller,
                     C &c, void (C::*body)(A), A a, int)
  {
    Invocation(*controller).operator()(c, body, a);
  }
  template<class C, class A, class B>
  static void invoke(loka::app::scene::IPlatformController *controller,
                     C &c, void (C::*body)(A, B), A a, B b)
  {
    Invocation(*controller).operator()(c, body, a, b);
  }

public:
  static void buttonPress(MacButtonContext &c)
  {
    collect(c.controller(), c, &MacButtonContext::handlePress, 0, 0);
  }
  static void editTextChange(MacEditTextContext &c)
  {
    collect(c.controller(), c, &MacEditTextContext::handleTextDidChange, 0, 0);
  }
  static void popupChange(MacPopupMenuContext &c)
  {
    collect(c.controller(), c, &MacPopupMenuContext::handleSelectionChange, 0, 0);
  }
  static void scrollBoundsChange(MacScrollViewContext &c)
  {
    collect(c.controller(), c, &MacScrollViewContext::publishClipViewBoundsOrigin, 0, 0);
  }
  static void scrollerStyleChange(MacScrollViewContext &c)
  {
    collect(c.controller(), c, &MacScrollViewContext::handleScrollerStyleChange, 0, 0);
  }
  static void cellClick(MacCellContext &c, void *event)
  {
    collect(c.controller(), c, &MacCellContext::handleClick, event, 0);
  }
  static void textEditorChange(MacTextEditorContext &c, MacTextEditorContext::TextObservation source, std::size_t caret)
  {
    collect(c.controller(), c, &MacTextEditorContext::handleTextDidChange, source, caret);
  }
  static void textEditorSelection(MacTextEditorContext &c)
  {
    collect(c.controller(), c, &MacTextEditorContext::handleSelectionDidChange, 0, 0);
  }
  static void textEditorCapture(MacTextEditorContext &c)
  {
    collect(c.controller(), c, &MacTextEditorContext::captureSelection, 0, 0);
  }
  static void textEditorRestore(MacTextEditorContext &c)
  {
    collect(c.controller(), c, &MacTextEditorContext::restoreCommittedProjection, 0, 0);
  }
  static void textEditorHighlights(MacTextEditorContext &c)
  {
    collect(c.controller(), c, &MacTextEditorContext::applyHighlights, 0, 0);
  }
  static void windowResize(MacWindow &w)
  {
    collect(w.scenePlatformController_, w, &MacWindow::handleWindowDidResize, 0, 0);
  }
  static void windowMove(MacWindow &w)
  {
    collect(w.scenePlatformController_, w, &MacWindow::handleWindowDidMove, 0, 0);
  }
  static void pendingRelayout(MacScenePlatformController &c)
  {
    collect(&c, c, &MacScenePlatformController::handlePendingRelayout, 0, 0);
  }
};
#endif
