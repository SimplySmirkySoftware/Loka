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

// Page 4 of the card probe (pages 1-3: a6997e5d, d2dc2f17, dcf1b0f8): can
// Classic QuickDraw blend a per-pixel alpha shadow over arbitrary pixels?
// The page 3 intensity (blurred offset round rect) becomes an 8-bit gray
// mask; a solid black source is composited through it with CopyDeepMask, and
// with CopyMask given the same deep mask, onto 8-, 16- and 32-bit canvases
// holding colored stripes and a gray ramp. A `blend` CopyBits with a 50%
// OpColor is the constant-alpha control. Not for merge.
namespace
{
  enum { kCanvasW = 560, kCanvasH = 270, kBodyW = 240, kBodyH = 160, kBand = 4, kPeak = 140 };
  enum { kRampW = 160, kRampH = 48, kRowTop = 212, kSampleY = 252 };

  /** The rounded body (oval 12 = radius 6) as a region whose box starts at (0, 0). */
  RgnHandle MakeBody(short w, short h)
  {
    RgnHandle rgn = NewRgn();
    if (!rgn)
      return 0;
    Rect r;
    SetRect(&r, 0, 0, w, h);
    OpenRgn();
    FrameRoundRect(&r, 12, 12);
    CloseRgn(rgn);
    return rgn;
  }

  /** Shadow geometry, as on page 3. */
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

  bool PixelsOf(GWorldPtr world, char *&base, long &rowBytes)
  {
    PixMapHandle pixels = GetGWorldPixMap(world);
    if (!LockPixels(pixels) || PixMap32Bit(pixels))
      return false;
    base = GetPixBaseAddr(pixels);
    rowBytes = (**pixels).rowBytes & 0x3FFF;
    return true;
  }

  const BitMap *BitsOf(GWorldPtr world)
  {
    return reinterpret_cast<const BitMap *>(*GetGWorldPixMap(world));
  }

  const BitMap *PortBits()
  {
    GrafPtr port;
    GetPort(&port);
    return &port->portBits;
  }

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

  /** Shadow alpha (0..kPeak) over the outer frame: the body offset
      down-right, rasterized through a 1-bit GWorld, blurred twice per axis. */
  unsigned char *BuildAlpha(RgnHandle body, const Rect &outer, const Spread &spread)
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
        BlurFast(values, w, h, spread.radius, line);
        const long n = static_cast<long>(w) * h;
        for (long i = 0; i < n; ++i)
          values[i] = static_cast<unsigned char>((values[i] * kPeak) >> 8);
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

  /** Which mask gray means "take the source": decided once by pixels. */
  bool gBlackIsSource = true;
  CTabHandle gGrays = 0;

  /** An 8-bit gray GWorld; level(x, y) is the source weight 0..255. */
  GWorldPtr NewGrayMask(const Rect &bounds)
  {
    GWorldPtr world = 0;
    if (NewGWorld(&world, 8, &bounds, gGrays, 0, 0) != noErr)
      return 0;
    return world;
  }

  /** Index for a source weight under the mask world's own device. */
  void MaskIndices(GWorldPtr mask, unsigned char *index)
  {
    CGrafPtr savedPort;
    GDHandle savedDevice;
    GetGWorld(&savedPort, &savedDevice);
    SetGWorld(mask, 0);
    for (short a = 0; a < 256; ++a)
    {
      const unsigned short level = static_cast<unsigned short>((gBlackIsSource ? 255 - a : a) * 0x0101);
      RGBColor gray;
      gray.red = gray.green = gray.blue = level;
      index[a] = static_cast<unsigned char>(Color2Index(&gray));
    }
    SetGWorld(savedPort, savedDevice);
  }

  struct ShadowMask
  {
    GWorldPtr mask;
    RgnHandle body;       // outer-local
    RgnHandle shadowRgn;  // offset body minus body, outer-local
    unsigned char *alpha; // outer-local source weight, for expected values
    Rect outer;
    Rect bodyBox;
    Rect content;
    Spread spread;
    explicit ShadowMask(short s)
        : mask(0), body(0), shadowRgn(0), alpha(0), spread(s)
    {
    }
  };

