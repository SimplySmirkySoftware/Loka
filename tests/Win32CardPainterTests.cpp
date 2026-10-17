#include "Win32CardPainterTests.hpp"
#include "Win32CardPainter.hpp"
#include "app/nodes/controls/Card.hpp"
#include "support/TestVerify.hpp"
#include <cstring>
#include <cstdio>
#include "app/style/CardShadow.hpp"
/** Independent pre-table region painter, retained for DROP pixel reports. */
namespace Win32CardV1Oracle
{
  namespace
  {
    /** Offset in layout units before projection, so every real shadow edge has
        the same absolute rounding in a band and in the whole-card reference. */
    RECT ProjectCardShadow(const loka::core::Frame &body,
                           const loka::app::CardStyle &style,
                           const loka::win32::Win32DisplayScale &scale)
    {
      return scale
          .projectFrame(
              loka::core::Frame(body.x + style.kShadowRight, body.y + style.kShadowBottom, body.width, body.height))
          .r;
    }

    /** A temporary region with a single lexical owner. */
    class CardRegion
    {
    public:
      explicit CardRegion(HRGN value)
          : value_(value)
      {
      }
      ~CardRegion()
      {
        if (this->value_)
          DeleteObject(this->value_);
      }
      HRGN get() const
      {
        return this->value_;
      }

    private:
      HRGN value_;
      CardRegion(const CardRegion &);
      CardRegion &operator=(const CardRegion &);
    };

    /** Fixed-diameter quarter ellipses plus the two straight middle strips.
        Quarter squares overlap at an odd, one-pixel-short minimum; OR preserves
        that union without asking GDI to clamp an ellipse to the body extent. */
    bool BuildCardBody(HRGN result, const RECT &body, int diameter, unsigned corners)
    {
      if (!result || diameter <= 0 || !SetRectRgn(result, 0, 0, 0, 0))
        return false;
      const int radius = (diameter + 1) / 2;
      CardRegion piece(CreateRectRgn(0, 0, 0, 0));
      if (!piece.get())
        return false;
      if (body.right - body.left > 2 * radius)
      {
        if (!SetRectRgn(piece.get(), body.left + radius, body.top, body.right - radius, body.bottom)
            || CombineRgn(result, result, piece.get(), RGN_OR) == ERROR)
          return false;
      }
      if (body.bottom - body.top > 2 * radius)
      {
        if (!SetRectRgn(piece.get(), body.left, body.top + radius, body.right, body.bottom - radius)
            || CombineRgn(result, result, piece.get(), RGN_OR) == ERROR)
          return false;
      }
      for (int corner = 0; corner < 4; ++corner)
      {
        if ((corners & (1u << corner)) == 0)
          continue;
        const bool right = (corner & 1) != 0;
        const bool bottom = (corner & 2) != 0;
        const int x = static_cast<int>(right ? body.right - diameter : body.left);
        const int y = static_cast<int>(bottom ? body.bottom - diameter : body.top);
        CardRegion ellipse(CreateEllipticRgn(x, y, x + diameter, y + diameter));
        if (!ellipse.get()
            || !SetRectRgn(piece.get(),
                           right ? body.right - radius : body.left,
                           bottom ? body.bottom - radius : body.top,
                           right ? body.right : body.left + radius,
                           bottom ? body.bottom : body.top + radius)
            || CombineRgn(piece.get(), piece.get(), ellipse.get(), RGN_AND) == ERROR
            || CombineRgn(result, result, piece.get(), RGN_OR) == ERROR)
          return false;
      }
      return true;
    }

