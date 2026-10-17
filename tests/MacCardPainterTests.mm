#include "MacCardPainterTests.hpp"
#include "MacCardPainter.hpp"
#include "app/style/CardShadow.hpp"
#include "support/MacCardV1Oracle.hpp"
#include "context/MacScrollViewContext.hpp"
namespace loka
{
  namespace testing
  {
    void failMacCardBitmaps(unsigned count);
  }
} // namespace loka
#include "MacScenePlatformController.hpp"
#include "testing/MacScenePlatformTestAccess.hpp"
#include "app/scene/Scene.hpp"
#include "support/LifecycleFactTestAccess.hpp"
#include "support/MacNativeRefusal.hpp"
#include "support/MacRetirementProbe.hpp"
#include "support/TestVerify.hpp"
#include <cmath>
#include <cstdio>

@interface LokaCardTestRoot : NSView
@end
@implementation LokaCardTestRoot
- (BOOL)isFlipped
{
  return YES;
}
@end

namespace
{
  using namespace loka::app;
  using namespace loka::app::scene;
  using namespace loka::macos;
  using loka::core::Frame;

  void GroundPixel(int x, int y, NSUInteger *pixel)
  {
    const bool alternate = ((x / 5 + y / 7) & 1) != 0;
    pixel[0] = alternate ? 81 : 209;
    pixel[1] = alternate ? 137 : 111;
    pixel[2] = alternate ? 193 : 57;
    pixel[3] = 255;
  }

  /** MacGroundTests' drawRect tool extended to an explicit backing scale,
      top-down points and a patterned ground. Every band has its own bitmap. */
  class CardBitmap
  {
  public:
    CardBitmap(int width, int height, int backing, int originX = 0, int originY = 0)
        : bitmap_([[NSBitmapImageRep alloc] initWithBitmapDataPlanes:NULL
                                                          pixelsWide:width * backing
                                                          pixelsHigh:height * backing
                                                       bitsPerSample:8
                                                     samplesPerPixel:4
                                                            hasAlpha:YES
                                                            isPlanar:NO
                                                      colorSpaceName:NSDeviceRGBColorSpace
                                                         bytesPerRow:0
                                                        bitsPerPixel:0]),
          backing_(backing)
    {
      LOKA_VERIFY(this->bitmap_ != nil);
      for (int y = 0; y < height * backing; ++y)
        for (int x = 0; x < width * backing; ++x)
        {
          NSUInteger pixel[4];
          GroundPixel(x + originX * backing, y + originY * backing, pixel);
          [this->bitmap_ setPixel:pixel atX:x y:y];
        }
    }
    ~CardBitmap()
    {
      [this->bitmap_ release];
    }
    CGContextRef begin(bool flipped = true)
    {
      NSGraphicsContext *context = [NSGraphicsContext graphicsContextWithBitmapImageRep:this->bitmap_];
      LOKA_VERIFY(context != nil);
      [NSGraphicsContext saveGraphicsState];
      [NSGraphicsContext setCurrentContext:context];
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
      CGContextRef graphics = static_cast<CGContextRef>([context graphicsPort]);
#pragma clang diagnostic pop
      if (flipped)
      {
        CGContextTranslateCTM(graphics, 0, [this->bitmap_ pixelsHigh]);
        CGContextScaleCTM(graphics, this->backing_, -this->backing_);
      }
      else
        CGContextScaleCTM(graphics, this->backing_, this->backing_);
      return graphics;
    }
    void end()
    {
      [NSGraphicsContext restoreGraphicsState];
    }
    NSBitmapImageRep *bitmap() const
    {
      return this->bitmap_;
    }
    void clear()
    {
      NSUInteger pixel[4] = {0, 0, 0, 0};
      for (NSInteger y = 0; y < [this->bitmap_ pixelsHigh]; ++y)
        for (NSInteger x = 0; x < [this->bitmap_ pixelsWide]; ++x)
          [this->bitmap_ setPixel:pixel atX:x y:y];
    }
    void composite(const CardBitmap &strip, NSPoint origin)
    {
      for (NSInteger y = 0; y < [strip.bitmap() pixelsHigh]; ++y)
        for (NSInteger x = 0; x < [strip.bitmap() pixelsWide]; ++x)
        {
          NSUInteger pixel[4];
          [strip.bitmap() getPixel:pixel atX:x y:y];
          [this->bitmap_ setPixel:pixel
                              atX:x + static_cast<NSInteger>(origin.x * this->backing_)
                                y:y + static_cast<NSInteger>(origin.y * this->backing_)];
        }
    }

  private:
    NSBitmapImageRep *bitmap_;
    int backing_;
    CardBitmap(const CardBitmap &);
    CardBitmap &operator=(const CardBitmap &);
  };

  bool SamePixel(NSBitmapImageRep *a, NSBitmapImageRep *b, NSInteger x, NSInteger y)
  {
    NSUInteger first[4] = {0, 0, 0, 0}, second[4] = {0, 0, 0, 0};
    [a getPixel:first atX:x y:y];
    [b getPixel:second atX:x y:y];
    for (int channel = 0; channel < 4; ++channel)
      if (first[channel] != second[channel])
        return false;
    return true;
  }

