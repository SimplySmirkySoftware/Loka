#ifndef LOKA_TOOLBOX_DIRTY_REPLAY_HPP
#define LOKA_TOOLBOX_DIRTY_REPLAY_HPP
#include "app/scene/Node.hpp"
#include "ToolboxInputDoor.hpp"
#include <Quickdraw.h>

/** Composition-order fallback for drawers without hit-ledger replay. Walks
    the current projection tree once per dirty delivery, without retained rows. */
inline bool ToolboxTreeHasStackingContainer(loka::app::scene::Node *node)
{
  if (!node)
  {
    return false;
  }
  if (node->kind() == loka::app::scene::NODE_KIND_ZSTACK || node->kind() == loka::app::scene::NODE_KIND_LAYER)
  {
    return true;
  }
  if (loka::app::scene::INestable *nestable = node->asNestable())
  {
    loka::dsl::CompositionCursor<loka::app::scene::Node> it(nestable->childrenHead(), nestable->childrenCount());
    for (loka::app::scene::Node *child = it.next(); child; child = it.next())
    {
      if (ToolboxTreeHasStackingContainer(child))
      {
        return true;
      }
    }
  }
  return false;
}
/** Shared clipped replay ritual. Erase precedes composition, so transparent
    text does not erase already-rendered siblings. The controller owns the walk. */
template <class Controller> void ToolboxRenderDirtyInCompositionOrder(Controller &controller, const Rect &rect)
{
  // A ZStack declares shared pixels. Rebuild the registries only before any
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
  }
  EraseRect(&rect);
  ToolboxInputDoor::render(controller);
  if (oldClip != 0)
  {
    SetClip(oldClip);
    DisposeRgn(oldClip);
  }
  controller.drawControlsInRect(rect);
  SetPort(oldPort);
}
#endif
