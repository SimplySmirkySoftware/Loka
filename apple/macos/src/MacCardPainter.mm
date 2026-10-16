#include "MacCardPainter.hpp"
#include <cmath>

namespace
{
  /** One paint owns its paths; allocation refusal draws nothing. */
  class CardPath
  {
  public:
    CardPath()
        : value_(CGPathCreateMutable())
    {
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

  /** Deliberate Win32 ProjectCardShadow twin: displace layout edges before
      projection. Offsetting a rounded native rectangle loses absolute phase. */
  NSRect ProjectCardShadow(const loka::core::Frame &body,
                           const loka::app::CardStyle &style,
                           const loka::macos::MacProjection &projection)
  {
    return projection
        .projectFrame(
            loka::core::Frame(body.x + style.kShadowRight, body.y + style.kShadowBottom, body.width, body.height))
        .r;
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

  /** At most one visit per backing pixel of this paint's clipped shadow bounds.
      No image resampling: AA is off and every black dot is exactly one pixel.
      Alternating parity is the AA/55 eight-row stencil, with transparent gaps. */
  void FillCardStencil(CGContextRef graphics, NSPoint phase)
  {
    const CGAffineTransform transform = CGContextGetCTM(graphics);
    if (transform.a == 0 || transform.d == 0)
      return;
    const CGRect bounds = CGContextGetClipBoundingBox(graphics);
    if (CGRectIsEmpty(bounds))
      return;
    const CGRect clip = CGRectApplyAffineTransform(bounds, transform);
    const CGPoint anchor = CGPointApplyAffineTransform(CGPointMake(-phase.x, -phase.y), transform);
    const int left = static_cast<int>(std::floor(CGRectGetMinX(clip)));
    const int top = static_cast<int>(std::floor(CGRectGetMinY(clip)));
    const int right = static_cast<int>(std::ceil(CGRectGetMaxX(clip)));
    const int bottom = static_cast<int>(std::ceil(CGRectGetMaxY(clip)));
    const int originX = static_cast<int>(std::floor(anchor.x + 0.5));
    const int originY = static_cast<int>(std::floor(anchor.y + 0.5));
    // The enclosing paint scope restores the transform. Device rectangles
    // stay pixel-aligned even when a scrolled view has a fractional origin.
    CGContextConcatCTM(graphics, CGAffineTransformInvert(transform));
    for (int y = top; y < bottom; ++y)
      for (int x = left; x < right; ++x)
      {
        const int column = transform.a > 0 ? x - originX : originX - x - 1;
        const int row = transform.d > 0 ? y - originY : originY - y - 1;
        if ((column + row) & 1)
          CGContextFillRect(graphics, CGRectMake(x, y, 1, 1));
      }
  }

  /** Per nonempty draw: three owned paths, two clips, one fill per black
      backing dot and one border stroke. Walks only the clipped band pixels. */
  void PaintNativeCardInk(
      CGContextRef graphics, CGRect body, CGRect shadow, CGFloat diameter, CGFloat pen, NSPoint phase, unsigned corners)
  {
    CardPath bodyPath, shadowPath, borderPath;
    if (!bodyPath.get() || !shadowPath.get() || !borderPath.get())
      return;
    BuildCardPath(bodyPath.get(), body, diameter / 2, corners);
    BuildCardPath(shadowPath.get(), shadow, diameter / 2, corners);
    BuildCardPath(borderPath.get(), CGRectInset(body, pen / 2, pen / 2), (diameter - pen) / 2, corners);
    CGContextSaveGState(graphics);
    CGContextSetRGBFillColor(graphics, 0, 0, 0, 1);
    CGContextSetShouldAntialias(graphics, false);
    CGContextAddPath(graphics, shadowPath.get());
    CGContextClip(graphics);
    // Intersect displaced body with the complement of the original body.
    const CGRect clip = CGContextGetClipBoundingBox(graphics);
    if (!CGRectIsEmpty(clip))
    {
      CGContextAddRect(graphics, clip);
      CGContextAddPath(graphics, bodyPath.get());
      CGContextEOClip(graphics);
      FillCardStencil(graphics, phase);
    }
    CGContextRestoreGState(graphics);
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
  PaintNativeCardInk(graphics,
                     LocalRect(projection.projectFrame(body).r, origin),
                     LocalRect(ProjectCardShadow(body, style, projection), origin),
                     projection.projectLength(0, 2 * style.kCornerRadius).pt,
                     projection.projectLength(0, 1).pt,
                     phase,
                     15u);
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
  NSRect shadow = ProjectCardShadow(logical, props.style, projection);
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
    body.size.height = shadow.size.height = 2 * diameter;
    break;
  case app::CARD_BAND_LEFT:
    corners = 5u;
    thickness = NSMaxX(frame) - NSMinX(body);
    body.size.width = shadow.size.width = 2 * diameter;
    break;
  case app::CARD_BAND_RIGHT:
    corners = 10u;
    thickness = NSMaxX(body) - NSMinX(frame);
    body.origin.x = NSMaxX(body) - 2 * diameter;
    shadow.origin.x = NSMaxX(shadow) - 2 * diameter;
    body.size.width = shadow.size.width = 2 * diameter;
    break;
  case app::CARD_BAND_BOTTOM:
    corners = 12u;
    thickness = NSMaxY(body) - NSMinY(frame);
    body.origin.y = NSMaxY(body) - 2 * diameter;
    shadow.origin.y = NSMaxY(shadow) - 2 * diameter;
    body.size.height = shadow.size.height = 2 * diameter;
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
  PaintNativeCardInk(graphics,
                     LocalRect(body, frame.origin),
                     LocalRect(shadow, frame.origin),
                     diameter,
                     projection.projectLength(0, 1).pt,
                     phase,
                     corners);
  CGContextRestoreGState(graphics);
}
