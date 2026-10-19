// Probe only (decoration rally 2 page 5, not for merge): what a frosted first
// unit could look like and cost on an 8-bit color screen. Five panels share
// one document; each paints the same toolbar band a different way and logs
// the band effect's cost in microseconds. A second, basic (NewWindow) port
// asks whether a GWorld round trip keeps color there.
#include <cstdio>
#include <cstring>
#include <Controls.h>
#include <Dialogs.h>
#include <Events.h>
#include <Fonts.h>
#include <Gestalt.h>
#include <Menus.h>
#include <Palettes.h>
#include <QDOffscreen.h>
#include <Quickdraw.h>
#include <TextEdit.h>
#include <Timer.h>
#include <Windows.h>

#include "core/io/File.hpp"
#include "platform/file/AppLocation.hpp"
#include "platform/file/FileIO.hpp"

namespace
{
  const short kWidth = 456;
  const short kPanelHeight = 80;
  const short kBandHeight = 30;
  const short kPanels = 5;
  const short kPad = 3; // Rows below the band the blur may read.
  const short kShrink = 4;
  const short kMilk = 160; // White weight out of 256 (62.5 %).

  std::FILE *gLog = 0;
  GWorldPtr gWorld = 0;
  GWorldPtr gBasicWorld = 0;
  unsigned long *gLine = 0;
  unsigned long *gSmall = 0;
  unsigned long *gRows = 0;

  unsigned long NowUs()
  {
    UnsignedWide now;
    Microseconds(&now);
    return now.lo;
  }

  void Label(short x, short y, const char *text)
  {
    Str255 label;
    const std::size_t length = std::strlen(text);
    label[0] = static_cast<unsigned char>(length);
    std::memcpy(label + 1, text, length);
    MoveTo(x, y);
    DrawString(label);
  }

  void Rgb(unsigned short r, unsigned short g, unsigned short b)
  {
    const RGBColor color = {r, g, b};
    RGBForeColor(&color);
  }

  void Normal()
  {
    PenNormal();
    Rgb(0, 0, 0);
    const RGBColor white = {0xFFFF, 0xFFFF, 0xFFFF};
    RGBBackColor(&white);
  }

  // The document under every band: text lines, a colour stripe ramp and two
  // shapes, part of each under the band.
  void DrawDocument(short top, bool color)
  {
    const Rect panel = {top, 0, static_cast<short>(top + kPanelHeight), kWidth};
    ClipRect(&panel);
    if (color)
      Normal();
    else
    {
      ForeColor(blackColor);
      BackColor(whiteColor);
    }
    EraseRect(&panel);
    TextFont(3);
    TextSize(12);
    TextFace(normal);
    TextMode(srcOr);
    const char *const lines[] = {"The quick brown fox jumps over the lazy dog.",
                                 "Frosted glass on a 1990 Macintosh IIx, 8-bit",
                                 "color. Text and shapes pass under the bar.",
                                 "0123456789 ABCDEFGHIJ abcdefghij"};
    for (short i = 0; i < 4; ++i)
      Label(6, static_cast<short>(top + 12 + i * 16), lines[i]);
    const unsigned short ramp[][3] = {{0xFFFF, 0, 0},      {0xFFFF, 0x8000, 0}, {0xFFFF, 0xFFFF, 0},
                                      {0, 0xC000, 0},      {0, 0xC000, 0xC000}, {0, 0, 0xFFFF},
                                      {0x8000, 0, 0xFFFF}, {0xFFFF, 0, 0xFFFF}};
    for (short i = 0; i < 8; ++i)
    {
      const Rect stripe = {static_cast<short>(top + 2), static_cast<short>(300 + i * 8),
                           static_cast<short>(top + 70), static_cast<short>(308 + i * 8)};
      if (color)
        Rgb(ramp[i][0], ramp[i][1], ramp[i][2]);
      else
        ForeColor(i % 2 ? blueColor : redColor);
      PaintRect(&stripe);
    }
    const Rect oval = {static_cast<short>(top + 8), 378, static_cast<short>(top + 56), 418};
    if (color)
      Rgb(0xDDDD, 0x2222, 0x2222);
    else
      ForeColor(redColor);
    PaintOval(&oval);
    const Rect box = {static_cast<short>(top + 20), 410, static_cast<short>(top + 66), 450};
    if (color)
      Rgb(0x2222, 0x4444, 0xCCCC);
    else
      ForeColor(blueColor);
    PaintRect(&box);
    if (color)
      Normal();
    else
      ForeColor(blackColor);
    ClipRect(&qd.thePort->portRect);
  }

  struct Surface
  {
    unsigned long *base;
    long stride; // In pixels.
    short width;
    short height;
  };

  Surface SurfaceOf(GWorldPtr world)
  {
    PixMapHandle pixels = GetGWorldPixMap(world);
    Surface surface;
    surface.base = reinterpret_cast<unsigned long *>(GetPixBaseAddr(pixels));
    surface.stride = ((**pixels).rowBytes & 0x3FFF) / 4;
    surface.width = static_cast<short>((**pixels).bounds.right - (**pixels).bounds.left);
    surface.height = static_cast<short>((**pixels).bounds.bottom - (**pixels).bounds.top);
    return surface;
  }

