// MLTE probe (branch probe/mlte-crlf, never merged). Observes, on a real
// Mac OS 9 machine, what Textension does with CR / LF / CRLF in 'utxt' data,
// how Return / paste / Kotoeri commit reach the buffer, the MLTE version, and
// the heap peak of a 1 Mi-character document. Results go to PROBE.TXT beside
// the application; every change of the buffer during the interactive phase is
// logged from the idle loop, so no key other than the ones under test is needed.
#include <Dialogs.h>
#include <Events.h>
#include <Fonts.h>
#include <Gestalt.h>
#include <MacTextEditor.h>
#include <MacWindows.h>
#include <Memory.h>
#include <Menus.h>
#include <Quickdraw.h>
#include <StandardFile.h>
#include <TextEdit.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

namespace
{
  FILE *gLog = 0;
  TXNObject gTxn = 0;
  TXNFrameID gFrame = 0;
  WindowPtr gWindow = 0;
  // P2 / P3 (page 2e): a second window with its own object, so a click on
  // another window of the same application reaches TXNFocus(false) here.
  TXNObject gTxn2 = 0;
  TXNFrameID gFrame2 = 0;
  WindowPtr gWindow2 = 0;
  bool gQuit = false;
  ByteCount gLastSize = 0;
  ItemCount gLastChange = 0;
  enum
  {
    kMenuApple = 128,
    kMenuFile = 129,
    kMenuEdit = 130
  };
  enum
  {
    kFileQuit = 1
  };
  enum
  {
    kEditCut = 1,
    kEditCopy = 2,
    kEditPaste = 3,
    kEditSelectAll = 4,
    kEditDump = 6,
    kEditModal = 7,
    kEditGetFile = 8
  };

  void logf(const char *fmt, ...)
  {
    if (!gLog)
      return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(gLog, fmt, ap);
    va_end(ap);
    fputc('\r', gLog);
  }

  // Hex of the first `limit` UTF-16 units of the buffer, plus size and line count.
  void dumpObject(const char *tag, int limit, TXNObject obj)
  {
    const ByteCount size = TXNDataSize(obj);
    ItemCount lines = 0;
    const OSStatus lineStatus = TXNGetLineCount(obj, &lines);
    TXNOffset selStart = 0, selEnd = 0;
    TXNGetSelection(obj, &selStart, &selEnd);
    Handle h = 0;
    const OSStatus getStatus = TXNGetData(obj, kTXNStartOffset, kTXNEndOffset, &h);
    logf("%s size=%lu lines=%lu(st=%ld) sel=%lu..%lu get=%ld hsize=%ld change=%lu",
         tag,
         (unsigned long)size,
         (unsigned long)lines,
         (long)lineStatus,
         (unsigned long)selStart,
         (unsigned long)selEnd,
         (long)getStatus,
         h ? (long)GetHandleSize(h) : -1L,
         (unsigned long)TXNGetChangeCount(obj));
    if (h)
    {
      const long bytes = GetHandleSize(h);
      const long units = bytes / 2;
      HLock(h);
      const unsigned char *p = (const unsigned char *)*h;
      char line[512];
      int n = 0;
      n += sprintf(line + n, "  hex(%ld units%s):", units, (bytes & 1) ? ", ODD BYTE COUNT" : "");
      for (long i = 0; i < units && i < limit; ++i)
        n += sprintf(line + n, " %02X%02X", p[2 * i], p[2 * i + 1]);
      if (units > limit)
        sprintf(line + n, " ...");
      logf("%s", line);
      HUnlock(h);
      DisposeHandle(h);
    }
  }

  void dumpBuffer(const char *tag, int limit)
  {
    dumpObject(tag, limit, gTxn);
  }

