#ifndef LOKA_TOOLBOX_BLUR_DIRTY_EXTENT_HPP
#define LOKA_TOOLBOX_BLUR_DIRTY_EXTENT_HPP
#include "ToolboxUnitLedger.hpp"

/** O(1) delivery extent from the previous layout key. Destination clipping,
    not this query, bounds drawing to the current port after a window shrink. */
inline Rect ToolboxBlurDirtyExtent(const Rect &dirty, const Rect &key)
{
  Rect overlap;
  if (!ToolboxUnitLedger::overlap(dirty, key, overlap))
    return dirty;
  Rect result;
  result.left = dirty.left < key.left ? dirty.left : key.left;
  result.top = dirty.top < key.top ? dirty.top : key.top;
  result.right = dirty.right > key.right ? dirty.right : key.right;
  result.bottom = dirty.bottom > key.bottom ? dirty.bottom : key.bottom;
  return result;
}
#endif