  inline short Clamp(short i, short n) { return i < 0 ? 0 : (i >= n ? static_cast<short>(n - 1) : i); }

  inline unsigned long Pack(long r, long g, long b)
  {
    return (static_cast<unsigned long>(r) << 16) | (static_cast<unsigned long>(g) << 8) |
           static_cast<unsigned long>(b);
  }

  // One box pass over n pixels at the given stride; line holds a copy.
  void BoxLine(unsigned long *first, long stride, short n, short radius)
  {
    for (short i = 0; i < n; ++i)
      gLine[i] = first[i * stride];
    const long inv = 65536L / (2 * radius + 1);
    long r = 0, g = 0, b = 0;
    for (short i = static_cast<short>(-radius); i <= radius; ++i)
    {
      const unsigned long p = gLine[Clamp(i, n)];
      r += (p >> 16) & 255;
      g += (p >> 8) & 255;
      b += p & 255;
    }
    for (short x = 0; x < n; ++x)
    {
      first[x * stride] = Pack((r * inv) >> 16, (g * inv) >> 16, (b * inv) >> 16);
      const unsigned long out = gLine[Clamp(static_cast<short>(x - radius), n)];
      const unsigned long in = gLine[Clamp(static_cast<short>(x + radius + 1), n)];
      r += static_cast<long>((in >> 16) & 255) - static_cast<long>((out >> 16) & 255);
      g += static_cast<long>((in >> 8) & 255) - static_cast<long>((out >> 8) & 255);
      b += static_cast<long>(in & 255) - static_cast<long>(out & 255);
    }
  }

  void BoxBlur(const Surface &s, short radius)
  {
    for (short y = 0; y < s.height; ++y)
      BoxLine(s.base + y * s.stride, 1, s.width, radius);
    for (short x = 0; x < s.width; ++x)
      BoxLine(s.base + x, s.stride, s.height, radius);
  }

  inline unsigned long Milk(unsigned long p)
  {
    const long r = (p >> 16) & 255, g = (p >> 8) & 255, b = p & 255;
    return Pack(r + (((255 - r) * kMilk) >> 8), g + (((255 - g) * kMilk) >> 8),
                b + (((255 - b) * kMilk) >> 8));
  }

  void MilkRows(const Surface &s, short rows)
  {
    for (short y = 0; y < rows; ++y)
    {
      unsigned long *row = s.base + y * s.stride;
      for (short x = 0; x < s.width; ++x)
        row[x] = Milk(row[x]);
    }
  }

  // The cut-down variant: average 4x4 blocks, blur the small image with
  // radius 1, then stretch it back bilinearly over the band rows only.
  void ShrinkBlur(const Surface &s, short bandRows)
  {
    const short sw = static_cast<short>((s.width + kShrink - 1) / kShrink);
    const short sh = static_cast<short>((s.height + kShrink - 1) / kShrink);
    for (short j = 0; j < sh; ++j)
      for (short i = 0; i < sw; ++i)
      {
        long r = 0, g = 0, b = 0, n = 0;
        for (short y = static_cast<short>(j * kShrink); y < j * kShrink + kShrink && y < s.height; ++y)
          for (short x = static_cast<short>(i * kShrink); x < i * kShrink + kShrink && x < s.width; ++x)
          {
            const unsigned long p = s.base[y * s.stride + x];
            r += (p >> 16) & 255;
            g += (p >> 8) & 255;
            b += p & 255;
            ++n;
          }
        gSmall[j * sw + i] = Pack(r / n, g / n, b / n);
      }
    const Surface small = {gSmall, sw, sw, sh};
    BoxBlur(small, 1);
    // Horizontal stretch of every small row into gRows (sh rows of s.width).
    for (short j = 0; j < sh; ++j)
      for (short x = 0; x < s.width; ++x)
      {
        const long fx = ((static_cast<long>(x) * 256 + 128) / kShrink) - 128; // 8.8, centre-aligned
        const short i0 = Clamp(static_cast<short>(fx >> 8), sw);
        const short i1 = Clamp(static_cast<short>((fx >> 8) + 1), sw);
        const long w = fx < 0 ? 0 : (fx & 255);
        const unsigned long a = gSmall[j * sw + i0], c = gSmall[j * sw + i1];
        gRows[j * s.width + x] =
            Pack((((a >> 16) & 255) * (256 - w) + ((c >> 16) & 255) * w) >> 8,
                 (((a >> 8) & 255) * (256 - w) + ((c >> 8) & 255) * w) >> 8,
                 ((a & 255) * (256 - w) + (c & 255) * w) >> 8);
      }
    for (short y = 0; y < bandRows; ++y)
    {
      const long fy = ((static_cast<long>(y) * 256 + 128) / kShrink) - 128;
      const short j0 = Clamp(static_cast<short>(fy >> 8), sh);
      const short j1 = Clamp(static_cast<short>((fy >> 8) + 1), sh);
      const long w = fy < 0 ? 0 : (fy & 255);
      unsigned long *row = s.base + y * s.stride;
      for (short x = 0; x < s.width; ++x)
      {
        const unsigned long a = gRows[j0 * s.width + x], c = gRows[j1 * s.width + x];
        row[x] = Milk(Pack((((a >> 16) & 255) * (256 - w) + ((c >> 16) & 255) * w) >> 8,
                           (((a >> 8) & 255) * (256 - w) + ((c >> 8) & 255) * w) >> 8,
                           ((a & 255) * (256 - w) + (c & 255) * w) >> 8));
      }
    }
  }