  /** The antialiased border is rasterized from band-local coordinates in a
      band and from absolute coordinates in the reference, and the pixels it
      touches go through a color conversion; either side can land one level
      apart, including near-zero coverage over the ground (Tahoe, macOS 26:
      every observed difference was exactly 1). With allowBorderRounding, each
      channel may differ by at most 1; anything a mutation changes (a dot, a
      corner, the pen, the shadow) differs by far more. Containment is checked
      separately with SamePixel, and the tolerated count is printed per case. */
  void EqualBitmaps(const CardBitmap &actual, const CardBitmap &expected, bool allowBorderRounding = false)
  {
    unsigned tolerated = 0;
    for (NSInteger y = 0; y < [actual.bitmap() pixelsHigh]; ++y)
      for (NSInteger x = 0; x < [actual.bitmap() pixelsWide]; ++x)
      {
        if (SamePixel(actual.bitmap(), expected.bitmap(), x, y))
          continue;
        NSUInteger first[4], second[4];
        [actual.bitmap() getPixel:first atX:x y:y];
        [expected.bitmap() getPixel:second atX:x y:y];
        bool accepted = allowBorderRounding;
        for (int channel = 0; channel < 4; ++channel)
        {
          const NSUInteger difference =
              first[channel] > second[channel] ? first[channel] - second[channel] : second[channel] - first[channel];
          accepted = accepted && difference <= 1;
        }
        if (!accepted)
          std::printf("card pixel differs at (%ld,%ld) actual=%lu,%lu,%lu,%lu expected=%lu,%lu,%lu,%lu; tolerated=%u\n",
                      static_cast<long>(x),
                      static_cast<long>(y),
                      static_cast<unsigned long>(first[0]),
                      static_cast<unsigned long>(first[1]),
                      static_cast<unsigned long>(first[2]),
                      static_cast<unsigned long>(first[3]),
                      static_cast<unsigned long>(second[0]),
                      static_cast<unsigned long>(second[1]),
                      static_cast<unsigned long>(second[2]),
                      static_cast<unsigned long>(second[3]),
                      tolerated);
        LOKA_VERIFY(accepted);
        ++tolerated;
      }
    std::printf("card comparison tolerated AA pixels=%u\n", tolerated);
  }

  LayoutState BandState(const Frame &frame)
  {
    LayoutState state;
    state.x = static_cast<short>(frame.x);
    state.y = static_cast<short>(frame.y);
    state.width = static_cast<short>(frame.width);
    state.height = static_cast<short>(frame.height);
    return state;
  }

  void RenderViews(NSView *root, CardBitmap &result, int backing)
  {
    for (NSUInteger i = 0; i < [[root subviews] count]; ++i)
    {
      NSView *view = [[root subviews] objectAtIndex:i];
      const NSRect frame = [view frame];
      if ([view isHidden] || NSIsEmptyRect(frame))
        continue;
      CardBitmap strip(static_cast<int>(frame.size.width),
                       static_cast<int>(frame.size.height),
                       backing,
                       static_cast<int>(frame.origin.x),
                       static_cast<int>(frame.origin.y));
      strip.begin();
      [view drawRect:[view bounds]];
      strip.end();
      result.composite(strip, frame.origin);
    }
  }

  void
  WholeReference(CardBitmap &result, const Frame &body, const MacProjection &projection, NSPoint phase = NSZeroPoint)
  {
    CGContextRef graphics = result.begin();
    const NSRect outer = projection.projectFrame(Frame(body.x, body.y, body.width + 3, body.height + 3)).r;
    const NSRect seat = projection.projectFrame(Frame(body.x + 3, body.y + 3, body.width - 6, body.height - 6)).r;
    // The same clip as the union of the four band views: the outer frame minus
    // the content seat, which no band owns (Q1). The whole ink's antialiased
    // inner fringe can reach a seat corner at a fractional scale (3/2: one
    // pixel, five levels); the bands' own seat pixels are checked separately.
    CGContextBeginPath(graphics);
    CGContextAddRect(graphics, CGRectMake(outer.origin.x, outer.origin.y, outer.size.width, outer.size.height));
    CGContextAddRect(graphics, CGRectMake(seat.origin.x, seat.origin.y, seat.size.width, seat.size.height));
    CGContextEOClip(graphics);
    PaintCardInk(graphics, body, CardStyle(), projection, NSZeroPoint, phase);
    result.end();
  }

  id RefuseCardView(id object, SEL, NSRect)
  {
    [object release];
    return nil;
  }
} // namespace

namespace
{
  void CardBands(const Frame &body, const CardStyle &style, Frame *bands)
  {
    const loka::app::detail::CardShadowTable &g = style.geometry();
    const int x = body.x, y = body.y, w = body.width, h = body.height;
    bands[0] = Frame(x - g.allowance[0], y - g.allowance[1], w + g.allowance[0] - 3, g.allowance[1] + 3);
    bands[1] = Frame(x - g.allowance[0], y + 3, g.allowance[0] + 3, h + g.allowance[3] - 3);
    bands[2] = Frame(x + w - 3, y - g.allowance[1], g.allowance[2] + 3, h + g.allowance[1] - 3);
    bands[3] = Frame(x + 3, y + h - 3, w + g.allowance[2] - 3, g.allowance[3] + 3);
  }

  void ClipRing(CGContextRef graphics, const Frame &body, const CardStyle &style, const MacProjection &projection)
  {
    const loka::app::detail::CardShadowTable &g = style.geometry();
    const NSRect outer = projection
                             .projectFrame(Frame(body.x - g.allowance[0],
                                                 body.y - g.allowance[1],
                                                 body.width + g.allowance[0] + g.allowance[2],
                                                 body.height + g.allowance[1] + g.allowance[3]))
                             .r;
    const NSRect seat = projection.projectFrame(Frame(body.x + 3, body.y + 3, body.width - 6, body.height - 6)).r;
    CGContextBeginPath(graphics);
    CGContextAddRect(graphics, CGRectMake(outer.origin.x, outer.origin.y, outer.size.width, outer.size.height));
    CGContextAddRect(graphics, CGRectMake(seat.origin.x, seat.origin.y, seat.size.width, seat.size.height));
    CGContextEOClip(graphics);
  }

