#include <Dialogs.h>
#include <Fonts.h>
#include <Menus.h>
#include <Quickdraw.h>
#include <TextEdit.h>
#include <Timer.h>
#include <Windows.h>
#include <Processes.h>
#include <cstdio>

#include "core/io/File.hpp"
#include "platform/file/AppLocation.hpp"
#include "platform/file/FileIO.hpp"

// Page 5 of the card probe (pages 1-4: a6997e5d, d2dc2f17, dcf1b0f8, b990d92f):
// the candidate ToolboxCardBandContext::render (feat/card-toolbox d56e7352)
// copied call for call, one call per band under that band's own clip, with
// the band rects of FRAME_PLACEMENT_CARD_BANDS (FramedLayout.hpp) and the
// CardStyle constants. (a) four 240x160-body cards over 50% gray, white, 25%
// and 75% grounds with a label in each content seat, dumped as 1-bit through
// CopyBits; (b) the cost of a full four-band repaint; (c) the cost of one band
// under a 20x20 dirty clip. Not for merge.
namespace
{
  enum { kWidth = 560, kHeight = 380, kBodyW = 240, kBodyH = 160, kReps = 50 };

  // CardStyle (common/app/style/CardStyle.hpp at d56e7352).
  enum { kCornerRadius = 6, kBorderBand = 3, kShadowLeft = 0, kShadowTop = 0, kShadowRight = 3, kShadowBottom = 3 };
  // Card.hpp: FrameInsets(shadow + border band) per edge.
  enum
  {
    kInsetLeft = kShadowLeft + kBorderBand,
    kInsetTop = kShadowTop + kBorderBand,
    kInsetRight = kShadowRight + kBorderBand,
    kInsetBottom = kShadowBottom + kBorderBand
  };
  enum Role { CARD_BAND_TOP, CARD_BAND_LEFT, CARD_BAND_RIGHT, CARD_BAND_BOTTOM, CARD_BAND_COUNT };
  const char *const kRoleNames[CARD_BAND_COUNT] = {"top", "left", "right", "bottom"};

  // kToolboxGradientPatterns[4] (ToolboxPaintGradient.hpp).
  const Pattern kShadowPattern = {{0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55}};

  /** A card frame (body plus shadow) and the four band rects of CARD_BANDS. */
  struct CardFrame
  {
    Rect card;
    Rect content;
    Rect bands[CARD_BAND_COUNT];
    CardFrame(short x, short y)
    {
      const short left = x, top = y;
      const short right = static_cast<short>(x + kShadowLeft + kBodyW + kShadowRight);
      const short bottom = static_cast<short>(y + kShadowTop + kBodyH + kShadowBottom);
      SetRect(&this->card, left, top, right, bottom);
      SetRect(&this->content, left + kInsetLeft, top + kInsetTop, right - kInsetRight, bottom - kInsetBottom);
      // Frame(left, top, right - i.right - left, i.top) and its three siblings.
      SetRect(&this->bands[CARD_BAND_TOP], left, top, right - kInsetRight, top + kInsetTop);
      SetRect(&this->bands[CARD_BAND_LEFT], left, top + kInsetTop, left + kInsetLeft, bottom);
      SetRect(&this->bands[CARD_BAND_RIGHT], right - kInsetRight, top, right, bottom - kInsetBottom);
      SetRect(&this->bands[CARD_BAND_BOTTOM], left + kInsetLeft, bottom - kInsetBottom, right, bottom);
    }
  };

  // ---- Copied from the candidate (ToolboxPaintSupport.hpp, ToolboxPaintGround.hpp,
  // ---- ToolboxCardBandContext.cpp); keep call for call.
  class PaintClip
  {
  public:
    explicit PaintClip(const Rect &rect)
        : saved_(NewRgn()), clip_(NewRgn())
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
    bool isActive() const { return this->saved_ && this->clip_; }
    bool touches(const Rect &rect) const { return !this->isActive() || RectInRgn(&rect, this->clip_); }

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
    ~TextInkScope() { ForeColor(this->saved_); }