  // The tuned cut-down variant: exact 4x4 block averages (shifts only, the
  // copy is 32 rows), a [1 2 1] small blur, and a 4x stretch whose four
  // phases have fixed eighth weights, so no inner loop multiplies or divides.
  const short kFastRows = 32;
  const short kSmallW = kWidth / 4;
  const short kSmallH = kFastRows / 4;
  unsigned char gMilk[256];
  unsigned char gSR[kSmallH][kSmallW], gSG[kSmallH][kSmallW], gSB[kSmallH][kSmallW];
  unsigned char gHR[kSmallH][kWidth], gHG[kSmallH][kWidth], gHB[kSmallH][kWidth];
  unsigned short gAccR[kSmallW], gAccG[kSmallW], gAccB[kSmallW];
  GWorldPtr gFastWorld = 0;
  unsigned long gFastIn = 0, gFastMid = 0, gFastOut = 0;

  void Smooth121(unsigned char *line, short n, short stride)
  {
    unsigned char prev = line[0];
    for (short i = 0; i < n; ++i)
    {
      const unsigned char here = line[i * stride];
      const unsigned char next = line[(i + 1 < n ? i + 1 : i) * stride];
      line[i * stride] = static_cast<unsigned char>((prev + 2 * here + next) >> 2);
      prev = here;
    }
  }

  template <int WA>
  inline unsigned char Mix(unsigned char a, unsigned char b)
  {
    return static_cast<unsigned char>((a * WA + b * (8 - WA)) >> 3);
  }

  void StretchRow(const unsigned char *small, unsigned char *wide)
  {
    for (short i = 0; i < kSmallW; ++i)
    {
      const unsigned char left = small[i > 0 ? i - 1 : 0];
      const unsigned char here = small[i];
      const unsigned char right = small[i + 1 < kSmallW ? i + 1 : i];
      unsigned char *out = wide + i * 4;
      out[0] = Mix<3>(left, here);
      out[1] = Mix<1>(left, here);
      out[2] = Mix<7>(here, right);
      out[3] = Mix<5>(here, right);
    }
  }

  template <int WA>
  void BlendRows(const unsigned char *ra, const unsigned char *ga, const unsigned char *ba, const unsigned char *rb,
                 const unsigned char *gb, const unsigned char *bb, unsigned long *row)
  {
    for (short x = 0; x < kWidth; ++x)
      row[x] = (static_cast<unsigned long>(gMilk[Mix<WA>(ra[x], rb[x])]) << 16) |
               (static_cast<unsigned long>(gMilk[Mix<WA>(ga[x], gb[x])]) << 8) | gMilk[Mix<WA>(ba[x], bb[x])];
  }

  void FastShrink(const Surface &s)
  {
    for (short j = 0; j < kSmallH; ++j)
    {
      for (short i = 0; i < kSmallW; ++i)
        gAccR[i] = gAccG[i] = gAccB[i] = 0;
      for (short y = static_cast<short>(j * 4); y < j * 4 + 4; ++y)
      {
        const unsigned long *row = s.base + y * s.stride;
        for (short x = 0; x < kWidth; ++x)
        {
          const unsigned long p = row[x];
          const short i = static_cast<short>(x >> 2);
          gAccR[i] = static_cast<unsigned short>(gAccR[i] + ((p >> 16) & 255));
          gAccG[i] = static_cast<unsigned short>(gAccG[i] + ((p >> 8) & 255));
          gAccB[i] = static_cast<unsigned short>(gAccB[i] + (p & 255));
        }
      }
      for (short i = 0; i < kSmallW; ++i)
      {
        gSR[j][i] = static_cast<unsigned char>(gAccR[i] >> 4);
        gSG[j][i] = static_cast<unsigned char>(gAccG[i] >> 4);
        gSB[j][i] = static_cast<unsigned char>(gAccB[i] >> 4);
      }
    }
    for (short j = 0; j < kSmallH; ++j)
    {
      Smooth121(gSR[j], kSmallW, 1);
      Smooth121(gSG[j], kSmallW, 1);
      Smooth121(gSB[j], kSmallW, 1);
    }
    for (short i = 0; i < kSmallW; ++i)
    {
      Smooth121(&gSR[0][i], kSmallH, kSmallW);
      Smooth121(&gSG[0][i], kSmallH, kSmallW);
      Smooth121(&gSB[0][i], kSmallH, kSmallW);
    }
    for (short j = 0; j < kSmallH; ++j)
    {
      StretchRow(gSR[j], gHR[j]);
      StretchRow(gSG[j], gHG[j]);
      StretchRow(gSB[j], gHB[j]);
    }
    for (short y = 0; y < kBandHeight; ++y)
    {
      const short j = static_cast<short>(y >> 2);
      const short up = j > 0 ? static_cast<short>(j - 1) : 0;
      const short down = j + 1 < kSmallH ? static_cast<short>(j + 1) : j;
      unsigned long *row = s.base + y * s.stride;
      switch (y & 3)
      {
      case 0:
        BlendRows<3>(gHR[up], gHG[up], gHB[up], gHR[j], gHG[j], gHB[j], row);
        break;
      case 1:
        BlendRows<1>(gHR[up], gHG[up], gHB[up], gHR[j], gHG[j], gHB[j], row);
        break;
      case 2:
        BlendRows<7>(gHR[j], gHG[j], gHB[j], gHR[down], gHG[down], gHB[down], row);
        break;
      default:
        BlendRows<5>(gHR[j], gHG[j], gHB[j], gHR[down], gHG[down], gHB[down], row);
        break;
      }
    }
  }

