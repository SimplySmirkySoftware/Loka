#ifndef LOKA_TOOLBOX_WINDOW_GEOMETRY_HPP
#define LOKA_TOOLBOX_WINDOW_GEOMETRY_HPP

/** The documentProc size box in window port coordinates; shared by paint and controls. */
template <typename RectT> inline RectT ToolboxWindowSizeBoxRect(const RectT &port)
{
  const int size = 15;
  RectT rect = port;
  rect.left = port.right - size;
  rect.top = port.bottom - size;
  return rect;
}

#endif // LOKA_TOOLBOX_WINDOW_GEOMETRY_HPP
