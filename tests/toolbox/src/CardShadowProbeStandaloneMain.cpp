#include <Dialogs.h>
#include <Fonts.h>
#include <Menus.h>
#include <Quickdraw.h>
#include <TextEdit.h>
#include <Windows.h>
#include <Processes.h>
#include <cstdio>

#include "ToolboxPaintGradient.hpp"
#include "core/io/File.hpp"
#include "platform/file/AppLocation.hpp"
#include "platform/file/FileIO.hpp"

namespace
{
  enum { kWidth = 432, kHeight = 280 };

  void FillCard(const Rect &rect, short oval, const Pattern &pattern)
  {
    if (oval)
      FillRoundRect(&rect, oval, oval, &pattern);
    else
      FillRect(&rect, &pattern);
  }

  void DrawCard(short left, short top, short oval, short depth,
                const unsigned char *levels, ConstStr255Param label,
                ConstStr255Param caption)
  {
    Rect card;
    SetRect(&card, left, top, left + 120, top + 72);
    // Outer bands first; nearer silhouettes overwrite them with darker ink.
    for (short k = depth; k > 0; --k)
    {
      Rect shadow = card;
      OffsetRect(&shadow, k, k);
      FillCard(shadow, oval, levels ? loka::toolbox::kToolboxGradientPatterns[levels[k - 1]] : qd.gray);
      if (!levels)
        break; // The gray variant has one silhouette, at the full offset.
    }
    if (oval)
    {
      EraseRoundRect(&card, oval, oval);
      FrameRoundRect(&card, oval, oval);
    }
    else
    {
      EraseRect(&card);
      FrameRect(&card);
    }
    MoveTo(left + 12, top + 39);
    DrawString(label);
    MoveTo(left, top + 94);
    DrawString(caption);
  }

  /** Main calls once while the window is frontmost and the cursor is hidden.
      CopyBits reads the screen through QuickDraw, which handles the 32-bit
      NuBus frame buffer on a Mac II in 24-bit mode; a direct read does not. */
  bool DumpContent()
  {
    const short rowBytes = kWidth / 8;
    BitMap shot;
    shot.baseAddr = NewPtrClear(static_cast<long>(rowBytes) * kHeight);
    if (!shot.baseAddr)
      return false;
    shot.rowBytes = rowBytes;
    SetRect(&shot.bounds, 0, 0, kWidth, kHeight);
    Rect content;
    SetRect(&content, 0, 0, kWidth, kHeight);
    CopyBits(&qd.thePort->portBits, &shot, &content, &shot.bounds, srcCopy, 0);

    loka::platform::file::FileHandle destination;
    bool ok = loka::platform::file::ResolveApplicationSidecar(
        loka::file::File::Application() << loka::file::File("LOG.TXT"), destination);
    std::FILE *file = ok ? loka::platform::file::OpenWriteTruncate(destination) : 0;
    if (file)
    {
      const char header[] = "P4\n432 280\n";
      ok = std::fwrite(header, 1, sizeof(header) - 1, file) == sizeof(header) - 1;
      if (ok)
        ok = std::fwrite(shot.baseAddr, 1, static_cast<size_t>(rowBytes) * kHeight, file)
             == static_cast<size_t>(rowBytes) * kHeight;
      if (!loka::platform::file::FlushWrite(file, destination))
        ok = false;
      if (std::fclose(file) != 0)
        ok = false;
    }
    else
      ok = false;
    DisposePtr(shot.baseAddr);
    return ok;
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

  Rect bounds;
  SetRect(&bounds, 32, 60, 32 + kWidth, 60 + kHeight);
  WindowPtr window = NewWindow(0, &bounds, "\pCard shadow probe", true, documentProc,
                               reinterpret_cast<WindowPtr>(-1L), false, 0);
  if (!window)
  {
    ExitToShell();
    return 1;
  }
  SetPort(window);
  HideCursor();
  ForeColor(blackColor);
  BackColor(whiteColor);
  BackPat(&qd.white);
  PenNormal();
  TextFont(0); // Chicago on the intended System 7 rig.
  TextSize(12);
  TextMode(srcOr);
  Rect content;
  SetRect(&content, 0, 0, kWidth, kHeight);
  EraseRect(&content);

  const unsigned char solid[] = {8};
  const unsigned char three[] = {6, 4, 2};
  const unsigned char four[] = {6, 5, 3, 2};
  DrawCard(16, 18, 0, 1, solid, "\pSquare", "\pa 1px");
  DrawCard(156, 18, 0, 3, three, "\pSquare", "\pb 3-step");
  DrawCard(296, 18, 0, 4, four, "\pSquare", "\pc 4-step");
  DrawCard(16, 150, 12, 3, three, "\pRound 12", "\pd R12 3-step");
  DrawCard(156, 150, 16, 3, three, "\pRound 16", "\pe R16 3-step");
  DrawCard(296, 150, 12, 2, 0, "\pRound 12", "\pf R12 gray 2px");

  const bool dumped = DumpContent();
  ShowCursor();
  DisposeWindow(window);
  ExitToShell();
  return dumped ? 0 : 1;
}