  void FreeMask(ShadowMask &m)
  {
    if (m.mask)
      DisposeGWorld(m.mask);
    if (m.body)
      DisposeRgn(m.body);
    if (m.shadowRgn)
      DisposeRgn(m.shadowRgn);
    if (m.alpha)
      DisposePtr(reinterpret_cast<Ptr>(m.alpha));
    m.mask = 0;
    m.body = m.shadowRgn = 0;
    m.alpha = 0;
  }

  bool BuildMask(ShadowMask &m)
  {
    m.body = MakeBody(kBodyW, kBodyH);
    if (!m.body)
      return false;
    const short left = static_cast<short>(m.spread.reach - m.spread.offset);
    const short right = static_cast<short>(m.spread.reach + m.spread.offset);
    SetRect(&m.outer, 0, 0, static_cast<short>(left + kBodyW + right), static_cast<short>(left + kBodyH + right));
    OffsetRgn(m.body, left, left);
    SetRect(&m.bodyBox, left, left, left + kBodyW, left + kBodyH);
    SetRect(&m.content, left + kBand, left + kBand, left + kBodyW - kBand, left + kBodyH - kBand);
    m.shadowRgn = NewRgn();
    if (!m.shadowRgn)
      return false;
    CopyRgn(m.body, m.shadowRgn);
    OffsetRgn(m.shadowRgn, m.spread.offset, m.spread.offset);
    DiffRgn(m.shadowRgn, m.body, m.shadowRgn);
    m.alpha = BuildAlpha(m.body, m.outer, m.spread);
    if (!m.alpha)
      return false;
    m.mask = NewGrayMask(m.outer);
    char *base = 0;
    long rowBytes = 0;
    if (!m.mask || !PixelsOf(m.mask, base, rowBytes))
      return false;
    unsigned char index[256];
    MaskIndices(m.mask, index);
    const short w = m.outer.right, h = m.outer.bottom;
    for (short y = 0; y < h; ++y)
    {
      unsigned char *row = reinterpret_cast<unsigned char *>(base) + y * rowBytes;
      const unsigned char *src = m.alpha + static_cast<long>(y) * w;
      for (short x = 0; x < w; ++x)
        row[x] = index[src[x]];
    }
    // The opaque body is drawn afterwards; keep the mask from darkening it.
    CGrafPtr savedPort;
    GDHandle savedDevice;
    GetGWorld(&savedPort, &savedDevice);
    SetGWorld(m.mask, 0);
    RGBColor none;
    none.red = none.green = none.blue = static_cast<unsigned short>(gBlackIsSource ? 0xFFFF : 0);
    RGBForeColor(&none);
    PaintRgn(m.body);
    ForeColor(blackColor);
    SetGWorld(savedPort, savedDevice);
    return true;
  }

  /** A solid black source at the canvas depth, large enough for every frame. */
  GWorldPtr NewBlackSource(short depth, const Rect &bounds)
  {
    GWorldPtr world = 0;
    if (NewGWorld(&world, depth, &bounds, 0, 0, 0) != noErr || !world)
      return 0;
    char *base = 0;
    long rowBytes = 0;
    if (!PixelsOf(world, base, rowBytes))
    {
      DisposeGWorld(world);
      return 0;
    }
    CGrafPtr savedPort;
    GDHandle savedDevice;
    GetGWorld(&savedPort, &savedDevice);
    SetGWorld(world, 0);
    ForeColor(blackColor);
    PaintRect(&bounds);
    SetGWorld(savedPort, savedDevice);
    return world;
  }

  enum Method { PLAIN_COPY, DEEP_WHOLE, DEEP_STRIPS, MASK_WHOLE, BLEND_WHOLE };

  void Strips(const ShadowMask &m, Rect *strips)
  {
    const Rect &o = m.outer;
    const Rect &c = m.content;
    SetRect(&strips[0], 0, 0, c.right, c.top);
    SetRect(&strips[1], c.right, 0, o.right, c.bottom);
    SetRect(&strips[2], c.left, c.bottom, o.right, o.bottom);
    SetRect(&strips[3], 0, c.top, c.left, o.bottom);
  }

