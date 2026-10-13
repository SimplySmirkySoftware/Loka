#ifndef LOKA_TOOLBOX_DIALOG_CONSTRUCTION_HPP
#define LOKA_TOOLBOX_DIALOG_CONSTRUCTION_HPP
#include <Dialogs.h>
#include <Quickdraw.h>
#include <cstring>

namespace loka { namespace toolbox {
/** DITL uses four-byte placeholders and big-endian words, irrespective of the
    host pointer size. Both normal confirm and emergency OOM use this encoding. */
inline void PutDialogWord(unsigned char *&out, unsigned short value)
{
  *out++ = static_cast<unsigned char>(value >> 8);
  *out++ = static_cast<unsigned char>(value);
}
inline void PutDialogItem(unsigned char *&out, const Rect &bounds, unsigned char type,
                          const unsigned char *data, unsigned char length)
{
  PutDialogWord(out, 0); PutDialogWord(out, 0);
  PutDialogWord(out, bounds.top); PutDialogWord(out, bounds.left);
  PutDialogWord(out, bounds.bottom); PutDialogWord(out, bounds.right);
  *out++ = type; *out++ = length;
  std::memcpy(out, data, length); out += length;
  if (length & 1) *out++ = 0;
}
/** Alert placement: centered horizontally, one third down the work area. */
inline Rect CenterDialogBounds(short width, short height)
{
  const Rect &screen = qd.screenBits.bounds;
  const short workTop = static_cast<short>(screen.top + GetMBarHeight());
  const short left = static_cast<short>((screen.left + screen.right - width) / 2);
  const short top = static_cast<short>(workTop + (screen.bottom - workTop - height) / 3);
  const Rect bounds = {top, left, static_cast<short>(top + height), static_cast<short>(left + width)};
  return bounds;
}
/** Preserve the caller's port and pen while drawing the default button ring. */
inline void DrawDialogDefaultOutline(DialogPtr dialog, const Rect &button)
{
  GrafPtr savedPort;
  GetPort(&savedPort);
  SetPort(reinterpret_cast<GrafPtr>(dialog));
  PenState savedPen;
  GetPenState(&savedPen);
  PenNormal();
  PenSize(3, 3);
  Rect bounds = button;
  InsetRect(&bounds, -4, -4);
  FrameRoundRect(&bounds, 16, 16);
  SetPenState(&savedPen);
  SetPort(savedPort);
}
} }
#endif
