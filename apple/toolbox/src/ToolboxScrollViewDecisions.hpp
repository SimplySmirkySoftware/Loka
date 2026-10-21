#ifndef LOKA_TOOLBOX_SCROLL_VIEW_DECISIONS_HPP
#define LOKA_TOOLBOX_SCROLL_VIEW_DECISIONS_HPP

#include "app/nodes/controls/ScrollBar.hpp"
#include "app/nodes/nestable/ScrollView.hpp"

/** Host-compilable value used at the Toolbox ScrollView decision seam. */
struct ToolboxScrollViewRect
{
  ToolboxScrollViewRect()
      : left(0), top(0), right(0), bottom(0)
  {
  }

  ToolboxScrollViewRect(int l, int t, int r, int b)
      : left(l), top(t), right(r), bottom(b)
  {
  }

  int left;
  int top;
  int right;
  int bottom;
};

/** Completed range and offset decision for one viewport layout pass. */
struct ToolboxScrollViewMetrics
{
  ToolboxScrollViewMetrics(int maximumValue, int clampedValue)
      : maximum(maximumValue),
        clampedOffset(clampedValue)
  {
  }

  int maximum;
  int clampedOffset;
};

/** Completed frame contacts, derived only from an answered window port. */
struct ToolboxScrollViewEdges
{
  ToolboxScrollViewEdges(bool l, bool t, bool r, bool b)
      : left(l),
        top(t),
        right(r),
        bottom(b)
  {
  }
  const bool left;
  const bool top;
  const bool right;
  const bool bottom;
};

template <typename RectT>
inline ToolboxScrollViewEdges ToolboxScrollViewFrameEdges(const RectT &seat, const RectT &port, bool hasPort)
{
  return ToolboxScrollViewEdges(hasPort && seat.left == port.left,
                                hasPort && seat.top == port.top,
                                hasPort && seat.right == port.right,
                                hasPort && seat.bottom == port.bottom);
}

/** Only a horizontal bar may occupy the window's bottom frame line. */
template <typename RectT>
inline RectT ToolboxScrollViewFrameRect(const RectT &seat, bool horizontalBar, const ToolboxScrollViewEdges &edges)
{
  RectT frame = seat;
  frame.right += edges.right;
  if (horizontalBar)
    frame.bottom += edges.bottom;
  return frame;
}

inline int ToolboxScrollViewChildWidth(int viewportWidth)
{
  const int width = viewportWidth - loka::app::SCROLL_BAR_THICKNESS;
  return width > 0 ? width : 0;
}

/** Stores an edge computed in int into the rect's own coordinate type (short
    for QuickDraw). Callers pass a value between the rect's existing edges. */
template <typename CoordT> inline void ToolboxScrollViewStoreEdge(CoordT &edge, int value)
{
  edge = static_cast<CoordT>(value);
}

/** Bars exclude the bottom-right corner, which remains window ground. */
template <typename RectT> inline RectT ToolboxScrollViewContentRect(const RectT &seat, bool horizontalBar)
{
  RectT content = seat;
  // The child width never exceeds the seat's width, so the new edge stays within the seat.
  ToolboxScrollViewStoreEdge(content.right, content.left + ToolboxScrollViewChildWidth(seat.right - seat.left));
  if (horizontalBar)
    ToolboxScrollViewStoreEdge(content.bottom, content.top + ToolboxScrollViewChildWidth(seat.bottom - seat.top));
  return content;
}

/** Intersects a registration rect with the active viewport. Empty edges do
    not register: QuickDraw's right and bottom edges are exclusive. */
template <typename RectT, typename ViewportT>
inline bool ToolboxScrollViewIntersectHitRect(const RectT &rect, const ViewportT &viewport, RectT &out)
{
  out.left = rect.left > viewport.left ? rect.left : viewport.left;
  out.top = rect.top > viewport.top ? rect.top : viewport.top;
  out.right = rect.right < viewport.right ? rect.right : viewport.right;
  out.bottom = rect.bottom < viewport.bottom ? rect.bottom : viewport.bottom;
  return out.left < out.right && out.top < out.bottom;
}

