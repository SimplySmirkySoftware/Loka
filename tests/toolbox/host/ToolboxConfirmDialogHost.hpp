#ifndef LOKA_TOOLBOX_CONFIRM_DIALOG_HOST_HPP
#define LOKA_TOOLBOX_CONFIRM_DIALOG_HOST_HPP
#include <Dialogs.h>
#include <cstdlib>

static unsigned confirmNewCalls, confirmModalCalls, confirmDisposals, confirmLiveHandles;
static unsigned refuseConfirmDialogs, refuseConfirmItems, refuseConfirmFilters;
ModalFilterUPP NewModalFilterUPP(ModalFilterUPP filter)
{
  if (refuseConfirmFilters) { --refuseConfirmFilters; return 0; }
  return filter;
}
void DisposeModalFilterUPP(ModalFilterUPP filter) { LOKA_VERIFY(filter != 0); }
static unsigned *confirmWritesDuringModal;
static unsigned confirmExpectedWrites;
static short confirmItem = 1;
static Handle confirmItems;
static long confirmItemsSize;
static std::string confirmText;
static GrafPort confirmPort;
QDGlobals qd = {{0, 0, {0, 0, 342, 512}}};
short GetMBarHeight() { return 20; }
Handle NewHandle(long size)
{
  if (refuseConfirmItems) { --refuseConfirmItems; return 0; }
  Handle handle = new char *;
  *handle = static_cast<char *>(std::malloc(size));
  LOKA_VERIFY(*handle != 0);
  ++confirmLiveHandles;
  confirmItemsSize = size;
  return handle;
}
void DisposeHandle(Handle handle)
{
  LOKA_VERIFY(confirmLiveHandles == 1);
  --confirmLiveHandles;
  std::free(*handle);
  delete handle;
}
static unsigned DialogWord(const unsigned char *bytes) { return (bytes[0] << 8) | bytes[1]; }
DialogPtr NewDialog(void *, const Rect *bounds, const unsigned char *title, Boolean visible,
    short proc, WindowPtr behind, Boolean closeBox, long ref, Handle items)
{
  ++confirmNewCalls;
  LOKA_VERIFY(title[0] == 0 && visible && proc == dBoxProc && !closeBox && ref == 0);
  LOKA_VERIFY(behind == reinterpret_cast<WindowPtr>(-1));
  LOKA_VERIFY(bounds->left == 80 && bounds->right == 432 && bounds->top == 80 && bounds->bottom == 220);
  const unsigned char *p = reinterpret_cast<const unsigned char *>(*items);
  const unsigned char *end = p + confirmItemsSize;
  LOKA_VERIFY(DialogWord(p) == 4);
  p += 2;
  const char *labels[] = {"Save", "Cancel", "Don't Save"};
  const short lefts[] = {264, 176, 20};
  for (unsigned i = 0; i < 3; ++i)
  {
    LOKA_VERIFY(p + 14 <= end && p + 14 + p[13] <= end);
    LOKA_VERIFY(DialogWord(p) == 0 && DialogWord(p + 2) == 0);
    LOKA_VERIFY(DialogWord(p + 4) == 100 && DialogWord(p + 6) == static_cast<unsigned>(lefts[i]));
    LOKA_VERIFY(p[12] == ctrlItem + btnCtrl);
    LOKA_VERIFY(std::string(reinterpret_cast<const char *>(p + 14), p[13]) == labels[i]);
    p += 14 + ((p[13] + 1) & ~1);
  }
  LOKA_VERIFY(p + 14 <= end && p + 14 + p[13] <= end);
  LOKA_VERIFY(p[12] == statText + itemDisable);
  LOKA_VERIFY(DialogWord(p + 4) == 20 && DialogWord(p + 6) == 64);
  confirmText.assign(reinterpret_cast<const char *>(p + 14), p[13]);
  LOKA_VERIFY(confirmText == "Save Caf\x8e?" || confirmText == "Newest");
  p += 14 + ((p[13] + 1) & ~1);
  // The caution icon (System ICON 2) at the OOM alert's icon position.
  LOKA_VERIFY(p + 16 <= end && p[12] == iconItem + itemDisable && p[13] == 2 && DialogWord(p + 14) == 2);
  LOKA_VERIFY(DialogWord(p + 4) == 20 && DialogWord(p + 6) == 20 && DialogWord(p + 8) == 52
              && DialogWord(p + 10) == 52);
  p += 16;
  LOKA_VERIFY(p == end);
  if (refuseConfirmDialogs) { --refuseConfirmDialogs; return 0; }
  confirmItems = items;
  return &confirmPort;
}
void DisposeDialog(DialogPtr dialog)
{
  LOKA_VERIFY(dialog == &confirmPort);
  ++confirmDisposals;
  DisposeHandle(confirmItems);
  confirmItems = 0;
}
void GetPort(GrafPtr *port) { *port = &confirmPort; }
void SetPort(GrafPtr) {}
void GetPenState(PenState *) {}
void SetPenState(const PenState *) {}
void PenNormal() {}
void PenSize(short x, short y) { LOKA_VERIFY(x == 3 && y == 3); }
void InsetRect(Rect *r, short x, short y)
{ r->left += x; r->right -= x; r->top += y; r->bottom -= y; }
void FrameRoundRect(const Rect *r, short x, short y)
{
  LOKA_VERIFY(x == 16 && y == 16);
  LOKA_VERIFY(r->top == 96 && r->left == 260 && r->bottom == 124 && r->right == 336);
}
void DrawDialog(DialogPtr) {}
void BeginUpdate(WindowPtr) {}
void EndUpdate(WindowPtr) {}
void ModalDialog(ModalFilterUPP filter, short *item)
{
  ++confirmModalCalls;
  LOKA_VERIFY(confirmWritesDuringModal && *confirmWritesDuringModal == confirmExpectedWrites);
  EventRecord event = {updateEvt, reinterpret_cast<unsigned long>(&confirmPort), 0};
  LOKA_VERIFY(!filter(&confirmPort, &event, item));
  const unsigned char keys[] = {13, 3, 27, '.', '.', 'x', 'd', 'D', 'd'};
  const unsigned short modifiers[] = {0, 0, 0, cmdKey, 0, 0, cmdKey, cmdKey, 0};
  const short expected[] = {1, 1, 2, 2, 0, 0, 3, 3, 0};
  for (unsigned kind = 0; kind != 2; ++kind)
    for (unsigned i = 0; i != 9; ++i)
    {
      event.what = kind ? autoKey : keyDown;
      event.message = keys[i]; event.modifiers = modifiers[i]; *item = 0;
      const Boolean consumed = filter(&confirmPort, &event, item);
      LOKA_VERIFY(*item == expected[i] && !!consumed == !!expected[i]);
    }
  if (duringModal) duringModal();
  LOKA_VERIFY(*confirmWritesDuringModal == confirmExpectedWrites);
  *item = confirmItem;
}
#endif