  // P2 / P3: read the first object immediately before and after TXNFocus(false),
  // then once more after one TXNIdle, so a commit or a discard of a pending
  // Kotoeri composition shows as a size / change-count step at a named point.
  void probeFocusOff(const char *why)
  {
    char tag[64];
    sprintf(tag, "P-PRE %s", why);
    dumpBuffer(tag, 32);
    TXNFocus(gTxn, false);
    sprintf(tag, "P-FOCUSOFF %s", why);
    dumpBuffer(tag, 32);
    TXNIdle(gTxn);
    sprintf(tag, "P-IDLE %s", why);
    dumpBuffer(tag, 32);
  }

  void probeFocusOn(const char *why)
  {
    TXNFocus(gTxn, true);
    char tag[64];
    sprintf(tag, "P-FOCUSON %s", why);
    dumpBuffer(tag, 32);
  }

  void heapLine(const char *tag)
  {
    logf("  heap %s free=%ld maxblock=%ld", tag, (long)FreeMem(), (long)MaxBlock());
  }

  OSStatus setUnits(const UniChar *units, ByteCount count)
  {
    return TXNSetData(gTxn, kTXNUnicodeTextData, (void *)units, count * 2, kTXNStartOffset, kTXNEndOffset);
  }

  void caseSet(const char *name, const UniChar *units, ByteCount count)
  {
    const OSStatus st = setUnits(units, count);
    char tag[64];
    sprintf(tag, "%s set=%ld in=%lu units", name, (long)st, (unsigned long)count);
    dumpBuffer(tag, 32);
  }

  void caseSelect(const char *name, TXNOffset at)
  {
    const OSStatus st = TXNSetSelection(gTxn, at, at);
    TXNOffset s = 0, e = 0;
    TXNGetSelection(gTxn, &s, &e);
    logf("%s setsel(%lu)=%ld -> %lu..%lu", name, (unsigned long)at, (long)st, (unsigned long)s, (unsigned long)e);
  }

  void automaticCases()
  {
    static const UniChar l1[] = {'A', 0x000D, 'B'};
    static const UniChar l2[] = {'A', 0x000A, 'B'};
    static const UniChar l3[] = {'A', 0x000D, 0x000A, 'B'};
    static const UniChar l4[] = {'A', 0x000D};
    static const UniChar l6[] = {'A', 0x000D, 'B', 0x000D, 'C'};
    static const UniChar l8[] = {0x65E5, 0x672C, 0x000D, 0x8A9E};
    static const UniChar l9[] = {0xD83D, 0xDE00, 0x000D, 'A'};
    caseSet("L1 A CR B", l1, 3);
    caseSet("L2 A LF B", l2, 3);
    caseSet("L3 A CRLF B", l3, 4);
    caseSet("L4 A CR", l4, 2);
    caseSet("L5 empty", l1, 0);
    caseSet("L6 A CR B CR C", l6, 5);
    caseSelect("L6", 2);
    caseSelect("L6", 3);
    caseSelect("L6", 4);
    caseSet("L7 A CRLF B", l3, 4);
    caseSelect("L7", 2);
    caseSelect("L7", 3);
    caseSet("L8 nihon CR go", l8, 4);
    caseSet("L9 U+1F600 CR A", l9, 4);

    // H1: 32,768 rows x (31 ASCII + CR) = 1 Mi units = 2 MiB of UTF-16.
    heapLine("H1 before alloc");
    const long rows = 32768, width = 32;
    const long units = rows * width;
    Ptr buf = NewPtr(units * 2);
    if (!buf)
    {
      logf("H1 NewPtr(%ld) failed", units * 2);
      return;
    }
    UniChar *u = (UniChar *)buf;
    for (long r = 0; r < rows; ++r)
    {
      for (long c = 0; c < width - 1; ++c)
        u[r * width + c] = (UniChar)('a' + ((r + c) % 26));
      u[r * width + width - 1] = 0x000D;
    }
    heapLine("H1 after alloc, before set");
    const OSStatus st = setUnits(u, units);
    heapLine("H1 after set");
    logf("H1 set=%ld", (long)st);
    DisposePtr(buf);
    heapLine("H1 after source freed");
    dumpBuffer("H1", 8); // dumpBuffer takes and frees the capture Handle
    heapLine("H1 after capture freed");

    // H2: select all, copy, paste at the end (document doubles).
    TXNSelectAll(gTxn);
    const OSStatus copyStatus = TXNCopy(gTxn);
    TXNSetSelection(gTxn, kTXNEndOffset, kTXNEndOffset);
    heapLine("H2 before paste");
    const OSStatus pasteStatus = TXNPaste(gTxn);
    heapLine("H2 after paste");
    logf("H2 copy=%ld paste=%ld pastable=%d", (long)copyStatus, (long)pasteStatus, (int)TXNIsScrapPastable());
    dumpBuffer("H2", 8);
    heapLine("H2 after capture freed");

    setUnits(l1, 0);
    dumpBuffer("cleared for K1..K3", 8);
  }