/** An empty size box means the window has none to answer (no native window yet). Edges are exclusive. */
template <typename RectT, typename SizeBoxT>
inline bool ToolboxScrollViewReachesSizeBox(const RectT &bar, const SizeBoxT &sizeBox)
{
  RectT intersection;
  return ToolboxScrollViewIntersectHitRect(bar, sizeBox, intersection);
}

/** Child width and bar placement share the same frame expansion. */
template <typename RectT> inline int ToolboxScrollViewChildWidth(const RectT &seat, const ToolboxScrollViewEdges &edges)
{
  const RectT frame = ToolboxScrollViewFrameRect(seat, false, edges);
  return ToolboxScrollViewChildWidth(frame.right - frame.left);
}

template <typename RectT>
inline RectT ToolboxScrollViewVerticalBarRect(
    const RectT &seat,
    bool horizontalBar,
    const RectT &sizeBox,
    const ToolboxScrollViewEdges &edges,
    const loka::app::ScrollViewContentInsets &insets = loka::app::ScrollViewContentInsets())
{
  const RectT frame = ToolboxScrollViewFrameRect(seat, horizontalBar, edges);
  RectT bar = ToolboxScrollViewContentRect(frame, horizontalBar);
  bar.left = bar.right;
  bar.right = frame.right;
  bar.top = seat.top - edges.top;
  if (!horizontalBar)
    bar.bottom = seat.bottom + edges.bottom;
  ToolboxScrollViewRect reach(bar.left, bar.top, bar.right, bar.bottom);
  ++reach.bottom;
  if (ToolboxScrollViewReachesSizeBox(reach, sizeBox))
    bar.bottom = sizeBox.top + 1 > bar.top ? sizeBox.top + 1 : bar.top;
  // Layout-erasure invariant: only layout changes this rect. Boundary layout
  // invalidates the whole window, so the next draw erases both old and new
  // tracks. Scrolling never moves the bar; no separate damage path is needed.
  const int trackTop = bar.top;
  const int trackBottom = bar.bottom;
  int top = insets.top() > 0 ? seat.top + insets.top() : trackTop;
  int bottom = insets.bottom() > 0 ? seat.bottom - insets.bottom() : trackBottom;
  top = top < trackTop ? trackTop : top > trackBottom ? trackBottom : top;
  bottom = bottom < trackTop ? trackTop : bottom > trackBottom ? trackBottom : bottom;
  ToolboxScrollViewStoreEdge(bar.top, top);
  ToolboxScrollViewStoreEdge(bar.bottom, bottom > top ? bottom : top);
  return bar;
}

template <typename RectT>
inline RectT
ToolboxScrollViewHorizontalBarRect(const RectT &seat, const RectT &sizeBox, const ToolboxScrollViewEdges &edges)
{
  const RectT frame = ToolboxScrollViewFrameRect(seat, true, edges);
  RectT bar = ToolboxScrollViewContentRect(frame, true);
  bar.top = bar.bottom;
  bar.bottom = frame.bottom;
  bar.left = seat.left - edges.left;
  ToolboxScrollViewRect reach(bar.left, bar.top, bar.right, bar.bottom);
  ++reach.right;
  if (ToolboxScrollViewReachesSizeBox(reach, sizeBox))
    bar.right = sizeBox.left + 1 > bar.left ? sizeBox.left + 1 : bar.left;
  return bar;
}

inline int ToolboxScrollViewMaximum(int contentHeight, int viewportHeight)
{
  if (contentHeight <= viewportHeight)
  {
    return 0;
  }
  return contentHeight - viewportHeight;
}

inline ToolboxScrollViewMetrics ToolboxScrollViewResolveMetrics(
    int contentHeight,
    int viewportHeight,
    int requestedOffset)
{
  const int maximum = ToolboxScrollViewMaximum(contentHeight, viewportHeight);
  int clamped = requestedOffset;
  if (clamped < 0)
  {
    clamped = 0;
  }
  else if (clamped > maximum)
  {
    clamped = maximum;
  }
  return ToolboxScrollViewMetrics(maximum, clamped);
}

#endif // LOKA_TOOLBOX_SCROLL_VIEW_DECISIONS_HPP
