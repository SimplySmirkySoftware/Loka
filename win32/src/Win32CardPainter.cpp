#include "Win32CardPainter.hpp"
#include "app/style/CardShadow.hpp"
#include <climits>
#ifdef TEST_BUILD
namespace loka
{
  namespace testing
  {
    bool declineWin32CardStencil();
    void noteWin32CardPaintStage(unsigned stage);
  }
} // namespace loka
#endif

namespace
{
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
      {
        const BOOL deleted = DeleteObject(this->value_);
        assert(deleted);
        (void)deleted;
      }
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
      {
        const BOOL restored = RestoreDC(this->dc_, this->saved_);
        assert(restored);
        (void)restored;
      }
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

/** Once per paint; the child paint door does not guarantee fresh ground.
    Keep requested props intact until child-only repaint can safely blend. */
static loka::app::CardStyle Win32StencilStyle(const loka::app::CardStyle &requested)
{
  return loka::app::CardStyle(requested.extent(), loka::app::CARD_SHADOW_STENCIL);
}

namespace
{
  /** One paint owns the transfer DC and its top-down mask; selection is restored
      before either object is deleted. No native resource enters retirement. */
  class CardStencilSurface
  {
  public:
    CardStencilSurface(int width, int height)
        : dc_(CreateCompatibleDC(NULL)),
          bitmap_(0),
          previous_(0),
          bits_(0)
    {
      if (!this->dc_)
        return;
#ifdef TEST_BUILD
      if (loka::testing::declineWin32CardStencil())
        return;
#endif
      BITMAPINFO info;
      ZeroMemory(&info, sizeof(info));
      info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
      info.bmiHeader.biWidth = static_cast<LONG>(width);
      info.bmiHeader.biHeight = -static_cast<LONG>(height);
      info.bmiHeader.biPlanes = 1;
      info.bmiHeader.biBitCount = 32;
      info.bmiHeader.biCompression = BI_RGB;
      void *bits = 0;
      this->bitmap_ = CreateDIBSection(this->dc_, &info, DIB_RGB_COLORS, &bits, NULL, 0);
      if (!this->bitmap_ || !bits)
        return;
      this->previous_ = SelectObject(this->dc_, this->bitmap_);
      if (!this->previous_ || this->previous_ == HGDI_ERROR)
      {
        this->previous_ = 0;
        return;
      }
      this->bits_ = static_cast<DWORD *>(bits);
    }
    ~CardStencilSurface()
    {
      if (this->previous_)
      {
        const HGDIOBJ restored = SelectObject(this->dc_, this->previous_);
        assert(restored && restored != HGDI_ERROR);
        (void)restored;
      }
      // Destroying the private DC also releases any selection if restoration
      // refused. The bitmap is therefore never deleted while selected here.
      if (this->dc_)
      {
        const BOOL deleted = DeleteDC(this->dc_);
        assert(deleted);
        (void)deleted;
      }
      if (this->bitmap_)
      {
        const BOOL deleted = DeleteObject(this->bitmap_);
        assert(deleted);
        (void)deleted;
      }
    }
    DWORD *bits() const
    {
      return this->bits_;
    }
    HDC dc() const
    {
      return this->dc_;
    }

  private:
    HDC dc_;
    HBITMAP bitmap_;
    HGDIOBJ previous_;
    DWORD *bits_;
    CardStencilSurface(const CardStencilSurface &);
    CardStencilSurface &operator=(const CardStencilSurface &);
  };

  /** Once per band paint: O(clipped device area), one DC and DIB. Only the
      current clip's rows are allocated/walked; common owns density and phase. */
  bool PaintCardStencil(HDC dc,
                        const loka::core::Frame &body,
                        const loka::app::CardStyle &requested,
                        const loka::win32::Win32DisplayScale &scale,
                        int originX,
                        int originY,
                        int phaseX,
                        int phaseY)
  {
#ifdef TEST_BUILD
    loka::testing::noteWin32CardPaintStage(1);
#endif
    using namespace loka::app;
    const CardStyle style = Win32StencilStyle(requested);
    if (style.extent() == CARD_SHADOW_NONE)
      return true;
    RECT clip;
    const int clipKind = GetClipBox(dc, &clip);
    if (clipKind == ERROR)
      return false;
    if (clipKind == NULLREGION)
      return true;
    const CardShadowGridLines grid = CardShadowGrid(style, body);
    RECT support =
        scale.projectFrame(loka::core::Frame(grid.x[0], grid.y[0], grid.x[3] - grid.x[0], grid.y[3] - grid.y[0])).r;
    if (!OffsetRect(&support, -originX, -originY))
      return false;
    if (!IntersectRect(&clip, &clip, &support))
      return true;
    const int width = static_cast<int>(clip.right - clip.left);
    const int height = static_cast<int>(clip.bottom - clip.top);
    // Bound byte size and row indexing before handing dimensions to GDI.
    if (width <= 0 || height <= 0 || width > INT_MAX / 4 / height)
      return false;
    CardStencilSurface surface(width, height);
    if (!surface.bits())
      return false;
    for (int y = 0; y < height; ++y)
    {
      const int deviceY = static_cast<int>(clip.top) + y;
      const int layoutY = scale.unprojectEdge(deviceY + originY);
      unsigned char thresholds[8];
      CardShadowStencilThresholdRow(deviceY + phaseY, thresholds);
      for (int x = 0; x < width; ++x)
      {
        const int deviceX = static_cast<int>(clip.left) + x;
        const unsigned char density = CardShadowDensity(style, body, scale.unprojectEdge(deviceX + originX), layoutY);
        surface.bits()[y * width + x] = density > thresholds[(deviceX + phaseX) & 7] ? 0 : 0x00ffffff;
      }
    }
    // SRCAND is the mask equivalent of v1's transparent PDa brush transfer.
    return BitBlt(
               dc, static_cast<int>(clip.left), static_cast<int>(clip.top), width, height, surface.dc(), 0, 0, SRCAND)
           != FALSE;
  }
} // namespace

/** Once after shadow transfer, O(1); the region belongs to this paint. */
static bool FillCardBorder(HDC dc, HRGN border)
{
#ifdef TEST_BUILD
  loka::testing::noteWin32CardPaintStage(2);
#endif
  return FillRgn(dc, border, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH))) != FALSE;
}

