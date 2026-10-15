#include <Dialogs.h>
#include <Fonts.h>
#include <Menus.h>
#include <Quickdraw.h>
#include <TextEdit.h>
#include <Timer.h>
#include <Windows.h>
#include <Processes.h>
#include <cstdio>
#include <cstring>
#include <QDOffscreen.h>
#include "../../../apple/toolbox/src/ToolboxPaintGradient.hpp"

#include "core/io/File.hpp"
#include "platform/file/AppLocation.hpp"
#include "platform/file/FileIO.hpp"

// Page 6: startup-built corner tiles and edge profiles, not for merge.
// The page 5 v1 painter below remains the independent timing/pixel reference.
namespace
{
  enum
  {
    kWidth = 560,
    kHeight = 380,
    kBodyW = 240,
    kBodyH = 160,
    kReps = 50
  };

  // CardStyle (common/app/style/CardStyle.hpp at d56e7352).
  enum
  {
    kCornerRadius = 6,
    kBorderBand = 3,
    kShadowLeft = 0,
    kShadowTop = 0,
    kShadowRight = 3,
    kShadowBottom = 3
  };
  enum Role
  {
    CARD_BAND_TOP,
    CARD_BAND_LEFT,
    CARD_BAND_RIGHT,
    CARD_BAND_BOTTOM,
    CARD_BAND_COUNT
  };
  const char *const kRoleNames[CARD_BAND_COUNT] = {"top", "left", "right", "bottom"};

  // kToolboxGradientPatterns[4] (ToolboxPaintGradient.hpp).
  const Pattern kShadowPattern = {{0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55}};

  /** A card frame (body plus shadow) and the four band rects of CARD_BANDS. */
  struct CardFrame
  {
    Rect card;
    Rect content;
    Rect bands[CARD_BAND_COUNT];
    CardFrame(short x,
              short y,
              short aL = 0,
              short aT = 0,
              short aR = 3,
              short aB = 3,
              short width = kBodyW,
              short height = kBodyH)
    {
      const short left = x, top = y;
      const short right = static_cast<short>(x + aL + width + aR);
      const short bottom = static_cast<short>(y + aT + height + aB);
      const short iL = aL + 3, iT = aT + 3, iR = aR + 3, iB = aB + 3;
      SetRect(&this->card, left, top, right, bottom);
      SetRect(&this->content, left + iL, top + iT, right - iR, bottom - iB);
      // Frame(left, top, right - i.right - left, i.top) and its three siblings.
      SetRect(&this->bands[CARD_BAND_TOP], left, top, right - iR, top + iT);
      SetRect(&this->bands[CARD_BAND_LEFT], left, top + iT, left + iL, bottom);
      SetRect(&this->bands[CARD_BAND_RIGHT], right - iR, top, right, bottom - iB);
      SetRect(&this->bands[CARD_BAND_BOTTOM], left + iL, bottom - iB, right, bottom);
    }
  };

  // ---- Copied from the candidate (ToolboxPaintSupport.hpp, ToolboxPaintGround.hpp,
  // ---- ToolboxCardBandContext.cpp); keep call for call.
  class PaintClip
  {
  public:
    explicit PaintClip(const Rect &rect)
        : saved_(NewRgn()),
          clip_(NewRgn())
    {
      if (!this->isActive())
        return;
      GetClip(this->saved_);
      RectRgn(this->clip_, &rect);
      SectRgn(this->saved_, this->clip_, this->clip_);
      SetClip(this->clip_);
    }
    ~PaintClip()
    {
      if (this->isActive())
        SetClip(this->saved_);
      if (this->clip_)
        DisposeRgn(this->clip_);
      if (this->saved_)
        DisposeRgn(this->saved_);
    }
    bool isActive() const
    {
      return this->saved_ && this->clip_;
    }
    bool touches(const Rect &rect) const
    {
      return !this->isActive() || RectInRgn(&rect, this->clip_);
    }

  private:
    RgnHandle saved_;
    RgnHandle clip_;
    PaintClip(const PaintClip &);
    PaintClip &operator=(const PaintClip &);
  };

  class TextInkScope
  {
  public:
    explicit TextInkScope(long ink)
    {
      GrafPtr port;
      GetPort(&port);
      this->saved_ = port->fgColor;
      ForeColor(ink);
    }
    ~TextInkScope()
    {
      ForeColor(this->saved_);
    }

  private:
    long saved_;
    TextInkScope(const TextInkScope &);
    TextInkScope &operator=(const TextInkScope &);
  };

