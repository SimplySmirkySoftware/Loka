#ifndef LOKA_TOOLBOX_UNICODE_TEXT_EDITOR_GEOMETRY_HPP
#define LOKA_TOOLBOX_UNICODE_TEXT_EDITOR_GEOMETRY_HPP
#include "ToolboxTextEditorGeometry.hpp"
#include "core/Frame.hpp"
/** Unicode owns its scrollbar inside the same frame, trimmed above the size box,
    and keeps every edge at least one pixel inside the port. The TE geometry
    shares a window-edge seat's border line with the window (one pixel past the
    port); a Textension frame that spans the port's width shows neither the
    caret nor an active scrollbar on focus (Mac OS 9.2, observed on hardware and
    under emulation), so the Unicode frame never reaches a port edge. */
inline Rect ToolboxUnicodeTextEditorFrame(const Rect &seat, const Rect &port, bool hasPort, const Rect &sizeBox)
{
  const ToolboxTextEditorRects geometry = ToolboxTextEditorGeometry(seat, port, hasPort, sizeBox);
  Rect frame = geometry.frame;
  if (geometry.bar.bottom < frame.bottom)
    frame.bottom = geometry.bar.bottom;
  if (hasPort)
  {
    if (frame.left <= port.left)
      frame.left = static_cast<short>(port.left + 1);
    if (frame.top <= port.top)
      frame.top = static_cast<short>(port.top + 1);
    if (frame.right >= port.right)
      frame.right = static_cast<short>(port.right - 1);
    if (frame.bottom >= port.bottom)
      frame.bottom = static_cast<short>(port.bottom - 1);
  }
  if (frame.right - frame.left <= 21 || frame.bottom - frame.top <= 6)
    frame.bottom = frame.top;
  return frame;
}
inline loka::core::Frame ToolboxUnicodeLogicalFrame(const Rect &rect)
{
  return loka::core::Frame(rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top);
}
#endif
