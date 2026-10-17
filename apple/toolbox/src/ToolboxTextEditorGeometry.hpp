#ifndef LOKA_TOOLBOX_TEXT_EDITOR_GEOMETRY_HPP
#define LOKA_TOOLBOX_TEXT_EDITOR_GEOMETRY_HPP
#include "ToolboxScrollViewDecisions.hpp"
#include <Quickdraw.h>
#include <climits>
#include <algorithm>

/** Completed editor placement, before ancestor clipping. Empty text/bar refuse placement. */
struct ToolboxTextEditorRects
{
  const Rect frame, bar, text, hit;
  ToolboxTextEditorRects(const Rect &f, const Rect &b, const Rect &t, const Rect &h)
      : frame(f),
        bar(b),
        text(t),
        hit(h)
  {
  }
};

/** One geometry answer for native creation, layout, input and paint. */
inline ToolboxTextEditorRects
ToolboxTextEditorGeometry(const Rect &seat, const Rect &port, bool hasPort, const Rect &sizeBox)
{
  const ToolboxScrollViewEdges edges = ToolboxScrollViewFrameEdges(seat, port, hasPort);
  const long left = static_cast<long>(seat.left) - edges.left;
  const long top = static_cast<long>(seat.top) - edges.top;
  const long right = static_cast<long>(seat.right) + edges.right;
  const long bottom = static_cast<long>(seat.bottom) + edges.bottom;
  Rect empty = {seat.top, seat.left, seat.top, seat.left};
  if (left < SHRT_MIN || top < SHRT_MIN || right > SHRT_MAX || bottom > SHRT_MAX || right - left <= 21
      || bottom - top <= 6 || right - left > SHRT_MAX || bottom - top > SHRT_MAX)
    return ToolboxTextEditorRects(empty, empty, empty, empty);
  long barBottom = bottom;
  if (sizeBox.right > sizeBox.left && sizeBox.bottom > sizeBox.top && right > sizeBox.left && right - 16 < sizeBox.right
      && bottom + 1 > sizeBox.top && top < sizeBox.bottom)
    barBottom = std::max(top, static_cast<long>(sizeBox.top) + 1);
  const Rect frame = {
      static_cast<short>(top), static_cast<short>(left), static_cast<short>(bottom), static_cast<short>(right)};
  const Rect bar = {static_cast<short>(top),
                    static_cast<short>(right - 16),
                    static_cast<short>(barBottom),
                    static_cast<short>(right)};
  const Rect text = {static_cast<short>(top + 3),
                     static_cast<short>(left + 3),
                     static_cast<short>(bottom - 3),
                     static_cast<short>(right - 18)};
  const Rect hit = {frame.top, frame.left, frame.bottom, bar.left};
  return ToolboxTextEditorRects(frame, bar, barBottom > top ? text : empty, hit);
}
#endif
