#include "ToolboxOutOfMemory.hpp"
#include "ToolboxDialogConstruction.hpp"
#include "ToolboxReserveGrowZone.hpp"
#include <Dialogs.h>
#include <Events.h>
#include <LowMem.h>
#include <Memory.h>
#include <Processes.h>
#include <Quickdraw.h>
#include <Sound.h>
#include <cstring>

namespace {
const long kOutOfMemoryReserveBytes = 8 * 1024;
/** Process-owned reserve and its one-way release/reporting phases. */
class OutOfMemoryReserve
{
public:
  OutOfMemoryReserve() : block_(0), phase_(UNARMED) {}

  void arm()
  {
    this->block_ = NewPtr(kOutOfMemoryReserveBytes);
    this->phase_ = this->block_ ? HELD : UNARMED;
  }
  bool held() const { return this->phase_ == HELD; }
  bool spentForSystem() const { return this->phase_ == RELEASED_FOR_SYSTEM; }
  long releaseForSystem(Size cbNeeded)
  {
    if (!this->held() || cbNeeded > kOutOfMemoryReserveBytes) return 0;
    this->release();
    this->phase_ = RELEASED_FOR_SYSTEM;
    return kOutOfMemoryReserveBytes;
  }
  bool beginReporting()
  {
    switch (this->phase_)
    {
      case HELD:
        this->release();
        break;
      case RELEASED_FOR_SYSTEM:
        break;
      case UNARMED:
      case REPORTING:
        return false;
    }
    this->phase_ = REPORTING;
    return true;
  }
private:
  enum Phase { UNARMED, HELD, RELEASED_FOR_SYSTEM, REPORTING };
  Ptr block_;
  Phase phase_;
  void release()
  {
    DisposePtr(this->block_);
    this->block_ = 0;
  }
  OutOfMemoryReserve(const OutOfMemoryReserve &);
  OutOfMemoryReserve &operator=(const OutOfMemoryReserve &);
};
OutOfMemoryReserve gOutOfMemoryReserve;

pascal long ReleaseReserveForSystem(Size cbNeeded)
{
  // Retro68 68K globals use absolute addresses, not A5-relative storage.
  // No SetCurrentA5 is needed; the UPP also supplies the PPC CFM entry.
  return gOutOfMemoryReserve.releaseForSystem(cbNeeded);
}
const Rect kQuitBounds = {100, 264, 120, 332};

pascal Boolean QuitFilter(DialogPtr dialog, EventRecord *event, short *item)
{
  if (event->what == updateEvt &&
      static_cast<unsigned long>(event->message) == reinterpret_cast<unsigned long>(dialog))
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

void ShowOutOfMemoryDialog()
{
  const char prefix[] = "Not enough memory to keep ";
  const char suffix[] = " running. In the Finder, use Get Info to raise its memory size.";
  unsigned char text[255];
  unsigned char name[32];
  BlockMoveData(LMGetCurApName(), name, sizeof(name));
  const unsigned int fixedLength = sizeof(prefix) + sizeof(suffix) - 2;
  unsigned int nameLength = name[0];
  if (nameLength > sizeof(name) - 1) nameLength = sizeof(name) - 1;
  if (nameLength > sizeof(text) - fixedLength)
    nameLength = sizeof(text) - fixedLength;
  std::memcpy(text, prefix, sizeof(prefix) - 1);
  std::memcpy(text + sizeof(prefix) - 1, name + 1, nameLength);
  std::memcpy(text + sizeof(prefix) - 1 + nameLength, suffix, sizeof(suffix) - 1);
  const unsigned char textLength = static_cast<unsigned char>(fixedLength + nameLength);

  unsigned char bytes[2 + 18 + 14 + 256 + 16];
  unsigned char *out = bytes;
  loka::toolbox::PutDialogWord(out, 2); // count minus one
  loka::toolbox::PutDialogItem(out, kQuitBounds, ctrlItem + btnCtrl,
          reinterpret_cast<const unsigned char *>("Quit"), 4);
  const Rect textBounds = {20, 64, 88, 332};
  loka::toolbox::PutDialogItem(out, textBounds, statText + itemDisable, text, textLength);
  const Rect iconBounds = {20, 20, 52, 52};
  const unsigned char stopIcon[] = {0, 0}; // System ICON resource 0
  loka::toolbox::PutDialogItem(out, iconBounds, iconItem + itemDisable, stopIcon, 2);
  Handle items = NewHandle(out - bytes);
  if (!items) return;
  std::memcpy(*items, bytes, out - bytes);
  // Centre horizontally and place a third of the way down the work area, as
  // the Dialog Manager places alerts; items are in dialog-local coordinates.
  const Rect bounds = loka::toolbox::CenterDialogBounds(352, 140);
  const unsigned char title[] = {0};
  DialogPtr dialog = NewDialog(0, &bounds, title, true, dBoxProc,
                              reinterpret_cast<WindowPtr>(-1), false, 0, items);
  if (!dialog)
  {
    DisposeHandle(items);
    return;
  }
  // NewDialog transfers the item list to the dialog; DisposeDialog owns it.
  ModalFilterUPP filter = NewModalFilterUPP(QuitFilter);
  loka::toolbox::DrawDialogDefaultOutline(dialog, kQuitBounds);
  SysBeep(1);
  short item = 0;
  do { ModalDialog(filter, &item); } while (item != 1);
  DisposeModalFilterUPP(filter);
  DisposeDialog(dialog);
}
}

namespace loka { namespace toolbox {
void ArmOutOfMemoryReserve()
{
  gOutOfMemoryReserve.arm();
  if (gOutOfMemoryReserve.held())
  {
    InstallReserveGrowZone(NewGrowZoneUPP(ReleaseReserveForSystem));
  }
}

void QuitIfOutOfMemoryReserveSpent()
{
  if (gOutOfMemoryReserve.spentForSystem()) QuitForOutOfMemory();
}

void QuitForOutOfMemory()
{
  if (gOutOfMemoryReserve.beginReporting())
    ShowOutOfMemoryDialog();
  ExitToShell();
#if defined(LOKA_RETRO68)
  __builtin_unreachable();
#endif
}
} }
