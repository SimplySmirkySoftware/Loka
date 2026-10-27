#ifndef LOKA_TOOLBOX_CLIP_BOX_CONTEXT_HPP
#define LOKA_TOOLBOX_CLIP_BOX_CONTEXT_HPP

#include "app/scene/projection/PlatformNodeHandler.hpp"
#include <Quickdraw.h>

/** Completed latest placement: a window-coordinate rectangle, or no clip. */
class ToolboxClipPlacement
{
public:
  ToolboxClipPlacement()
      : rect_(),
        present_(false)
  {
  }
  explicit ToolboxClipPlacement(const Rect &rect)
      : rect_(rect),
        present_(true)
  {
  }
  bool query(Rect &out) const
  {
    if (!this->present_)
      return false;
    out = this->rect_;
    return true;
  }

private:
  Rect rect_;
  bool present_;
};

/** Node-owned clip fact; no native resources, projection, or drawing role. */
class ToolboxClipBoxContext : public loka::app::scene::NodeContext
{
public:
  void readLifecycleFactOnAttach() {}
  const ToolboxClipPlacement &placement() const
  {
    return this->placement_;
  }
  /** Replace the latest placement, including none after an unflagged or refused placement. */
  void setPlacement(const ToolboxClipPlacement &placement)
  {
    this->placement_ = placement;
  }

private:
  ToolboxClipPlacement placement_;
};

bool IsToolboxClipBoxType(const void *key);
bool RegisterToolboxClipBoxNodeHandlers(loka::app::scene::PlatformNodeHandlerRegistry &registry);

#endif