  class CardInkRegions
  {
  public:
    CardInkRegions()
        : body(NewRgn()),
          shadow(NewRgn())
    {
    }
    ~CardInkRegions()
    {
      if (this->shadow)
        DisposeRgn(this->shadow);
      if (this->body)
        DisposeRgn(this->body);
    }
    RgnHandle body;
    RgnHandle shadow;

  private:
    CardInkRegions(const CardInkRegions &);
    CardInkRegions &operator=(const CardInkRegions &);
  };

  class CardPenScope
  {
  public:
    CardPenScope()
    {
      GetPenState(&this->saved_);
      PenNormal();
    }
    ~CardPenScope()
    {
      SetPenState(&this->saved_);
    }

  private:
    PenState saved_;
    CardPenScope(const CardPenScope &);
    CardPenScope &operator=(const CardPenScope &);
  };

  Rect CardBodyInBand(const Rect &band, Role role)
  {
    const int b = kBorderBand;
    const int diameter = 2 * kCornerRadius;
    Rect body = band;
    switch (role)
    {
    case CARD_BAND_TOP:
      body.left += kShadowLeft;
      body.top += kShadowTop;
      body.right += b;
      body.bottom = body.top + diameter;
      break;
    case CARD_BAND_RIGHT:
      body.top += kShadowTop;
      body.right -= kShadowRight;
      body.bottom += b;
      body.left = body.right - diameter;
      break;
    case CARD_BAND_BOTTOM:
      body.left -= b;
      body.right -= kShadowRight;
      body.bottom -= kShadowBottom;
      body.top = body.bottom - diameter;
      break;
    case CARD_BAND_LEFT:
      body.left += kShadowLeft;
      body.top -= b;
      body.bottom -= kShadowBottom;
      body.right = body.left + diameter;
      break;
    case CARD_BAND_COUNT:
      SetRect(&body, 0, 0, 0, 0);
      break;
    }
    return body;
  }

  /** ToolboxCardBandContext::render with rect_ == paintRect_ == band (the
      projection clip is the whole window here). */
  void RenderBand(const Rect &band, Role role)
  {
    if (EmptyRect(&band))
      return;
    PaintClip clip(band);
    if (!clip.isActive() || !clip.touches(band))
      return;
    CardInkRegions regions;
    if (!regions.body || !regions.shadow)
      return;
    CardPenScope pen;
    TextInkScope ink(blackColor);
    Rect body = CardBodyInBand(band, role);
    const short oval = 2 * kCornerRadius;
    OpenRgn();
    FrameRoundRect(&body, oval, oval);
    CloseRgn(regions.body);
    Rect shadow = body;
    OffsetRect(&shadow, kShadowRight, kShadowBottom);
    OpenRgn();
    FrameRoundRect(&shadow, oval, oval);
    CloseRgn(regions.shadow);
    DiffRgn(regions.shadow, regions.body, regions.shadow);
    PenPat(&kShadowPattern);
    PenMode(patOr);
    PaintRgn(regions.shadow);
    PenNormal();
    FrameRoundRect(&body, oval, oval);
  }
  // ---- End of the copied sequence.

  void RenderCard(const CardFrame &card)
  {
    for (short role = 0; role < CARD_BAND_COUNT; ++role)
      RenderBand(card.bands[role], static_cast<Role>(role));
  }

  /** Reference only: the same ink drawn once for the whole body, no band clips. */
  void RenderWholeOnce(const CardFrame &card)
  {
    CardInkRegions regions;
    if (!regions.body || !regions.shadow)
      return;
    CardPenScope pen;
    Rect body = card.card;
    body.right -= kShadowRight;
    body.bottom -= kShadowBottom;
    const short oval = 2 * kCornerRadius;
    OpenRgn();
    FrameRoundRect(&body, oval, oval);
    CloseRgn(regions.body);
    Rect shadow = body;
    OffsetRect(&shadow, kShadowRight, kShadowBottom);
    OpenRgn();
    FrameRoundRect(&shadow, oval, oval);
    CloseRgn(regions.shadow);
    DiffRgn(regions.shadow, regions.body, regions.shadow);
    PenPat(&kShadowPattern);
    PenMode(patOr);
    PaintRgn(regions.shadow);
    PenNormal();
    FrameRoundRect(&body, oval, oval);
  }

  unsigned long Elapsed(const UnsignedWide &before, const UnsignedWide &after)
  {
    return after.lo - before.lo;
  }

