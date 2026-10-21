#ifndef LOKA_TOOLBOX_DIRTY_REPLAY_HPP
#define LOKA_TOOLBOX_DIRTY_REPLAY_HPP
#include "app/scene/Node.hpp"
#include "ToolboxInputDoor.hpp"
#include "ToolboxErasedPass.hpp"
#include <Quickdraw.h>

/** Shared clipped replay ritual. Erase precedes composition, so transparent
    text does not erase already-rendered siblings. The controller owns the walk. */
template <class Controller> void ToolboxRenderDirtyInCompositionOrder(Controller &controller, const Rect &rect)
{
  // Rebuild the registries only before any
  // replay prefix is frozen (#315), and let the clipped render walk own the
  // dirty pixels instead of letting one text run erase its siblings.
  GrafPtr oldPort;
  GetPort(&oldPort);
  SetPort(controller.window_->window());
  // Own the clip save locally: beginClip/endClip share one region and one
  // flag, and the walk below re-enters them (EditText::draw clips TEUpdate),
  // so nesting through the shared pair would leave the port clipped to this
  // dirty rect after the inner endClip consumed the flag. Same shape as
  // redrawTextHit's save/restore.
  RgnHandle oldClip = NewRgn();
  if (oldClip != 0)
  {
    GetClip(oldClip);
    ClipRect(&rect);
    EraseRect(&rect);
    ToolboxErasedPass::Guard erased(controller.erasedPass(), rect);
    ToolboxInputDoor::render(controller);
  }
  else
  {
    EraseRect(&rect);
    ToolboxInputDoor::render(controller);
  }
  if (oldClip != 0)
  {
    SetClip(oldClip);
    DisposeRgn(oldClip);
  }
  SetPort(oldPort);
}
#endif