    /** All masks and stencil storage belong to one paint, including refusal. */
    class CardInkObjects
    {
    public:
      CardInkObjects()
          : body_(CreateRectRgn(0, 0, 0, 0)),
            shadow_(CreateRectRgn(0, 0, 0, 0)),
            inner_(CreateRectRgn(0, 0, 0, 0)),
            border_(CreateRectRgn(0, 0, 0, 0)),
            bitmap_(0),
            brush_(0)
      {
        // WORD-aligned monochrome rows; dots never scale with the geometry.
        const WORD rows[8] = {0x00aa, 0x0055, 0x00aa, 0x0055, 0x00aa, 0x0055, 0x00aa, 0x0055};
        this->bitmap_ = CreateBitmap(8, 8, 1, 1, rows);
        if (this->bitmap_)
          this->brush_ = CreatePatternBrush(this->bitmap_);
      }
      ~CardInkObjects()
      {
        if (this->brush_)
          DeleteObject(this->brush_);
        if (this->bitmap_)
          DeleteObject(this->bitmap_);
      }
      bool prepare(const RECT &body, const RECT &shadow, int diameter, int pen, unsigned corners)
      {
        RECT inner = body;
        InflateRect(&inner, -pen, -pen);
        return this->body_.get() && this->shadow_.get() && this->inner_.get() && this->border_.get() && this->bitmap_
               && this->brush_ && BuildCardBody(this->body_.get(), body, diameter, corners)
               && BuildCardBody(this->inner_.get(), inner, diameter - 2 * pen, corners)
               && BuildCardBody(this->shadow_.get(), shadow, diameter, corners)
               && CombineRgn(this->shadow_.get(), this->shadow_.get(), this->body_.get(), RGN_DIFF) != ERROR
               && CombineRgn(this->border_.get(), this->body_.get(), this->inner_.get(), RGN_DIFF) != ERROR;
      }
      HRGN shadow() const
      {
        return this->shadow_.get();
      }
      HRGN border() const
      {
        return this->border_.get();
      }
      HBRUSH brush() const
      {
        return this->brush_;
      }

    private:
      CardRegion body_, shadow_, inner_, border_;
      HBITMAP bitmap_;
      HBRUSH brush_;
      CardInkObjects(const CardInkObjects &);
      CardInkObjects &operator=(const CardInkObjects &);
    };

    class CardDCScope
    {
    public:
      explicit CardDCScope(HDC dc)
          : dc_(dc),
            saved_(SaveDC(dc))
      {
      }
      ~CardDCScope()
      {
        if (this->saved_)
          RestoreDC(this->dc_, this->saved_);
      }
      bool valid() const
      {
        return this->saved_ != 0;
      }

    private:
      HDC dc_;
      int saved_;
      CardDCScope(const CardDCScope &);
      CardDCScope &operator=(const CardDCScope &);
    };
  } // namespace

  /** WM_PAINT: O(1) per nonempty band; 13 regions, one bitmap, one brush (8 objects peak),
      at most 20 region combinations plus the DC clip intersection. Whole-body
      rendering uses 19 regions and at most 32 combinations. No cached pixels or objects and no row walks. */
  static void PaintNativeCardInk(HDC dc,
                                 const RECT &native,
                                 const RECT &shadow,
                                 const loka::app::CardStyle &style,
                                 const loka::win32::Win32DisplayScale &scale,
                                 int phaseX,
                                 int phaseY,
                                 unsigned corners)
  {
    const int diameter = scale.projectLength(2 * style.kCornerRadius);
    const int projectedPen = scale.projectLength(1);
    const int pen = projectedPen > 0 ? projectedPen : 1;
    CardInkObjects objects;
    if (!objects.prepare(native, shadow, diameter, pen, corners))
      return;
    {
      CardDCScope saved(dc);
      if (!saved.valid() || ExtSelectClipRgn(dc, objects.shadow(), RGN_AND) == ERROR)
        return;
      const HGDIOBJ previous = SelectObject(dc, objects.brush());
      if (!previous || previous == HGDI_ERROR)
        return;
      SetTextColor(dc, RGB(0, 0, 0));
      SetBkColor(dc, RGB(255, 255, 255));
      SetBrushOrgEx(dc, -phaseX, -phaseY, NULL);
      // Destination AND brush: black dots darken; white leaves any ground intact.
      PatBlt(dc, shadow.left, shadow.top, shadow.right - shadow.left, shadow.bottom - shadow.top, 0x00A000C9);
    }
    // A region difference is an inside-only rounded pen, including widths > 1.
    FillRgn(dc, objects.border(), static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
  }

  void PaintCardInk(HDC dc,
                    const loka::core::Frame &body,
                    const loka::app::CardStyle &style,
                    const loka::win32::Win32DisplayScale &scale,
                    int deviceOriginX,
                    int deviceOriginY,
                    int phaseX,
                    int phaseY)
  {
    if (!dc || !body.hasSize())
      return;
    RECT native = scale.projectFrame(body).r;
    RECT shadow = ProjectCardShadow(body, style, scale);
    OffsetRect(&native, -deviceOriginX, -deviceOriginY);
    OffsetRect(&shadow, -deviceOriginX, -deviceOriginY);
    PaintNativeCardInk(dc, native, shadow, style, scale, phaseX, phaseY, 15u);
  }

} // namespace Win32CardV1Oracle

namespace loka
{
  namespace testing
  {
    void failWin32CardStencils(unsigned count);
    void resetWin32CardPaintOrder();
    unsigned readWin32CardPaintOrder();
  } // namespace testing
} // namespace loka

namespace
{
  COLORREF ground(int x, int y)
  {
    return ((x / 5 + y / 7) & 1) ? RGB(81, 137, 193) : RGB(209, 111, 57);
  }