  long SnapshotBytes()
  {
    return static_cast<long>(kWidth / 8) * kHeight;
  }

  /** The Mac II frame buffer is NuBus memory; read it through QuickDraw. */
  bool Snapshot(Ptr bytes)
  {
    BitMap shot;
    shot.baseAddr = bytes;
    shot.rowBytes = kWidth / 8;
    SetRect(&shot.bounds, 0, 0, kWidth, kHeight);
    Rect content;
    SetRect(&content, 0, 0, kWidth, kHeight);
    CopyBits(&qd.thePort->portBits, &shot, &content, &shot.bounds, srcCopy, 0);
    return QDError() == noErr;
  }

  long CountDiff(Ptr a, Ptr b)
  {
    long diff = 0;
    const long size = SnapshotBytes();
    for (long i = 0; i < size; ++i)
      if (a[i] != b[i])
        ++diff;
    return diff;
  }

  void TimeLine(std::FILE *file,
                const char *name,
                short reps,
                const UnsignedWide &before,
                const UnsignedWide &after,
                unsigned long ticks)
  {
    const unsigned long total = Elapsed(before, after);
    std::fprintf(
        file, "TIME case=%s reps=%d total_us=%lu per_us=%lu ticks=%lu\n", name, reps, total, total / reps, ticks);
  }

  /** Lexical owner for startup scratch and snapshot storage. */
  class Bytes
  {
  public:
    explicit Bytes(long size)
        : data_(NewPtrClear(size))
    {
    }
    ~Bytes()
    {
      if (this->data_)
        DisposePtr(this->data_);
    }
    Ptr get() const
    {
      return this->data_;
    }

  private:
    Ptr data_;
    Bytes(const Bytes &);
    Bytes &operator=(const Bytes &);
  };

  /** A monochrome scratch port; all allocations and port restoration are scoped. */
  class Raster
  {
  public:
    Raster()
        : bytes_(40L * 224)
    {
      GetPort(&this->saved_);
      OpenPort(&this->port_);
      this->port_.portBits.baseAddr = this->bytes_.get();
      this->port_.portBits.rowBytes = 40;
      SetRect(&this->port_.portBits.bounds, 0, 0, 320, 224);
      this->port_.portRect = this->port_.portBits.bounds;
      ClipRect(&this->port_.portRect);
      RectRgn(this->port_.visRgn, &this->port_.portRect);
      PenNormal();
    }
    ~Raster()
    {
      ClosePort(&this->port_);
      SetPort(this->saved_);
    }
    bool valid() const
    {
      return this->bytes_.get() != 0;
    }
    void clear()
    {
      std::memset(this->bytes_.get(), 0, 40L * 224);
    }
    bool pixel(short x, short y) const
    {
      return (static_cast<unsigned char>(this->bytes_.get()[y * 40L + x / 8]) & (0x80 >> (x & 7))) != 0;
    }

  private:
    Bytes bytes_;
    GrafPort port_;
    GrafPtr saved_;
    Raster(const Raster &);
    Raster &operator=(const Raster &);
  };

  enum Look
  {
    DROP,
    W6,
    W8,
    W12,
    LOOK_COUNT
  };
  const char *const kLookNames[LOOK_COUNT] = {"DROP", "W6", "W8", "W12"};
  enum
  {
    kMaskW = 272,
    kMaskH = 200,
    kPad = 16,
    kTileStride = 6,
    kTileSide = 40
  };
  const unsigned char kBayer[8][8] = {{0, 48, 12, 60, 3, 51, 15, 63},
                                      {32, 16, 44, 28, 35, 19, 47, 31},
                                      {8, 56, 4, 52, 11, 59, 7, 55},
                                      {40, 24, 36, 20, 43, 27, 39, 23},
                                      {2, 50, 14, 62, 1, 49, 13, 61},
                                      {34, 18, 46, 30, 33, 17, 45, 29},
                                      {10, 58, 6, 54, 9, 57, 5, 53},
                                      {42, 26, 38, 22, 41, 25, 37, 21}};

