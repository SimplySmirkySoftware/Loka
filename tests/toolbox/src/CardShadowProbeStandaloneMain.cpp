#include <Dialogs.h>
#include <Fonts.h>
#include <Menus.h>
#include <Quickdraw.h>
#include <QDOffscreen.h>
#include <TextEdit.h>
#include <Timer.h>
#include <Windows.h>
#include <Processes.h>
#include <cstdio>

#include "ToolboxPaintGradient.hpp"
#include "core/io/File.hpp"
#include "platform/file/AppLocation.hpp"
#include "platform/file/FileIO.hpp"

// Page 2 of the card probe (page 1 is a6997e5d): single-pattern shadows on
// 1-bit, solid-gray shadows in an 8-bit GWorld, and the cost of painting the
// decoration through four band strips on both depths. Not for merge.
namespace
{
  enum { kWidth = 432, kHeight = 280, kReps = 40 };

  /** One silhouette: the card body offset by `offset`, inked with a pattern
      level (1-bit) or a 16-bit gray (color). Steps are drawn outer first. */
  struct Step
  {
    short offset;
    unsigned short ink;
  };

  struct Look
  {
    const Step *steps;
    short count;
    short oval;
    bool color;
  };

  void InkShape(const Rect &rect, short oval, const Step &step, bool color)
  {
    if (color)
    {
      RGBColor gray;
      gray.red = gray.green = gray.blue = step.ink;
      RGBForeColor(&gray);
      if (oval)
        PaintRoundRect(&rect, oval, oval);
      else
        PaintRect(&rect);
      return;
    }
    const Pattern &pattern = loka::toolbox::kToolboxGradientPatterns[step.ink];
    if (oval)
      FillRoundRect(&rect, oval, oval, &pattern);
    else
      FillRect(&rect, &pattern);
  }

  void DrawShadow(const Rect &body, const Look &look)
  {
    for (short i = 0; i < look.count; ++i)
    {
      Rect silhouette = body;
      OffsetRect(&silhouette, look.steps[i].offset, look.steps[i].offset);
      InkShape(silhouette, look.oval, look.steps[i], look.color);
    }
    if (look.color)
    {
      RGBColor black;
      black.red = black.green = black.blue = 0;
      RGBForeColor(&black);
    }
  }

  void DrawBody(const Rect &body, short oval)
  {
    if (oval)
    {
      EraseRoundRect(&body, oval, oval);
      FrameRoundRect(&body, oval, oval);
    }
    else
    {
      EraseRect(&body);
      FrameRect(&body);
    }
  }

  void DrawCard(short left, short top, const Look &look, ConstStr255Param label, ConstStr255Param caption)
  {
    Rect body;
    SetRect(&body, left, top, left + 120, top + 72);
    DrawShadow(body, look);
    DrawBody(body, look.oval);
    MoveTo(left + 12, top + 39);
    DrawString(label);
    MoveTo(left, top + 94);
    DrawString(caption);
  }

  // Shadow inks. 1-bit levels index kToolboxGradientPatterns (k/8 black).
  const Step kOne2[] = {{3, 2}};
  const Step kOne3[] = {{3, 3}};
  const Step kOne4[] = {{3, 4}};
  const Step kThree[] = {{3, 2}, {2, 4}, {1, 6}};
  // Grays with the same darkness as the 2/8, 4/8, 6/8 patterns.
  const Step kGrayOne[] = {{3, 0x9999}};
  const Step kGrayThree[] = {{3, 0xBBBB}, {2, 0x8888}, {1, 0x4444}};
  const Step kGraySoft[] = {{6, 0xEEEE}, {5, 0xDDDD}, {4, 0xCCCC}, {3, 0xBBBB}, {2, 0xAAAA}, {1, 0x9999}};

  void DrawPageMono()
  {
    const Look g = {kOne2, 1, 0, false};
    const Look h = {kOne3, 1, 0, false};
    const Look i = {kOne4, 1, 0, false};
    const Look j = {kOne3, 1, 12, false};
    const Look k = {kOne4, 1, 12, false};
    const Look l = {kThree, 3, 12, false};
    DrawCard(16, 18, g, "\pSquare", "\pg one 2/8");
    DrawCard(156, 18, h, "\pSquare", "\ph one 3/8");
    DrawCard(296, 18, i, "\pSquare", "\pi one 4/8");
    DrawCard(16, 150, j, "\pRound 12", "\pj R12 one 3/8");
    DrawCard(156, 150, k, "\pRound 12", "\pk R12 one 4/8");
    DrawCard(296, 150, l, "\pRound 12", "\pl R12 3-step");
  }

