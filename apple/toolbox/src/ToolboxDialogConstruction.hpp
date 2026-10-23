#ifndef LOKA_TOOLBOX_DIALOG_CONSTRUCTION_HPP
#define LOKA_TOOLBOX_DIALOG_CONSTRUCTION_HPP
#include <Dialogs.h>
#include <Quickdraw.h>
#include <cstring>
#include <cstddef>

namespace loka
{
  namespace toolbox
  {
    /** DITL uses four-byte placeholders and big-endian words, irrespective of the
        host pointer size. Both normal confirm and emergency OOM use this encoding. */
    inline void PutDialogWord(unsigned char *&out, unsigned short value)
    {
      *out++ = static_cast<unsigned char>(value >> 8);
      *out++ = static_cast<unsigned char>(value);
    }
    inline void PutDialogItem(
        unsigned char *&out, const Rect &bounds, unsigned char type, const unsigned char *data, unsigned char length)
    {
      PutDialogWord(out, 0);
      PutDialogWord(out, 0);
      PutDialogWord(out, bounds.top);
      PutDialogWord(out, bounds.left);
      PutDialogWord(out, bounds.bottom);
      PutDialogWord(out, bounds.right);
      *out++ = type;
      *out++ = length;
      std::memcpy(out, data, length);
      out += length;
      if (length & 1)
        *out++ = 0;
    }
    /** Alert placement: centered horizontally, one third down the work area. */
    Rect CenterDialogBounds(short width, short height);
    /** Preserve the caller's port and pen while drawing the default button ring. */
    void DrawDialogDefaultOutline(DialogPtr dialog, const Rect &button);
    /** Synchronous emergency dialog. Owns its Handle, Dialog and modal filter.
        Message bytes use the system encoding, are length-delimited and capped at
        255 bytes; no NUL terminator is read or required. Construction or filter
        refusal returns silently. Storage is bounded; dismissal waiting is not.
        The caller owns the decision to exit even when construction is refused. */
    void RunEmergencyQuitDialog(const char *message, std::size_t length);
  } // namespace toolbox
} // namespace loka
#endif
