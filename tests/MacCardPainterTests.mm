#include "MacCardPainterTests.hpp"
#include "MacCardPainter.hpp"
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
    CGContextRef begin()
    {
      NSGraphicsContext *context = [NSGraphicsContext graphicsContextWithBitmapImageRep:this->bitmap_];
      LOKA_VERIFY(context != nil);
      [NSGraphicsContext saveGraphicsState];
      [NSGraphicsContext setCurrentContext:context];
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
      CGContextRef graphics = static_cast<CGContextRef>([context graphicsPort]);
#pragma clang diagnostic pop
      CGContextTranslateCTM(graphics, 0, [this->bitmap_ pixelsHigh]);
      CGContextScaleCTM(graphics, this->backing_, -this->backing_);
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

  // Signed distance to the outline: straight sides and four corner circles.
  // The inner rectangle locates the nearest circle center in a corner quadrant.
  // This is independent of the production arc paths and their rasterizer.
  double RoundedCardDistance(NSRect rect, double radius, double x, double y)
  {
    const double dx = std::fabs(x - NSMidX(rect)) - (rect.size.width / 2 - radius);
    const double dy = std::fabs(y - NSMidY(rect)) - (rect.size.height / 2 - radius);
    const double outsideX = dx > 0 ? dx : 0;
    const double outsideY = dy > 0 ? dy : 0;
    const double inside = dx > dy ? dx : dy;
    return std::sqrt(outsideX * outsideX + outsideY * outsideY) + (inside < 0 ? inside : 0) - radius;
  }

  void VerifyShadowOracle(const CardBitmap &actual,
                          const Frame &body,
                          const Frame *bands,
                          const MacProjection &projection,
                          NSPoint phase,
                          int backing)
  {
    // Absolute layout edges are the oracle's only shadow geometry source.
    const double left = projection.projectEdge(body.x + 3).pt;
    const double top = projection.projectEdge(body.y + 3).pt;
    const double right = projection.projectEdge(body.x + body.width + 3).pt;
    const double bottom = projection.projectEdge(body.y + body.height + 3).pt;
    const NSRect shadow = NSMakeRect(left, top, right - left, bottom - top);
    const NSRect nativeBody = projection.projectFrame(body).r;
    const double radius = projection.projectLength(0, 12).pt / 2;
    const CardBandRole roles[] = {CARD_BAND_RIGHT, CARD_BAND_BOTTOM};
    for (int side = 0; side < 2; ++side)
    {
      const NSRect band = projection.projectFrame(bands[roles[side]]).r;
      unsigned compared = 0;
      for (int y = static_cast<int>(NSMinY(band) * backing); y < NSMaxY(band) * backing; ++y)
        for (int x = static_cast<int>(NSMinX(band) * backing); x < NSMaxX(band) * backing; ++x)
        {
          const double centerX = (x + 0.5) / backing;
          const double centerY = (y + 0.5) / backing;
          // Sample only the exposed top-right / bottom-left corner quadrants.
          // Include both sides of the shadow arc and the outermost frame pixels.
          if (side == 0 ? (centerX < right - radius || centerY > top + radius)
                        : (centerX > left + radius || centerY < bottom - radius))
            continue;
          // Exterior samples farther than one backing pixel from the body
          // cannot include its antialiased border. No outer clip exclusion.
          if (RoundedCardDistance(nativeBody, radius, centerX, centerY) <= 1.0 / backing)
            continue;
          // CoreGraphics' aliased fill is not center sampling at an arc (a
          // pixel whose center is 0.36 px outside was filled on macOS 26), so
          // the shadow outline gets the same one-pixel guard. A misplaced
          // shadow (1 pt at 3/2) still moves pixels beyond it at backing 2.
          const double shadowDistance = RoundedCardDistance(shadow, radius, centerX, centerY);
          if (std::fabs(shadowDistance) <= 1.0 / backing)
            continue;
          NSUInteger pixel[4], wanted[4];
          [actual.bitmap() getPixel:pixel atX:x y:y];
          GroundPixel(x, y, wanted);
          const bool member = shadowDistance < 0;
          if (member && ((x + y + static_cast<int>((phase.x + phase.y) * backing)) & 1))
            wanted[0] = wanted[1] = wanted[2] = 0;
          for (int channel = 0; channel < 4; ++channel)
          {
            if (pixel[channel] != wanted[channel])
              std::printf("shadow oracle side=%d backing=%d pixel=(%d,%d) channel=%d actual=%lu expected=%lu\n",
                          side,
                          backing,
                          x,
                          y,
                          channel,
                          static_cast<unsigned long>(pixel[channel]),
                          static_cast<unsigned long>(wanted[channel]));
            LOKA_VERIFY(pixel[channel] == wanted[channel]);
          }
          ++compared;
        }
      std::printf("shadow oracle side=%d backing=%d compared=%u\n", side, backing, compared);
      LOKA_VERIFY(compared != 0);
    }
  }

  id RefuseCardView(id object, SEL, NSRect)
  {
    [object release];
    return nil;
  }
} // namespace