  void DrawPageColor()
  {
    const Look m = {kGrayOne, 1, 0, true};
    const Look n = {kGrayThree, 3, 0, true};
    const Look o = {kGraySoft, 6, 0, true};
    const Look p = {kGrayOne, 1, 12, true};
    const Look q = {kGrayThree, 3, 12, true};
    const Look r = {kGraySoft, 6, 12, true};
    DrawCard(16, 18, m, "\pSquare", "\pm one gray");
    DrawCard(156, 18, n, "\pSquare", "\pn 3 grays");
    DrawCard(296, 18, o, "\pSquare", "\po 6 grays");
    DrawCard(16, 150, p, "\pRound 12", "\pp R12 one gray");
    DrawCard(156, 150, q, "\pRound 12", "\pq R12 3 grays");
    DrawCard(296, 150, r, "\pRound 12", "\pr R12 6 grays");
  }

  /** Paint a 240x160 card's band the way four strip leaves would: each strip
      clips to itself, erases to the parent ground, then draws every
      silhouette and the body. Strips tile the frame band without overlap. */
  void PaintBandStrips(const Look &look)
  {
    const short x = 20, y = 20, bw = 240, bh = 160, t = 4;
    short d = 0;
    for (short s = 0; s < look.count; ++s)
      if (look.steps[s].offset > d)
        d = look.steps[s].offset;
    Rect body;
    SetRect(&body, x, y, x + bw, y + bh);
    Rect strips[4];
    SetRect(&strips[0], x, y, x + bw - t, y + t);                 // top
    SetRect(&strips[1], x + bw - t, y, x + bw + d, y + bh - t);   // right + shadow
    SetRect(&strips[2], x + t, y + bh - t, x + bw + d, y + bh + d); // bottom + shadow
    SetRect(&strips[3], x, y + t, x + t, y + bh + d);             // left
    for (short s = 0; s < 4; ++s)
    {
      ClipRect(&strips[s]);
      EraseRect(&strips[s]);
      DrawShadow(body, look);
      DrawBody(body, look.oval);
    }
    Rect all;
    SetRect(&all, 0, 0, kWidth, kHeight);
    ClipRect(&all);
  }

  void SetUpPort()
  {
    PenNormal();
    TextFont(0); // Chicago on the intended System 7 rig.
    TextSize(12);
    TextMode(srcOr);
    ForeColor(blackColor);
    BackColor(whiteColor);
    Rect content;
    SetRect(&content, 0, 0, kWidth, kHeight);
    ClipRect(&content);
    EraseRect(&content);
  }
  bool Touches(const Rect &piece, const Rect &strip)
  {
    Rect overlap;
    return SectRect(&piece, &strip, &overlap);
  }

  /** Ink (step != 0) or erase (step == 0) one piece; corner pieces are circles. */
  void ApplyPiece(const Rect &piece, short oval, const Step *step, bool color)
  {
    if (step)
    {
      InkShape(piece, oval, *step, color);
      return;
    }
    if (oval)
      EraseRoundRect(&piece, oval, oval);
    else
      EraseRect(&piece);
  }

  /** A round rect as a cross of two rects plus four corner circles. Only
      pieces whose own quadrant touches the strip are drawn; a circle's other
      quadrants overlap the cross with the same ink. */
  void FillRoundFast(const Rect &r, short oval, const Step *step, bool color, const Rect &strip)
  {
    if (!oval)
    {
      if (Touches(r, strip))
        ApplyPiece(r, 0, step, color);
      return;
    }
    const short radius = oval / 2;
    Rect cross[2];
    SetRect(&cross[0], r.left + radius, r.top, r.right - radius, r.bottom);
    SetRect(&cross[1], r.left, r.top + radius, r.right, r.bottom - radius);
    for (short i = 0; i < 2; ++i)
      if (Touches(cross[i], strip))
        ApplyPiece(cross[i], 0, step, color);
    for (short corner = 0; corner < 4; ++corner)
    {
      const short left = (corner & 1) ? r.right - oval : r.left;
      const short top = (corner & 2) ? r.bottom - oval : r.top;
      Rect circle, quadrant;
      SetRect(&circle, left, top, left + oval, top + oval);
      SetRect(&quadrant, (corner & 1) ? r.right - radius : r.left, (corner & 2) ? r.bottom - radius : r.top,
              (corner & 1) ? r.right : r.left + radius, (corner & 2) ? r.bottom : r.top + radius);
      if (Touches(quadrant, strip))
        ApplyPiece(circle, oval, step, color);
    }
  }