  /** Independent oracle: expand each logical table cell through projected
      endpoints, then threshold each backing pixel on the window lattice. */
  void TableOracle(CGContextRef graphics,
                   const Frame &body,
                   const CardStyle &style,
                   const MacProjection &projection,
                   NSPoint phase,
                   int backing)
  {
    const CardShadowGridLines grid = CardShadowGrid(style, body);
    CGContextSaveGState(graphics);
    CGContextSetShouldAntialias(graphics, false);
    for (int y = grid.y[0]; y < grid.y[3]; ++y)
      for (int x = grid.x[0]; x < grid.x[3]; ++x)
      {
        const unsigned char density = CardShadowDensity(style, body, x, y);
        const NSRect cell = projection.projectFrame(Frame(x, y, 1, 1)).r;
        for (int py = static_cast<int>(NSMinY(cell) * backing); py < NSMaxY(cell) * backing; ++py)
          for (int px = static_cast<int>(NSMinX(cell) * backing); px < NSMaxX(cell) * backing; ++px)
          {
            const bool dot = CardShadowStencilInk(
                density, px + static_cast<int>(phase.x * backing), py + static_cast<int>(phase.y * backing));
            if (style.ink() == CARD_SHADOW_STENCIL && !dot)
              continue;
            CGContextSetRGBFillColor(
                graphics, 0, 0, 0, style.ink() == CARD_SHADOW_ALPHA ? static_cast<CGFloat>(density) / 255 : 1);
            CGContextFillRect(graphics,
                              CGRectMake(static_cast<CGFloat>(px) / backing,
                                         static_cast<CGFloat>(py) / backing,
                                         static_cast<CGFloat>(1) / backing,
                                         static_cast<CGFloat>(1) / backing));
          }
      }
    CGContextRestoreGState(graphics);
    PaintCardInk(graphics, body, CardStyle(CARD_SHADOW_NONE), projection, NSZeroPoint, phase);
  }

  void DumpPBM(const CardBitmap &bitmap, const char *path)
  {
    FILE *file = std::fopen(path, "wb");
    LOKA_VERIFY(file != 0);
    std::fprintf(file,
                 "P1\n%ld %ld\n",
                 static_cast<long>([bitmap.bitmap() pixelsWide]),
                 static_cast<long>([bitmap.bitmap() pixelsHigh]));
    for (NSInteger y = 0; y < [bitmap.bitmap() pixelsHigh]; ++y)
    {
      for (NSInteger x = 0; x < [bitmap.bitmap() pixelsWide]; ++x)
      {
        NSUInteger pixel[4];
        [bitmap.bitmap() getPixel:pixel atX:x y:y];
        std::fprintf(file, "%d ", pixel[3] != 0 && pixel[0] == 0 && pixel[1] == 0 && pixel[2] == 0 ? 1 : 0);
      }
      std::fprintf(file, "\n");
    }
    std::fclose(file);
  }

  void CompareV1(const CardBitmap &actual,
                 const Frame &body,
                 const MacProjection &projection,
                 NSPoint phase,
                 int backing,
                 int metric,
                 int residue)
  {
    CardBitmap old(180, 150, backing);
    old.clear();
    CGContextRef graphics = old.begin();
    ClipRing(graphics, body, CardStyle(), projection);
    MacCardV1::PaintCardInk(graphics, body, CardStyle(), projection, NSZeroPoint, phase);
    old.end();
    unsigned differences = 0;
    for (NSInteger y = 0; y < [old.bitmap() pixelsHigh]; ++y)
      for (NSInteger x = 0; x < [old.bitmap() pixelsWide]; ++x)
        if (!SamePixel(actual.bitmap(), old.bitmap(), x, y))
          ++differences;
    std::printf(
        "macOS DROP v1 differences=%u backing=%d metric=%d residue=%d\n", differences, backing, metric, residue);
    if (differences)
    {
      char path[160];
      ::snprintf(path, sizeof(path), "/tmp/loka-card-v1-b%d-m%d-r%d.pbm", backing, metric, residue);
      DumpPBM(old, path);
      ::snprintf(path, sizeof(path), "/tmp/loka-card-table-b%d-m%d-r%d.pbm", backing, metric, residue);
      DumpPBM(actual, path);
    }
  }

  void VerifyAlphaBytes()
  {
    const MacProjection projection;
    const Frame body(20, 20, 50, 50);
    const CardStyle style(CARD_SHADOW_WIDE, CARD_SHADOW_ALPHA);
    for (int backing = 1; backing <= 2; ++backing)
    {
      CardBitmap actual(100, 100, backing), border(100, 100, backing), twice(100, 100, backing);
      actual.clear();
      border.clear();
      twice.clear();
      CGContextRef graphics = actual.begin();
      PaintCardInk(graphics, body, style, projection, NSZeroPoint, NSZeroPoint);
      actual.end();
      graphics = border.begin();
      PaintCardInk(graphics, body, CardStyle(CARD_SHADOW_NONE), projection, NSZeroPoint, NSZeroPoint);
      border.end();
      graphics = twice.begin();
      PaintCardInk(graphics, body, style, projection, NSZeroPoint, NSZeroPoint);
      PaintCardInk(graphics, body, style, projection, NSZeroPoint, NSZeroPoint);
      twice.end();
      CardBitmap patterned(100, 100, backing), patternedExpected(100, 100, backing);
      graphics = patterned.begin();
      PaintCardInk(graphics, body, style, projection, NSZeroPoint, NSZeroPoint);
      patterned.end();
      graphics = patternedExpected.begin();
      TableOracle(graphics, body, style, projection, NSZeroPoint, backing);
      patternedExpected.end();
      EqualBitmaps(patterned, patternedExpected, true);
      unsigned samples = 0, different = 0;
      bool densities[256] = {false};
      for (int y = 0; y < 100 * backing; ++y)
        for (int x = 0; x < 100 * backing; ++x)
        {
          NSUInteger pixel[4], edge[4];
          [actual.bitmap() getPixel:pixel atX:x y:y];
          [border.bitmap() getPixel:edge atX:x y:y];
          if (edge[3])
            continue;
          const unsigned char density = CardShadowDensity(style, body, x / backing, y / backing);
          LOKA_VERIFY(pixel[0] == 0 && pixel[1] == 0 && pixel[2] == 0 && pixel[3] == density);
          if (density)
          {
            ++samples;
            densities[density] = true;
          }
          if (!SamePixel(actual.bitmap(), twice.bitmap(), x, y))
            ++different;
        }
      unsigned distinct = 0;
      for (int i = 1; i < 255; ++i)
        if (densities[i])
          ++distinct;
      LOKA_VERIFY(samples > 0 && distinct > 3 && different > 0);
      std::printf("alpha bytes backing=%d samples=%u densities=%u double-blend differences=%u\n",
                  backing,
                  samples,
                  distinct,
                  different);
    }
  }