  // Index-domain variants: an 8-bit GWorld sharing the screen's CLUT, so both
  // CopyBits are plain byte copies. Colour math runs on three 7-bit channels
  // packed 10 bits apart in one long (r<<20 | g<<10 | b); every step keeps a
  // field below 1024, so one add or shift serves all three channels.
  const unsigned long kMask7 = 0x7FUL | (0x7FUL << 10) | (0x7FUL << 20);
  unsigned long gPacked[256];     // CLUT index -> packed 7-bit RGB
  unsigned char gMilkIndex[256];  // CLUT index -> index of its milked colour
  unsigned char gToRes[128];      // 7-bit channel -> milked, at ITab resolution
  unsigned char *gITable = 0;
  short gResShift = 0;
  GWorldPtr gIndexWorld = 0;
  unsigned long gSmallP[kSmallH][kSmallW];
  unsigned long gWideP[kSmallH][kWidth];
  unsigned long gIdxIn = 0, gIdxMid = 0, gIdxOut = 0;

  template <int WA>
  inline unsigned long MixP(unsigned long a, unsigned long b)
  {
    return ((a * WA + b * (8 - WA)) >> 3) & kMask7;
  }

  inline unsigned char ToIndex(unsigned long v)
  {
    return gITable[(static_cast<unsigned short>(gToRes[(v >> 20) & 127]) << (2 * gResShift)) |
                   (static_cast<unsigned short>(gToRes[(v >> 10) & 127]) << gResShift) | gToRes[v & 127]];
  }

  bool BuildIndexTables(std::FILE *log)
  {
    GDHandle device = GetMainDevice();
    CTabHandle clut = (**(**device).gdPMap).pmTable;
    RGBColor probe = {0xFFFF, 0, 0};
    const long red = Color2Index(&probe); // Also brings the device ITab up to date.
    ITabHandle inverse = (**device).gdITable;
    gResShift = (**inverse).iTabRes;
    gITable = reinterpret_cast<unsigned char *>((**inverse).iTTable);
    std::fprintf(log, "clut size=%d seed=%ld itab res=%d seed=%ld red_index=%ld\r", (**clut).ctSize + 1,
                 (**clut).ctSeed, gResShift, (**inverse).iTabSeed, red);
    for (short i = 0; i < 256; ++i)
      gPacked[i] = 0;
    for (short i = 0; i <= (**clut).ctSize && i < 256; ++i)
    {
      const RGBColor c = (**clut).ctTable[i].rgb;
      gPacked[i] = (static_cast<unsigned long>(c.red >> 9) << 20) |
                   (static_cast<unsigned long>(c.green >> 9) << 10) | (c.blue >> 9);
      RGBColor milked;
      milked.red = static_cast<unsigned short>(gMilk[c.red >> 8] * 257);
      milked.green = static_cast<unsigned short>(gMilk[c.green >> 8] * 257);
      milked.blue = static_cast<unsigned short>(gMilk[c.blue >> 8] * 257);
      gMilkIndex[i] = static_cast<unsigned char>(Color2Index(&milked));
    }
    for (short v = 0; v < 128; ++v)
      gToRes[v] = static_cast<unsigned char>(gMilk[(v << 1) | (v >> 6)] >> (8 - gResShift));
    const Rect bounds = {0, 0, kFastRows, kWidth};
    const QDErr error = NewGWorld(&gIndexWorld, 8, &bounds, clut, 0, 0);
    std::fprintf(log, "gworld8 err=%d\r", error);
    return error == noErr && LockPixels(GetGWorldPixMap(gIndexWorld));
  }

  struct Bytes
  {
    unsigned char *base;
    long stride;
  };

  Bytes BytesOf(GWorldPtr world)
  {
    PixMapHandle pixels = GetGWorldPixMap(world);
    Bytes bytes;
    bytes.base = reinterpret_cast<unsigned char *>(GetPixBaseAddr(pixels));
    bytes.stride = (**pixels).rowBytes & 0x3FFF;
    return bytes;
  }