  void drawInstructions()
  {
    SetPort(gWindow);
    MoveTo(8, 14);
    DrawString("\pP2: compose, click win2 / other app.  P3: Cmd-M dialog / Cmd-G getfile / close box");
  }

  void doMenu(long choice)
  {
    const short menu = (short)((choice >> 16) & 0xFFFF), item = (short)(choice & 0xFFFF);
    if (menu == kMenuFile && item == kFileQuit)
      gQuit = true;
    else if (menu == kMenuEdit)
    {
      OSStatus st = noErr;
      switch (item)
      {
      case kEditCut:
        st = TXNCut(gTxn);
        break;
      case kEditCopy:
        st = TXNCopy(gTxn);
        break;
      case kEditPaste:
        logf("K2 paste pastable=%d", (int)TXNIsScrapPastable());
        st = TXNPaste(gTxn);
        break;
      case kEditSelectAll:
        TXNSelectAll(gTxn);
        break;
      case kEditDump:
        dumpBuffer("DUMP", 96);
        break;
      case kEditModal:
      {
        // The Loka confirm dialog's shape: NewDialog with a built item list and
        // ModalDialog, no text field, no TSMTE document.
        probeFocusOff("DIALOG");
        unsigned char items[64];
        unsigned char *o = items;
        *o++ = 0; *o++ = 0; // one item (count minus one)
        *o++ = 0; *o++ = 0; *o++ = 0; *o++ = 0; // item handle placeholder
        const Rect ok = {60, 120, 80, 200};
        memcpy(o, &ok, 8); o += 8;
        *o++ = ctrlItem + btnCtrl;
        *o++ = 2; *o++ = 'O'; *o++ = 'K';
        Handle h = NewHandle(o - items);
        memcpy(*h, items, o - items);
        Rect b;
        SetRect(&b, 160, 140, 480, 240);
        DialogPtr d = NewDialog(0, &b, "\p", true, dBoxProc, (WindowPtr)-1L, false, 0, h);
        short item = 0;
        do { ModalDialog(0, &item); } while (item != 1);
        DisposeDialog(d);
        dumpBuffer("P-POST-DIALOG", 32);
        probeFocusOn("DIALOG");
        break;
      }
      case kEditGetFile:
      {
        probeFocusOff("MODAL");
        StandardFileReply reply;
        StandardGetFile(0, -1, 0, &reply);
        dumpBuffer("P-POST-MODAL", 32);
        probeFocusOn("MODAL");
        break;
      }
      }
      if (st != noErr)
        logf("edit item %d status=%ld", (int)item, (long)st);
    }
    HiliteMenu(0);
  }

  void buildMenus()
  {
    MenuHandle apple = NewMenu(kMenuApple, "\p\024");
    AppendResMenu(apple, 'DRVR');
    InsertMenu(apple, 0);
    MenuHandle file = NewMenu(kMenuFile, "\pFile");
    AppendMenu(file, "\pQuit/Q");
    InsertMenu(file, 0);
    MenuHandle edit = NewMenu(kMenuEdit, "\pEdit");
    AppendMenu(edit, "\pCut/X;Copy/C;Paste/V;Select All/A;(-;Dump/D;Modal/M;GetFile/G");
    InsertMenu(edit, 0);
    DrawMenuBar();
  }