  void VerifyFailureAndState()
  {
    const MacProjection projection;
    CardBitmap actual(100, 100, 2), expected(100, 100, 2);
    CGContextRef graphics = actual.begin();
    CGContextSetLineWidth(graphics, 7);
    CGContextClipToRect(graphics, CGRectMake(5, 7, 88, 83));
    const CGRect clip = CGContextGetClipBoundingBox(graphics);
    const CGAffineTransform transform = CGContextGetCTM(graphics);
    loka::testing::failMacCardBitmaps(1);
    PaintCardInk(graphics,
                 Frame(20, 20, 50, 50),
                 CardStyle(CARD_SHADOW_WIDE, CARD_SHADOW_ALPHA),
                 projection,
                 NSZeroPoint,
                 NSZeroPoint);
    LOKA_VERIFY(CGAffineTransformEqualToTransform(transform, CGContextGetCTM(graphics)));
    LOKA_VERIFY(CGRectEqualToRect(clip, CGContextGetClipBoundingBox(graphics)));
    CGContextMoveToPoint(graphics, 8, 10);
    CGContextAddLineToPoint(graphics, 18, 10);
    CGContextStrokePath(graphics);
    actual.end();
    graphics = expected.begin();
    CGContextClipToRect(graphics, CGRectMake(5, 7, 88, 83));
    PaintCardInk(graphics, Frame(20, 20, 50, 50), CardStyle(CARD_SHADOW_NONE), projection, NSZeroPoint, NSZeroPoint);
    CGContextSetLineWidth(graphics, 7);
    CGContextMoveToPoint(graphics, 8, 10);
    CGContextAddLineToPoint(graphics, 18, 10);
    CGContextStrokePath(graphics);
    expected.end();
    EqualBitmaps(actual, expected);
  }
} // namespace

void testMacCardPainterPixels()
{
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  [NSApplication sharedApplication];
  for (int metric = 0; metric < 2; ++metric)
    for (int backing = 1; backing <= 2; ++backing)
      for (int residue = 0; residue < 8; ++residue)
        for (int extent = CARD_SHADOW_NONE; extent < CARD_SHADOW_EXTENT_COUNT; ++extent)
          for (int ink = CARD_SHADOW_STENCIL; ink < CARD_SHADOW_INK_COUNT; ++ink)
          {
            NSAutoreleasePool *iteration = [[NSAutoreleasePool alloc] init];
            const CardStyle style(static_cast<CardShadowExtent>(extent), static_cast<CardShadowInk>(ink));
            const Frame body(16 + residue, 16 + (residue * 3) % 8, 64 + residue, 48 + (residue * 5) % 8);
            const NSPoint phase = NSMakePoint(3, 5);
            const MacProjection projection(0, RailMetrics(Ratio(), metric ? Ratio(3, 2) : Ratio()));
            Frame bands[4];
            CardBands(body, style, bands);
            CardBitmap actual(180, 150, backing), expected(180, 150, backing), whole(180, 150, backing);
            CardBitmap partialBands(180, 150, backing);
            partialBands.clear();
            actual.clear();
            expected.clear();
            whole.clear();
            for (int role = 0; role < CARD_BAND_COUNT; ++role)
            {
              const NSRect native = projection.projectFrame(bands[role]).r;
              CardBitmap strip(static_cast<int>(native.size.width),
                               static_cast<int>(native.size.height),
                               backing,
                               static_cast<int>(native.origin.x),
                               static_cast<int>(native.origin.y));
              strip.clear();
              CGContextRef graphics = strip.begin();
              PaintCardBandInk(graphics,
                               bands[role],
                               CardBandProps(static_cast<CardBandRole>(role), style),
                               projection,
                               NSMakePoint(phase.x + native.origin.x, phase.y + native.origin.y));
              strip.end();
              actual.composite(strip, native.origin);
              CardBitmap clippedStrip(
                  static_cast<int>(native.size.width), static_cast<int>(native.size.height), backing);
              clippedStrip.clear();
              graphics = clippedStrip.begin();
              CGContextClipToRect(graphics, CGRectMake(33 - native.origin.x, 29 - native.origin.y, 61, 53));
              PaintCardBandInk(graphics,
                               bands[role],
                               CardBandProps(static_cast<CardBandRole>(role), style),
                               projection,
                               NSMakePoint(phase.x + native.origin.x, phase.y + native.origin.y));
              clippedStrip.end();
              partialBands.composite(clippedStrip, native.origin);
            }
            CGContextRef graphics = expected.begin();
            ClipRing(graphics, body, style, projection);
            TableOracle(graphics, body, style, projection, phase, backing);
            expected.end();
            graphics = whole.begin();
            ClipRing(graphics, body, style, projection);
            PaintCardInk(graphics, body, style, projection, NSZeroPoint, phase);
            whole.end();
            std::printf("table matrix backing=%d metric=%d residue=%d extent=%d ink=%d\n",
                        backing,
                        metric,
                        residue,
                        extent,
                        ink);
            EqualBitmaps(actual, expected, true);
            EqualBitmaps(actual, whole, true);
            CardBitmap partial(180, 150, backing), partialExpected(180, 150, backing);
            partial.clear();
            partialExpected.clear();
            graphics = partial.begin();
            CGContextClipToRect(graphics, CGRectMake(33, 29, 61, 53));
            ClipRing(graphics, body, style, projection);
            PaintCardInk(graphics, body, style, projection, NSZeroPoint, phase);
            partial.end();
            graphics = partialExpected.begin();
            CGContextClipToRect(graphics, CGRectMake(33, 29, 61, 53));
            ClipRing(graphics, body, style, projection);
            TableOracle(graphics, body, style, projection, phase, backing);
            partialExpected.end();
            EqualBitmaps(partial, partialExpected, true);
            EqualBitmaps(partialBands, partialExpected, true);
            CardBitmap upright(180, 150, backing), uprightExpected(180, 150, backing);
            upright.clear();
            uprightExpected.clear();
            graphics = upright.begin(false);
            ClipRing(graphics, body, style, projection);
            PaintCardInk(graphics, body, style, projection, NSZeroPoint, phase);
            upright.end();
            graphics = uprightExpected.begin(false);
            ClipRing(graphics, body, style, projection);
            TableOracle(graphics, body, style, projection, phase, backing);
            uprightExpected.end();
            EqualBitmaps(upright, uprightExpected, true);
            if (extent == CARD_SHADOW_DROP && ink == CARD_SHADOW_STENCIL)
              CompareV1(whole, body, projection, phase, backing, metric, residue);
            [iteration drain];
          }
  VerifyAlphaBytes();
  VerifyFailureAndState();
  [pool drain];
}