  /** One composite of the shadow frame at (atX, atY) on the current port. */
  void Composite(Method method, const ShadowMask &m, GWorldPtr source, short atX, short atY, const Rect &portRect)
  {
    Rect dst = m.outer;
    OffsetRect(&dst, atX, atY);
    const BitMap *src = BitsOf(source);
    const BitMap *mask = BitsOf(m.mask);
    switch (method)
    {
    case PLAIN_COPY:
      CopyBits(src, PortBits(), &m.outer, &dst, srcCopy, 0);
      break;
    case DEEP_WHOLE:
      CopyDeepMask(src, mask, PortBits(), &m.outer, &m.outer, &dst, srcCopy, 0);
      break;
    case DEEP_STRIPS:
    {
      Rect strips[4];
      Strips(m, strips);
      for (short s = 0; s < 4; ++s)
      {
        Rect piece = strips[s];
        OffsetRect(&piece, atX, atY);
        ClipRect(&piece);
        CopyDeepMask(src, mask, PortBits(), &strips[s], &strips[s], &piece, srcCopy, 0);
      }
      ClipRect(&portRect);
      break;
    }
    case MASK_WHOLE:
      CopyMask(src, mask, PortBits(), &m.outer, &m.outer, &dst);
      break;
    case BLEND_WHOLE:
    {
      RGBColor half;
      half.red = half.green = half.blue = 0x8000;
      OpColor(&half);
      OffsetRgn(m.shadowRgn, atX, atY);
      CopyBits(src, PortBits(), &m.outer, &dst, blend, m.shadowRgn);
      OffsetRgn(m.shadowRgn, static_cast<short>(-atX), static_cast<short>(-atY));
      break;
    }
    }
  }

  void DrawBody(const ShadowMask &m, short atX, short atY)
  {
    OffsetRgn(m.body, atX, atY);
    BackColor(whiteColor);
    EraseRgn(m.body);
    ForeColor(blackColor);
    FrameRgn(m.body);
    OffsetRgn(m.body, static_cast<short>(-atX), static_cast<short>(-atY));
  }

  void PaintGround(const Rect &canvas)
  {
    static const RGBColor stripes[6] = {{0xFFFF, 0x6666, 0x6666}, {0x6666, 0xDDDD, 0x6666},
                                        {0x6666, 0x8888, 0xFFFF}, {0xFFFF, 0xEEEE, 0x4444},
                                        {0x4444, 0xDDDD, 0xDDDD}, {0xDDDD, 0x6666, 0xDDDD}};
    for (short x = 0; x < canvas.right; x += 12)
    {
      Rect r;
      SetRect(&r, x, 0, x + 12, canvas.bottom);
      RGBForeColor(&stripes[(x / 12) % 6]);
      PaintRect(&r);
    }
    for (short x = 0; x < canvas.right; x += 8)
    {
      Rect r;
      SetRect(&r, x, 150, x + 8, 200);
      RGBColor gray;
      gray.red = gray.green = gray.blue = static_cast<unsigned short>(0x2000 + (0xD000L * x) / canvas.right);
      RGBForeColor(&gray);
      PaintRect(&r);
    }
    ForeColor(blackColor);
  }

  struct Sample
  {
    const char *where;
    short x;
    short y;
    short alpha; // source weight 0..255, -1 when not known
    RGBColor ground;
  };

  void WriteRGB(std::FILE *file, const char *label, const RGBColor &c)
  {
    std::fprintf(file, " %s=%04x,%04x,%04x", label, c.red, c.green, c.blue);
  }

  void ReportSample(std::FILE *file, short depth, const Sample &s)
  {
    RGBColor result;
    GetCPixel(s.x, s.y, &result);
    std::fprintf(file, "SAMPLE depth=%d where=%s x=%d y=%d alpha=%d", depth, s.where, s.x, s.y, s.alpha);
    WriteRGB(file, "ground", s.ground);
    WriteRGB(file, "result", result);
    if (s.alpha >= 0)
    {
      RGBColor expected;
      expected.red = static_cast<unsigned short>((static_cast<unsigned long>(s.ground.red) * (255 - s.alpha)) / 255);
      expected.green = static_cast<unsigned short>((static_cast<unsigned long>(s.ground.green) * (255 - s.alpha)) / 255);
      expected.blue = static_cast<unsigned short>((static_cast<unsigned long>(s.ground.blue) * (255 - s.alpha)) / 255);
      WriteRGB(file, "expected", expected);
    }
    std::fprintf(file, "\n");
  }