  /** Completed tables: four corner rectangles and one density profile per side.
      Only Build writes these facts; drawing receives const references. */
  struct Tables
  {
    Look look;
    short left, top, right, bottom, cut;
    // Fixed capacity storage; byte reporting also gives packed serialized size.
    unsigned char tiles[2][4][kTileStride * kTileSide];
    unsigned char profile[4][16];
    explicit Tables(Look value)
        : look(value),
          left(0),
          top(0),
          right(3),
          bottom(3),
          cut(12)
    {
      if (value != DROP)
      {
        const short radius = value == W6 ? 2 : value == W8 ? 3 : 4;
        const short dy = value == W6 ? 2 : value == W8 ? 3 : 4;
        this->left = this->right = 3 * radius;
        this->top = 3 * radius - dy;
        this->bottom = 3 * radius + dy;
        this->cut = 6 + 3 * radius;
      }
      std::memset(this->tiles, 0, sizeof(this->tiles));
      std::memset(this->profile, 0, sizeof(this->profile));
    }
    CardFrame frame(short x, short y, short width = kBodyW) const
    {
      return CardFrame(x, y, this->left, this->top, this->right, this->bottom, width);
    }
    Rect body(const CardFrame &f) const
    {
      Rect r = f.card;
      r.left += this->left;
      r.top += this->top;
      r.right -= this->right;
      r.bottom -= this->bottom;
      return r;
    }
    Rect corner(const Rect &b, short n) const
    {
      Rect r;
      SetRect(&r,
              (n & 1) ? b.right - this->cut : b.left - this->left,
              (n & 2) ? b.bottom - this->cut : b.top - this->top,
              (n & 1) ? b.right + this->right : b.left + this->cut,
              (n & 2) ? b.bottom + this->bottom : b.top + this->cut);
      return r;
    }
    short extent(short side) const
    {
      return side == CARD_BAND_TOP     ? this->top
             : side == CARD_BAND_LEFT  ? this->left
             : side == CARD_BAND_RIGHT ? this->right
                                       : this->bottom;
    }
  };

  // Startup only: sliding sums, O(mask pixels) per axis/pass, no radius-sized inner loop.
  void BlurAxis(const unsigned char *src, unsigned char *dst, short radius, bool vertical)
  {
    const short length = vertical ? kMaskH : kMaskW;
    const short lines = vertical ? kMaskW : kMaskH;
    const long step = vertical ? kMaskW : 1;
    for (short line = 0; line < lines; ++line)
    {
      const long base = vertical ? line : static_cast<long>(line) * kMaskW;
      unsigned short sum = 0;
      for (short i = 0; i <= radius; ++i)
        sum += src[base + i * step];
      for (short i = 0; i < length; ++i)
      {
        dst[base + i * step] = static_cast<unsigned char>((sum + radius) / (2 * radius + 1));
        if (i >= radius)
          sum -= src[base + (i - radius) * step];
        if (i + radius + 1 < length)
          sum += src[base + (i + radius + 1) * step];
      }
    }
  }

  /** Startup-only generation. Alpha is the subtracted undithered shadow, retained
      by the caller only until its reference dump has been written. */
  bool Build(Tables &t, unsigned char *alpha)
  {
    Raster raster;
    Bytes scratch(kMaskW * static_cast<long>(kMaskH));
    if (!raster.valid() || !scratch.get())
      return false;
    Rect body;
    SetRect(&body, kPad, kPad, kPad + kBodyW, kPad + kBodyH);
    if (t.look != DROP)
    {
      const short radius = t.look == W6 ? 2 : t.look == W8 ? 3 : 4;
      Rect shifted = body;
      OffsetRect(&shifted, 0, radius);
      PaintRoundRect(&shifted, 12, 12);
      for (short y = 0; y < kMaskH; ++y)
        for (short x = 0; x < kMaskW; ++x)
          alpha[y * static_cast<long>(kMaskW) + x] = raster.pixel(x, y) ? 255 : 0;
      for (short pass = 0; pass < 3; ++pass)
      {
        BlurAxis(alpha, reinterpret_cast<unsigned char *>(scratch.get()), radius, false);
        BlurAxis(reinterpret_cast<unsigned char *>(scratch.get()), alpha, radius, true);
      }
      raster.clear();
      PaintRoundRect(&body, 12, 12);
      for (short y = 0; y < kMaskH; ++y)
        for (short x = 0; x < kMaskW; ++x)
          if (raster.pixel(x, y))
            alpha[y * static_cast<long>(kMaskW) + x] = 0;
      for (short side = 0; side < 4; ++side)
        for (short d = 0; d < t.extent(side); ++d)
        {
          short x = kPad + kBodyW / 2, y = kPad + kBodyH / 2;
          if (side == CARD_BAND_TOP)
            y = kPad - 1 - d;
          if (side == CARD_BAND_LEFT)
            x = kPad - 1 - d;
          if (side == CARD_BAND_RIGHT)
            x = kPad + kBodyW + d;
          if (side == CARD_BAND_BOTTOM)
            y = kPad + kBodyH + d;
          t.profile[side][d] = static_cast<unsigned char>((alpha[y * static_cast<long>(kMaskW) + x] * 8U + 127) / 255);
        }
    }
    else
    {
      for (short d = 0; d < 3; ++d)
        t.profile[CARD_BAND_RIGHT][d] = t.profile[CARD_BAND_BOTTOM][d] = 4;
    }
    for (short parity = 0; parity < (t.look == DROP ? 2 : 1); ++parity)
    {
      raster.clear();
      if (t.look == DROP)
        RenderWholeOnce(CardFrame(kPad + parity, kPad));
      else
        FrameRoundRect(&body, 12, 12);
      for (short n = 0; n < 4; ++n)
      {
        const Rect r = t.corner(body, n);
        for (short y = 0; y < r.bottom - r.top; ++y)
          for (short x = 0; x < r.right - r.left; ++x)
          {
            bool ink = raster.pixel(r.left + x + parity, r.top + y);
            if (t.look != DROP)
            {
              const unsigned char density = alpha[(r.top + y) * static_cast<long>(kMaskW) + r.left + x];
              ink = ink || density > 4 * kBayer[(r.top + y - kPad) & 7][(r.left + x - kPad) & 7] + 2;
            }
            if (ink)
              t.tiles[parity][n][y * kTileStride + x / 8] |= 0x80 >> (x & 7);
          }
      }
    }
    return QDError() == noErr;
  }