  void FrameRoundFast(const Rect &r, short oval, const Rect &strip)
  {
    if (!oval)
    {
      FrameRect(&r);
      return;
    }
    const short radius = oval / 2;
    Rect edges[4];
    SetRect(&edges[0], r.left + radius, r.top, r.right - radius, r.top + 1);
    SetRect(&edges[1], r.left + radius, r.bottom - 1, r.right - radius, r.bottom);
    SetRect(&edges[2], r.left, r.top + radius, r.left + 1, r.bottom - radius);
    SetRect(&edges[3], r.right - 1, r.top + radius, r.right, r.bottom - radius);
    for (short i = 0; i < 4; ++i)
      if (Touches(edges[i], strip))
        PaintRect(&edges[i]);
    bool clipped = false;
    for (short corner = 0; corner < 4; ++corner)
    {
      Rect quadrant, clip;
      SetRect(&quadrant, (corner & 1) ? r.right - radius : r.left, (corner & 2) ? r.bottom - radius : r.top,
              (corner & 1) ? r.right : r.left + radius, (corner & 2) ? r.bottom : r.top + radius);
      if (!SectRect(&quadrant, &strip, &clip))
        continue;
      const short left = (corner & 1) ? r.right - oval : r.left;
      const short top = (corner & 2) ? r.bottom - oval : r.top;
      Rect circle;
      SetRect(&circle, left, top, left + oval, top + oval);
      ClipRect(&clip);
      FrameOval(&circle);
      clipped = true;
    }
    if (clipped)
      ClipRect(&strip);
  }

  void PaintCardFast(const Rect &body, const Look &look, const Rect &strip)
  {
    for (short i = 0; i < look.count; ++i)
    {
      Rect silhouette = body;
      OffsetRect(&silhouette, look.steps[i].offset, look.steps[i].offset);
      FillRoundFast(silhouette, look.oval, &look.steps[i], look.color, strip);
    }
    if (look.color)
    {
      RGBColor black;
      black.red = black.green = black.blue = 0;
      RGBForeColor(&black);
    }
    FillRoundFast(body, look.oval, 0, false, strip);
    FrameRoundFast(body, look.oval, strip);
  }

  void BandStrips(short d, Rect *strips)
  {
    const short x = 20, y = 20, bw = 240, bh = 160, t = 4;
    SetRect(&strips[0], x, y, x + bw - t, y + t);
    SetRect(&strips[1], x + bw - t, y, x + bw + d, y + bh - t);
    SetRect(&strips[2], x + t, y + bh - t, x + bw + d, y + bh + d);
    SetRect(&strips[3], x, y + t, x + t, y + bh + d);
  }

  short Depth(const Look &look)
  {
    short d = 0;
    for (short s = 0; s < look.count; ++s)
      if (look.steps[s].offset > d)
        d = look.steps[s].offset;
    return d;
  }

  /** Same band as PaintBandStrips, but each strip draws only pieces that touch it. */
  void PaintBandStripsFast(const Look &look)
  {
    Rect body;
    SetRect(&body, 20, 20, 260, 180);
    Rect strips[4];
    BandStrips(Depth(look), strips);
    for (short s = 0; s < 4; ++s)
    {
      ClipRect(&strips[s]);
      EraseRect(&strips[s]);
      PaintCardFast(body, look, strips[s]);
    }
    Rect all;
    SetRect(&all, 0, 0, kWidth, kHeight);
    ClipRect(&all);
  }

  /** Reference cost: erasing the 240x160 body, the cheapest paint its inside gets. */
  void PaintBodyErase(const Look &)
  {
    Rect body;
    SetRect(&body, 20, 20, 260, 180);
    EraseRect(&body);
  }

  typedef void (*PaintFn)(const Look &);