  short AlphaAt(const ShadowMask &m, short lx, short ly)
  {
    if (lx < 0 || ly < 0 || lx >= m.outer.right || ly >= m.outer.bottom)
      return -1;
    return m.alpha[static_cast<long>(ly) * m.outer.right + lx];
  }

  /** Decide the deep-mask polarity on a 32-bit scratch: white ground, black
      source, mask left half black and right half white. */
  bool ProbePolarity(std::FILE *file)
  {
    Rect r;
    SetRect(&r, 0, 0, 16, 4);
    GWorldPtr dst = 0, src = 0, mask = 0;
    bool ok = NewGWorld(&dst, 32, &r, 0, 0, 0) == noErr && dst;
    ok = ok && NewGWorld(&src, 32, &r, 0, 0, 0) == noErr && src;
    ok = ok && (mask = NewGrayMask(r)) != 0;
    char *base = 0;
    long rowBytes = 0;
    ok = ok && PixelsOf(dst, base, rowBytes) && PixelsOf(src, base, rowBytes) && PixelsOf(mask, base, rowBytes);
    if (ok)
    {
      CGrafPtr savedPort;
      GDHandle savedDevice;
      GetGWorld(&savedPort, &savedDevice);
      SetGWorld(src, 0);
      ForeColor(blackColor);
      PaintRect(&r);
      SetGWorld(mask, 0);
      BackColor(whiteColor);
      EraseRect(&r);
      Rect half;
      SetRect(&half, 0, 0, 8, 4);
      ForeColor(blackColor);
      PaintRect(&half);
      SetGWorld(dst, 0);
      BackColor(whiteColor);
      EraseRect(&r);
      CopyDeepMask(BitsOf(src), BitsOf(mask), PortBits(), &r, &r, &r, srcCopy, 0);
      const short err = QDError();
      RGBColor underBlack, underWhite;
      GetCPixel(2, 2, &underBlack);
      GetCPixel(12, 2, &underWhite);
      SetGWorld(savedPort, savedDevice);
      gBlackIsSource = underBlack.red < underWhite.red;
      std::fprintf(file, "POLARITY qderr=%d under_black_mask=%04x,%04x,%04x under_white_mask=%04x,%04x,%04x black_is_source=%d\n",
                   err, underBlack.red, underBlack.green, underBlack.blue, underWhite.red, underWhite.green,
                   underWhite.blue, gBlackIsSource ? 1 : 0);
    }
    else
      std::fprintf(file, "POLARITY refused\n");
    if (dst)
      DisposeGWorld(dst);
    if (src)
      DisposeGWorld(src);
    if (mask)
      DisposeGWorld(mask);
    return ok;
  }

  /** A horizontal ramp mask: white (left) to black (right), independent of polarity. */
  GWorldPtr BuildRampMask()
  {
    Rect r;
    SetRect(&r, 0, 0, kRampW, kRampH);
    GWorldPtr mask = NewGrayMask(r);
    char *base = 0;
    long rowBytes = 0;
    if (!mask || !PixelsOf(mask, base, rowBytes))
    {
      if (mask)
        DisposeGWorld(mask);
      return 0;
    }
    CGrafPtr savedPort;
    GDHandle savedDevice;
    GetGWorld(&savedPort, &savedDevice);
    SetGWorld(mask, 0);
    unsigned char index[kRampW];
    for (short x = 0; x < kRampW; ++x)
    {
      RGBColor gray;
      gray.red = gray.green = gray.blue = static_cast<unsigned short>((255 - (x * 255L) / (kRampW - 1)) * 0x0101);
      index[x] = static_cast<unsigned char>(Color2Index(&gray));
    }
    SetGWorld(savedPort, savedDevice);
    for (short y = 0; y < kRampH; ++y)
    {
      unsigned char *row = reinterpret_cast<unsigned char *>(base) + y * rowBytes;
      for (short x = 0; x < kRampW; ++x)
        row[x] = index[x];
    }
    return mask;
  }

  unsigned long Elapsed(const UnsignedWide &before, const UnsignedWide &after)
  {
    return after.lo - before.lo;
  }