  void RotatePatterns(Pattern *patterns, const Rect &body, bool windowAligned)
  {
    for (short level = 0; level < 9; ++level)
      for (short row = 0; row < 8; ++row)
      {
        const short shift = windowAligned ? 0 : body.left & 7;
        const unsigned char bits =
            loka::toolbox::kToolboxGradientPatterns[level].pat[windowAligned ? row : (row - body.top) & 7];
        patterns[level].pat[row] = static_cast<unsigned char>((bits >> shift) | (bits << ((8 - shift) & 7)));
      }
  }

  /** Once per band (or once for whole-card reference), at most four tile tests
      and four bounded profile scans. No rasterization, blur, or pixel loops. */
  void TableInk(const Tables &t, const CardFrame &frame, const PaintClip *clip)
  {
    CardPenScope pen;
    TextInkScope ink(blackColor);
    const Rect body = t.body(frame);
    const short parity = t.look == DROP ? (body.left + body.top) & 1 : 0;
    for (short n = 0; n < 4; ++n)
    {
      const Rect dest = t.corner(body, n);
      if (clip && !clip->touches(dest))
        continue;
      BitMap bits;
      bits.baseAddr = reinterpret_cast<Ptr>(const_cast<unsigned char *>(t.tiles[parity][n]));
      bits.rowBytes = kTileStride;
      SetRect(&bits.bounds, 0, 0, dest.right - dest.left, dest.bottom - dest.top);
      CopyBits(&bits, &qd.thePort->portBits, &bits.bounds, &dest, srcOr, 0);
    }
    Pattern patterns[9];
    RotatePatterns(patterns, body, t.look == DROP);
    PenMode(patOr);
    for (short side = 0; side < 4; ++side)
    {
      for (short d = 0; d < t.extent(side);)
      {
        const short begin = d;
        const unsigned char level = t.profile[side][d++];
        while (d < t.extent(side) && t.profile[side][d] == level)
          ++d;
        if (!level)
          continue;
        Rect r;
        if (side == CARD_BAND_TOP || side == CARD_BAND_BOTTOM)
          SetRect(&r,
                  body.left + t.cut,
                  side == CARD_BAND_TOP ? body.top - d : body.bottom + begin,
                  body.right - t.cut,
                  side == CARD_BAND_TOP ? body.top - begin : body.bottom + d);
        else
          SetRect(&r,
                  side == CARD_BAND_LEFT ? body.left - d : body.right + begin,
                  body.top + t.cut,
                  side == CARD_BAND_LEFT ? body.left - begin : body.right + d,
                  body.bottom - t.cut);
        if (clip && !clip->touches(r))
          continue;
        PenPat(&patterns[level]);
        PaintRect(&r);
      }
    }
    PenNormal();
    MoveTo(body.left + t.cut, body.top);
    LineTo(body.right - t.cut - 1, body.top);
    MoveTo(body.left + t.cut, body.bottom - 1);
    LineTo(body.right - t.cut - 1, body.bottom - 1);
    MoveTo(body.left, body.top + t.cut);
    LineTo(body.left, body.bottom - t.cut - 1);
    MoveTo(body.right - 1, body.top + t.cut);
    LineTo(body.right - 1, body.bottom - t.cut - 1);
  }

