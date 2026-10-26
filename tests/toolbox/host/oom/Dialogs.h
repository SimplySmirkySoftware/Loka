#ifndef LOKA_OOM_HOST_DIALOGS_H
#define LOKA_OOM_HOST_DIALOGS_H
#include <cstddef>
typedef char *Ptr;
typedef char **Handle;
typedef unsigned char Boolean;
typedef long Size;
struct Rect { short top, left, bottom, right; };
struct Port {};
struct BitMap { Rect bounds; };
struct QDGlobals { BitMap screenBits; };
extern QDGlobals qd;
short GetMBarHeight();
typedef Port *GrafPtr;
typedef Port *WindowPtr;
typedef Port *DialogPtr;
struct PenState { short ignored; };
struct EventRecord { short what; unsigned long message; };
typedef Boolean (*ModalFilterUPP)(DialogPtr, EventRecord *, short *);
#define pascal
enum { ctrlItem = 4, btnCtrl = 0, statText = 8, iconItem = 32, itemDisable = 128,
       dBoxProc = 1, keyDown = 3, autoKey = 5, updateEvt = 6, charCodeMask = 255 };
void BlockMoveData(const void *, void *, Size);
Ptr NewPtr(Size);
void DisposePtr(Ptr);
Handle NewHandle(Size);
void DisposeHandle(Handle);
unsigned char *LMGetCurApName();
DialogPtr NewDialog(void *, const Rect *, const unsigned char *, Boolean, short, WindowPtr, Boolean, long, Handle);
void DisposeDialog(DialogPtr);
void ModalDialog(ModalFilterUPP, short *);
void SysBeep(short);
void ExitToShell();
void GetPort(GrafPtr *);
void SetPort(GrafPtr);
void GetPenState(PenState *);
void SetPenState(const PenState *);
void PenNormal();
void PenSize(short, short);
void InsetRect(Rect *, short, short);
void FrameRoundRect(const Rect *, short, short);
void DrawDialog(DialogPtr);
void BeginUpdate(WindowPtr);
void EndUpdate(WindowPtr);
ModalFilterUPP NewModalFilterUPP(ModalFilterUPP);
void DisposeModalFilterUPP(ModalFilterUPP);
#endif
