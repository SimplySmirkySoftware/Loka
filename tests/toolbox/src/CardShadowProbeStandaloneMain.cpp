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

#include "core/io/File.hpp"
#include "platform/file/AppLocation.hpp"
#include "platform/file/FileIO.hpp"

// Page 3 of the card probe (page 1 is a6997e5d, page 2 is d2dc2f17): build a
// card's whole decoration once into a cached offscreen (shape mask, offset,
// two box-blur passes per axis, then a Bayer stencil on 1-bit or grays on
// 8-bit), and repaint it with one CopyBits per strip. Not for merge.
namespace
{
  enum { kWidth = 512, kHeight = 342, kColorHeight = 220, kReps = 40, kBand = 4, kPeak = 140 };

  const unsigned char kBayer8[8][8] = {{0, 32, 8, 40, 2, 34, 10, 42},  {48, 16, 56, 24, 50, 18, 58, 26},
                                       {12, 44, 4, 36, 14, 46, 6, 38}, {60, 28, 52, 20, 62, 30, 54, 22},
                                       {3, 35, 11, 43, 1, 33, 9, 41},  {51, 19, 59, 27, 49, 17, 57, 25},
                                       {15, 47, 7, 39, 13, 45, 5, 37}, {63, 31, 55, 23, 61, 29, 53, 21}};

  enum ShapeKind { SHAPE_SQUARE, SHAPE_ROUND, SHAPE_OVAL, SHAPE_BUBBLE };

  /** The body as a region whose box starts at (0, 0); the bubble's tail hangs below h. */
  RgnHandle MakeShape(ShapeKind kind, short w, short h)
  {
    RgnHandle rgn = NewRgn();
    if (!rgn)
      return 0;
    Rect r;
    SetRect(&r, 0, 0, w, h);
    switch (kind)
    {
    case SHAPE_SQUARE:
      RectRgn(rgn, &r);
      break;
    case SHAPE_ROUND:
      OpenRgn();
      FrameRoundRect(&r, 12, 12);
      CloseRgn(rgn);
      break;
    case SHAPE_OVAL:
      OpenRgn();
      FrameOval(&r);
      CloseRgn(rgn);
      break;
    case SHAPE_BUBBLE:
    {
      OpenRgn();
      FrameRoundRect(&r, 12, 12);
      CloseRgn(rgn);
      RgnHandle tail = NewRgn();
      if (tail)
      {
        const short root = w / 6;
        OpenRgn();
        MoveTo(root, h - 2);
        LineTo(root - 8, h + h / 4);
        LineTo(root + 18, h - 2);
        LineTo(root, h - 2);
        CloseRgn(tail);
        UnionRgn(rgn, tail, rgn);
        DisposeRgn(tail);
      }
      break;
    }
    }
    return rgn;
  }

  /** Shadow geometry: blur radius per pass, total reach, and offset. */
  struct Spread
  {
    short radius;
    short reach;
    short offset;
    explicit Spread(short spread)
        : radius(static_cast<short>((spread + 1) / 2)), reach(static_cast<short>(2 * ((spread + 1) / 2))),
          offset(static_cast<short>((spread + 2) / 3))
    {
    }
  };

  /** A built card: the offscreen holding the whole outer frame, and the
      local rects the strips are cut from. */
  struct CardCache
  {
    GWorldPtr world;
    Rect outer;   // (0, 0, W, H)
    Rect body;    // body box (w x h) inside outer
    Rect content; // what the strips leave to the content leaf
    long bytes;
  };

  // Build-stage marks for the breakdown line: 0 start, 1 mask rasterized,
  // 2 expanded to bytes, 3 blurred, 4 cache GWorld ready, 5 stencil written, 6 body drawn.
  UnsignedWide gMarks[7];
  bool gFastBlur = false;

  void Mark(short stage)
  {
    Microseconds(&gMarks[stage]);
  }

  bool PixelsOf(GWorldPtr world, char *&base, long &rowBytes)
  {
    PixMapHandle pixels = GetGWorldPixMap(world);
    if (!LockPixels(pixels) || PixMap32Bit(pixels))
      return false;
    base = GetPixBaseAddr(pixels);
    rowBytes = (**pixels).rowBytes & 0x3FFF;
    return true;
  }

