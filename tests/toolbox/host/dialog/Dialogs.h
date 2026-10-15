#ifndef LOKA_CONFIRM_HOST_DIALOGS_H
#define LOKA_CONFIRM_HOST_DIALOGS_H
#include "Quickdraw.h"
#include "Files.h"
typedef char **Handle;
typedef unsigned char Boolean;
typedef GrafPtr WindowPtr;
typedef GrafPtr DialogPtr;
struct BitMap { Rect bounds; };
struct QDGlobals { BitMap screenBits; };
extern QDGlobals qd;
short GetMBarHeight();
struct EventRecord { short what; unsigned long message; unsigned short modifiers; };
typedef Boolean (*ModalFilterUPP)(DialogPtr, EventRecord *, short *);
#define pascal
enum { ctrlItem = 4, btnCtrl = 0, statText = 8, itemDisable = 128,
       dBoxProc = 1, keyDown = 3, autoKey = 5, updateEvt = 6, charCodeMask = 255, cmdKey = 256 };
Handle NewHandle(long);
void DisposeHandle(Handle);
DialogPtr NewDialog(void *, const Rect *, const unsigned char *, Boolean, short, WindowPtr, Boolean, long, Handle);
void DisposeDialog(DialogPtr);
void ModalDialog(ModalFilterUPP, short *);
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