  void TableBand(const Tables &t, const CardFrame &frame, Role role)
  {
    PaintClip clip(frame.bands[role]);
    if (clip.isActive() && clip.touches(frame.bands[role]))
      TableInk(t, frame, &clip);
  }
  void TableCard(const Tables &t, const CardFrame &frame)
  {
    for (short role = 0; role < 4; ++role)
      TableBand(t, frame, static_cast<Role>(role));
  }

  bool DumpMono(std::FILE *file, const char *name, Ptr picture)
  {
    if (!Snapshot(picture))
      return false;
    std::fprintf(file, "IMG1 %s %d %d\n", name, kWidth, kHeight);
    const bool ok = std::fwrite(picture, 1, SnapshotBytes(), file) == static_cast<size_t>(SnapshotBytes());
    std::fprintf(file, "\n");
    return ok;
  }

  /** Untimed 8-bit GWorld with an explicit linear grayscale palette. */
  bool DumpAlpha(std::FILE *file, const Tables &t, const unsigned char *alpha)
  {
    CTabHandle colors = reinterpret_cast<CTabHandle>(NewHandleClear(sizeof(ColorTable) + 255L * sizeof(ColorSpec)));
    if (!colors)
      return false;
    (*colors)->ctSeed = GetCTSeed();
    (*colors)->ctFlags = 0;
    (*colors)->ctSize = 255;
    for (short i = 0; i < 256; ++i)
    {
      (*colors)->ctTable[i].value = i;
      (*colors)->ctTable[i].rgb.red = (*colors)->ctTable[i].rgb.green = (*colors)->ctTable[i].rgb.blue =
          static_cast<unsigned short>(i * 257U);
    }
    Rect bounds;
    SetRect(&bounds, 0, 0, kMaskW, kMaskH);
    GWorldPtr world = 0;
    const QDErr error = NewGWorld(&world, 8, &bounds, colors, 0, 0);
    DisposeHandle(reinterpret_cast<Handle>(colors));
    if (error != noErr || !world)
      return false;
    PixMapHandle pixels = GetGWorldPixMap(world);
    bool ok = LockPixels(pixels);
    if (ok)
    {
      Ptr base = GetPixBaseAddr(pixels);
      const long stride = (*pixels)->rowBytes & 0x3fff;
      for (short y = 0; y < kMaskH; ++y)
        for (short x = 0; x < kMaskW; ++x)
          base[y * stride + x] = static_cast<char>(255 - alpha[y * static_cast<long>(kMaskW) + x]);
      CGrafPtr saved;
      GDHandle device;
      GetGWorld(&saved, &device);
      SetGWorld(world, 0);
      RGBColor black = {0, 0, 0};
      RGBForeColor(&black);
      PenNormal();
      Rect body;
      SetRect(&body, kPad, kPad, kPad + kBodyW, kPad + kBodyH);
      FrameRoundRect(&body, 12, 12);
      SetGWorld(saved, device);
      std::fprintf(file, "PGM %s %d %d\nP5\n%d %d\n255\n", kLookNames[t.look], kMaskW, kMaskH, kMaskW, kMaskH);
      for (short y = 0; y < kMaskH; ++y)
        for (short x = 0; x < kMaskW; ++x)
        {
          const unsigned char index = static_cast<unsigned char>(base[y * stride + x]);
          const unsigned char gray = (*(*pixels)->pmTable)->ctTable[index].rgb.red >> 8;
          if (std::fputc(gray, file) == EOF)
            ok = false;
        }
      std::fprintf(file, "\n");
      UnlockPixels(pixels);
    }
    DisposeGWorld(world);
    return ok;
  }

