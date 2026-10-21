#include "MacCardPainter.hpp"
#include "app/style/CardShadow.hpp"
#include <cmath>
#include <climits>
#include <cstdlib>
#ifdef TEST_BUILD
namespace loka
{
  namespace testing
  {
    bool declineMacCardBitmap();
    bool declineMacCardPath();
    bool declineMacCardCornerTile();
  } // namespace testing
} // namespace loka
#endif

namespace
{
  /** One paint owns its paths; allocation refusal draws nothing. */
  class CardPath
  {
  public:
    CardPath()
        : value_(0)
    {
#ifdef TEST_BUILD
      if (loka::testing::declineMacCardPath())
        return;
#endif
      this->value_ = CGPathCreateMutable();
    }
    ~CardPath()
    {
      if (this->value_)
        CGPathRelease(this->value_);
    }
    CGMutablePathRef get() const
    {
      return this->value_;
    }

  private:
    CGMutablePathRef value_;
    CardPath(const CardPath &);
    CardPath &operator=(const CardPath &);
  };

  CGRect LocalRect(NSRect value, NSPoint origin)
  {
    return CGRectMake(value.origin.x - origin.x, value.origin.y - origin.y, value.size.width, value.size.height);
  }

  /** Fixed-radius arcs, never AppKit's clamped rounded-rect convenience API.
      A band's remote corners are square; only its two real corners are arcs. */
  void BuildCardPath(CGMutablePathRef path, CGRect rect, CGFloat radius, unsigned corners)
  {
    const CGFloat left = CGRectGetMinX(rect), right = CGRectGetMaxX(rect);
    const CGFloat top = CGRectGetMinY(rect), bottom = CGRectGetMaxY(rect);
    const CGFloat pi = static_cast<CGFloat>(3.14159265358979323846);
    CGPathMoveToPoint(path, NULL, left + radius, top);
    CGPathAddLineToPoint(path, NULL, right - radius, top);
    if (corners & 2u)
      CGPathAddArc(path, NULL, right - radius, top + radius, radius, -pi / 2, 0, false);
    else
      CGPathAddLineToPoint(path, NULL, right, top);
    CGPathAddLineToPoint(path, NULL, right, bottom - radius);
    if (corners & 8u)
      CGPathAddArc(path, NULL, right - radius, bottom - radius, radius, 0, pi / 2, false);
    else
      CGPathAddLineToPoint(path, NULL, right, bottom);
    CGPathAddLineToPoint(path, NULL, left + radius, bottom);
    if (corners & 4u)
      CGPathAddArc(path, NULL, left + radius, bottom - radius, radius, pi / 2, pi, false);
    else
      CGPathAddLineToPoint(path, NULL, left, bottom);
    CGPathAddLineToPoint(path, NULL, left, top + radius);
    if (corners & 1u)
      CGPathAddArc(path, NULL, left + radius, top + radius, radius, pi, 3 * pi / 2, false);
    else
      CGPathAddLineToPoint(path, NULL, left, top);
    CGPathCloseSubpath(path);
  }

  /** Lexical ownership of a Tiger-compatible RGBA premultiplied bitmap.
      Explicit storage is required: NULL bitmap data allocation is newer than Tiger. */
  class CardImage
  {
  public:
    CardImage(int width, int height)
        : bytes_(0),
          space_(0),
          context_(0),
          image_(0)
    {
#ifdef TEST_BUILD
      if (loka::testing::declineMacCardBitmap())
        return;
#endif
      this->bytes_ = static_cast<unsigned char *>(std::calloc(static_cast<size_t>(width) * height, 4));
      if (!this->bytes_)
        return;
      this->space_ = CGColorSpaceCreateDeviceRGB();
      if (this->space_)
        this->context_ = CGBitmapContextCreate(
            this->bytes_, width, height, 8, width * 4, this->space_, kCGImageAlphaPremultipliedLast);
    }
    ~CardImage()
    {
      if (this->image_)
        CGImageRelease(this->image_);
      if (this->context_)
        CGContextRelease(this->context_);
      if (this->space_)
        CGColorSpaceRelease(this->space_);
      std::free(this->bytes_);
    }
    unsigned char *bytes() const
    {
      return this->context_ ? this->bytes_ : 0;
    }
    CGImageRef seal()
    {
      this->image_ = CGBitmapContextCreateImage(this->context_);
      return this->image_;
    }