  void MilkIndices(const Bytes &b)
  {
    for (short y = 0; y < kBandHeight; ++y)
    {
      unsigned char *row = b.base + y * b.stride;
      for (short x = 0; x < kWidth; ++x)
        row[x] = gMilkIndex[row[x]];
    }
  }

  void IndexShrink(const Bytes &b)
  {
    for (short j = 0; j < kSmallH; ++j)
    {
      unsigned long *small = gSmallP[j];
      for (short i = 0; i < kSmallW; ++i)
        small[i] = 0;
      for (short y = static_cast<short>(j * 4); y < j * 4 + 4; ++y)
      {
        const unsigned char *row = b.base + y * b.stride;
        for (short i = 0; i < kSmallW; ++i, row += 4)
        {
          const unsigned long four = gPacked[row[0]] + gPacked[row[1]] + gPacked[row[2]] + gPacked[row[3]];
          small[i] += (four >> 2) & kMask7;
        }
      }
      for (short i = 0; i < kSmallW; ++i)
        small[i] = (small[i] >> 2) & kMask7;
    }
    for (short j = 0; j < kSmallH; ++j)
    {
      unsigned long prev = gSmallP[j][0];
      for (short i = 0; i < kSmallW; ++i)
      {
        const unsigned long here = gSmallP[j][i];
        const unsigned long next = gSmallP[j][i + 1 < kSmallW ? i + 1 : i];
        gSmallP[j][i] = ((prev + here + here + next) >> 2) & kMask7;
        prev = here;
      }
    }
    for (short i = 0; i < kSmallW; ++i)
    {
      unsigned long prev = gSmallP[0][i];
      for (short j = 0; j < kSmallH; ++j)
      {
        const unsigned long here = gSmallP[j][i];
        const unsigned long next = gSmallP[j + 1 < kSmallH ? j + 1 : j][i];
        gSmallP[j][i] = ((prev + here + here + next) >> 2) & kMask7;
        prev = here;
      }
    }
    for (short j = 0; j < kSmallH; ++j)
    {
      const unsigned long *small = gSmallP[j];
      unsigned long *wide = gWideP[j];
      for (short i = 0; i < kSmallW; ++i, wide += 4)
      {
        const unsigned long left = small[i > 0 ? i - 1 : 0];
        const unsigned long here = small[i];
        const unsigned long right = small[i + 1 < kSmallW ? i + 1 : i];
        wide[0] = MixP<3>(left, here);
        wide[1] = MixP<1>(left, here);
        wide[2] = MixP<7>(here, right);
        wide[3] = MixP<5>(here, right);
      }
    }
    for (short y = 0; y < kBandHeight; ++y)
    {
      const short j = static_cast<short>(y >> 2);
      const short up = j > 0 ? static_cast<short>(j - 1) : 0;
      const short down = j + 1 < kSmallH ? static_cast<short>(j + 1) : j;
      unsigned char *row = b.base + y * b.stride;
      const unsigned long *a = (y & 3) < 2 ? gWideP[up] : gWideP[j];
      const unsigned long *c = (y & 3) < 2 ? gWideP[j] : gWideP[down];
      switch (y & 3)
      {
      case 0:
        for (short x = 0; x < kWidth; ++x)
          row[x] = ToIndex(MixP<3>(a[x], c[x]));
        break;
      case 1:
        for (short x = 0; x < kWidth; ++x)
          row[x] = ToIndex(MixP<1>(a[x], c[x]));
        break;
      case 2:
        for (short x = 0; x < kWidth; ++x)
          row[x] = ToIndex(MixP<7>(a[x], c[x]));
        break;
      default:
        for (short x = 0; x < kWidth; ++x)
          row[x] = ToIndex(MixP<5>(a[x], c[x]));
        break;
      }
    }
  }

  // Run 2 showed CopyBits leaving the 8-bit index in the low byte when the
  // window's port and the screen device are current; the GWorld must be the
  // current port and device while it is the destination.
  void CopyIn(GWorldPtr world, const Rect &from)
  {
    CGrafPtr savedPort;
    GDHandle savedDevice;
    GetGWorld(&savedPort, &savedDevice);
    GrafPtr source = qd.thePort;
    SetGWorld(world, 0);
    PixMapHandle pixels = GetGWorldPixMap(world);
    const Rect to = (**pixels).bounds;
    CopyBits(&source->portBits, &reinterpret_cast<GrafPtr>(world)->portBits, &from, &to, srcCopy, 0);
    SetGWorld(savedPort, savedDevice);
  }

  void CopyOut(GWorldPtr world, const Rect &band)
  {
    PixMapHandle pixels = GetGWorldPixMap(world);
    const Rect from = {0, 0, static_cast<short>(band.bottom - band.top),
                       static_cast<short>(band.right - band.left)};
    CopyBits(reinterpret_cast<const BitMap *>(*pixels), &qd.thePort->portBits, &from, &band, srcCopy, 0);
  }

  void Hairline(const Rect &band)
  {
    Rgb(0xBBBB, 0xBBBB, 0xBBBB);
    MoveTo(band.left, static_cast<short>(band.bottom - 1));
    LineTo(static_cast<short>(band.right - 1), static_cast<short>(band.bottom - 1));
    Normal();
  }