  void Measure(std::FILE *file, const Tables &t, const Rect &window, bool legacy)
  {
    const CardFrame frame = t.frame(40, 40);
    for (short test = 0; test < 7; ++test)
    {
      ClipRect(&window);
      if (test >= 5)
      {
        Rect damage;
        const Rect body = t.body(frame);
        if (test == 5)
          SetRect(&damage, body.right - 10, body.bottom - 10, body.right + 10, body.bottom + 10);
        else
          SetRect(&damage, body.left + 50, body.top + 50, body.left + 70, body.top + 70);
        ClipRect(&damage);
      }
      UnsignedWide before, end;
      const unsigned long ticks = TickCount();
      Microseconds(&before);
      for (short rep = 0; rep < kReps; ++rep)
      {
        if (test == 0)
        {
          if (legacy)
            RenderCard(frame);
          else
            TableCard(t, frame);
        }
        else
        {
          const Role role = test < 5 ? static_cast<Role>(test - 1) : CARD_BAND_BOTTOM;
          if (legacy)
            RenderBand(frame.bands[role], role);
          else
            TableBand(t, frame, role);
        }
      }
      Microseconds(&end);
      char name[80];
      std::sprintf(name,
                   "%s-%s-%s",
                   legacy ? "v1" : "table",
                   kLookNames[t.look],
                   test == 0   ? "full"
                   : test < 5  ? kRoleNames[test - 1]
                   : test == 5 ? "dirty20-bottom"
                               : "miss-bottom");
      TimeLine(file, name, kReps, before, end, TickCount() - ticks);
    }
    ClipRect(&window);
  }

  bool Check(std::FILE *file, const char *name, long differences)
  {
    std::fprintf(file, "%s %s diffbytes=%ld\n", differences == 0 ? "PASS" : "FAIL", name, differences);
    return differences == 0;
  }

  bool PixelChecks(std::FILE *file, const Tables &t, const Rect &window, Ptr a, Ptr b, Ptr ground)
  {
    bool ok = true;
    char name[100];
    if (t.look == DROP)
      for (short parity = 0; parity < 2; ++parity)
        for (short g = 0; g < 2; ++g)
        {
          const CardFrame frame = t.frame(40 + parity, 40);
          const Pattern *pattern = g ? &qd.ltGray : &qd.white;
          FillRect(&window, pattern);
          RenderCard(frame);
          if (!Snapshot(a))
            return false;
          FillRect(&window, pattern);
          TableCard(t, frame);
          if (!Snapshot(b))
            return false;
          std::sprintf(name, "DROP-v1-%s-%s", parity ? "odd" : "even", g ? "25" : "white");
          if (!Check(file, name, CountDiff(a, b)))
            ok = false;
        }
    const CardFrame frame = t.frame(41, 42);
    FillRect(&window, &qd.white);
    TableCard(t, frame);
    if (!Snapshot(a))
      return false;
    FillRect(&window, &qd.white);
    TableInk(t, frame, 0);
    if (!Snapshot(b))
      return false;
    std::sprintf(name, "%s-bands-whole", kLookNames[t.look]);
    if (!Check(file, name, CountDiff(a, b)))
      ok = false;
    const Pattern *patterns[3] = {&qd.ltGray, &qd.gray, &qd.dkGray};
    for (short g = 0; g < 3; ++g)
    {
      FillRect(&window, patterns[g]);
      if (!Snapshot(ground))
        return false;
      TableCard(t, frame);
      if (!Snapshot(b))
        return false;
      long differences = 0;
      for (long i = 0; i < SnapshotBytes(); ++i)
        if (static_cast<unsigned char>(b[i])
            != (static_cast<unsigned char>(ground[i]) | static_cast<unsigned char>(a[i])))
          ++differences;
      std::sprintf(name, "%s-transparent-%d", kLookNames[t.look], (g + 1) * 25);
      if (!Check(file, name, differences))
        ok = false;
    }
    return ok;
  }