  void BlurRows(unsigned char *values, short w, short h, short radius, unsigned char *line)
  {
    const long scale = 65536L / (2 * radius + 1);
    for (short y = 0; y < h; ++y)
    {
      unsigned char *row = values + static_cast<long>(y) * w;
      for (short x = 0; x < w; ++x)
        line[x] = row[x];
      long sum = 0;
      for (short k = 0; k <= radius && k < w; ++k)
        sum += line[k];
      for (short x = 0; x < w; ++x)
      {
        row[x] = static_cast<unsigned char>((sum * scale) >> 16);
        if (x + radius + 1 < w)
          sum += line[x + radius + 1];
        if (x - radius >= 0)
          sum -= line[x - radius];
      }
    }
  }

  void BlurColumns(unsigned char *values, short w, short h, short radius, unsigned char *line)
  {
    const long scale = 65536L / (2 * radius + 1);
    for (short x = 0; x < w; ++x)
    {
      for (short y = 0; y < h; ++y)
        line[y] = values[static_cast<long>(y) * w + x];
      long sum = 0;
      for (short k = 0; k <= radius && k < h; ++k)
        sum += line[k];
      for (short y = 0; y < h; ++y)
      {
        values[static_cast<long>(y) * w + x] = static_cast<unsigned char>((sum * scale) >> 16);
        if (y + radius + 1 < h)
          sum += line[y + radius + 1];
        if (y - radius >= 0)
          sum -= line[y - radius];
      }
    }
  }

  /** Same passes with a quotient table and pointer stepping instead of a
      multiply per pixel and an index multiply per column step. */
  unsigned char gQuotient[255 * 33 + 1];
  short gQuotientRadius = -1;

  void PrepareQuotients(short radius)
  {
    if (gQuotientRadius == radius)
      return;
    const long width = 2 * radius + 1;
    for (long sum = 0; sum <= 255 * width; ++sum)
      gQuotient[sum] = static_cast<unsigned char>(sum / width);
    gQuotientRadius = radius;
  }

  void BlurLineFast(unsigned char *start, long stride, short n, short radius, unsigned char *line)
  {
    unsigned char *p = start;
    for (short i = 0; i < n; ++i, p += stride)
      line[i] = *p;
    unsigned short sum = 0;
    for (short k = 0; k <= radius && k < n; ++k)
      sum += line[k];
    p = start;
    const unsigned char *add = line + radius + 1;
    const unsigned char *sub = line - radius;
    for (short i = 0; i < n; ++i, p += stride, ++add, ++sub)
    {
      *p = gQuotient[sum];
      if (i + radius + 1 < n)
        sum += *add;
      if (i - radius >= 0)
        sum -= *sub;
    }
  }

  void BlurFast(unsigned char *values, short w, short h, short radius, unsigned char *line)
  {
    PrepareQuotients(radius);
    for (short pass = 0; pass < 2; ++pass)
      for (short y = 0; y < h; ++y)
        BlurLineFast(values + static_cast<long>(y) * w, 1, w, radius, line);
    for (short pass = 0; pass < 2; ++pass)
      for (short x = 0; x < w; ++x)
        BlurLineFast(values + x, w, h, radius, line);
  }