  unsigned long TimeReps(PaintFn paint, const Look &look)
  {
    UnsignedWide before, after;
    Microseconds(&before);
    for (short i = 0; i < kReps; ++i)
      paint(look);
    Microseconds(&after);
    return (after.lo - before.lo) / kReps;
  }

  struct Timing
  {
    const char *name;
    PaintFn paint;
    Look look;
    bool colorOnly;
  };

  const Step kNone[] = {{0, 0}};

  void RunTimings(std::FILE *file, const char *depth, bool colorPort)
  {
    const Timing timings[] = {
        {"body-erase", PaintBodyErase, {kNone, 0, 0, false}, false},
        {"square-border-only", PaintBandStrips, {kNone, 0, 0, false}, false},
        {"square-one-pattern", PaintBandStrips, {kOne3, 1, 0, false}, false},
        {"square-3-step", PaintBandStrips, {kThree, 3, 0, false}, false},
        {"r12-border-only", PaintBandStrips, {kNone, 0, 12, false}, false},
        {"r12-one-pattern", PaintBandStrips, {kOne3, 1, 12, false}, false},
        {"r12-3-step", PaintBandStrips, {kThree, 3, 12, false}, false},
        {"r12-3-grays", PaintBandStrips, {kGrayThree, 3, 12, true}, true},
        {"r12-6-grays", PaintBandStrips, {kGraySoft, 6, 12, true}, true},
        {"fast-square-border-only", PaintBandStripsFast, {kNone, 0, 0, false}, false},
        {"fast-square-one-pattern", PaintBandStripsFast, {kOne3, 1, 0, false}, false},
        {"fast-square-3-step", PaintBandStripsFast, {kThree, 3, 0, false}, false},
        {"fast-r12-border-only", PaintBandStripsFast, {kNone, 0, 12, false}, false},
        {"fast-r12-one-pattern", PaintBandStripsFast, {kOne3, 1, 12, false}, false},
        {"fast-r12-3-step", PaintBandStripsFast, {kThree, 3, 12, false}, false},
        {"fast-r12-3-grays", PaintBandStripsFast, {kGrayThree, 3, 12, true}, true},
    };
    for (unsigned i = 0; i < sizeof(timings) / sizeof(timings[0]); ++i)
    {
      if (timings[i].colorOnly && !colorPort)
        continue;
      const unsigned long us = TimeReps(timings[i].paint, timings[i].look);
      std::fprintf(file, "TIME depth=%s case=%s reps=%d us_per_card=%lu\n", depth, timings[i].name, kReps, us);
    }
  }

  /** Snapshot the window content into `bytes` (1-bit, kWidth/8 per row). */
  void Snapshot(Ptr bytes)
  {
    BitMap shot;
    shot.baseAddr = bytes;
    shot.rowBytes = kWidth / 8;
    SetRect(&shot.bounds, 0, 0, kWidth, kHeight);
    Rect content;
    SetRect(&content, 0, 0, kWidth, kHeight);
    CopyBits(&qd.thePort->portBits, &shot, &content, &shot.bounds, srcCopy, 0);
  }

  /** The fast pieces must give the same pixels as the whole-shape strips. */
  void CompareFastToNaive(std::FILE *file)
  {
    const long size = static_cast<long>(kWidth / 8) * kHeight;
    Ptr naive = NewPtrClear(size);
    Ptr fast = NewPtrClear(size);
    if (!naive || !fast)
    {
      std::fprintf(file, "MATCH refused=memory\n");
      if (naive)
        DisposePtr(naive);
      if (fast)
        DisposePtr(fast);
      return;
    }
    struct Case
    {
      const char *name;
      Look look;
    };
    const Case cases[] = {{"square-3-step", {kThree, 3, 0, false}},
                          {"r12-one-pattern", {kOne3, 1, 12, false}},
                          {"r12-3-step", {kThree, 3, 12, false}},
                          {"r16-3-step", {kThree, 3, 16, false}}};
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
    {
      SetUpPort();
      PaintBandStrips(cases[i].look);
      Snapshot(naive);
      SetUpPort();
      PaintBandStripsFast(cases[i].look);
      Snapshot(fast);
      long diff = 0;
      for (long b = 0; b < size; ++b)
        if (naive[b] != fast[b])
          ++diff;
      std::fprintf(file, "MATCH case=%s diffbytes=%ld\n", cases[i].name, diff);
    }
    DisposePtr(naive);
    DisposePtr(fast);
  }

