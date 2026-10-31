#ifndef LOKA_TOOLBOX_UNICODE_TEXT_EDITOR_GEOMETRY_HPP
#define LOKA_TOOLBOX_UNICODE_TEXT_EDITOR_GEOMETRY_HPP
#include "ToolboxTextEditorGeometry.hpp"
#include "core/Frame.hpp"
/** Unicode owns its scrollbar inside the same frame, trimmed above the size box. */
inline Rect ToolboxUnicodeTextEditorFrame(const Rect &seat, const Rect &port, bool hasPort, const Rect &sizeBox)
{
  const ToolboxTextEditorRects geometry = ToolboxTextEditorGeometry(seat, port, hasPort, sizeBox);
  Rect frame = geometry.frame;
  if (geometry.bar.bottom < frame.bottom)
    frame.bottom = geometry.bar.bottom;
  if (frame.bottom - frame.top <= 6)
    frame.bottom = frame.top;
  return frame;
}
inline loka::core::Frame ToolboxUnicodeLogicalFrame(const Rect &rect)
{
  return loka::core::Frame(rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top);
}
#endif
