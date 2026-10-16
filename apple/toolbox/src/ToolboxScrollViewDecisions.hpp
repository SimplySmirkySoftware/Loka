#ifndef LOKA_TOOLBOX_SCROLL_VIEW_DECISIONS_HPP
#define LOKA_TOOLBOX_SCROLL_VIEW_DECISIONS_HPP

#include "app/nodes/controls/ScrollBar.hpp"

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

inline int ToolboxScrollViewChildWidth(int viewportWidth)
{
  const int width = viewportWidth - loka::app::SCROLL_BAR_THICKNESS;
  return width > 0 ? width : 0;
}

/** Bars exclude the bottom-right corner, which remains window ground. */
template <typename RectT> inline RectT ToolboxScrollViewContentRect(const RectT &seat, bool horizontalBar)
{
  RectT content = seat;
  content.right = content.left + ToolboxScrollViewChildWidth(seat.right - seat.left);
  if (horizontalBar)
    content.bottom = content.top + ToolboxScrollViewChildWidth(seat.bottom - seat.top);
  return content;
}

/** Intersects a registration rect with the active viewport. Empty edges do
    not register: QuickDraw's right and bottom edges are exclusive. */
template <typename RectT>
inline bool ToolboxScrollViewIntersectHitRect(const RectT &rect, const RectT &viewport, RectT &out)
{
  out.left = rect.left > viewport.left ? rect.left : viewport.left;
  out.top = rect.top > viewport.top ? rect.top : viewport.top;
  out.right = rect.right < viewport.right ? rect.right : viewport.right;
  out.bottom = rect.bottom < viewport.bottom ? rect.bottom : viewport.bottom;
  return out.left < out.right && out.top < out.bottom;
}

/** An empty size box means the window has none to answer (no native window yet). Edges are exclusive. */
template <typename RectT> inline bool ToolboxScrollViewReachesSizeBox(const RectT &bar, const RectT &sizeBox)
{
  RectT intersection;
  return ToolboxScrollViewIntersectHitRect(bar, sizeBox, intersection);
}

template <typename RectT>
inline RectT ToolboxScrollViewVerticalBarRect(const RectT &seat, bool horizontalBar, const RectT &sizeBox)
{
  RectT bar = ToolboxScrollViewContentRect(seat, horizontalBar);
  bar.left = bar.right;
  bar.right = seat.right;
  if (ToolboxScrollViewReachesSizeBox(bar, sizeBox))
    bar.bottom = sizeBox.top > bar.top ? sizeBox.top : bar.top;
  return bar;
}

template <typename RectT> inline RectT ToolboxScrollViewHorizontalBarRect(const RectT &seat, const RectT &sizeBox)
{
  RectT bar = ToolboxScrollViewContentRect(seat, true);
  bar.top = bar.bottom;
  bar.bottom = seat.bottom;
  if (ToolboxScrollViewReachesSizeBox(bar, sizeBox))
    bar.right = sizeBox.left > bar.left ? sizeBox.left : bar.left;
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