  enum Variant
  {
    TODAY,
    TINT,
    MILKY,
    BLUR,
    SHRINK,
    SHRINK_NAIVE,
    MILKY_INDEX
  };
  const Variant kPanelVariant[] = {TODAY, TINT, MILKY_INDEX, BLUR, SHRINK};
  const char *const kNames[] = {"D today (white)", "T tint EEEE (fake)", "L milky 62% (index LUT)",
                                "F fast shrink4 + milky", "I index shrink4 + milky"};

  void Effect(Variant variant, const Rect &band)
  {
    switch (variant)
    {
    case TODAY:
      EraseRect(&band);
      break;
    case TINT:
      Rgb(0xEEEE, 0xEEEE, 0xEEEE);
      PaintRect(&band);
      Hairline(band);
      break;
    case MILKY:
    {
      const RGBColor weight = {static_cast<unsigned short>(kMilk << 8), static_cast<unsigned short>(kMilk << 8),
                               static_cast<unsigned short>(kMilk << 8)};
      OpColor(&weight);
      PenMode(blend);
      Rgb(0xFFFF, 0xFFFF, 0xFFFF);
      PaintRect(&band);
      Normal();
      Hairline(band);
      break;
    }
    case BLUR:
    {
      const unsigned long t0 = NowUs();
      const Rect from = {band.top, band.left, static_cast<short>(band.top + kFastRows), band.right};
      CopyIn(gFastWorld, from);
      const unsigned long t1 = NowUs();
      FastShrink(SurfaceOf(gFastWorld));
      const unsigned long t2 = NowUs();
      CopyOut(gFastWorld, band);
      const unsigned long t3 = NowUs();
      gFastIn = t1 - t0;
      gFastMid = t2 - t1;
      gFastOut = t3 - t2;
      Hairline(band);
      break;
    }
    case SHRINK:
    {
      const unsigned long t0 = NowUs();
      const Rect from = {band.top, band.left, static_cast<short>(band.top + kFastRows), band.right};
      CopyIn(gIndexWorld, from);
      const unsigned long t1 = NowUs();
      const Bytes bytes = BytesOf(gIndexWorld);
      if (gLog)
        std::fprintf(gLog, "index in x=302 y=4: %02X x=342: %02X x=200: %02X\r",
                     bytes.base[4 * bytes.stride + 302], bytes.base[4 * bytes.stride + 342],
                     bytes.base[4 * bytes.stride + 200]);
      IndexShrink(bytes);
      const unsigned long t2 = NowUs();
      CopyOut(gIndexWorld, band);
      const unsigned long t3 = NowUs();
      gIdxIn = t1 - t0;
      gIdxMid = t2 - t1;
      gIdxOut = t3 - t2;
      Hairline(band);
      break;
    }
    case SHRINK_NAIVE:
    {
      const Rect from = {band.top, band.left, static_cast<short>(band.bottom + kPad), band.right};
      CopyIn(gWorld, from);
      const Surface surface = SurfaceOf(gWorld);
      if (gLog && variant == SHRINK)
      {
        PixMapHandle pm = GetGWorldPixMap(gWorld);
        std::fprintf(gLog, "pm rowBytes=0x%04X pixelType=%d pixelSize=%d cmpCount=%d cmpSize=%d pmVersion=%d "
                           "base=%08lX pixbase=%08lX 32bit=%d mmu=%d screen=%08lX\r",
                     static_cast<unsigned short>((**pm).rowBytes), (**pm).pixelType, (**pm).pixelSize,
                     (**pm).cmpCount, (**pm).cmpSize, (**pm).pmVersion,
                     reinterpret_cast<unsigned long>((**pm).baseAddr),
                     reinterpret_cast<unsigned long>(GetPixBaseAddr(pm)), PixMap32Bit(pm) ? 1 : 0,
                     static_cast<int>(GetMMUMode()),
                     reinterpret_cast<unsigned long>((**(**GetMainDevice()).gdPMap).baseAddr));
        const short xs[] = {200, 8, 302, 342, 430};
        for (short i = 0; i < 5; ++i)
          std::fprintf(gLog, "in x=%d y=4: %08lX\r", xs[i], surface.base[4 * surface.stride + xs[i]]);
      }
      ShrinkBlur(surface, kBandHeight);
      CopyOut(gWorld, band);
      Hairline(band);
      break;
    }
    case MILKY_INDEX:
    {
      const Rect from = {band.top, band.left, static_cast<short>(band.top + kFastRows), band.right};
      CopyIn(gIndexWorld, from);
      MilkIndices(BytesOf(gIndexWorld));
      CopyOut(gIndexWorld, band);
      Hairline(band);
      break;
    }
    }
  }

  // What sits on the bar: a push button (control), a title, a search field
  // that paints its own white.
  void DrawResidents(const Rect &band)
  {
    TextFont(3);
    TextSize(12);
    TextFace(bold);
    Label(static_cast<short>(band.left + 76), static_cast<short>(band.top + 20), "Notes");
    TextFace(normal);
    const Rect field = {static_cast<short>(band.top + 6), static_cast<short>(band.left + 150),
                        static_cast<short>(band.top + 24), static_cast<short>(band.left + 270)};
    EraseRect(&field);
    FrameRect(&field);
    Rgb(0x7777, 0x7777, 0x7777);
    Label(static_cast<short>(field.left + 5), static_cast<short>(field.top + 13), "Search");
    Normal();
  }

