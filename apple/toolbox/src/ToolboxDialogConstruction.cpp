#include "ToolboxDialogConstruction.hpp"
#include <Events.h>
#include <Memory.h>
#include <Sound.h>

namespace
{
  const Rect kQuitBounds = {100, 264, 120, 332};

  pascal Boolean QuitFilter(DialogPtr dialog, EventRecord *event, short *item)
  {
    if (event->what == updateEvt
        && static_cast<unsigned long>(event->message) == reinterpret_cast<unsigned long>(dialog))
    {
      // Draw the standard default outline inside the update clip, after items.
      BeginUpdate(reinterpret_cast<WindowPtr>(dialog));
      DrawDialog(dialog);
      loka::toolbox::DrawDialogDefaultOutline(dialog, kQuitBounds);
      EndUpdate(reinterpret_cast<WindowPtr>(dialog));
      return false;
    }
    if (event->what == keyDown || event->what == autoKey)
    {
      const unsigned char key = event->message & charCodeMask;
      if (key == 13 || key == 3)
      {
        *item = 1;
        return true;
      }
    }
    return false;
  }

} // namespace

// Out of line on purpose: these read the QuickDraw and low-memory globals
// (`qd`, the menu bar height), which the Multiversal interfaces declare with
// unknown bounds; inlined into a caller they trip -Warray-bounds. The
// out-of-memory dialog and the confirm dialog share this one definition.
namespace loka
{
  namespace toolbox
  {
    /** Alert placement: centered horizontally, one third down the work area. */
    Rect CenterDialogBounds(short width, short height)
    {
      const Rect &screen = qd.screenBits.bounds;
      const short workTop = static_cast<short>(screen.top + GetMBarHeight());
      const short left = static_cast<short>((screen.left + screen.right - width) / 2);
      const short top = static_cast<short>(workTop + (screen.bottom - workTop - height) / 3);
      const Rect bounds = {top, left, static_cast<short>(top + height), static_cast<short>(left + width)};
      return bounds;
    }
    /** Preserve the caller's port and pen while drawing the default button ring. */
    void DrawDialogDefaultOutline(DialogPtr dialog, const Rect &button)
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
    void RunEmergencyQuitDialog(const char *message, std::size_t length)
    {
      const unsigned char textLength = static_cast<unsigned char>(length > 255 ? 255 : length);
      unsigned char bytes[2 + 18 + 14 + 256 + 16];
      unsigned char *out = bytes;
      loka::toolbox::PutDialogWord(out, 2); // count minus one
      loka::toolbox::PutDialogItem(
          out, kQuitBounds, ctrlItem + btnCtrl, reinterpret_cast<const unsigned char *>("Quit"), 4);
      const Rect textBounds = {20, 64, 88, 332};
      loka::toolbox::PutDialogItem(
          out, textBounds, statText + itemDisable, reinterpret_cast<const unsigned char *>(message), textLength);
      const Rect iconBounds = {20, 20, 52, 52};
      const unsigned char stopIcon[] = {0, 0}; // System ICON resource 0
      loka::toolbox::PutDialogItem(out, iconBounds, iconItem + itemDisable, stopIcon, 2);
      Handle items = NewHandle(out - bytes);
      if (!items)
        return;
      std::memcpy(*items, bytes, out - bytes);
      ModalFilterUPP filter = NewModalFilterUPP(QuitFilter);
      if (!filter)
      {
        DisposeHandle(items);
        return;
      }
      // Centre horizontally and place a third of the way down the work area, as
      // the Dialog Manager places alerts; items are in dialog-local coordinates.
      const Rect bounds = loka::toolbox::CenterDialogBounds(352, 140);
      const unsigned char title[] = {0};
      DialogPtr dialog = NewDialog(0, &bounds, title, true, dBoxProc, reinterpret_cast<WindowPtr>(-1), false, 0, items);
      if (!dialog)
      {
        DisposeModalFilterUPP(filter);
        DisposeHandle(items);
        return;
      }
      // NewDialog transfers the item list to the dialog; DisposeDialog owns it.
      loka::toolbox::DrawDialogDefaultOutline(dialog, kQuitBounds);
      SysBeep(1);
      short item = 0;
      do
      {
        ModalDialog(filter, &item);
      } while (item != 1);
      DisposeModalFilterUPP(filter);
      DisposeDialog(dialog);
    }
  } // namespace toolbox
} // namespace loka