  private:
    unsigned char *bytes_;
    CGColorSpaceRef space_;
    CGContextRef context_;
    CGImageRef image_;
    CardImage(const CardImage &);
    CardImage &operator=(const CardImage &);
  };

  /** Inverse of the projection's half-open absolute edge cells. Capacity gives
      a lower candidate; nearest-edge rounding can admit the following cell.
      O(1) at the supported space scales (>= 1), no table or tree walk. */
  int LayoutSample(CGFloat point, const loka::macos::MacProjection &projection)
  {
    int result = projection.capacityToLu(point);
    if (projection.projectEdge(result + 1).pt <= point)
      ++result;
    return result;
  }

  /** Corner/fallback transfers call this once per candidate backing pixel.
      Projection and lookup are O(1); this paint owns the sampled rectangle. */
  unsigned char DensityAtBackingPixel(const loka::app::CardStyle &style,
                                      const loka::core::Frame &body,
                                      const loka::macos::MacProjection &projection,
                                      NSPoint origin,
                                      const CGAffineTransform &inverse,
                                      int x,
                                      int y)
  {
    const CGPoint local = CGPointApplyAffineTransform(CGPointMake(x + 0.5, y + 0.5), inverse);
    return loka::app::CardShadowDensity(
        style, body, LayoutSample(local.x + origin.x, projection), LayoutSample(local.y + origin.y, projection));
  }

  /** Process-global immutable density storage, allocated once per successful
      (extent, backing scale, corner) lookup. Intentionally leaked at exit like
      other global immutable resources; only the main-thread painter accesses it. */
  struct CornerDensityCache
  {
    const unsigned char *tiles[loka::app::CARD_SHADOW_EXTENT_COUNT][2][4];

    /** PaintTableInk calls per corner cell per paint (four per band).
        Hits walk no rows; first use computes only this tile's own pixels. */
    const unsigned char *tile(loka::app::CardShadowExtent extent, int scale, int corner, int &w, int &h)
    {
      using namespace loka::app;
      const CardStyle style(extent);
      const detail::CardShadowTable &g = style.geometry();
      const int horizontal = (corner & 1) ? 2 : 0;
      const int vertical = (corner & 2) ? 3 : 1;
      w = (g.allowance[horizontal] + g.cut[horizontal]) * scale;
      h = (g.allowance[vertical] + g.cut[vertical]) * scale;
#ifdef TEST_BUILD
      if (loka::testing::declineMacCardCornerTile())
        return 0;
#endif
      const unsigned char *&entry = this->tiles[extent][scale - 1][corner];
      if (!entry)
      {
        unsigned char *bytes = static_cast<unsigned char *>(std::malloc(static_cast<size_t>(w) * h));
        if (!bytes)
          return 0;
        const loka::core::Frame body(0, 0, g.cut[0] + g.cut[2], g.cut[1] + g.cut[3]);
        const int x = horizontal ? body.width - g.cut[2] : -g.allowance[0];
        const int y = vertical == 3 ? body.height - g.cut[3] : -g.allowance[1];
        const loka::macos::MacProjection projection;
        const CGAffineTransform inverse = CGAffineTransformMakeScale(1.0 / scale, 1.0 / scale);
        for (int row = 0; row < h; ++row)
          for (int column = 0; column < w; ++column)
            bytes[row * w + column] = DensityAtBackingPixel(
                style, body, projection, NSZeroPoint, inverse, x * scale + column, y * scale + row);
        entry = bytes;
      }
      return entry;
    }
  };

  CornerDensityCache cornerDensities = {};