  /** Shadow intensity (0..255 before kPeak) over the outer frame: the body
      offset down-right, rasterized through a 1-bit GWorld, blurred twice per axis. */
  unsigned char *BuildIntensity(RgnHandle body, const Rect &outer, const Spread &spread)
  {
    const short w = outer.right, h = outer.bottom;
    GWorldPtr mask = 0;
    if (NewGWorld(&mask, 1, &outer, 0, 0, 0) != noErr || !mask)
      return 0;
    char *bits = 0;
    long rowBytes = 0;
    unsigned char *values = 0;
    unsigned char *line = 0;
    if (PixelsOf(mask, bits, rowBytes))
    {
      CGrafPtr savedPort;
      GDHandle savedDevice;
      GetGWorld(&savedPort, &savedDevice);
      SetGWorld(mask, 0);
      EraseRect(&outer);
      OffsetRgn(body, spread.offset, spread.offset);
      PaintRgn(body);
      OffsetRgn(body, static_cast<short>(-spread.offset), static_cast<short>(-spread.offset));
      SetGWorld(savedPort, savedDevice);
      Mark(1);
      values = reinterpret_cast<unsigned char *>(NewPtr(static_cast<long>(w) * h));
      line = reinterpret_cast<unsigned char *>(NewPtr(w > h ? w : h));
      if (values && line)
      {
        for (short y = 0; y < h; ++y)
        {
          const unsigned char *src = reinterpret_cast<const unsigned char *>(bits) + y * rowBytes;
          unsigned char *dst = values + static_cast<long>(y) * w;
          for (short x = 0; x < w; ++x)
            dst[x] = ((src[x >> 3] >> (7 - (x & 7))) & 1) ? 255 : 0;
        }
        Mark(2);
        if (gFastBlur)
          BlurFast(values, w, h, spread.radius, line);
        else
        {
          BlurRows(values, w, h, spread.radius, line);
          BlurRows(values, w, h, spread.radius, line);
          BlurColumns(values, w, h, spread.radius, line);
          BlurColumns(values, w, h, spread.radius, line);
        }
        Mark(3);
      }
      else if (values)
      {
        DisposePtr(reinterpret_cast<Ptr>(values));
        values = 0;
      }
    }
    if (line)
      DisposePtr(reinterpret_cast<Ptr>(line));
    DisposeGWorld(mask);
    return values;
  }

  unsigned char gGrayIndex[256];

  /** Build the whole decoration once. depth 1 = Bayer stencil, depth 8 = grays onto white. */
  bool BuildCard(ShapeKind kind, short w, short h, short spreadPx, short depth, CardCache &card)
  {
    card.world = 0;
    card.bytes = 0;
    const Spread spread(spreadPx);
    RgnHandle body = MakeShape(kind, w, h);
    if (!body)
      return false;
    const Rect box = (**body).rgnBBox;
    const short left = static_cast<short>(spread.reach > spread.offset ? spread.reach - spread.offset : 0);
    const short right = static_cast<short>(spread.reach + spread.offset);
    SetRect(&card.outer, 0, 0, static_cast<short>(left + box.right + right),
            static_cast<short>(left + box.bottom + right));
    OffsetRgn(body, left, left);
    SetRect(&card.body, left, left, left + w, left + h);
    const short inset = kind == SHAPE_OVAL ? static_cast<short>(w * 3 / 20 + 2) : static_cast<short>(kBand);
    const short insetY = kind == SHAPE_OVAL ? static_cast<short>(h * 3 / 20 + 2) : static_cast<short>(kBand);
    SetRect(&card.content, card.body.left + inset, card.body.top + insetY, card.body.right - inset,
            card.body.bottom - insetY);

    Mark(0);
    unsigned char *values = BuildIntensity(body, card.outer, spread);
    bool ok = values != 0;
    if (ok)
      ok = NewGWorld(&card.world, depth, &card.outer, 0, 0, 0) == noErr && card.world;
    char *base = 0;
    long rowBytes = 0;
    if (ok)
      ok = PixelsOf(card.world, base, rowBytes);
    Mark(4);
    if (ok)
    {
      const short ow = card.outer.right, oh = card.outer.bottom;
      card.bytes = rowBytes * oh;
      for (short y = 0; y < oh; ++y)
      {
        unsigned char *row = reinterpret_cast<unsigned char *>(base) + y * rowBytes;
        const unsigned char *src = values + static_cast<long>(y) * ow;
        if (depth == 1)
        {
          for (short x = 0; x < (ow + 7) / 8; ++x)
            row[x] = 0;
          for (short x = 0; x < ow; ++x)
            if (((src[x] * kPeak) >> 8) > kBayer8[y & 7][x & 7] * 4 + 2)
              row[x >> 3] |= static_cast<unsigned char>(0x80 >> (x & 7));
        }
        else
        {
          for (short x = 0; x < ow; ++x)
            row[x] = gGrayIndex[255 - ((src[x] * kPeak) >> 8)];
        }
      }
      Mark(5);
      CGrafPtr savedPort;
      GDHandle savedDevice;
      GetGWorld(&savedPort, &savedDevice);
      SetGWorld(card.world, 0);
      PenNormal();
      ForeColor(blackColor);
      BackColor(whiteColor);
      EraseRgn(body);
      FrameRgn(body);
      SetGWorld(savedPort, savedDevice);
      Mark(6);
    }
    if (values)
      DisposePtr(reinterpret_cast<Ptr>(values));
    DisposeRgn(body);
    if (!ok && card.world)
    {
      DisposeGWorld(card.world);
      card.world = 0;
    }
    return ok;
  }