  void TimeComposite(std::FILE *file, short depth, const char *name, Method method, const ShadowMask &m,
                     GWorldPtr source, const Rect &canvas)
  {
    UnsignedWide before, after;
    Microseconds(&before);
    Composite(method, m, source, 8, 4, canvas);
    Microseconds(&after);
    unsigned long total = Elapsed(before, after);
    short reps = 1;
    if (total < 250000UL)
    {
      Microseconds(&before);
      for (short i = 0; i < 4; ++i)
        Composite(method, m, source, 8, 4, canvas);
      Microseconds(&after);
      total += Elapsed(before, after);
      reps = 5;
    }
    std::fprintf(file, "TIME depth=%d case=%s reps=%d us=%lu outer=%dx%d qderr=%d\n", depth, name, reps, total / reps,
                 m.outer.right, m.outer.bottom, QDError());
  }

  bool DumpCanvas(std::FILE *file, short depth, GWorldPtr canvas)
  {
    PixMapHandle pixels = GetGWorldPixMap(canvas);
    const long rowBytes = (**pixels).rowBytes & 0x3FFF;
    const char *base = GetPixBaseAddr(pixels);
    const long bytesPerRow = static_cast<long>(kCanvasW) * (depth / 8);
    bool ok = true;
    if (depth == 8)
    {
      CTabHandle table = (**pixels).pmTable;
      const short entries = static_cast<short>((**table).ctSize + 1);
      std::fprintf(file, "IMGD %d %d %d %d\n", depth, kCanvasW, kCanvasH, entries);
      for (short i = 0; i < entries && ok; ++i)
      {
        const RGBColor &rgb = (**table).ctTable[i].rgb;
        unsigned char entry[6] = {static_cast<unsigned char>(rgb.red >> 8), static_cast<unsigned char>(rgb.red),
                                  static_cast<unsigned char>(rgb.green >> 8), static_cast<unsigned char>(rgb.green),
                                  static_cast<unsigned char>(rgb.blue >> 8), static_cast<unsigned char>(rgb.blue)};
        ok = std::fwrite(entry, 1, 6, file) == 6;
      }
    }
    else
      std::fprintf(file, "IMGD %d %d %d 0\n", depth, kCanvasW, kCanvasH);
    for (short row = 0; row < kCanvasH && ok; ++row)
      ok = std::fwrite(base + row * rowBytes, 1, static_cast<size_t>(bytesPerRow), file)
           == static_cast<size_t>(bytesPerRow);
    return ok;
  }