  /** PaintTableInk calls once per corner or profile step intersecting its clip.
      Negative density reads the tile (or samples on refusal); a constant uses one ALPHA pixel
      or walks only this run's integer STENCIL pixels, without inverse sampling. */
  bool PaintTableRect(CGContextRef graphics,
                      const loka::core::Frame &body,
                      const loka::app::CardStyle &style,
                      const loka::macos::MacProjection &projection,
                      NSPoint origin,
                      const CGAffineTransform &transform,
                      const CGAffineTransform &inverse,
                      int originX,
                      int originY,
                      int left,
                      int top,
                      int right,
                      int bottom,
                      int density,
                      const unsigned char *tile = 0,
                      int tileStride = 0)
  {
    using namespace loka::app;
    const int width = right - left, height = bottom - top;
    if (width <= 0 || height <= 0 || density == 0)
      return true;
    if (style.ink() == CARD_SHADOW_ALPHA)
    {
      CardImage image(density < 0 ? width : 1, density < 0 ? height : 1);
      if (!image.bytes())
        return false;
      if (density >= 0)
        image.bytes()[3] = static_cast<unsigned char>(density);
      else
        for (int y = top; y < bottom; ++y)
          for (int x = left; x < right; ++x)
          {
            // Image row zero is its top, while device space grows upward.
            image.bytes()[((bottom - y - 1) * width + x - left) * 4 + 3] =
                tile ? tile[(y - top) * tileStride + (transform.a > 0 ? x - left : left - x)]
                     : DensityAtBackingPixel(style, body, projection, origin, inverse, x, y);
          }
      CGImageRef sealed = image.seal();
      if (!sealed)
        return false;
      CGContextSetInterpolationQuality(graphics, kCGInterpolationNone);
      CGContextDrawImage(graphics, CGRectMake(left, top, width, height), sealed);
    }
    else
    {
      CGRect dots[64];
      size_t count = 0;
      for (int y = top; y < bottom; ++y)
        for (int x = left; x < right; ++x)
        {
          const unsigned char sample =
              density < 0 ? (tile ? tile[(y - top) * tileStride + (transform.a > 0 ? x - left : left - x)]
                                  : DensityAtBackingPixel(style, body, projection, origin, inverse, x, y))
                          : static_cast<unsigned char>(density);
          const int column = transform.a > 0 ? x - originX : originX - x - 1;
          const int row = transform.d > 0 ? y - originY : originY - y - 1;
          if (CardShadowStencilInk(sample, column, row))
          {
            dots[count++] = CGRectMake(x, y, 1, 1);
            if (count == 64)
            {
              CGContextFillRects(graphics, dots, count);
              count = 0;
            }
          }
        }
      if (count)
        CGContextFillRects(graphics, dots, count);
    }
    return true;
  }

  /** Lower bound of the exact pixel-center LayoutSample predicate used above.
      PaintTableInk calls four times per cell/run, O(log clipped device span),
      with no table walk. Reversed axes use the complementary half-open test. */
  int SampleBoundary(int first,
                     int last,
                     int edge,
                     const CGAffineTransform &inverse,
                     bool horizontal,
                     CGFloat origin,
                     const loka::macos::MacProjection &projection)
  {
    while (first < last)
    {
      const int middle = first + (last - first) / 2;
      const CGPoint local = CGPointApplyAffineTransform(CGPointMake(middle + 0.5, middle + 0.5), inverse);
      const int sample = LayoutSample((horizontal ? local.x : local.y) + origin, projection);
      if ((horizontal ? inverse.a : inverse.d) > 0 ? sample < edge : sample >= edge)
        first = middle + 1;
      else
        last = middle;
    }
    return first;
  }

