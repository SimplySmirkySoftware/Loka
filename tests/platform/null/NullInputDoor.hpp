#ifndef LOKA_NULL_INPUT_DOOR_HPP
#define LOKA_NULL_INPUT_DOOR_HPP

#include "platform/null/context/NullScrollBarContext.hpp"
#include "platform/null/context/NullAttributedTextContext.hpp"
#include "platform/null/context/NullTextEditorContext.hpp"

/** Synchronous Null input operations. The invocation restores the previous
    controller phase; only the existing owner pump advances held Scene work.
    Callers supply a live receiver and do not borrow it after a retiring input. */
class NullInputDoor
{
private:
  typedef loka::app::scene::detail::InputInvocation Invocation;

public:
  /** Injects an accepted press against the current value inside a turn. */
  static bool activateLink(NullAttributedTextContext &c, std::size_t segmentIndex)
  {
    return Invocation(c.controller_)(c, &NullAttributedTextContext::activateLink, segmentIndex);
  }
  static void simulatePress(NullScrollBarContext &c, NullScrollBarContext::Part part, int repeats)
  { Invocation(*c.controller_)(c, &NullScrollBarContext::simulatePress, part, repeats); }
  static void simulateThumbDragTo(NullScrollBarContext &c, int value)
  { Invocation(*c.controller_)(c, &NullScrollBarContext::simulateThumbDragTo, value); }
  static void pressTick(NullScrollBarContext &c, NullScrollBarContext::Part part)
  { Invocation(*c.controller_)(c, &NullScrollBarContext::pressTick, part); }
  static void dragThumbTo(NullScrollBarContext &c, int value)
  { Invocation(*c.controller_)(c, &NullScrollBarContext::dragThumbTo, value); }
  static void release(NullScrollBarContext &c)
  { Invocation(*c.controller_)(c, &NullScrollBarContext::release); }
  static loka::app::EditorResult textEditorInput(NullTextEditorContext &c,
      const std::string &bytes, bool join, const loka::app::LineCursor *move)
  {
    return Invocation(c.controller_).operator()<NullTextEditorContext,
        loka::app::EditorResult, const std::string &, bool, const loka::app::LineCursor *>(
            c, &NullTextEditorContext::input, bytes, join, move);
  }
};
#endif