  void FreeCard(CardCache &card)
  {
    if (card.world)
      DisposeGWorld(card.world);
    card.world = 0;
  }

  /** Four strips tiling outer minus content (top, right, bottom, left), local coordinates. */
  void Strips(const CardCache &card, Rect *strips)
  {
    const Rect &o = card.outer;
    const Rect &c = card.content;
    SetRect(&strips[0], 0, 0, c.right, c.top);
    SetRect(&strips[1], c.right, 0, o.right, c.bottom);
    SetRect(&strips[2], c.left, c.bottom, o.right, o.bottom);
    SetRect(&strips[3], 0, c.top, c.left, o.bottom);
  }

  const BitMap *CacheBits(const CardCache &card)
  {
    return reinterpret_cast<const BitMap *>(*GetGWorldPixMap(card.world));
  }

  /** The current port's own bits: a window's portBits or a GWorld's pixmap. */
  const BitMap *PortBits()
  {
    GrafPtr port;
    GetPort(&port);
    return &port->portBits;
  }

  void RepaintStrips(const CardCache &card, short atX, short atY, const Rect &portRect)
  {
    Rect strips[4];
    Strips(card, strips);
    for (short s = 0; s < 4; ++s)
    {
      Rect dst = strips[s];
      OffsetRect(&dst, atX, atY);
      ClipRect(&dst);
      CopyBits(CacheBits(card), PortBits(), &strips[s], &dst, srcCopy, 0);
    }
    ClipRect(&portRect);
  }

  void RepaintWhole(const CardCache &card, short atX, short atY)
  {
    Rect dst = card.outer;
    OffsetRect(&dst, atX, atY);
    CopyBits(CacheBits(card), PortBits(), &card.outer, &dst, srcCopy, 0);
  }

  unsigned long Elapsed(const UnsignedWide &before, const UnsignedWide &after)
  {
    return after.lo - before.lo;
  }

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

  struct TimedCase
  {
    const char *name;
    ShapeKind kind;
    short spread;
  };

