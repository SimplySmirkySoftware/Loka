#ifndef LOKA_TOOLBOX_EDITOR_SEAT_HPP
#define LOKA_TOOLBOX_EDITOR_SEAT_HPP
#include "app/nodes/controls/TextEditorRequests.hpp"
#include "app/EditVerb.hpp"
#include "ToolboxScrollKey.hpp"
#include <Events.h>
#include <cstddef>

/** Borrowed editor behavior. The controller ledger owns native-handle facts;
    focus is callback-free and called only for Unicode resource rows. */
class ToolboxEditorSeat
{
public:
  virtual ~ToolboxEditorSeat() {}
  /** Outer chrome bounds used by dirty replay. */
  virtual Rect chromeRect() const = 0;
  /** Service the focused native editor after projection retry. */
  virtual void idleTick() = 0;
  /** Retry a previously refused projection before idle service. */
  virtual void retryProjection() = 0;
  /** Synchronously cut the context presentation before ledger retirement. */
  virtual void invalidateNativePresentation() = 0;
  /** Deliver one view-navigation key. */
  virtual loka::app::EditorResult scrollKey(ToolboxScrollKey) = 0;
  /** Deliver one legacy character through the existing input phase. */
  virtual loka::app::EditorResult key(char) = 0;
  /** Replace the selection from bounded bytes through the editor input phase. */
  virtual loka::app::EditorResult paste(const char *, std::size_t) = 0;
  /** Replay the installed presentation without changing ledger membership. */
  virtual void repaintNative() = 0;
  /** Deliver local coordinates and the original native event. */
  virtual loka::app::EditorResult click(const Point &, const EventRecord &) = 0;
  /** Offer a non-Command native key; false leaves legacy handling in place. */
  virtual bool offerKeyEvent(const EventRecord &) = 0;
  /** Offer an Edit command; false means this seat did not handle it. */
  virtual bool editVerb(loka::app::EditVerb) = 0;
  /** Unicode-row admission only; TE admission uses the captured ledger handle. */
  virtual bool hasNativeHandle() const = 0;
  /** Unicode-row activation only. Must not call back into Loka. */
  virtual void focus(bool) = 0;
};
#endif