  /** One canvas depth: picture, samples, dump, then timings. */
  bool RunDepth(std::FILE *file, short depth, ShadowMask &m8, ShadowMask &m16, GWorldPtr ramp)
  {
    Rect canvasRect;
    SetRect(&canvasRect, 0, 0, kCanvasW, kCanvasH);
    GWorldPtr canvas = 0;
    if (NewGWorld(&canvas, depth, &canvasRect, 0, 0, 0) != noErr || !canvas)
    {
      std::fprintf(file, "CANVAS depth=%d refused\n", depth);
      return false;
    }
    char *base = 0;
    long rowBytes = 0;
    GWorldPtr source = PixelsOf(canvas, base, rowBytes) ? NewBlackSource(depth, m16.outer) : 0;
    if (!source)
    {
      std::fprintf(file, "CANVAS depth=%d unreadable-or-no-source\n", depth);
      DisposeGWorld(canvas);
      return false;
    }
    CGrafPtr savedPort;
    GDHandle savedDevice;
    GetGWorld(&savedPort, &savedDevice);
    SetGWorld(canvas, 0);
    PenNormal();
    TextFont(0);
    TextSize(12);
    TextMode(srcOr);
    ClipRect(&canvasRect);
    PaintGround(canvasRect);

    const short at8X = 8, at16X = static_cast<short>(8 + m8.outer.right + 16), atY = 4;
    Sample samples[16];
    short count = 0;
    struct CardAt
    {
      const ShadowMask *m;
      short atX;
      const char *right1;
      const char *right2;
      const char *bottom1;
      const char *bottom2;
    };
    const CardAt cards[2] = {{&m8, at8X, "s8-right-near", "s8-right-out", "s8-bottom-dark", "s8-bottom-light"},
                             {&m16, at16X, "s16-right-near", "s16-right-out", "s16-bottom-dark", "s16-bottom-light"}};
    for (short c = 0; c < 2; ++c)
    {
      const ShadowMask &m = *cards[c].m;
      const short lx[4] = {static_cast<short>(m.bodyBox.right + 1),
                           static_cast<short>(m.bodyBox.right + m.spread.offset + 2),
                           static_cast<short>(m.bodyBox.left + 60), static_cast<short>(m.bodyBox.left + 180)};
      const short ly[4] = {static_cast<short>(m.bodyBox.top + 60), static_cast<short>(m.bodyBox.top + 60),
                           static_cast<short>(m.bodyBox.bottom + 1), static_cast<short>(m.bodyBox.bottom + 1)};
      const char *names[4] = {cards[c].right1, cards[c].right2, cards[c].bottom1, cards[c].bottom2};
      for (short k = 0; k < 4; ++k)
      {
        Sample &s = samples[count++];
        s.where = names[k];
        s.x = static_cast<short>(cards[c].atX + lx[k]);
        s.y = static_cast<short>(atY + ly[k]);
        s.alpha = AlphaAt(m, lx[k], ly[k]);
      }
    }
    // Row 2 controls: A blend 50%, B CopyDeepMask ramp, C CopyMask ramp.
    const short aX = 10, bX = 190, cX = 370;
    const short rampX[3] = {2, 80, 157};
    const char *bNames[3] = {"B-deep-ramp-left", "B-deep-ramp-mid", "B-deep-ramp-right"};
    const char *cNames[3] = {"C-mask-ramp-left", "C-mask-ramp-mid", "C-mask-ramp-right"};
    for (short k = 0; k < 3; ++k)
    {
      Sample &a = samples[count++];
      a.where = k == 0 ? "A-blend50-1" : (k == 1 ? "A-blend50-2" : "A-blend50-3");
      a.x = static_cast<short>(aX + 20 + 60 * k);
      a.y = kSampleY;
      a.alpha = 128;
    }
    for (short k = 0; k < 3; ++k)
    {
      Sample &b = samples[count++];
      b.where = bNames[k];
      b.x = static_cast<short>(bX + rampX[k]);
      b.y = kSampleY;
      b.alpha = static_cast<short>((rampX[k] * 255L) / (kRampW - 1));
    }
    for (short k = 0; k < 2; ++k)
    {
      Sample &cs = samples[count++];
      cs.where = cNames[k == 0 ? 1 : 2];
      cs.x = static_cast<short>(cX + rampX[k == 0 ? 1 : 2]);
      cs.y = kSampleY;
      cs.alpha = static_cast<short>((rampX[k == 0 ? 1 : 2] * 255L) / (kRampW - 1));
    }
    for (short i = 0; i < count; ++i)
      GetCPixel(samples[i].x, samples[i].y, &samples[i].ground);

    Composite(DEEP_WHOLE, m8, source, at8X, atY, canvasRect);
    std::fprintf(file, "QDERR depth=%d after=deep-s8 err=%d\n", depth, QDError());
    Composite(DEEP_WHOLE, m16, source, at16X, atY, canvasRect);
    DrawBody(m8, at8X, atY);
    DrawBody(m16, at16X, atY);
    {
      Rect sr, dr;
      SetRect(&sr, 0, 0, kRampW, kRampH);
      RGBColor half;
      half.red = half.green = half.blue = 0x8000;
      OpColor(&half);
      SetRect(&dr, aX, kRowTop, aX + kRampW, kRowTop + kRampH);
      CopyBits(BitsOf(source), PortBits(), &sr, &dr, blend, 0);
      std::fprintf(file, "QDERR depth=%d after=blend err=%d\n", depth, QDError());
      SetRect(&dr, bX, kRowTop, bX + kRampW, kRowTop + kRampH);
      CopyDeepMask(BitsOf(source), BitsOf(ramp), PortBits(), &sr, &sr, &dr, srcCopy, 0);
      std::fprintf(file, "QDERR depth=%d after=deep-ramp err=%d\n", depth, QDError());
      SetRect(&dr, cX, kRowTop, cX + kRampW, kRowTop + kRampH);
      CopyMask(BitsOf(source), BitsOf(ramp), PortBits(), &sr, &sr, &dr);
      std::fprintf(file, "QDERR depth=%d after=mask-ramp err=%d\n", depth, QDError());
    }
    for (short i = 0; i < count; ++i)
      ReportSample(file, depth, samples[i]);
    ForeColor(blackColor);
    MoveTo(m8.bodyBox.left + at8X + 12, atY + m8.bodyBox.top + 40);
    DrawString("\ps8 CopyDeepMask");
    MoveTo(m16.bodyBox.left + at16X + 12, atY + m16.bodyBox.top + 40);
    DrawString("\ps16 CopyDeepMask");
    MoveTo(aX + 4, kRowTop + 14);
    DrawString("\pA blend 50%");
    MoveTo(bX + 4, kRowTop + 14);
    DrawString("\pB CopyDeepMask ramp");
    MoveTo(cX + 4, kRowTop + 14);
    DrawString("\pC CopyMask ramp");
    SetGWorld(savedPort, savedDevice);
    bool ok = DumpCanvas(file, depth, canvas);

    SetGWorld(canvas, 0);
    TimeComposite(file, depth, "plain-copy-s8", PLAIN_COPY, m8, source, canvasRect);
    TimeComposite(file, depth, "deep-whole-s8", DEEP_WHOLE, m8, source, canvasRect);
    TimeComposite(file, depth, "deep-strips-s8", DEEP_STRIPS, m8, source, canvasRect);
    TimeComposite(file, depth, "plain-copy-s16", PLAIN_COPY, m16, source, canvasRect);
    TimeComposite(file, depth, "deep-whole-s16", DEEP_WHOLE, m16, source, canvasRect);
    TimeComposite(file, depth, "deep-strips-s16", DEEP_STRIPS, m16, source, canvasRect);
    TimeComposite(file, depth, "copymask-whole-s16", MASK_WHOLE, m16, source, canvasRect);
    TimeComposite(file, depth, "blend50-whole-s16", BLEND_WHOLE, m16, source, canvasRect);
    SetGWorld(savedPort, savedDevice);
    DisposeGWorld(source);
    DisposeGWorld(canvas);
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
  SetRect(&bounds, 0, 40, 200, 140);
  WindowPtr window = NewWindow(0, &bounds, "\pCard shadow probe 4", true, plainDBox,
                               reinterpret_cast<WindowPtr>(-1L), false, 0);
  if (!window)
  {
    ExitToShell();
    return 1;
  }
  SetPort(window);
  HideCursor();

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
  std::fprintf(file, "CARDPROBE4\n");
  gGrays = GetCTable(40);
  if (gGrays)
  {
    const ColorSpec &mid = (**gGrays).ctTable[128];
    std::fprintf(file, "GRAYS ctSize=%d entry0=%04x entry128=%04x,%04x,%04x entryLast=%04x\n", (**gGrays).ctSize,
                 (**gGrays).ctTable[0].rgb.red, mid.rgb.red, mid.rgb.green, mid.rgb.blue,
                 (**gGrays).ctTable[(**gGrays).ctSize].rgb.red);
  }
  else
    std::fprintf(file, "GRAYS refused\n");

  ok = gGrays && ProbePolarity(file);
  ShadowMask m8(8), m16(16);
  GWorldPtr ramp = 0;
  if (ok)
  {
    UnsignedWide before, after;
    Microseconds(&before);
    ok = BuildMask(m8);
    Microseconds(&after);
    std::fprintf(file, "BUILD case=mask-s8 ok=%d us=%lu outer=%dx%d\n", ok ? 1 : 0, Elapsed(before, after),
                 m8.outer.right, m8.outer.bottom);
    Microseconds(&before);
    ok = ok && BuildMask(m16);
    Microseconds(&after);
    std::fprintf(file, "BUILD case=mask-s16 ok=%d us=%lu outer=%dx%d\n", ok ? 1 : 0, Elapsed(before, after),
                 m16.outer.right, m16.outer.bottom);
    ramp = ok ? BuildRampMask() : 0;
    ok = ramp != 0;
  }
  if (ok)
  {
    const short depths[3] = {32, 16, 8};
    for (short i = 0; i < 3; ++i)
      if (!RunDepth(file, depths[i], m8, m16, ramp))
        ok = false;
  }
  else
    std::fprintf(file, "SETUP refused\n");
  if (ramp)
    DisposeGWorld(ramp);
  FreeMask(m8);
  FreeMask(m16);
  if (gGrays)
    DisposeCTable(gGrays);
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