void testMacCardBandLifecycleAndRetry()
{
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  [NSApplication sharedApplication];
  NSView *root = [[LokaCardTestRoot alloc] initWithFrame:NSMakeRect(0, 0, 180, 150)];
  {
    MacScenePlatformController controller(root, RailMetrics());
    int released = 0;
    NSView *view = nil;
    NSAutoreleasePool *nativePool = [[NSAutoreleasePool alloc] init];
    {
      CardBandNode node((CardBandProps()));
      LayoutState state = BandState(Frame(11, 17, 74, 3));
      Class viewClass = NSClassFromString(@"LokaCardBandView");
      LOKA_VERIFY(viewClass != Nil);
      {
        MacNativeRefusal refusal(@selector(initWithFrame:), reinterpret_cast<IMP>(&RefuseCardView), viewClass);
        LOKA_VERIFY(!controller.prepareProjectedLayout(&node, state));
        LOKA_VERIFY(!node.getContext());
        LOKA_VERIFY([[root subviews] count] == 0);
      }
      LOKA_VERIFY(controller.prepareProjectedLayout(&node, state));
      NodeContext *context = node.getContext();
      LOKA_VERIFY(context != 0 && [[root subviews] count] == 1);
      view = [[root subviews] objectAtIndex:0];
      observeMacNativeDeallocation(view, released);
      NotifySubtreeNodeDetached(&node);
      LifecycleFactTestAccess::DeliverFacts(&node);
      LOKA_VERIFY([view isHidden]);
      NotifySubtreeNodeAttached(&node);
      LifecycleFactTestAccess::DeliverFacts(&node);
      LOKA_VERIFY(![view isHidden]);
      LOKA_VERIFY(controller.prepareProjectedLayout(&node, state));
      LOKA_VERIFY(node.getContext() == context && [[root subviews] count] == 1);
      [view setNeedsDisplay:YES];
    }
    [nativePool drain];
    LOKA_VERIFY(released == 0 && [view superview] == nil && [view isHidden]);
    controller.drainNativeRetirements();
    LOKA_VERIFY(released == 1);
    controller.drainNativeRetirements();
    LOKA_VERIFY(released == 1);
  }
  [root release];
  [pool drain];
}

// Declared for the 10.4 SDK; called only after the runtime selector check.
// Messaging an existing AppKit layer needs no QuartzCore class symbol or
// framework link. The runtime selector checks also preserve the 10.4 SDK build.
@protocol LokaCardLayerCapture
- (void)renderInContext:(CGContextRef)context;
@end

@interface NSView (LokaCardLayerTest)
- (void)setWantsLayer:(BOOL)value;
@end

@interface LokaCardGround : LokaCardTestRoot
{
  unsigned paints_;
}
- (unsigned)paints;
@end
@implementation LokaCardGround
- (BOOL)isOpaque
{
  return YES;
}
- (unsigned)paints
{
  return paints_;
}
- (void)drawRect:(NSRect)dirty
{
  ++paints_;
  for (int y = static_cast<int>(NSMinY(dirty)); y < NSMaxY(dirty); ++y)
    for (int x = static_cast<int>(NSMinX(dirty)); x < NSMaxX(dirty); ++x)
    {
      NSUInteger pixel[4];
      GroundPixel(x, y, pixel);
      [[NSColor colorWithDeviceRed:static_cast<CGFloat>(pixel[0]) / 255
                             green:static_cast<CGFloat>(pixel[1]) / 255
                              blue:static_cast<CGFloat>(pixel[2]) / 255
                             alpha:1] set];
      NSRectFill(NSMakeRect(x, y, 1, 1));
    }
}
@end