  private:
    long saved_;
    TextInkScope(const TextInkScope &);
    TextInkScope &operator=(const TextInkScope &);
  };

  class CardInkRegions
  {
  public:
    CardInkRegions()
        : body(NewRgn()), shadow(NewRgn())
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
    ~CardPenScope() { SetPenState(&this->saved_); }

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

  void TimeLine(std::FILE *file, const char *name, short reps, const UnsignedWide &before, const UnsignedWide &after,
                unsigned long ticks)
  {
    const unsigned long total = Elapsed(before, after);
    std::fprintf(file, "TIME case=%s reps=%d total_us=%lu per_us=%lu ticks=%lu\n", name, reps, total, total / reps,
                 ticks);
  }

  struct Ground
  {
    const Pattern *pattern;
    ConstStr255Param label;
  };

  void Label(const CardFrame &card, ConstStr255Param text)
  {
    MoveTo(card.content.left + 10, card.content.top + 22);
    DrawString(text);
    MoveTo(card.content.left + 10, card.content.top + 40);
    DrawString("\pbody 240x160, content 234x154");
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
  WindowPtr window = NewWindow(0, &bounds, "\pCard shadow probe 5", true, plainDBox,
                               reinterpret_cast<WindowPtr>(-1L), false, 0);
  if (!window)
  {
    ExitToShell();
    return 1;
  }
  SetPort(window);
  HideCursor();
  Rect windowRect;
  SetRect(&windowRect, 0, 0, kWidth, kHeight);
  PenNormal();
  TextFont(0);
  TextSize(12);
  TextMode(srcOr);
  ForeColor(blackColor);
  BackColor(whiteColor);
  BackPat(&qd.white);
  ClipRect(&windowRect);
  EraseRect(&windowRect);

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
  std::fprintf(file, "CARDPROBE5\n");
  {
    GDHandle device = GetMainDevice();
    std::fprintf(file, "SCREEN depth=%d\n", device ? (*(*device)->gdPMap)->pixelSize : -1);
  }

  // (a) Four quadrants: 50% gray (the asked ground), white, 25% and 75%.
  const Ground grounds[4] = {{&qd.gray, "\pA  50% gray ground"},
                             {&qd.white, "\pB  white ground"},
                             {&qd.ltGray, "\pC  25% gray ground"},
                             {&qd.dkGray, "\pD  75% gray ground"}};
  const CardFrame cards[4] = {CardFrame(18, 14), CardFrame(298, 14), CardFrame(18, 204), CardFrame(298, 204)};
  for (short q = 0; q < 4; ++q)
  {
    Rect quad;
    SetRect(&quad, (q % 2) * (kWidth / 2), (q / 2) * (kHeight / 2), (q % 2 + 1) * (kWidth / 2),
            (q / 2 + 1) * (kHeight / 2));
    FillRect(&quad, grounds[q].pattern);
  }
  for (short q = 0; q < 4; ++q)
  {
    RenderCard(cards[q]);
    Label(cards[q], grounds[q].label);
  }
  for (short q = 0; q < 4; ++q)
  {
    const CardFrame &c = cards[q];
    std::fprintf(file, "CARD %d card=%d,%d,%d,%d content=%d,%d,%d,%d", q, c.card.left, c.card.top, c.card.right,
                 c.card.bottom, c.content.left, c.content.top, c.content.right, c.content.bottom);
    for (short role = 0; role < CARD_BAND_COUNT; ++role)
      std::fprintf(file, " %s=%d,%d,%d,%d", kRoleNames[role], c.bands[role].left, c.bands[role].top,
                   c.bands[role].right, c.bands[role].bottom);
    std::fprintf(file, "\n");
  }

  const long size = SnapshotBytes();
  Ptr picture = NewPtrClear(size);
  Ptr after = NewPtrClear(size);
  if (!picture || !after || !Snapshot(picture))
  {
    std::fprintf(file, "SNAPSHOT refused\n");
    ok = false;
  }

  // (b) Full four-band repaint of card A, each band under its own clip.
  const CardFrame &a = cards[0];
  UnsignedWide before, end;
  unsigned long ticks = 0;
  ClipRect(&windowRect);
  ticks = TickCount();
  Microseconds(&before);
  for (short i = 0; i < kReps; ++i)
    RenderCard(a);
  Microseconds(&end);
  TimeLine(file, "full-four-bands", kReps, before, end, TickCount() - ticks);
  for (short role = 0; role < CARD_BAND_COUNT; ++role)
  {
    char name[32];
    std::sprintf(name, "band-%s", kRoleNames[role]);
    ticks = TickCount();
    Microseconds(&before);
    for (short i = 0; i < kReps; ++i)
      RenderBand(a.bands[role], static_cast<Role>(role));
    Microseconds(&end);
    TimeLine(file, name, kReps, before, end, TickCount() - ticks);
  }

  // (c) One band under a 20x20 dirty clip (the caller's clip, as a damaged paint).
  struct Dirty
  {
    const char *name;
    Role role;
    short dx;
    short dy;
  };
  const Dirty dirty[4] = {{"dirty20-bottom-corner", CARD_BAND_BOTTOM, 228, 148},
                          {"dirty20-right-corner", CARD_BAND_RIGHT, 228, 148},
                          {"dirty20-top-straight", CARD_BAND_TOP, 110, -8},
                          {"dirty20-top-miss", CARD_BAND_TOP, 110, 60}};
  for (short d = 0; d < 4; ++d)
  {
    Rect damage;
    SetRect(&damage, a.card.left + dirty[d].dx, a.card.top + dirty[d].dy, a.card.left + dirty[d].dx + 20,
            a.card.top + dirty[d].dy + 20);
    ClipRect(&damage);
    ticks = TickCount();
    Microseconds(&before);
    for (short i = 0; i < kReps; ++i)
      RenderBand(a.bands[dirty[d].role], dirty[d].role);
    Microseconds(&end);
    ClipRect(&windowRect);
    TimeLine(file, dirty[d].name, kReps, before, end, TickCount() - ticks);
  }

  // Repainting over existing ink (no ground erase) must change nothing.
  if (picture && after && Snapshot(after))
    std::fprintf(file, "REPAINT idempotent_diffbytes=%ld\n", CountDiff(picture, after));

  // Card B (white ground) redrawn as one whole-card drawing must equal its four bands.
  FillRect(&cards[1].card, grounds[1].pattern);
  RenderWholeOnce(cards[1]);
  Label(cards[1], grounds[1].label);
  if (picture && after && Snapshot(after))
    std::fprintf(file, "MATCH whole_once_vs_bands_diffbytes=%ld\n", CountDiff(picture, after));

  // References: the whole-card drawing once (no band clips) and a ground fill of the card rect.
  ticks = TickCount();
  Microseconds(&before);
  for (short i = 0; i < kReps; ++i)
    RenderWholeOnce(a);
  Microseconds(&end);
  TimeLine(file, "ref-whole-card-once", kReps, before, end, TickCount() - ticks);
  ticks = TickCount();
  Microseconds(&before);
  for (short i = 0; i < kReps; ++i)
    FillRect(&a.card, &qd.gray);
  Microseconds(&end);
  TimeLine(file, "ref-ground-fill-card-rect", kReps, before, end, TickCount() - ticks);

  if (picture)
  {
    std::fprintf(file, "IMG1 %d %d\n", kWidth, kHeight);
    if (std::fwrite(picture, 1, static_cast<size_t>(size), file) != static_cast<size_t>(size))
      ok = false;
    std::fprintf(file, "\n");
  }
  if (picture)
    DisposePtr(picture);
  if (after)
    DisposePtr(after);
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