  void handleEvent(const EventRecord &ev)
  {
    switch (ev.what)
    {
    case nullEvent:
      TXNIdle(gTxn);
      if (gTxn2)
        TXNIdle(gTxn2);
      {
        const ByteCount size = TXNDataSize(gTxn);
        const ItemCount change = TXNGetChangeCount(gTxn);
        if (size != gLastSize || change != gLastChange)
        {
          gLastSize = size;
          gLastChange = change;
          dumpBuffer("CHG", 96);
        }
      }
      break;
    case keyDown:
    case autoKey:
      if (ev.modifiers & cmdKey)
        doMenu(MenuKey((char)(ev.message & charCodeMask)));
      else
        TXNKeyDown(gTxn, &ev);
      break;
    case mouseDown:
    {
      WindowPtr w = 0;
      switch (FindWindow(ev.where, &w))
      {
      case inMenuBar:
        doMenu(MenuSelect(ev.where));
        break;
      case inContent:
        if (w != FrontWindow())
          SelectWindow(w);
        else if (w == gWindow)
          TXNClick(gTxn, &ev);
        else if (w == gWindow2 && gTxn2)
          TXNClick(gTxn2, &ev);
        break;
      case inDrag:
        DragWindow(w, ev.where, &qd.screenBits.bounds);
        break;
      case inGoAway:
        if (TrackGoAway(w, ev.where) && w == gWindow)
        {
          probeFocusOff("CLOSE");
          gQuit = true;
        }
        break;
      case inSysWindow:
        SystemClick(&ev, w);
        break;
      }
      break;
    }
    case updateEvt:
      if ((WindowPtr)ev.message == gWindow)
      {
        BeginUpdate(gWindow);
        TXNUpdate(gTxn);
        drawInstructions();
        EndUpdate(gWindow);
      }
      else if ((WindowPtr)ev.message == gWindow2 && gTxn2)
      {
        BeginUpdate(gWindow2);
        TXNUpdate(gTxn2);
        EndUpdate(gWindow2);
      }
      break;
    case activateEvt:
      if ((WindowPtr)ev.message == gWindow)
      {
        const bool active = (ev.modifiers & activeFlag) != 0;
        if (active)
          probeFocusOn("ACTIVATE");
        else
          probeFocusOff("DEACTIVATE");
        TXNActivate(gTxn, gFrame, active);
      }
      else if ((WindowPtr)ev.message == gWindow2 && gTxn2)
      {
        const bool active = (ev.modifiers & activeFlag) != 0;
        TXNFocus(gTxn2, active);
        TXNActivate(gTxn2, gFrame2, active);
      }
      break;
    case osEvt:
      if (((ev.message >> 24) & 0xFF) == suspendResumeMessage)
      {
        const bool resumed = (ev.message & resumeFlag) != 0;
        // K2 (re-measure): the rail form. Textension keeps a private scrap;
        // the public clipboard reaches TXNPaste only after this conversion.
        const OSStatus convertStatus = resumed ? TXNConvertFromPublicScrap() : TXNConvertToPublicScrap();
        logf("%s convert=%ld pastable=%d", resumed ? "RESUME from-public" : "SUSPEND to-public",
             (long)convertStatus, (int)TXNIsScrapPastable());
        if (resumed)
          probeFocusOn("RESUME");
        else
          probeFocusOff("SUSPEND");
        TXNActivate(gTxn, gFrame, resumed);
      }
      break;
    }
  }
} // namespace