namespace
{
  /** Capture existing layer contents without lockFocus, display, or a transaction
      flush. Once per snapshot, O(root backing area + this fixture's layers).
      The caller owns the returned bitmap; the graphics wrapper is autoreleased. */
  NSBitmapImageRep *CaptureCardLayers(LokaCardGround *root)
  {
    LOKA_VERIFY([root respondsToSelector:@selector(layer)]);
    id layer = [root performSelector:@selector(layer)];
    LOKA_VERIFY(layer != nil && [layer respondsToSelector:@selector(renderInContext:)]);
    const NSRect bounds = [root bounds];
    CGFloat scale = 1;
    if ([[root window] respondsToSelector:@selector(backingScaleFactor)])
      scale = [[root window] backingScaleFactor];
    const NSInteger width = static_cast<NSInteger>(std::ceil(bounds.size.width * scale));
    const NSInteger height = static_cast<NSInteger>(std::ceil(bounds.size.height * scale));
    NSBitmapImageRep *image = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:NULL
                                                                      pixelsWide:width
                                                                      pixelsHigh:height
                                                                   bitsPerSample:8
                                                                 samplesPerPixel:4
                                                                        hasAlpha:YES
                                                                        isPlanar:NO
                                                                  colorSpaceName:NSDeviceRGBColorSpace
                                                                     bytesPerRow:0
                                                                    bitsPerPixel:0];
    LOKA_VERIFY(image != nil);
    NSGraphicsContext *context = [NSGraphicsContext graphicsContextWithBitmapImageRep:image];
    LOKA_VERIFY(context != nil);
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    CGContextRef graphics = static_cast<CGContextRef>([context graphicsPort]);
#pragma clang diagnostic pop
    LOKA_VERIFY(graphics != 0);
    CGContextClearRect(graphics, CGRectMake(0, 0, width, height));
    CGContextScaleCTM(graphics, scale, scale);
    CGContextTranslateCTM(graphics, -bounds.origin.x, -bounds.origin.y);
    [(id<LokaCardLayerCapture>)layer renderInContext:graphics];
    return image;
  }

  /** Read existing contents, never cacheDisplay (which repaints). Layer-backed
      views require their layer tree, not the window backing store. The counter
      brackets the entire capture to detect any implicit drawRect invocation. */
  NSBitmapImageRep *CaptureCardWindow(LokaCardGround *root, bool layered)
  {
    const unsigned before = [root paints];
    NSBitmapImageRep *image = nil;
    if (layered)
      image = CaptureCardLayers(root);
    else
    {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
      [root lockFocus];
      image = [[NSBitmapImageRep alloc] initWithFocusedViewRect:[root bounds]];
      [root unlockFocus];
#pragma clang diagnostic pop
    }
    LOKA_VERIFY(image != nil && [root paints] == before);
    return image;
  }

  /** Positive control and comparison share the same fixture-owned pixel walk. */
  unsigned CardCaptureDifferences(NSBitmapImageRep *first, NSBitmapImageRep *second)
  {
    LOKA_VERIFY([first pixelsWide] == [second pixelsWide] && [first pixelsHigh] == [second pixelsHigh]);
    unsigned differences = 0;
    for (NSInteger y = 0; y < [first pixelsHigh]; ++y)
      for (NSInteger x = 0; x < [first pixelsWide]; ++x)
        if (!SamePixel(first, second, x, y))
          ++differences;
    return differences;
  }

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
  void DisplayMatchesFresh(LokaCardGround *root,
                           MacScenePlatformController &controller,
                           CardBandNode **nodes,
                           const Frame *bands,
                           const char *label,
                           bool layered)
  {
    if (!layered)
      [[root window] flushWindow];
    NSBitmapImageRep *incremental = CaptureCardWindow(root, layered);
    // New views have new backing/layers. Repainting only the parent cannot
    // establish a fresh reference when a child keeps its own layer contents.
    for (int role = 0; role < CARD_BAND_COUNT; ++role)
    {
      const CardBandProps props = nodes[role]->props;
      delete nodes[role];
      nodes[role] = new CardBandNode(props);
      LayoutState state = BandState(bands[role]);
      nodes[role]->layoutProjected(&controller, state);
      LOKA_VERIFY(nodes[role]->getContext() != 0);
    }
    [root setNeedsDisplay:YES];
    [root displayIfNeeded];
    if (!layered)
      [[root window] flushWindow];
    NSBitmapImageRep *fresh = CaptureCardWindow(root, layered);
    const unsigned differences = CardCaptureDifferences(incremental, fresh);
    std::printf("card real display %s differences=%u ground paints=%u\n", label, differences, [root paints]);
    [incremental release];
    [fresh release];
    LOKA_VERIFY(differences == 0);
  }