  /** A fresh device-pixel surface; no window or desktop pixels enter the oracle. */
  class CardDib
  {
  public:
    CardDib(int width, int height, int originX = 0, int originY = 0)
        : dc(CreateCompatibleDC(NULL)),
          bitmap(0),
          old(0)
    {
      BITMAPINFO info;
      ZeroMemory(&info, sizeof(info));
      info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
      info.bmiHeader.biWidth = width;
      info.bmiHeader.biHeight = -height;
      info.bmiHeader.biPlanes = 1;
      info.bmiHeader.biBitCount = 32;
      info.bmiHeader.biCompression = BI_RGB;
      void *bits = 0;
      bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, NULL, 0);
      LOKA_VERIFY(dc && bitmap && bits);
      old = SelectObject(dc, bitmap);
      LOKA_VERIFY(old && old != HGDI_ERROR);
      for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
          LOKA_VERIFY(SetPixelV(dc, x, y, ground(x + originX, y + originY)));
    }
    ~CardDib()
    {
      SelectObject(dc, old);
      DeleteObject(bitmap);
      DeleteDC(dc);
    }
    HDC dc;

  private:
    HBITMAP bitmap;
    HGDIOBJ old;
    CardDib(const CardDib &);
    CardDib &operator=(const CardDib &);
  };
} // namespace

static void checkCardDensityMatrix();

