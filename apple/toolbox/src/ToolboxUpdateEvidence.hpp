#ifndef LOKA_TOOLBOX_UPDATE_EVIDENCE_HPP
#define LOKA_TOOLBOX_UPDATE_EVIDENCE_HPP
#include <Quickdraw.h>

/** Snapshot rectangular global update evidence in the window's local coordinates. */
inline Rect ToolboxUpdateEvidence(RgnHandle updateRgn, const Rect &portBitsBounds)
{
  Rect evidence = {0, 0, 0, 0};
  if (updateRgn && *updateRgn && (**updateRgn).rgnSize == sizeof(Region))
  {
    evidence = (**updateRgn).rgnBBox;
    OffsetRect(&evidence, portBitsBounds.left, portBitsBounds.top);
  }
  return evidence;
}
/** Evidence is bounded by both the actual erase and the entry's capture area. */
inline Rect ToolboxCaptureEvidence(const Rect &erased, const Rect &evidence)
{
  Rect result = {0, 0, 0, 0};
  if (!SectRect(&erased, &evidence, &result))
    result = Rect();
  return result;
}
#endif