#pragma clang diagnostic pop

  void VerifyFreshGround(bool layered, bool scrolling)
  {
    LokaCardGround *root = [[LokaCardGround alloc] initWithFrame:NSMakeRect(0, 0, 180, 150)];
    if (layered && ![root respondsToSelector:@selector(setWantsLayer:)])
    {
      std::printf("[skip] card layer repaint: setWantsLayer: unavailable\n");
      [root release];
      return;
    }
    NSWindow *window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 180, 150)
                                                   styleMask:0
                                                     backing:NSBackingStoreBuffered
                                                       defer:NO];
    LOKA_VERIFY(window != nil);
    [window setReleasedWhenClosed:NO];
    [window setContentView:root];
    if (layered)
      [root setWantsLayer:YES];
    [window orderFront:nil];
    {
      MacScenePlatformController outer(root, RailMetrics());
      ScrollViewNode scroll((ScrollViewProps()));
      NSView *host = [[LokaCardTestRoot alloc] initWithFrame:NSMakeRect(3, 5, 160, 130)];
      NSScrollView *nativeScroll = nil;
      if (scrolling)
      {
        outer.onChange(&scroll, NODE_DIRTY_NONE, false);
        outer.relayout(180, 150);
        for (NSUInteger i = 0; i < [[root subviews] count]; ++i)
          if ([[[root subviews] objectAtIndex:i] isKindOfClass:[NSScrollView class]])
            nativeScroll = [[root subviews] objectAtIndex:i];
        LOKA_VERIFY(nativeScroll != nil);
        MacScrollViewContext *context = static_cast<MacScrollViewContext *>(scroll.getContext());
        LOKA_VERIFY(context != 0);
        context->commitScrollMetrics(160, 400, 130, 0, 0, false);
        [host release];
        host = [[nativeScroll documentView] retain];
      }
      else
        [root addSubview:host];
      {
        MacScenePlatformController controller(host, RailMetrics());
        [root setNeedsDisplay:YES];
        [root displayIfNeeded];
        NSBitmapImageRep *ground = CaptureCardWindow(root, layered);
        CardBandNode *nodes[4];
        Frame bands[4];
        const CardStyle style(CARD_SHADOW_WIDE, CARD_SHADOW_ALPHA);
        CardBands(Frame(25, 25, 90, 65), style, bands);
        for (int role = 0; role < 4; ++role)
        {
          nodes[role] = new CardBandNode(CardBandProps(static_cast<CardBandRole>(role), style));
          LayoutState state = BandState(bands[role]);
          nodes[role]->layoutProjected(&controller, state);
          LOKA_VERIFY(nodes[role]->getContext() != 0);
        }
        [root setNeedsDisplay:YES];
        [root displayIfNeeded];
        NSBitmapImageRep *painted = CaptureCardWindow(root, layered);
        // A capture that misses the band layers must not pass blank-to-blank.
        const unsigned appeared = CardCaptureDifferences(ground, painted);
        for (NSUInteger i = 0; i < [[host subviews] count]; ++i)
          [[[host subviews] objectAtIndex:i] setHidden:YES];
        [root setNeedsDisplay:YES];
        [root displayIfNeeded];
        NSBitmapImageRep *hidden = CaptureCardWindow(root, layered);
        const unsigned disappeared = CardCaptureDifferences(painted, hidden);
        const unsigned groundDifference = CardCaptureDifferences(ground, hidden);
        std::printf("card capture control layer=%d scroll=%d appeared=%u disappeared=%u ground differences=%u\n",
                    layered,
                    scrolling,
                    appeared,
                    disappeared,
                    groundDifference);
        LOKA_VERIFY(appeared != 0 && disappeared != 0 && groundDifference == 0);
        [ground release];
        [painted release];
        [hidden release];
        for (NSUInteger i = 0; i < [[host subviews] count]; ++i)
          [[[host subviews] objectAtIndex:i] setHidden:NO];
        [root setNeedsDisplay:YES];
        [root displayIfNeeded];
        for (int repaint = 0; repaint < 3; ++repaint)
        {
          NSView *band = [[host subviews] objectAtIndex:CARD_BAND_RIGHT];
          const NSRect rect = repaint == 2 ? NSInsetRect([band bounds], 1, 3) : [band bounds];
          [band setNeedsDisplayInRect:rect];
          [band displayIfNeeded];
          LOKA_VERIFY(![band needsDisplay]);
          if (repaint == 1)
          {
            [band setNeedsDisplayInRect:[band bounds]];
            [band displayIfNeeded];
          }
          std::printf("repaint mode layer=%d scroll=%d pass=%d\n", layered, scrolling, repaint);
          DisplayMatchesFresh(root, controller, nodes, bands, repaint == 2 ? "partial band" : "band alone", layered);
        }
        if (scrolling)
        {
          for (int role = 0; role < 4; ++role)
          {
            nodes[role]->props.style = CardStyle(CARD_SHADOW_WIDE, CARD_SHADOW_STENCIL);
            nodes[role]->getContext()->onPropsApplied();
          }
          [root displayIfNeeded];
          [[nativeScroll contentView] scrollToPoint:NSMakePoint(0, 5)];
          [nativeScroll reflectScrolledClipView:[nativeScroll contentView]];
          [root displayIfNeeded];
          DisplayMatchesFresh(root, controller, nodes, bands, "incremental scroll 5", layered);
          [[nativeScroll contentView] scrollToPoint:NSMakePoint(0, 11)];
          [nativeScroll reflectScrolledClipView:[nativeScroll contentView]];
          [root displayIfNeeded];
          DisplayMatchesFresh(root, controller, nodes, bands, "incremental scroll 11", layered);
          const NSPoint phase = [host convertPoint:NSZeroPoint toView:root];
          CardBitmap actual(180, 420, 1), expected(180, 420, 1);
          RenderViews(host, actual, 1);
          CGContextRef graphics = expected.begin();
          const CardStyle stencil(CARD_SHADOW_WIDE, CARD_SHADOW_STENCIL);
          ClipRing(graphics, Frame(25, 25, 90, 65), stencil, controller.projection());
          TableOracle(graphics, Frame(25, 25, 90, 65), stencil, controller.projection(), phase, 1);
          expected.end();
          EqualBitmaps(actual, expected, true);
        }
        else
        {
          CardBands(Frame(38, 32, 62, 48), style, bands);
          for (int role = 0; role < 4; ++role)
          {
            LayoutState state = BandState(bands[role]);
            nodes[role]->layoutProjected(&controller, state);
          }
          [root displayIfNeeded];
          DisplayMatchesFresh(root, controller, nodes, bands, "move and shrink old ring", layered);
        }
        for (int role = 0; role < 4; ++role)
          delete nodes[role];
      }
      if (!scrolling)
        [host removeFromSuperview];
      [host release];
      outer.onChange(0, NODE_DIRTY_NONE, false);
    }
    [window orderOut:nil];
    [window release];
    [root release];
  }
} // namespace

