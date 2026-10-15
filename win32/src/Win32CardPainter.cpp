#include "Win32CardPainter.hpp"

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

void loka::win32::PaintCardInk(HDC dc,
                               const core::Frame &body,
                               const app::CardStyle &style,
                               const Win32DisplayScale &scale,
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

void loka::win32::PaintCardBandInk(HDC dc,
                                   const core::Frame &band,
                                   const app::CardBandProps &props,
                                   const Win32DisplayScale &scale,
                                   int phaseX,
                                   int phaseY)
{
  if (!dc || !band.hasSize())
    return;
  const core::Frame logicalBody = app::CardBodyInBand(band, props);
  RECT body = scale.projectFrame(logicalBody).r;
  RECT shadow = ProjectCardShadow(logicalBody, props.style, scale);
  const int diameter = scale.projectLength(2 * props.style.kCornerRadius);
  const RECT origin = scale.projectFrame(band).r;
  // Only the border's body-side sample must lie below the corner midpoint;
  // shadow allowance lies outside the body and is covered by its displaced mask.
  int thickness = 0;
  unsigned corners = 0;
  switch (props.role)
  {
  case app::CARD_BAND_TOP:
    corners = 3u;
    thickness = static_cast<int>(origin.bottom - body.top);
    body.bottom = body.top + 2 * diameter;
    shadow.bottom = shadow.top + 2 * diameter;
    break;
  case app::CARD_BAND_LEFT:
    corners = 5u;
    thickness = static_cast<int>(origin.right - body.left);
    body.right = body.left + 2 * diameter;
    shadow.right = shadow.left + 2 * diameter;
    break;
  case app::CARD_BAND_RIGHT:
    corners = 10u;
    thickness = static_cast<int>(body.right - origin.left);
    body.left = body.right - 2 * diameter;
    shadow.left = shadow.right - 2 * diameter;
    break;
  case app::CARD_BAND_BOTTOM:
    corners = 12u;
    thickness = static_cast<int>(body.bottom - origin.top);
    body.top = body.bottom - 2 * diameter;
    shadow.top = shadow.bottom - 2 * diameter;
    break;
  case app::CARD_BAND_COUNT:
    return;
  }
  const bool bounded = thickness > 0 && 2 * thickness < diameter;
  assert(bounded && "CardBand body-side thickness must be below the corner radius");
  if (!bounded)
    return;
  OffsetRect(&body, -origin.left, -origin.top);
  OffsetRect(&shadow, -origin.left, -origin.top);
  PaintNativeCardInk(dc, body, shadow, props.style, scale, phaseX, phaseY, corners);
}