  unsigned long gCost[kPanels];
  unsigned long gContent[kPanels];

  void DrawColorWindow(WindowPtr window, bool timed)
  {
    SetPort(window);
    Normal();
    for (short k = 0; k < kPanels; ++k)
    {
      const short top = static_cast<short>(k * kPanelHeight);
      const Rect band = {top, 0, static_cast<short>(top + kBandHeight), kWidth};
      if (timed)
      {
        unsigned long best = 0xFFFFFFFFUL;
        for (short rep = 0; rep < 3; ++rep)
        {
          const unsigned long c0 = NowUs();
          DrawDocument(top, true);
          const unsigned long c1 = NowUs();
          Effect(kPanelVariant[k], band);
          const unsigned long c2 = NowUs();
          if (c2 - c1 < best)
            best = c2 - c1;
          gContent[k] = c1 - c0;
        }
        gCost[k] = best;
      }
      else
      {
        DrawDocument(top, true);
        Effect(kPanelVariant[k], band);
      }
      DrawResidents(band);
      char text[64];
      std::sprintf(text, "%s: %lu us", kNames[k], gCost[k]);
      TextSize(9);
      const Rect tag = {static_cast<short>(top + kPanelHeight - 13), 120, static_cast<short>(top + kPanelHeight - 1),
                        298};
      EraseRect(&tag);
      Label(122, static_cast<short>(top + kPanelHeight - 3), text);
      TextSize(12);
      Rgb(0, 0, 0);
      MoveTo(0, static_cast<short>(top + kPanelHeight - 1));
      LineTo(kWidth, static_cast<short>(top + kPanelHeight - 1));
    }
    DrawControls(window);
  }

  unsigned long gBasicPixelBefore = 0;
  unsigned long gBasicPixelAfter = 0;

  void DrawBasicWindow(WindowPtr window)
  {
    SetPort(window);
    DrawDocument(0, false);
    const Rect band = {0, 0, kBandHeight, 160};
    const Rect from = {0, 0, static_cast<short>(kBandHeight + kPad), 160};
    PixMapHandle pixels = GetGWorldPixMap(gBasicWorld);
    const Rect to = (**pixels).bounds;
    // The window's portBits is a BitMap here, not a PixMap.
    CopyBits(&window->portBits, reinterpret_cast<const BitMap *>(*pixels), &from, &to, srcCopy, 0);
    const Surface surface = SurfaceOf(gBasicWorld);
    gBasicPixelBefore = surface.base[20 * surface.stride + 100]; // A red stripe under the band.
    BoxBlur(surface, 3);
    MilkRows(surface, kBandHeight);
    gBasicPixelAfter = surface.base[20 * surface.stride + 100];
    const Rect out = {0, 0, kBandHeight, 160};
    CopyBits(reinterpret_cast<const BitMap *>(*pixels), &window->portBits, &out, &band, srcCopy, 0);
    ForeColor(blackColor);
    TextFace(bold);
    Label(8, 20, "basic port");
    TextFace(normal);
  }