  bool WriteMono(std::FILE *file)
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
    // The Mac II frame buffer is NuBus memory; read it through QuickDraw.
    CopyBits(&qd.thePort->portBits, &shot, &content, &shot.bounds, srcCopy, 0);
    std::fprintf(file, "IMG1 %d %d\n", kWidth, kHeight);
    const size_t bytes = static_cast<size_t>(rowBytes) * kHeight;
    const bool ok = std::fwrite(shot.baseAddr, 1, bytes, file) == bytes;
    DisposePtr(shot.baseAddr);
    return ok;
  }

  bool WriteColor(std::FILE *file, PixMapHandle pixels)
  {
    CTabHandle table = (**pixels).pmTable;
    const short entries = static_cast<short>((**table).ctSize + 1);
    std::fprintf(file, "IMG8 %d %d %d\n", kWidth, kHeight, entries);
    bool ok = true;
    for (short i = 0; i < entries && ok; ++i)
    {
      const RGBColor &rgb = (**table).ctTable[i].rgb;
      unsigned char entry[6] = {static_cast<unsigned char>(rgb.red >> 8), static_cast<unsigned char>(rgb.red),
                                static_cast<unsigned char>(rgb.green >> 8), static_cast<unsigned char>(rgb.green),
                                static_cast<unsigned char>(rgb.blue >> 8), static_cast<unsigned char>(rgb.blue)};
      ok = std::fwrite(entry, 1, 6, file) == 6;
    }
    const long rowBytes = (**pixels).rowBytes & 0x3FFF;
    const char *base = GetPixBaseAddr(pixels);
    for (short row = 0; row < kHeight && ok; ++row)
      ok = std::fwrite(base + row * rowBytes, 1, kWidth, file) == static_cast<size_t>(kWidth);
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
  WindowPtr window = NewWindow(0, &bounds, "\pCard shadow probe 2", true, documentProc,
                               reinterpret_cast<WindowPtr>(-1L), false, 0);
  if (!window)
  {
    ExitToShell();
    return 1;
  }
  SetPort(window);
  HideCursor();
  BackPat(&qd.white);
  SetUpPort();

  loka::platform::file::FileHandle destination;
  bool ok = loka::platform::file::ResolveApplicationSidecar(
      loka::file::File::Application() << loka::file::File("LOG.TXT"), destination);
  std::FILE *file = ok ? loka::platform::file::OpenWriteTruncate(destination) : 0;
  if (!file)
  {
    ShowCursor();
    DisposeWindow(window);
    ExitToShell();
    return 1;
  }
  std::fprintf(file, "CARDPROBE2\n");

  // 1-bit: timings on the window, then the picture page.
  RunTimings(file, "1-window", false);
  CompareFastToNaive(file);
  SetUpPort();
  DrawPageMono();
  ok = WriteMono(file);

  // 8-bit offscreen: the closest proxy here for the 180c's 8-bit screen.
  GWorldPtr world = 0;
  Rect worldBounds;
  SetRect(&worldBounds, 0, 0, kWidth, kHeight);
  const QDErr err = NewGWorld(&world, 8, &worldBounds, 0, 0, 0);
  if (err != noErr || !world)
  {
    std::fprintf(file, "GWORLD-REFUSED err=%d\n", static_cast<int>(err));
  }
  else
  {
    PixMapHandle pixels = GetGWorldPixMap(world);
    if (!LockPixels(pixels) || PixMap32Bit(pixels))
    {
      std::fprintf(file, "GWORLD-UNREADABLE\n");
    }
    else
    {
      CGrafPtr savedPort;
      GDHandle savedDevice;
      GetGWorld(&savedPort, &savedDevice);
      SetGWorld(world, 0);
      SetUpPort();
      RunTimings(file, "8-gworld", true);
      SetUpPort();
      DrawPageColor();
      SetGWorld(savedPort, savedDevice);
      if (!WriteColor(file, pixels))
        ok = false;
    }
    DisposeGWorld(world);
  }
  std::fprintf(file, "END\n");
  if (!loka::platform::file::FlushWrite(file, destination))
    ok = false;
  if (std::fclose(file) != 0)
    ok = false;

  ShowCursor();
  DisposeWindow(window);
  ExitToShell();
  return ok ? 0 : 1;
}