  /** Once per band/whole paint: four cached corners and four constant profiles.
      Grid middle cells may be empty. All work belongs to this paint's clip;
      non-axis-aligned CTMs retain the original per-pixel transfer. */
  void PaintTableInk(CGContextRef graphics,
                     const loka::core::Frame &body,
                     const loka::app::CardStyle &style,
                     const loka::macos::MacProjection &projection,
                     NSPoint origin,
                     NSPoint phase)
  {
    using namespace loka::app;
    if (style.extent() == CARD_SHADOW_NONE)
      return;
    const CGAffineTransform transform = CGContextGetCTM(graphics);
    if (transform.a == 0 || transform.d == 0)
      return;
    const CardShadowGridLines grid = CardShadowGrid(style, body);
    const CGRect support = LocalRect(
        projection.projectFrame(loka::core::Frame(grid.x[0], grid.y[0], grid.x[3] - grid.x[0], grid.y[3] - grid.y[0]))
            .r,
        origin);
    const CGRect clipped = CGRectIntersection(CGContextGetClipBoundingBox(graphics), support);
    if (CGRectIsEmpty(clipped))
      return;
    const CGRect device = CGRectApplyAffineTransform(clipped, transform);
    const int left = static_cast<int>(std::floor(CGRectGetMinX(device)));
    const int top = static_cast<int>(std::floor(CGRectGetMinY(device)));
    const int right = static_cast<int>(std::ceil(CGRectGetMaxX(device)));
    const int bottom = static_cast<int>(std::ceil(CGRectGetMaxY(device)));
    const int width = right - left, height = bottom - top;
    if (width <= 0 || height <= 0 || width > INT_MAX / 4 / height)
      return;
    const CGAffineTransform inverse = CGAffineTransformInvert(transform);
    const CGPoint anchor = CGPointApplyAffineTransform(CGPointMake(-phase.x, -phase.y), transform);
    const int originX = static_cast<int>(std::floor(anchor.x + 0.5));
    const int originY = static_cast<int>(std::floor(anchor.y + 0.5));
    CGContextSaveGState(graphics);
    CGContextClipToRect(graphics, support);
    CGContextConcatCTM(graphics, inverse);
    CGContextSetShouldAntialias(graphics, false);
    CGContextSetRGBFillColor(graphics, 0, 0, 0, 1);
    if (transform.b != 0 || transform.c != 0)
      PaintTableRect(graphics,
                     body,
                     style,
                     projection,
                     origin,
                     transform,
                     inverse,
                     originX,
                     originY,
                     left,
                     top,
                     right,
                     bottom,
                     -1);
    else
      for (int row = 0; row < 3; ++row)
        for (int column = 0; column < 3; ++column)
        {
          if (row == 1 && column == 1)
            continue;
          const bool corner = row != 1 && column != 1;
          const int steps = corner        ? 1
                            : column == 1 ? grid.y[row + 1] - grid.y[row]
                                          : grid.x[column + 1] - grid.x[column];
          for (int step = 0; step < steps; ++step)
          {
            const int x = grid.x[column] + (!corner && row == 1 ? step : 0);
            const int y = grid.y[row] + (!corner && column == 1 ? step : 0);
            const int endX = !corner && row == 1 ? x + 1 : grid.x[column + 1];
            const int endY = !corner && column == 1 ? y + 1 : grid.y[row + 1];
            const int runLeft =
                SampleBoundary(left, right, transform.a > 0 ? x : endX, inverse, true, origin.x, projection);
            const int runRight =
                SampleBoundary(left, right, transform.a > 0 ? endX : x, inverse, true, origin.x, projection);
            const int runTop =
                SampleBoundary(top, bottom, transform.d > 0 ? y : endY, inverse, false, origin.y, projection);
            const int runBottom =
                SampleBoundary(top, bottom, transform.d > 0 ? endY : y, inverse, false, origin.y, projection);
            if (runLeft >= runRight || runTop >= runBottom)
              continue;
            const int density = corner ? -1 : CardShadowDensity(style, body, x, y);
            const unsigned char *tile = 0;
            int tileStride = 0;
            const CGFloat scale = std::fabs(transform.a);
            // Space-scale rounding is position-dependent and is not in the
            // cache key; retain the sampler for every non-unit space scale.
            if (corner && (scale == 1 || scale == 2) && std::fabs(transform.d) == scale
                && projection.railMetrics().spaceScale.isUnit())
            {
              int w = 0, h = 0;
              const unsigned char *bytes = cornerDensities.tile(
                  style.extent(), static_cast<int>(scale), (row == 2 ? 2 : 0) + (column == 2 ? 1 : 0), w, h);
              const detail::CardShadowTable &g = style.geometry();
              const int baseX = column == 0 ? body.x - g.allowance[0] : body.x + body.width - g.cut[2];
              const int baseY = row == 0 ? body.y - g.allowance[1] : body.y + body.height - g.cut[3];
              const CGPoint first = CGPointApplyAffineTransform(CGPointMake(runLeft + 0.5, runTop + 0.5), inverse);
              // TL/TR/BL/BR tiles all start at their logical minimum x/y.
              // runLeft/runTop selects (tx,ty) below, including clipped or
              // overlapping cells. Positive axes advance from that pixel;
              // reversed axes retreat (unclipped: w-1 and/or h-1).
              const int tx = static_cast<int>(std::floor((first.x + origin.x - baseX) * scale));
              const int ty = static_cast<int>(std::floor((first.y + origin.y - baseY) * scale));
              const int lastX = tx + (transform.a > 0 ? 1 : -1) * (runRight - runLeft - 1);
              const int lastY = ty + (transform.d > 0 ? 1 : -1) * (runBottom - runTop - 1);
              if (bytes && tx >= 0 && tx < w && ty >= 0 && ty < h && lastX >= 0 && lastX < w && lastY >= 0 && lastY < h)
              {
                tile = bytes + ty * w + tx;
                tileStride = transform.d > 0 ? w : -w;
              }
            }
            if (!PaintTableRect(graphics,
                                body,
                                style,
                                projection,
                                origin,
                                transform,
                                inverse,
                                originX,
                                originY,
                                runLeft,
                                runTop,
                                runRight,
                                runBottom,
                                density,
                                tile,
                                tileStride))
            {
              CGContextRestoreGState(graphics);
              return;
            }
          }
        }
    CGContextRestoreGState(graphics);
  }