int main()
{
  InitGraf(&qd.thePort);
  InitFonts();
  InitWindows();
  InitMenus();
  TEInit();
  InitDialogs(0);
  InitCursor();
  MaxApplZone();

  gLog = fopen("PROBE.TXT", "w");
  if (gLog)
    setvbuf(gLog, 0, _IONBF, 0);

  long sysv = 0, tsmv = 0;
  const OSErr sysErr = Gestalt(gestaltSystemVersion, &sysv);
  const OSErr tsmErr = Gestalt(gestaltTSMgrVersion, &tsmv);
  logf("ENV sysv=%lx(%d) tsmv=%lx(%d)", sysv, (int)sysErr, tsmv, (int)tsmErr);
  heapLine("start");

  const OSStatus initStatus = TXNInitTextension(0, 0, 0);
  TXNFeatureBits features = 0;
  const TXNVersionValue version = TXNVersionInformation(&features);
  logf("MLTE init=%ld version=%lx features=%lx", (long)initStatus, (unsigned long)version, (unsigned long)features);
  if (initStatus != noErr)
  {
    if (gLog)
      fclose(gLog);
    return 1;
  }

  Rect bounds;
  SetRect(&bounds, 40, 60, 600, 400);
  gWindow = NewCWindow(0, &bounds, "\pMLTE probe", true, documentProc, (WindowPtr)-1L, true, 0);
  buildMenus();
  Rect frame;
  SetRect(&frame, 4, 20, 540, 330);
  const OSStatus newStatus = TXNNewObject(0,
                                          gWindow,
                                          &frame,
                                          kTXNWantVScrollBarMask | kTXNAlwaysWrapAtViewEdgeMask,
                                          kTXNTextEditStyleFrameType,
                                          kTXNUnicodeTextFile,
                                          kTXNUnicodeEncoding,
                                          &gTxn,
                                          &gFrame,
                                          0);
  logf("TXNNewObject=%ld", (long)newStatus);
  if (newStatus != noErr || !gTxn)
  {
    if (gLog)
      fclose(gLog);
    return 1;
  }
  TXNControlTag tag = kTXNInlineStateTag;
  TXNControlData data;
  data.uValue = kTXNUseBottomline;
  logf("inline=bottomline status=%ld", (long)TXNSetTXNObjectControls(gTxn, false, 1, &tag, &data));
  TXNFocus(gTxn, true);
  TXNActivate(gTxn, gFrame, true);

  Rect bounds2;
  SetRect(&bounds2, 330, 330, 630, 470);
  gWindow2 = NewCWindow(0, &bounds2, "\pwin2", true, documentProc, (WindowPtr)-1L, true, 0);
  Rect frame2;
  SetRect(&frame2, 4, 4, 280, 120);
  const OSStatus new2 = TXNNewObject(0, gWindow2, &frame2, kTXNAlwaysWrapAtViewEdgeMask,
                                     kTXNTextEditStyleFrameType, kTXNUnicodeTextFile, kTXNUnicodeEncoding,
                                     &gTxn2, &gFrame2, 0);
  logf("TXNNewObject(win2)=%ld", (long)new2);
  if (new2 == noErr && gTxn2)
    TXNSetTXNObjectControls(gTxn2, false, 1, &tag, &data);
  else
    gTxn2 = 0;
  SelectWindow(gWindow);

  automaticCases();
  gLastSize = TXNDataSize(gTxn);
  gLastChange = TXNGetChangeCount(gTxn);
  logf("INTERACTIVE P2: compose (Kotoeri), click win2 / click another app; P3: compose, Cmd-M modal, close box; "
       "Cmd-D = dump; Cmd-Q = quit");
  drawInstructions();

  while (!gQuit)
  {
    EventRecord ev;
    WaitNextEvent(everyEvent, &ev, 6, 0);
    handleEvent(ev);
  }
  dumpBuffer("END", 96);
  heapLine("end");
  if (gTxn2)
    TXNDeleteObject(gTxn2);
  TXNDeleteObject(gTxn);
  TXNTerminateTextension();
  if (gLog)
    fclose(gLog);
  return 0;
}