void testMacCardPainterPixels()
{
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  [NSApplication sharedApplication];
  const int sizes[][2] = {{77, 57}, {14, 57}, {77, 14}, {14, 14}, {12, 57}, {77, 12}, {12, 12}};
  for (int metric = 0; metric < 2; ++metric)
    for (int backing = 1; backing <= 2; ++backing)
      for (int sample = 0; sample < 7; ++sample)
      {
        NSAutoreleasePool *iteration = [[NSAutoreleasePool alloc] init];
        NSView *root = [[LokaCardTestRoot alloc] initWithFrame:NSMakeRect(0, 0, 180, 150)];
        NSWindow *window = nil;
        NSPoint phase = NSZeroPoint;
        if (sample == 0 && metric == 0)
        {
          // A nested flipped view in an unflipped window content view makes
          // the window-origin phase observably different from the local one.
          window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 200, 180)
                                               styleMask:0
                                                 backing:NSBackingStoreBuffered
                                                   defer:NO];
          LOKA_VERIFY(window != nil && ![[window contentView] isFlipped]);
          [root setFrameOrigin:NSMakePoint(3, 4)];
          [[window contentView] addSubview:root];
          phase = NSMakePoint(3, 26);
        }
        {
          const RailMetrics metrics(Ratio(), metric ? Ratio(3, 2) : Ratio());
          MacScenePlatformController controller(root, metrics);
          const int width = sizes[sample][0], height = sizes[sample][1];
          const int originX = 11;
          const Frame body(originX, 17, width, height);
          const Frame bands[] = {Frame(originX, 17, width - 3, 3),
                                 Frame(originX, 20, 3, height),
                                 Frame(originX + width - 3, 17, 6, height - 3),
                                 Frame(originX + 3, 17 + height - 3, width, 6)};
          CardBandNode *nodes[4];
          for (int role = 0; role < CARD_BAND_COUNT; ++role)
          {
            nodes[role] = new CardBandNode(CardBandProps(static_cast<CardBandRole>(role)));
            LayoutState state = BandState(bands[role]);
            nodes[role]->layoutProjected(&controller, state);
            LOKA_VERIFY(nodes[role]->getContext() != 0);
            NSView *view = [[root subviews] objectAtIndex:role];
            LOKA_VERIFY([view isFlipped] && ![view isOpaque]);
            LOKA_VERIFY(NSEqualRects([view frame], controller.projection().projectFrame(bands[role]).r));
          }
          LOKA_VERIFY([[root subviews] count] == 4);
          CardBitmap actual(180, 150, backing), expected(180, 150, backing), ground(180, 150, backing);
          WholeReference(expected, body, controller.projection(), phase);
          RenderViews(root, actual, backing);
          std::printf("card pixels space=%d backing=%d body=%dx%d\n", metric, backing, width, height);
          EqualBitmaps(actual, expected, true);
          VerifyShadowOracle(actual, body, bands, controller.projection(), phase, backing);
          const NSRect seat = controller.projection().projectFrame(Frame(originX + 3, 20, width - 6, height - 6)).r;
          for (int y = static_cast<int>(NSMinY(seat) * backing); y < NSMaxY(seat) * backing; ++y)
            for (int x = static_cast<int>(NSMinX(seat) * backing); x < NSMaxX(seat) * backing; ++x)
            {
              // No band owns the content seat (Q1), so the bands never paint it.
              LOKA_VERIFY(SamePixel(actual.bitmap(), ground.bitmap(), x, y));
            }
          if (height == 57)
          {
            const NSRect native = controller.projection().projectFrame(body).r;
            const int middle = static_cast<int>((NSMinY(native) + NSMaxY(native)) * backing / 2);
            const int shadowX = static_cast<int>(NSMaxX(native) * backing) + 1;
            const int left = static_cast<int>(NSMinX(native) * backing);
            const int pen = static_cast<int>(controller.projection().projectLength(0, 1).pt * backing);
            for (int x = left; x < left + pen; ++x)
            {
              NSUInteger pixel[4];
              [actual.bitmap() getPixel:pixel atX:x y:middle];
              LOKA_VERIFY(pixel[0] == 0 && pixel[1] == 0 && pixel[2] == 0 && pixel[3] == 255);
            }
            LOKA_VERIFY(SamePixel(actual.bitmap(), ground.bitmap(), left + pen, middle));
            for (int y = middle; y < middle + 8; ++y)
            {
              NSUInteger pixel[4], wanted[4];
              [actual.bitmap() getPixel:pixel atX:shadowX y:y];
              GroundPixel(shadowX, y, wanted);
              if ((shadowX + y + static_cast<int>((phase.x + phase.y) * backing)) & 1)
                wanted[0] = wanted[1] = wanted[2] = 0;
              for (int c = 0; c < 4; ++c)
                LOKA_VERIFY(pixel[c] == wanted[c]);
            }
          }
          for (int role = 0; role < CARD_BAND_COUNT; ++role)
            delete nodes[role];
        }
        [root removeFromSuperview];
        [root release];
        [window release];
        [iteration drain];
      }
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