  /** Once per band/whole paint, acquire the border path before any ink so path
      refusal draws nothing. Shadow bitmap refusal still cannot suppress the
      unchanged antialiased border, which is always drawn last. */
  void PaintNativeCardInk(CGContextRef graphics,
                          CGRect body,
                          const loka::core::Frame &logical,
                          const loka::app::CardStyle &style,
                          const loka::macos::MacProjection &projection,
                          NSPoint origin,
                          NSPoint phase,
                          unsigned corners)
  {
    CardPath borderPath;
    if (!borderPath.get())
      return;
    PaintTableInk(graphics, logical, style, projection, origin, phase);
    const CGFloat diameter = projection.projectLength(0, 2 * style.kCornerRadius).pt;
    const CGFloat pen = projection.projectLength(0, 1).pt;
    BuildCardPath(borderPath.get(), CGRectInset(body, pen / 2, pen / 2), (diameter - pen) / 2, corners);
    CGContextSaveGState(graphics);
    CGContextSetShouldAntialias(graphics, true);
    CGContextSetRGBStrokeColor(graphics, 0, 0, 0, 1);
    CGContextSetLineWidth(graphics, pen);
    CGContextAddPath(graphics, borderPath.get());
    CGContextStrokePath(graphics);
    CGContextRestoreGState(graphics);
  }
} // namespace

void loka::macos::PaintCardInk(CGContextRef graphics,
                               const core::Frame &body,
                               const app::CardStyle &style,
                               const MacProjection &projection,
                               NSPoint origin,
                               NSPoint phase)
{
  if (!graphics || !body.hasSize())
    return;
  PaintNativeCardInk(
      graphics, LocalRect(projection.projectFrame(body).r, origin), body, style, projection, origin, phase, 15u);
}

void loka::macos::PaintCardBandInk(CGContextRef graphics,
                                   const core::Frame &band,
                                   const app::CardBandProps &props,
                                   const MacProjection &projection,
                                   NSPoint phase)
{
  if (!graphics || !band.hasSize())
    return;
  const core::Frame logical = app::CardBodyInBand(band, props);
  NSRect body = projection.projectFrame(logical).r;
  const NSRect frame = projection.projectFrame(band).r;
  const CGFloat diameter = projection.projectLength(0, 2 * props.style.kCornerRadius).pt;
  CGFloat thickness = 0;
  unsigned corners = 0;
  // Deliberate Win32 twin: preserve the three known projected edges, moving
  // only the virtual opposite edge beyond this band's sample.
  switch (props.role)
  {
  case app::CARD_BAND_TOP:
    corners = 3u;
    thickness = NSMaxY(frame) - NSMinY(body);
    body.size.height = 2 * diameter;
    break;
  case app::CARD_BAND_LEFT:
    corners = 5u;
    thickness = NSMaxX(frame) - NSMinX(body);
    body.size.width = 2 * diameter;
    break;
  case app::CARD_BAND_RIGHT:
    corners = 10u;
    thickness = NSMaxX(body) - NSMinX(frame);
    body.origin.x = NSMaxX(body) - 2 * diameter;
    body.size.width = 2 * diameter;
    break;
  case app::CARD_BAND_BOTTOM:
    corners = 12u;
    thickness = NSMaxY(body) - NSMinY(frame);
    body.origin.y = NSMaxY(body) - 2 * diameter;
    body.size.height = 2 * diameter;
    break;
  case app::CARD_BAND_COUNT:
    return;
  }
  const bool bounded = thickness > 0 && 2 * thickness < diameter;
  assert(bounded && "CardBand body-side thickness must be below the corner radius");
  if (!bounded)
    return;
  CGContextSaveGState(graphics);
  CGContextClipToRect(graphics, CGRectMake(0, 0, frame.size.width, frame.size.height));
  PaintNativeCardInk(
      graphics, LocalRect(body, frame.origin), logical, props.style, projection, frame.origin, phase, corners);
  CGContextRestoreGState(graphics);
}