void testWin32CardPainterPixels()
{
  checkCardDensityMatrix();
  using namespace loka::app;
  using namespace loka::win32;
  using loka::core::Frame;
  const UINT dpis[] = {96, 120, 144, 168, 192};
  // Body sizes: regular, the admitted minimum (14 lu) on each axis, and zero
  // shadow clearance (12 lu, below admission). At zero clearance the painter
  // alone must keep a band's displaced shadow equal to the whole reference:
  // offsetting the shadow after projection leaked one pixel of the 77x12 body
  // into the bottom band at scale 1.5 (188ddb2b).
  const int sizes[][2] = {{77, 57}, {14, 57}, {77, 14}, {14, 14}, {12, 57}, {77, 12}, {12, 12}};
  for (int metricsIndex = 0; metricsIndex < 2; ++metricsIndex)
    for (int dpiIndex = 0; dpiIndex < 5; ++dpiIndex)
      for (int sizeIndex = 0; sizeIndex < 7; ++sizeIndex)
      {
        const RailMetrics metrics(Ratio(), metricsIndex ? Ratio(3, 2) : Ratio());
        const Win32DisplayScale scale(dpis[dpiIndex], metrics);
        // Translated origin and asymmetric allowances exercise absolute-edge rounding.
        const int width = sizes[sizeIndex][0];
        const int height = sizes[sizeIndex][1];
        const Frame wholeBody(11, 17, width, height);
        const Frame bands[] = {Frame(11, 17, width - 3, 3),
                               Frame(11, 20, 3, height),
                               Frame(11 + width - 3, 17, 6, height - 3),
                               Frame(14, 17 + height - 3, width, 6)};
        CardDib reference(320, 320);
        CardDib split(320, 320);
        // Q1 limits the whole reference to the outer frame. Exclude the
        // content seat below: no band owns those pixels, including arc corners.
        const RECT outer = scale.projectFrame(Frame(11, 17, width + 3, height + 3)).r;
        const RECT seat = scale.projectFrame(Frame(14, 20, width - 6, height - 6)).r;
        const int savedReference = SaveDC(reference.dc);
        LOKA_VERIFY(savedReference != 0);
        LOKA_VERIFY(IntersectClipRect(reference.dc, outer.left, outer.top, outer.right, outer.bottom) != ERROR);
        LOKA_VERIFY(ExcludeClipRect(reference.dc, seat.left, seat.top, seat.right, seat.bottom) != ERROR);
        PaintCardInk(reference.dc, wholeBody, CardStyle(), scale, 0, 0, 0, 0);
        LOKA_VERIFY(RestoreDC(reference.dc, savedReference));
        for (int role = 0; role < CARD_BAND_COUNT; ++role)
        {
          const RECT native = scale.projectFrame(bands[role]).r;
          const int nativeWidth = static_cast<int>(native.right - native.left);
          const int nativeHeight = static_cast<int>(native.bottom - native.top);
          CardDib strip(nativeWidth, nativeHeight, native.left, native.top);
          const COLORREF oldText = SetTextColor(strip.dc, RGB(12, 34, 56));
          const COLORREF oldBack = SetBkColor(strip.dc, RGB(78, 90, 12));
          POINT oldOrigin;
          LOKA_VERIFY(SetBrushOrgEx(strip.dc, 5, 7, &oldOrigin));
          PaintCardBandInk(
              strip.dc, bands[role], CardBandProps(static_cast<CardBandRole>(role)), scale, native.left, native.top);
          LOKA_VERIFY(GetTextColor(strip.dc) == RGB(12, 34, 56) && GetBkColor(strip.dc) == RGB(78, 90, 12));
          POINT restored;
          LOKA_VERIFY(GetBrushOrgEx(strip.dc, &restored) && restored.x == 5 && restored.y == 7);
          SetTextColor(strip.dc, oldText);
          SetBkColor(strip.dc, oldBack);
          LOKA_VERIFY(BitBlt(split.dc, native.left, native.top, nativeWidth, nativeHeight, strip.dc, 0, 0, SRCCOPY));
        }
        int ink = 0;
        for (int y = 0; y < 320; ++y)
          for (int x = 0; x < 320; ++x)
          {
            const COLORREF expected = GetPixel(reference.dc, x, y);
            const COLORREF actual = GetPixel(split.dc, x, y);
            LOKA_VERIFY(actual == expected);
            LOKA_VERIFY(actual == RGB(0, 0, 0) || actual == ground(x, y));
            if (actual == RGB(0, 0, 0))
              ++ink;
          }
        LOKA_VERIFY(ink > 10);
        for (int y = static_cast<int>(seat.top); y < seat.bottom; ++y)
          for (int x = static_cast<int>(seat.left); x < seat.right; ++x)
            LOKA_VERIFY(GetPixel(split.dc, x, y) == ground(x, y));
        // Straight-side checks need space between top and bottom corner quadrants.
        if (height < 57)
          continue;
        const RECT body = scale.projectFrame(wholeBody).r;
        const int middle = static_cast<int>((body.top + body.bottom) / 2);
        const int pen = scale.projectLength(1);
        for (int x = static_cast<int>(body.left); x < body.left + pen; ++x)
          LOKA_VERIFY(GetPixel(split.dc, x, middle) == RGB(0, 0, 0));
        LOKA_VERIFY(GetPixel(split.dc, body.left + pen, middle) == ground(body.left + pen, middle));
      }
}

namespace
{
  void dumpCardPbm(HDC dc, int size, const char *label, int index)
  {
    char name[96];
    std::sprintf(name, "card-drop-%s-%d.pbm", label, index);
    FILE *file = std::fopen(name, "wb");
    LOKA_VERIFY(file != 0);
    std::fprintf(file, "P1\n%d %d\n", size, size);
    for (int y = 0; y < size; ++y)
    {
      for (int x = 0; x < size; ++x)
        std::fprintf(file, "%d ", GetPixel(dc, x, y) == RGB(0, 0, 0) ? 1 : 0);
      std::fprintf(file, "\n");
    }
    LOKA_VERIFY(std::fclose(file) == 0);
  }

  void verifyCardDC(HDC dc, HGDIOBJ bitmap, HGDIOBJ brush, HGDIOBJ pen, int rop, HRGN clip)
  {
    LOKA_VERIFY(GetCurrentObject(dc, OBJ_BITMAP) == bitmap);
    LOKA_VERIFY(GetCurrentObject(dc, OBJ_BRUSH) == brush);
    LOKA_VERIFY(GetCurrentObject(dc, OBJ_PEN) == pen);
    LOKA_VERIFY(GetROP2(dc) == rop);
    POINT origin;
    LOKA_VERIFY(GetBrushOrgEx(dc, &origin));
    LOKA_VERIFY(origin.x == 5 && origin.y == 7);
    HRGN after = CreateRectRgn(0, 0, 0, 0);
    LOKA_VERIFY(after != NULL);
    LOKA_VERIFY(GetClipRgn(dc, after) == 1);
    LOKA_VERIFY(EqualRgn(clip, after));
    LOKA_VERIFY(DeleteObject(after));
  }
} // namespace

