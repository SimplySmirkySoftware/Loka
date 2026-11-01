#ifndef LOKA_TOOLBOX_CLIP_BOX_CONTEXT_HPP
#define LOKA_TOOLBOX_CLIP_BOX_CONTEXT_HPP

#include "app/scene/projection/PlatformNodeHandler.hpp"
#include <Quickdraw.h>
#include "ToolboxPresentObservation.hpp"

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
class ToolboxClipBoxContext : public loka::app::scene::NodeContext, public loka::app::scene::IClipBoxContext
{
public:
  virtual loka::app::scene::IClipBoxContext *asClipBoxContext()
  {
    return this;
  }
  void readLifecycleFactOnAttach() {}
  const ToolboxClipPlacement &placement() const
  {
    return this->placement_;
  }
  /** Dispatch completes once per clip visit: compare, write, complete. O(1), own fields only. */
  void setPlacement(const ToolboxClipPlacement &placement, ToolboxViewportContentObservation *content = 0)
  {
    if (content)
    {
      Rect previous, next;
      const bool had = this->placement_.query(previous);
      const bool has = placement.query(next);
      bool translated = this->validity_.present() && content->motion().hasPrevious;
      if (translated && had && has)
      {
        const Point delta = content->motion().pixelDelta;
        const ToolboxLeafGeometry before = {previous, {previous.top, previous.left}, 0};
        const ToolboxLeafGeometry after = {next, {next.top, next.left}, 0};
        translated = before.translated(delta).equals(after);
      }
      else
        translated = translated && !had && !has;
      if (!translated)
        content->noteNotTranslated();
    }
    this->placement_ = placement;
    this->validity_.complete();
  }
  /** A refused visit draws none as before, but provides no comparison baseline. O(1). */
  void refusePlacement()
  {
    this->placement_ = ToolboxClipPlacement();
    this->validity_.invalidate();
  }

private:
  ToolboxClipPlacement placement_;
  /** Same value as the projected base; ClipBox is not a projected context. */
  ToolboxPlacementValidity validity_;
};

bool IsToolboxClipBoxType(const void *key);
bool RegisterToolboxClipBoxNodeHandlers(loka::app::scene::PlatformNodeHandlerRegistry &registry);

#endif