void testMacCardBandMovePropsAndEmpty()
{
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  [NSApplication sharedApplication];
  NSView *root = [[LokaCardTestRoot alloc] initWithFrame:NSMakeRect(0, 0, 180, 150)];
  // AppKit tracks needsDisplay only for views in a window (macOS 26 reports NO
  // for a windowless view). As the content view, root keeps the same phase.
  NSWindow *window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 180, 150)
                                                 styleMask:0
                                                   backing:NSBackingStoreBuffered
                                                     defer:NO];
  LOKA_VERIFY(window != nil);
  [window setContentView:root];
  {
    MacScenePlatformController controller(root, RailMetrics());
    CardNode card((CardProps()));
    card.addChild(new StackNode(StackProps(STACK_AXIS_COLUMN)));
    CardBandNode *bands[4];
    for (int i = 0; i < CARD_BAND_COUNT; ++i)
    {
      bands[i] = new CardBandNode(CardBandProps(static_cast<CardBandRole>(i)));
      card.addChild(bands[i]);
    }
    controller.onChange(&card, NODE_DIRTY_NONE, false);
    controller.relayout(180, 150);
    LOKA_VERIFY([[root subviews] count] == 4);
    NSView *first = [[root subviews] objectAtIndex:0];
    const NSRect before = [first frame];
    // Actual framed layout: enabled -> narrow -> enabled, same four contexts.
    for (int pass = 0; pass < 3; ++pass)
    {
      LayoutState state = BandState(Frame(21, 27, pass == 1 ? 16 : 80, 60));
      loka::dsl::testing::MacScenePlatformTestAccess::layoutNode(controller, &card, state);
      LOKA_VERIFY([[root subviews] count] == 4);
      LOKA_VERIFY([[root subviews] objectAtIndex:0] == first);
      CardBitmap actual(180, 150, 2), expected(180, 150, 2);
      RenderViews(root, actual, 2);
      if (pass != 1)
      {
        // Empty content gives the admitted 17-lu outer height.
        WholeReference(expected, Frame(21, 27, 77, 14), controller.projection());
        LOKA_VERIFY(!NSEqualRects([first frame], before));
      }
      else
        for (NSUInteger i = 0; i < [[root subviews] count]; ++i)
          LOKA_VERIFY(NSIsEmptyRect([[[root subviews] objectAtIndex:i] frame]));
      std::printf("card move/empty pass=%d\n", pass);
      EqualBitmaps(actual, expected, true);
    }
    // There is one style value in v1; a role change discriminates the props
    // copy, while needsDisplay pins the invalidation required by future styles.
    // setNeedsDisplay:NO does not clear a pending request on macOS 26; only a
    // display does, so the flag is proved clear before props are applied.
    [first displayIfNeeded];
    LOKA_VERIFY(![first needsDisplay]);
    bands[0]->getContext()->onPropsApplied();
    LOKA_VERIFY([first needsDisplay]);
    // Four rows satisfy both TOP (4 < radius) and BOTTOM (4-3 > 0) samples.
    const Frame strip(21, 27, 74, 4);
    LayoutState state = BandState(strip);
    bands[0]->layoutProjected(&controller, state);
    bands[0]->props.role = CARD_BAND_BOTTOM;
    bands[0]->props.style = CardStyle(CARD_SHADOW_DROP, CARD_SHADOW_ALPHA);
    bands[0]->getContext()->onPropsApplied();
    CardBitmap changed(74, 4, 2, 21, 27), reference(74, 4, 2, 21, 27);
    changed.begin();
    [first drawRect:[first bounds]];
    changed.end();
    CGContextRef graphics = reference.begin();
    PaintCardBandInk(graphics, strip, bands[0]->props, controller.projection(), NSMakePoint(21, 27));
    reference.end();
    std::printf("card props strip\n");
    EqualBitmaps(changed, reference, true);
    controller.onChange(0, NODE_DIRTY_NONE, false);
  }
  [root release];
  [window release];
  VerifyFreshGround(false, false);
  VerifyFreshGround(false, true);
  VerifyFreshGround(true, false);
  VerifyFreshGround(true, true);
  [pool drain];
}

void testMacCardPainterFixedDiameter()
{
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  const MacProjection projection(0, RailMetrics(Ratio(), Ratio(7, 5)));
  // This below-admission body is 16 points wide, but the fixed diameter is
  // 17 points. Check just its top-left border, away from displaced shadow.
  // The independently specified quarter arc discriminates a shared clamp in
  // both painter entries, which an equivalence-only test could miss.
  CardBitmap actual(80, 120, 2), expected(80, 120, 2);
  CGContextRef graphics = actual.begin();
  CGContextClipToRect(graphics, CGRectMake(20, 27, 4, 5));
  PaintCardInk(graphics, Frame(14, 17, 12, 57), CardStyle(), projection, NSZeroPoint, NSZeroPoint);
  actual.end();
  graphics = expected.begin();
  CGContextClipToRect(graphics, CGRectMake(20, 27, 4, 5));
  CGContextSetShouldAntialias(graphics, true);
  CGContextSetRGBStrokeColor(graphics, 0, 0, 0, 1);
  CGContextSetLineWidth(graphics, 1);
  const CGFloat pi = static_cast<CGFloat>(3.14159265358979323846);
  // Projected outer origin (20,24), 0.5-point inset, radius (17-1)/2.
  CGContextAddArc(graphics, 28.5, 32.5, 8, pi, 3 * pi / 2, false);
  CGContextStrokePath(graphics);
  expected.end();
  EqualBitmaps(actual, expected);
  [pool drain];
}