/** Once per band/whole paint; border construction is independent of ink refusal.
    Region geometry and inside-only border are unchanged from v1. */
static void PaintNativeCardInk(HDC dc,
                               const RECT &native,
                               const loka::core::Frame &logicalBody,
                               const loka::app::CardStyle &style,
                               const loka::win32::Win32DisplayScale &scale,
                               int originX,
                               int originY,
                               int phaseX,
                               int phaseY,
                               unsigned corners)
{
  if (!SetROP2(dc, R2_COPYPEN))
    return;
  const int diameter = scale.projectLength(2 * style.kCornerRadius);
  const int projectedPen = scale.projectLength(1);
  const int pen = projectedPen > 0 ? projectedPen : 1;
  CardRegion body(CreateRectRgn(0, 0, 0, 0));
  CardRegion inner(CreateRectRgn(0, 0, 0, 0));
  CardRegion border(CreateRectRgn(0, 0, 0, 0));
  RECT inside = native;
  if (!InflateRect(&inside, -pen, -pen) || !BuildCardBody(body.get(), native, diameter, corners)
      || !BuildCardBody(inner.get(), inside, diameter - 2 * pen, corners) || !border.get()
      || CombineRgn(border.get(), body.get(), inner.get(), RGN_DIFF) == ERROR)
    return;
  // Ink refusal deliberately falls through to the independently prepared border.
  if (!PaintCardStencil(dc, logicalBody, style, scale, originX, originY, phaseX, phaseY))
  {
    // No retry within this paint. A later paint may allocate successfully.
  }
  if (!FillCardBorder(dc, border.get()))
    return;
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
  CardDCScope saved(dc);
  if (!saved.valid())
    return;
  RECT native = scale.projectFrame(body).r;
  if (!OffsetRect(&native, -deviceOriginX, -deviceOriginY))
    return;
  PaintNativeCardInk(dc, native, body, style, scale, deviceOriginX, deviceOriginY, phaseX, phaseY, 15u);
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
  CardDCScope saved(dc);
  if (!saved.valid())
    return;
  const core::Frame logicalBody = app::CardBodyInBand(band, props);
  RECT body = scale.projectFrame(logicalBody).r;
  const int diameter = scale.projectLength(2 * props.style.kCornerRadius);
  const RECT origin = scale.projectFrame(band).r;
  if (IntersectClipRect(
          dc, 0, 0, static_cast<int>(origin.right - origin.left), static_cast<int>(origin.bottom - origin.top))
      == ERROR)
    return;
  // Only the border's body-side sample must lie below the corner midpoint;
  // the shadow samples the original logical body independently of this border mask.
  int thickness = 0;
  unsigned corners = 0;
  switch (props.role)
  {
  case app::CARD_BAND_TOP:
    corners = 3u;
    thickness = static_cast<int>(origin.bottom - body.top);
    body.bottom = body.top + 2 * diameter;
    break;
  case app::CARD_BAND_LEFT:
    corners = 5u;
    thickness = static_cast<int>(origin.right - body.left);
    body.right = body.left + 2 * diameter;
    break;
  case app::CARD_BAND_RIGHT:
    corners = 10u;
    thickness = static_cast<int>(body.right - origin.left);
    body.left = body.right - 2 * diameter;
    break;
  case app::CARD_BAND_BOTTOM:
    corners = 12u;
    thickness = static_cast<int>(body.bottom - origin.top);
    body.top = body.bottom - 2 * diameter;
    break;
  case app::CARD_BAND_COUNT:
    return;
  }
  const bool bounded = thickness > 0 && 2 * thickness < diameter;
  assert(bounded && "CardBand body-side thickness must be below the corner radius");
  if (!bounded)
    return;
  if (!OffsetRect(&body, -static_cast<int>(origin.left), -static_cast<int>(origin.top)))
    return;
  PaintNativeCardInk(dc,
                     body,
                     logicalBody,
                     props.style,
                     scale,
                     static_cast<int>(origin.left),
                     static_cast<int>(origin.top),
                     phaseX,
                     phaseY,
                     corners);
}
