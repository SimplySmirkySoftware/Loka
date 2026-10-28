#ifndef LOKA_WIN32_FOCUS_PARTICIPANT_HPP
#define LOKA_WIN32_FOCUS_PARTICIPANT_HPP

#include <windows.h>
#include "app/scene/Node.hpp"
#include "Win32EditTextContext.hpp"
#include "Win32TextEditorContext.hpp"

/** Typed, non-owning HWND property shared only by focus participants.
    SetPropW interns the name as a property atom. Store the adjusted NodeContext
    pointer, never GWLP_USERDATA. Attach sets it; detach removes it synchronously
    at lifecycle fact delivery. A refused SetPropW leaves the HWND unmarked;
    the next attach retries. This property never owns the context. */
class Win32FocusParticipant
{
public:
  /** Shared target resolution for posted focus and activation restore. */
  static HWND target(loka::app::scene::NodeContext *context)
  {
    loka::app::scene::Node *node = context ? context->owner() : 0;
    if (node && node->asEditTextNode())
      return static_cast<Win32EditTextContext *>(context)->hwnd();
    if (node && node->nodeTypeKey() == loka::app::scene::NodeTypeToken<loka::app::TextEditorNode>())
      return static_cast<Win32TextEditorContext *>(context)->hwnd();
    return 0;
  }

  /** Resolves only this root's marked target, with both identities agreeing. */
  static loka::app::scene::NodeContext *attachedTarget(HWND root, HWND hwnd)
  {
    if (!hwnd || !IsChild(root, hwnd))
      return 0;
    loka::app::scene::NodeContext *const context = read(hwnd);
    return context && target(context) == hwnd ? context : 0;
  }

  static bool attach(HWND hwnd, loka::app::scene::NodeContext *context)
  {
    return SetPropW(hwnd, name(), context) != FALSE;
  }
  static void detach(HWND hwnd)
  {
    RemovePropW(hwnd, name());
  }
  static loka::app::scene::NodeContext *read(HWND hwnd)
  {
    return static_cast<loka::app::scene::NodeContext *>(GetPropW(hwnd, name()));
  }

private:
  static LPCWSTR name() { return L"Loka.FocusParticipant.NodeContext"; }
};

#endif