  int RunProbe()
  {
    loka::platform::file::FileHandle file;
    if (!loka::platform::file::ResolveApplicationSidecar(
            loka::file::File::Application() << loka::file::File("LOG.TXT"), file))
      return 1;
    gLog = loka::platform::file::OpenWriteTruncate(file);
    if (!gLog)
      return 1;
    long version = 0;
    const bool colorQD = Gestalt(gestaltQuickdrawVersion, &version) == noErr && version >= gestalt32BitQD;
    std::fprintf(gLog, "qd_version=0x%04lX\r", version);
    if (!colorQD)
    {
      std::fprintf(gLog, "no 32-bit QuickDraw: frost off\rdone\r");
      std::fclose(gLog);
      return 0;
    }
    GDHandle device = GetMainDevice();
    std::fprintf(gLog, "depth_before=%d\r", (**(**device).gdPMap).pixelSize);
    if (HasDepth(device, 8, 1 << gdDevType, 1 << gdDevType))
      std::fprintf(gLog, "set_depth=8 err=%d\r", SetDepth(device, 8, 1 << gdDevType, 1 << gdDevType));
    else
      std::fprintf(gLog, "set_depth=8 unsupported\r");
    std::fprintf(gLog, "depth_now=%d\r", (**(**GetMainDevice()).gdPMap).pixelSize);

    const Rect bounds = {0, 0, static_cast<short>(kBandHeight + kPad), kWidth};
    const unsigned long a0 = NowUs();
    const QDErr worldError = NewGWorld(&gWorld, 32, &bounds, 0, 0, 0);
    const Rect basicBounds = {0, 0, static_cast<short>(kBandHeight + kPad), 160};
    const QDErr basicError = NewGWorld(&gBasicWorld, 32, &basicBounds, 0, 0, 0);
    for (short v = 0; v < 256; ++v)
      gMilk[v] = static_cast<unsigned char>(v + (((255 - v) * kMilk) >> 8));
    if (!BuildIndexTables(gLog))
      std::fprintf(gLog, "ERROR index tables\r");
    const Rect fastBounds = {0, 0, kFastRows, kWidth};
    const QDErr fastError = NewGWorld(&gFastWorld, 32, &fastBounds, 0, 0, 0);
    if (fastError != noErr || !LockPixels(GetGWorldPixMap(gFastWorld)))
      std::fprintf(gLog, "ERROR fast gworld %d\r", fastError);
    std::fprintf(gLog, "gworld32 %dx%d err=%d basic err=%d alloc_us=%lu\r", kWidth, kBandHeight + kPad,
                 worldError, basicError, NowUs() - a0);
    if (worldError != noErr || basicError != noErr || !LockPixels(GetGWorldPixMap(gWorld)) ||
        !LockPixels(GetGWorldPixMap(gBasicWorld)))
    {
      std::fprintf(gLog, "ERROR gworld\rdone\r");
      std::fclose(gLog);
      return 1;
    }
    gLine = reinterpret_cast<unsigned long *>(NewPtr(kWidth * 4));
    gSmall = reinterpret_cast<unsigned long *>(NewPtr(((kWidth + 3) / 4) * ((kBandHeight + kPad + 3) / 4) * 4));
    gRows = reinterpret_cast<unsigned long *>(NewPtr(kWidth * ((kBandHeight + kPad + 3) / 4) * 4));
    if (!gLine || !gSmall || !gRows)
    {
      std::fprintf(gLog, "ERROR buffers\rdone\r");
      std::fclose(gLog);
      return 1;
    }

    const Rect left = {40, 4, 440, static_cast<short>(4 + kWidth)};
    WindowPtr colorWindow = NewCWindow(0, &left, "\pColor port (NewCWindow)", true, noGrowDocProc,
                                       reinterpret_cast<WindowPtr>(-1L), false, 0);
    const Rect right = {40, 466, 120, 636};
    WindowPtr basicWindow = NewWindow(0, &right, "\pBasic port", true, noGrowDocProc,
                                      reinterpret_cast<WindowPtr>(-1L), false, 0);
    if (!colorWindow || !basicWindow)
    {
      std::fprintf(gLog, "ERROR windows\rdone\r");
      std::fclose(gLog);
      return 1;
    }
    for (short k = 0; k < kPanels; ++k)
    {
      const Rect button = {static_cast<short>(k * kPanelHeight + 6), 8, static_cast<short>(k * kPanelHeight + 24),
                           64};
      NewControl(colorWindow, &button, "\pOpen", true, 0, 0, 1, 0 /* pushButProc */, 0);
    }
    DrawColorWindow(colorWindow, true);
    DrawBasicWindow(basicWindow);
    for (short k = 0; k < kPanels; ++k)
      std::fprintf(gLog, "%s effect_us=%lu document_us=%lu\r", kNames[k], gCost[k], gContent[k]);
    std::fprintf(gLog, "index stages: copy_in_us=%lu compute_us=%lu copy_out_us=%lu\r", gIdxIn, gIdxMid, gIdxOut);
    {
      // The QD blend variant, timed once off-panel for comparison.
      SetPort(colorWindow);
      const Rect band = {0, 0, kBandHeight, kWidth};
      unsigned long best = 0xFFFFFFFFUL;
      for (short rep = 0; rep < 2; ++rep)
      {
        DrawDocument(0, true);
        const unsigned long t0 = NowUs();
        Effect(MILKY, band);
        const unsigned long t1 = NowUs();
        if (t1 - t0 < best)
          best = t1 - t0;
      }
      std::fprintf(gLog, "M milky 62%% (QD blend) effect_us=%lu\r", best);
      InvalRect(&band);
    }
    std::fprintf(gLog, "fast stages: copy_in_us=%lu compute_us=%lu copy_out_us=%lu\r", gFastIn, gFastMid, gFastOut);
    std::fprintf(gLog, "basic_port rowBytes=0x%04X pixel_before=%08lX after=%08lX\r",
                 static_cast<unsigned short>(basicWindow->portBits.rowBytes), gBasicPixelBefore, gBasicPixelAfter);
    std::fprintf(gLog, "done\r");
    int result = std::ferror(gLog) ? 1 : 0;
    if (!loka::platform::file::FlushWrite(gLog, file))
      result = 1;
    if (std::fclose(gLog) != 0)
      result = 1;
    gLog = 0;

    for (;;)
    {
      SystemTask();
      EventRecord event;
      if (!GetNextEvent(everyEvent, &event))
        continue;
      if (event.what == updateEvt)
      {
        WindowPtr window = reinterpret_cast<WindowPtr>(event.message);
        BeginUpdate(window);
        if (window == colorWindow)
          DrawColorWindow(window, false);
        else if (window == basicWindow)
          DrawBasicWindow(window);
        EndUpdate(window);
      }
      else if (event.what == keyDown && (event.modifiers & cmdKey))
        break;
    }
    return result;
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
  return RunProbe();
}
