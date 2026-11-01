#ifndef LOKA_TOOLBOX_LEAF_PLACEMENT_HPP
#define LOKA_TOOLBOX_LEAF_PLACEMENT_HPP

#include "core/Frame.hpp"
#include <Quickdraw.h>

/** Spatial descriptor, not a painter receipt; content inputs remain a separate condition. */
struct ToolboxLeafGeometry
{
  Rect rect;
  Point anchor;
  short constraint;
  bool equals(const ToolboxLeafGeometry &other) const
  {
    return this->rect.top == other.rect.top && this->rect.left == other.rect.left
           && this->rect.bottom == other.rect.bottom && this->rect.right == other.rect.right
           && this->anchor.h == other.anchor.h && this->anchor.v == other.anchor.v
           && this->constraint == other.constraint;
  }
  ToolboxLeafGeometry translated(Point delta) const
  {
    ToolboxLeafGeometry result = *this;
    result.rect.left += delta.h;
    result.rect.right += delta.h;
    result.rect.top += delta.v;
    result.rect.bottom += delta.v;
    result.anchor.h += delta.h;
    result.anchor.v += delta.v;
    return result;
  }
};

/** A completed placement exists; intentional empty geometry is still a placement. */
class ToolboxPlacementValidity
{
public:
  ToolboxPlacementValidity()
      : present_(false)
  {
  }
  void complete()
  {
    this->present_ = true;
  }
  void invalidate()
  {
    this->present_ = false;
  }
  bool present() const
  {
    return this->present_;
  }

private:
  bool present_;
};

/** Origin 0 -> 16 moves every leaf by -16. A null old origin means no previous placement.
    Layout constructs once per viewport visit; O(1), no rows. */
struct ToolboxExpectedMotion
{
  Point pixelDelta;
  bool hasPrevious;
  ToolboxExpectedMotion(const loka::core::Frame *oldOrigin, const loka::core::Frame &newOrigin)
      : pixelDelta(),
        hasPrevious(oldOrigin != 0)
  {
    if (oldOrigin)
    {
      this->pixelDelta.h = static_cast<short>(oldOrigin->x - newOrigin.x);
      this->pixelDelta.v = static_cast<short>(oldOrigin->y - newOrigin.y);
    }
  }
};
#endif
