#ifndef LOKA_TOOLBOX_FULL_DRAW_REASON_HPP
#define LOKA_TOOLBOX_FULL_DRAW_REASON_HPP

/** Closed causes of a Toolbox whole-window request; names preserve diagnostic history. */
enum ToolboxFullDrawReason
{
  TOOLBOX_FULL_DRAW_UNKNOWN,
  TOOLBOX_FULL_DRAW_LAYOUT_DIRTY,
  TOOLBOX_FULL_DRAW_TEXT_RELAYOUT,
  TOOLBOX_FULL_DRAW_CHILD_DIRTY,
  TOOLBOX_FULL_DRAW_FULL_REBUILD_OR_NO_ROOT,
  TOOLBOX_FULL_DRAW_STRUCTURE_SWAP,
  TOOLBOX_FULL_DRAW_PAINT_WIDENED_VIEWPORT_RENDER,
  /** A settled native bar value published as the viewport's requested origin. */
  TOOLBOX_FULL_DRAW_VIEWPORT_INPUT,
  TOOLBOX_FULL_DRAW_REASON_COUNT
};

/** Stable diagnostic spelling, shared by the first-request source and observations. */
inline const char *ToolboxFullDrawReasonName(ToolboxFullDrawReason reason)
{
  switch (reason)
  {
  case TOOLBOX_FULL_DRAW_UNKNOWN:
    return "unknown";
  case TOOLBOX_FULL_DRAW_LAYOUT_DIRTY:
    return "layout_dirty";
  case TOOLBOX_FULL_DRAW_TEXT_RELAYOUT:
    return "text_relayout";
  case TOOLBOX_FULL_DRAW_CHILD_DIRTY:
    return "child_dirty";
  case TOOLBOX_FULL_DRAW_FULL_REBUILD_OR_NO_ROOT:
    return "full_rebuild_or_no_root";
  case TOOLBOX_FULL_DRAW_STRUCTURE_SWAP:
    return "structure-swap";
  case TOOLBOX_FULL_DRAW_PAINT_WIDENED_VIEWPORT_RENDER:
    return "paint-widened-viewport-render";
  case TOOLBOX_FULL_DRAW_VIEWPORT_INPUT:
    return "viewport_input";
  case TOOLBOX_FULL_DRAW_REASON_COUNT:
    break;
  }
  return "unknown";
}
#endif