  void TimeCase(std::FILE *file, const char *depthName, short depth, const TimedCase &c, const Rect &portRect)
  {
    UnsignedWide before, after;
    CardCache card;
    Microseconds(&before);
    const bool built = BuildCard(c.kind, 240, 160, c.spread, depth, card);
    Microseconds(&after);
    if (!built)
    {
      std::fprintf(file, "TIME depth=%s case=%s refused=build\n", depthName, c.name);
      return;
    }
    const unsigned long build = Elapsed(before, after);
    std::fprintf(file, "STAGES depth=%s case=%s fast_blur=%d mask_us=%lu expand_us=%lu blur_us=%lu gworld_us=%lu stencil_us=%lu body_us=%lu\n",
                 depthName, c.name, gFastBlur ? 1 : 0, Elapsed(gMarks[0], gMarks[1]), Elapsed(gMarks[1], gMarks[2]),
                 Elapsed(gMarks[2], gMarks[3]), Elapsed(gMarks[3], gMarks[4]), Elapsed(gMarks[4], gMarks[5]),
                 Elapsed(gMarks[5], gMarks[6]));
    const short atX = static_cast<short>(24 - card.body.left), atY = static_cast<short>(24 - card.body.top);
    EraseRect(&portRect);
    Microseconds(&before);
    for (short i = 0; i < kReps; ++i)
      RepaintStrips(card, atX, atY, portRect);
    Microseconds(&after);
    const unsigned long strips = Elapsed(before, after) / kReps;
    Microseconds(&before);
    for (short i = 0; i < kReps; ++i)
      RepaintWhole(card, atX, atY);
    Microseconds(&after);
    const unsigned long whole = Elapsed(before, after) / kReps;
    if (depth == 1)
    {
      // Strip tiling must reproduce the whole-card copy exactly.
      const long size = static_cast<long>(kWidth / 8) * kHeight;
      Ptr viaStrips = NewPtrClear(size);
      Ptr viaWhole = NewPtrClear(size);
      if (viaStrips && viaWhole)
      {
        EraseRect(&portRect);
        RepaintStrips(card, atX, atY, portRect);
        Snapshot(viaStrips);
        EraseRect(&portRect);
        RepaintWhole(card, atX, atY);
        Snapshot(viaWhole);
        long diff = 0;
        for (long b = 0; b < size; ++b)
          if (viaStrips[b] != viaWhole[b])
            ++diff;
        std::fprintf(file, "MATCH case=%s strips_vs_whole_diffbytes=%ld\n", c.name, diff);
      }
      if (viaStrips)
        DisposePtr(viaStrips);
      if (viaWhole)
        DisposePtr(viaWhole);
    }
    std::fprintf(file, "TIME depth=%s case=%s outer=%dx%d build_us=%lu strips_us=%lu whole_us=%lu cache_bytes=%ld\n",
                 depthName, c.name, card.outer.right, card.outer.bottom, build, strips, whole, card.bytes);
    FreeCard(card);
  }

  void PlaceCard(ShapeKind kind, short spread, short depth, short bodyX, short bodyY, ConstStr255Param label,
                 ConstStr255Param caption)
  {
    CardCache card;
    if (!BuildCard(kind, 120, 72, spread, depth, card))
    {
      MoveTo(bodyX, bodyY + 40);
      DrawString("\pREFUSED");
      return;
    }
    RepaintWhole(card, static_cast<short>(bodyX - card.body.left), static_cast<short>(bodyY - card.body.top));
    MoveTo(bodyX + 12, bodyY + 40);
    DrawString(label);
    MoveTo(bodyX, static_cast<short>(bodyY + card.outer.bottom - card.body.top + 12));
    DrawString(caption);
    FreeCard(card);
  }