/** Paired residues cover every origin and size residue per axis, without a
    Cartesian explosion. The reference samples common independently per pixel. */
static void checkCardDensityMatrix()
{
  using namespace loka::app;
  using namespace loka::win32;
  using loka::core::Frame;
  const UINT dpis[] = {96, 120, 144};
  const int size = 256;
  for (int extent = CARD_SHADOW_NONE; extent <= CARD_SHADOW_WIDE; ++extent)
    for (int dpi = 0; dpi < 3; ++dpi)
      for (int residue = 0; residue < 8; ++residue)
      {
        const CardStyle style(static_cast<CardShadowExtent>(extent));
        const detail::CardShadowTable &g = style.geometry();
        const Win32DisplayScale scale(dpis[dpi]);
        const Frame body(30 + residue, 30 + ((residue * 3) & 7), 64 + residue, 64 + ((residue * 5) & 7));
        const int left = body.x - g.allowance[0], top = body.y - g.allowance[1];
        const int right = body.x + body.width + g.allowance[2], bottom = body.y + body.height + g.allowance[3];
        const int il = 3 + g.allowance[0], it = 3 + g.allowance[1];
        const int ir = 3 + g.allowance[2], ib = 3 + g.allowance[3];
        const Frame bands[] = {Frame(left, top, right - ir - left, it),
                               Frame(left, top + it, il, bottom - top - it),
                               Frame(right - ir, top, ir, bottom - ib - top),
                               Frame(left + il, bottom - ib, right - left - il, ib)};
        CardDib border(size, size), whole(size, size), split(size, size), alpha(size, size), refused(size, size),
            partial(size, size);
        const RECT outer = scale.projectFrame(Frame(left, top, right - left, bottom - top)).r;
        const RECT seat = scale.projectFrame(Frame(body.x + 3, body.y + 3, body.width - 6, body.height - 6)).r;
        HDC targets[] = {border.dc, whole.dc, split.dc, alpha.dc, refused.dc, partial.dc};
        for (int i = 0; i < 6; ++i)
        {
          LOKA_VERIFY(IntersectClipRect(targets[i], outer.left, outer.top, outer.right, outer.bottom) != ERROR);
          LOKA_VERIFY(ExcludeClipRect(targets[i], seat.left, seat.top, seat.right, seat.bottom) != ERROR);
        }
        PaintCardInk(border.dc, body, CardStyle(CARD_SHADOW_NONE), scale, 0, 0, 0, 0);
        PaintCardInk(whole.dc, body, style, scale, 0, 0, 0, 0);
        for (int role = 0; role < CARD_BAND_COUNT; ++role)
        {
          const RECT native = scale.projectFrame(bands[role]).r;
          const int w = static_cast<int>(native.right - native.left), h = static_cast<int>(native.bottom - native.top);
          CardDib strip(w, h, static_cast<int>(native.left), static_cast<int>(native.top));
          LOKA_VERIFY(SetBrushOrgEx(strip.dc, 5, 7, NULL));
          HRGN clip = CreateRectRgn(0, 0, w, h);
          LOKA_VERIFY(clip != NULL && SelectClipRgn(strip.dc, clip) != ERROR);
          HBRUSH customBrush = CreateSolidBrush(RGB(7, 19, 37));
          HPEN customPen = CreatePen(PS_SOLID, 2, RGB(31, 17, 3));
          LOKA_VERIFY(customBrush != NULL && customPen != NULL);
          HGDIOBJ oldBrush = SelectObject(strip.dc, customBrush);
          HGDIOBJ oldPen = SelectObject(strip.dc, customPen);
          LOKA_VERIFY(oldBrush && oldBrush != HGDI_ERROR && oldPen && oldPen != HGDI_ERROR);
          LOKA_VERIFY(SetROP2(strip.dc, R2_XORPEN));
          HGDIOBJ bitmap = GetCurrentObject(strip.dc, OBJ_BITMAP), brush = GetCurrentObject(strip.dc, OBJ_BRUSH),
                  pen = GetCurrentObject(strip.dc, OBJ_PEN);
          const int rop = GetROP2(strip.dc);
          loka::testing::resetWin32CardPaintOrder();
          PaintCardBandInk(strip.dc,
                           bands[role],
                           CardBandProps(static_cast<CardBandRole>(role), style),
                           scale,
                           static_cast<int>(native.left),
                           static_cast<int>(native.top));
          LOKA_VERIFY(loka::testing::readWin32CardPaintOrder() == 12);
          verifyCardDC(strip.dc, bitmap, brush, pen, rop, clip);
          LOKA_VERIFY(BitBlt(split.dc, native.left, native.top, w, h, strip.dc, 0, 0, SRCCOPY));
          loka::testing::failWin32CardStencils(1);
          loka::testing::resetWin32CardPaintOrder();
          PaintCardBandInk(strip.dc,
                           bands[role],
                           CardBandProps(static_cast<CardBandRole>(role), style),
                           scale,
                           static_cast<int>(native.left),
                           static_cast<int>(native.top));
          loka::testing::failWin32CardStencils(0);
          LOKA_VERIFY(loka::testing::readWin32CardPaintOrder() == 12);
          verifyCardDC(strip.dc, bitmap, brush, pen, rop, clip);
          LOKA_VERIFY(SelectObject(strip.dc, oldBrush) == customBrush);
          LOKA_VERIFY(SelectObject(strip.dc, oldPen) == customPen);
          LOKA_VERIFY(DeleteObject(customBrush));
          LOKA_VERIFY(DeleteObject(customPen));
          LOKA_VERIFY(DeleteObject(clip));
        }
        const RECT damage = {outer.left + 2, outer.top + 1, outer.right - 1, outer.bottom - 2};
        LOKA_VERIFY(IntersectClipRect(partial.dc, damage.left, damage.top, damage.right, damage.bottom) != ERROR);
        PaintCardInk(partial.dc, body, style, scale, 0, 0, 0, 0);
        const CardStyle requested(static_cast<CardShadowExtent>(extent), CARD_SHADOW_ALPHA);
        PaintCardInk(alpha.dc, body, requested, scale, 0, 0, 0, 0);
        PaintCardInk(alpha.dc, body, requested, scale, 0, 0, 0, 0);
        LOKA_VERIFY(IntersectClipRect(alpha.dc, outer.left + 2, outer.top + 1, outer.right - 1, outer.bottom - 2)
                    != ERROR);
        PaintCardInk(alpha.dc, body, requested, scale, 0, 0, 0, 0);
        LOKA_VERIFY(requested.ink() == CARD_SHADOW_ALPHA);
        loka::testing::failWin32CardStencils(1);
        PaintCardInk(refused.dc, body, style, scale, 0, 0, 0, 0);
        loka::testing::failWin32CardStencils(0);
        for (int y = 0; y < size; ++y)
          for (int x = 0; x < size; ++x)
          {
            const bool inRing = x >= outer.left && x < outer.right && y >= outer.top && y < outer.bottom
                                && !(x >= seat.left && x < seat.right && y >= seat.top && y < seat.bottom);
            const bool dot = inRing
                             && CardShadowStencilInk(
                                 CardShadowDensity(style, body, scale.unprojectEdge(x), scale.unprojectEdge(y)), x, y);
            const COLORREF expected = dot ? RGB(0, 0, 0) : GetPixel(border.dc, x, y);
            LOKA_VERIFY(GetPixel(whole.dc, x, y) == expected);
            const bool damaged = x >= damage.left && x < damage.right && y >= damage.top && y < damage.bottom;
            LOKA_VERIFY(GetPixel(partial.dc, x, y) == (damaged ? expected : ground(x, y)));
            LOKA_VERIFY(GetPixel(split.dc, x, y) == expected);
            LOKA_VERIFY(GetPixel(alpha.dc, x, y) == expected);
            LOKA_VERIFY(GetPixel(refused.dc, x, y) == GetPixel(border.dc, x, y));
          }
        if (extent == CARD_SHADOW_DROP && dpi == 0)
        {
          CardDib v1(size, size);
          LOKA_VERIFY(IntersectClipRect(v1.dc, outer.left, outer.top, outer.right, outer.bottom) != ERROR);
          LOKA_VERIFY(ExcludeClipRect(v1.dc, seat.left, seat.top, seat.right, seat.bottom) != ERROR);
          Win32CardV1Oracle::PaintCardInk(v1.dc, body, style, scale, 0, 0, 0, 0);
          int difference = 0;
          for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x)
              if (GetPixel(v1.dc, x, y) != GetPixel(whole.dc, x, y))
                ++difference;
          std::printf("card DROP residue=%d v1 differences=%d\n", residue, difference);
          if (difference)
          {
            dumpCardPbm(v1.dc, size, "v1", residue);
            dumpCardPbm(whole.dc, size, "table", residue);
          }
        }
      }
}
