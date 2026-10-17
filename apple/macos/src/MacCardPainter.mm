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
  }
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

  /** Both ink transfers call this once per candidate backing pixel. Projection
      and lookup are O(1); this paint owns the sampled rectangle. */
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

  /** Once per paint, O(clip intersect support backing area). STENCIL submits
      fixed batches; ALPHA allocates four bytes per pixel and transfers once.
      Both walk only this paint's pixels; common owns every density/threshold. */
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
    if (style.ink() == CARD_SHADOW_ALPHA)
    {
      CardImage image(width, height);
      if (image.bytes())
      {
        for (int y = top; y < bottom; ++y)
          for (int x = left; x < right; ++x)
          {
            // Image row zero is its top, while device space grows upward.
            image.bytes()[((bottom - y - 1) * width + x - left) * 4 + 3] =
                DensityAtBackingPixel(style, body, projection, origin, inverse, x, y);
          }
        CGImageRef sealed = image.seal();
        if (sealed)
        {
          CGContextSetInterpolationQuality(graphics, kCGInterpolationNone);
          CGContextDrawImage(graphics, CGRectMake(left, top, width, height), sealed);
        }
      }
    }
    else
    {
      CGRect dots[64];
      size_t count = 0;
      for (int y = top; y < bottom; ++y)
        for (int x = left; x < right; ++x)
        {
          const unsigned char density = DensityAtBackingPixel(style, body, projection, origin, inverse, x, y);
          const int column = transform.a > 0 ? x - originX : originX - x - 1;
          const int row = transform.d > 0 ? y - originY : originY - y - 1;
          if (CardShadowStencilInk(density, column, row))
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
    CGContextRestoreGState(graphics);
  }

  /** Once per band/whole paint, one owned border path. Shadow refusal cannot
      suppress the unchanged antialiased border, which is always drawn last. */
  void PaintNativeCardInk(CGContextRef graphics,
                          CGRect body,
                          const loka::core::Frame &logical,
                          const loka::app::CardStyle &style,
                          const loka::macos::MacProjection &projection,
                          NSPoint origin,
                          NSPoint phase,
                          unsigned corners)
  {
    PaintTableInk(graphics, logical, style, projection, origin, phase);
    const CGFloat diameter = projection.projectLength(0, 2 * style.kCornerRadius).pt;
    const CGFloat pen = projection.projectLength(0, 1).pt;
    CardPath borderPath;
    if (!borderPath.get())
      return;
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
