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
Rect CenterDialogBounds(short width, short height);
/** Preserve the caller's port and pen while drawing the default button ring. */
void DrawDialogDefaultOutline(DialogPtr dialog, const Rect &button);
} }
#endif