  void TableSize(std::FILE *file, const Tables &t)
  {
    const Rect b = t.body(t.frame(0, 0));
    long packed = 0, used = 0;
    for (short n = 0; n < 4; ++n)
    {
      const Rect r = t.corner(b, n);
      packed += ((r.right - r.left + 15) / 16) * 2L * (r.bottom - r.top);
      used += kTileStride * (r.bottom - r.top);
    }
    const short variants = t.look == DROP ? 2 : 1;
    const short profile = t.left + t.top + t.right + t.bottom;
    std::fprintf(file,
                 "TABLE %s allowances=%d,%d,%d,%d insets=%d,%d,%d,%d cut=%d variants=%d "
                 "packed_tiles=%ld profile=%d packed_total=%ld stride_tiles=%ld allocated_tiles_profile=%lu\n",
                 kLookNames[t.look],
                 t.left,
                 t.top,
                 t.right,
                 t.bottom,
                 t.left + 3,
                 t.top + 3,
                 t.right + 3,
                 t.bottom + 3,
                 t.cut,
                 variants,
                 packed * variants,
                 profile,
                 packed * variants + profile,
                 used * variants,
                 static_cast<unsigned long>(sizeof(t.tiles) + sizeof(t.profile)));
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
  SetRect(&bounds, 0, 40, kWidth, 40 + kHeight);
  WindowPtr window =
      NewWindow(0, &bounds, "\pCard shadow probe 6", true, plainDBox, reinterpret_cast<WindowPtr>(-1L), false, 0);
  if (!window)
  {
    ExitToShell();
    return 1;
  }
  SetPort(window);
  HideCursor();
  Rect windowRect;
  SetRect(&windowRect, 0, 0, kWidth, kHeight);
  ClipRect(&windowRect);
  PenNormal();
  ForeColor(blackColor);
  BackColor(whiteColor);
  BackPat(&qd.white);
  TextFont(0);
  TextSize(12);
  TextMode(srcOr);
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
  const unsigned long start = TickCount();
  std::fprintf(file, "CARDPROBE6\n");
  GDHandle device = GetMainDevice();
  const short depth = device ? (*(*device)->gdPMap)->pixelSize : -1;
  std::fprintf(file, "SCREEN depth=%d\n", depth);
  if (depth != 1)
  {
    std::fprintf(file, "FAIL screen-must-be-1-bit\n");
    ok = false;
  }
  {
    Tables tables[LOOK_COUNT] = {Tables(DROP), Tables(W6), Tables(W8), Tables(W12)};
    Bytes alpha(kMaskW * static_cast<long>(kMaskH));
    Bytes a(SnapshotBytes()), b(SnapshotBytes()), ground(SnapshotBytes());
    bool ready = alpha.get() && a.get() && b.get() && ground.get();
    for (short look = 0; ready && look < LOOK_COUNT; ++look)
    {
      ready = Build(tables[look], reinterpret_cast<unsigned char *>(alpha.get()));
      std::fprintf(file, "%s build-%s\n", ready ? "PASS" : "FAIL", kLookNames[look]);
      TableSize(file, tables[look]);
      if (ready && (look == W8 || look == W12))
      {
        const bool dumped = DumpAlpha(file, tables[look], reinterpret_cast<unsigned char *>(alpha.get()));
        std::fprintf(file, "%s alpha-%s\n", dumped ? "PASS" : "FAIL", kLookNames[look]);
        if (!dumped)
          ok = false;
      }
    }
    if (!ready)
    {
      std::fprintf(file, "FAIL startup-storage-or-generation\n");
      ok = false;
    }
    else
    {
      Measure(file, tables[DROP], windowRect, true);
      for (short look = 0; look < LOOK_COUNT; ++look)
      {
        Measure(file, tables[look], windowRect, false);
        if (!PixelChecks(file, tables[look], windowRect, a.get(), b.get(), ground.get()))
          ok = false;
      }
      const char *const labels[LOOK_COUNT] = {"DROP", "W6", "W8", "W12"};
      for (short g = 0; g < 2; ++g)
      {
        FillRect(&windowRect, g ? &qd.ltGray : &qd.white);
        for (short look = 0; look < LOOK_COUNT; ++look)
        {
          const CardFrame card = tables[look].frame(4 + look * 140, 40, 112);
          TableCard(tables[look], card);
          MoveTo(card.content.left + 8, card.content.top + 28);
          DrawText(labels[look], 0, std::strlen(labels[look]));
        }
        const bool dumped = DumpMono(file, g ? "ground25" : "white", a.get());
        std::fprintf(file, "%s image-%s\n", dumped ? "PASS" : "FAIL", g ? "ground25" : "white");
        if (!dumped)
          ok = false;
      }
    }
  }
  const unsigned long elapsed = TickCount() - start;
  std::fprintf(file, "%s budget-under-120s ticks=%lu\n", elapsed < 7200 ? "PASS" : "FAIL", elapsed);
  if (elapsed >= 7200)
    ok = false;
  std::fprintf(file, "RESULT %s\nEND\n", ok ? "PASS" : "FAIL");
  if (!loka::platform::file::FlushWrite(file, destination))
    ok = false;
  if (std::fclose(file) != 0)
    ok = false;
  ShowCursor();
  DisposeWindow(window);
  ExitToShell();
  return ok ? 0 : 1;
}