  void SetUpPort(const Rect &portRect)
  {
    PenNormal();
    TextFont(0); // Chicago on the intended System 7 rig.
    TextSize(12);
    TextMode(srcOr);
    ForeColor(blackColor);
    BackColor(whiteColor);
    ClipRect(&portRect);
    EraseRect(&portRect);
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

  bool WriteColor(std::FILE *file, GWorldPtr world)
  {
    PixMapHandle pixels = GetGWorldPixMap(world);
    CTabHandle table = (**pixels).pmTable;
    const short entries = static_cast<short>((**table).ctSize + 1);
    std::fprintf(file, "IMG8 %d %d %d\n", kWidth, kColorHeight, entries);
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
    for (short row = 0; row < kColorHeight && ok; ++row)
      ok = std::fwrite(base + row * rowBytes, 1, kWidth, file) == static_cast<size_t>(kWidth);
    return ok;
  }

  const TimedCase kMonoCases[] = {{"round-s3", SHAPE_ROUND, 3},   {"round-s8", SHAPE_ROUND, 8},
                                  {"round-s16", SHAPE_ROUND, 16}, {"square-s8", SHAPE_SQUARE, 8},
                                  {"oval-s8", SHAPE_OVAL, 8},     {"bubble-s8", SHAPE_BUBBLE, 8}};
  const TimedCase kColorCases[] = {{"round-s8-gray", SHAPE_ROUND, 8},
                                   {"round-s16-gray", SHAPE_ROUND, 16},
                                   {"bubble-s8-gray", SHAPE_BUBBLE, 8}};
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
  SetRect(&bounds, 0, 40, kWidth, 40 + kHeight);
  WindowPtr window = NewWindow(0, &bounds, "\pCard shadow probe 3", true, plainDBox,
                               reinterpret_cast<WindowPtr>(-1L), false, 0);
  if (!window)
  {
    ExitToShell();
    return 1;
  }
  SetPort(window);
  HideCursor();
  BackPat(&qd.white);
  Rect windowRect;
  SetRect(&windowRect, 0, 0, kWidth, kHeight);
  SetUpPort(windowRect);

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
  std::fprintf(file, "CARDPROBE3\n");

  // 1-bit: stencil timings on the window, then the picture page.
  for (unsigned i = 0; i < sizeof(kMonoCases) / sizeof(kMonoCases[0]); ++i)
    TimeCase(file, "1-window", 1, kMonoCases[i], windowRect);
  gFastBlur = true;
  {
    const TimedCase fastCases[] = {{"round-s8-fastblur", SHAPE_ROUND, 8}, {"round-s16-fastblur", SHAPE_ROUND, 16},
                                   {"bubble-s8-fastblur", SHAPE_BUBBLE, 8}};
    for (unsigned i = 0; i < sizeof(fastCases) / sizeof(fastCases[0]); ++i)
      TimeCase(file, "1-window", 1, fastCases[i], windowRect);
  }
  gFastBlur = false;
  SetUpPort(windowRect);
  PlaceCard(SHAPE_ROUND, 3, 1, 20, 24, "\pRound", "\pA round s3");
  PlaceCard(SHAPE_ROUND, 8, 1, 185, 24, "\pRound", "\pB round s8");
  PlaceCard(SHAPE_ROUND, 16, 1, 350, 24, "\pRound", "\pC round s16");
  PlaceCard(SHAPE_SQUARE, 8, 1, 20, 184, "\pSquare", "\pD square s8");
  PlaceCard(SHAPE_OVAL, 8, 1, 185, 184, "\pOval", "\pE oval s8");
  PlaceCard(SHAPE_BUBBLE, 8, 1, 350, 184, "\pBubble", "\pF bubble s8");
  ok = WriteMono(file);

  // 8-bit offscreen: grays onto white, no dither.
  GWorldPtr world = 0;
  Rect worldBounds;
  SetRect(&worldBounds, 0, 0, kWidth, kColorHeight);
  const QDErr err = NewGWorld(&world, 8, &worldBounds, 0, 0, 0);
  if (err != noErr || !world)
  {
    std::fprintf(file, "GWORLD-REFUSED err=%d\n", static_cast<int>(err));
  }
  else
  {
    char *base = 0;
    long rowBytes = 0;
    if (!PixelsOf(world, base, rowBytes))
    {
      std::fprintf(file, "GWORLD-UNREADABLE\n");
    }
    else
    {
      CGrafPtr savedPort;
      GDHandle savedDevice;
      GetGWorld(&savedPort, &savedDevice);
      SetGWorld(world, 0);
      for (short g = 0; g < 256; ++g)
      {
        RGBColor gray;
        gray.red = gray.green = gray.blue = static_cast<unsigned short>(g * 0x0101);
        gGrayIndex[g] = static_cast<unsigned char>(Color2Index(&gray));
      }
      SetUpPort(worldBounds);
      for (unsigned i = 0; i < sizeof(kColorCases) / sizeof(kColorCases[0]); ++i)
        TimeCase(file, "8-gworld", 8, kColorCases[i], worldBounds);
      SetUpPort(worldBounds);
      PlaceCard(SHAPE_ROUND, 8, 8, 20, 24, "\pRound", "\pG round s8 gray");
      PlaceCard(SHAPE_ROUND, 16, 8, 185, 24, "\pRound", "\pH round s16 gray");
      PlaceCard(SHAPE_BUBBLE, 8, 8, 350, 24, "\pBubble", "\pI bubble s8 gray");
      SetGWorld(savedPort, savedDevice);
      if (!WriteColor(file, world))
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
