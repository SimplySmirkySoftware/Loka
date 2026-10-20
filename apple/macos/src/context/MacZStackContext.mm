#include "MacZStackContext.hpp"
#include "../MacGround.hpp"
#include "../platform/MacNativeGeometry.hpp"
#include <cassert>
#include <new>
#include <climits>
#include <cstdlib>
#include <cmath>
#include "app/style/StackBlur.hpp"

#ifdef TEST_BUILD
namespace loka
{
  namespace testing
  {
    bool declineMacStackUnitView();
    bool declineMacPhotoCapture();
  }
} // namespace loka
#endif

@interface LokaStackUnitView : NSView
@end
@implementation LokaStackUnitView
- (BOOL)isFlipped
{
  return YES;
}
- (BOOL)isOpaque
{
  return YES;
}
- (BOOL)acceptsFirstResponder
{
  return NO;
}
- (void)drawRect:(NSRect)rect
{
  // Paint the unit's own bounds, as LokaRectSurfaceView does: on a layer-backed
  // window (Tahoe) the dirty rect handed to an opaque view can span the whole
  // root, and filling it would paint the window ground over the ground child.
  (void)rect;
  loka::macos::MacPaintGround(loka::app::SURFACE_GROUND_WINDOW, [self bounds]);
}
@end

@class CALayer;

// Runtime declarations keep pre-10.10 SDKs independent of the effect type.
@interface NSView (LokaStackVisualEffect)
- (void)setBlendingMode:(NSInteger)mode;
- (void)setMaterial:(NSInteger)material;
- (void)setState:(NSInteger)state;
- (void)setWantsLayer:(BOOL)value;
- (CALayer *)layer;
- (NSRect)convertRectToBacking:(NSRect)rect;
- (void)viewDidChangeBackingProperties;
@end

@interface NSWindow (LokaStackBacking)
- (NSRect)convertRectToBacking:(NSRect)rect;
@end

namespace
{
  bool MenuBarTransparency()
  {
    NSDictionary *global = [[NSUserDefaults standardUserDefaults] persistentDomainForName:NSGlobalDomain];
    id value = [global objectForKey:@"AppleEnableMenuBarTransparency"];
    return !value || ![value respondsToSelector:@selector(boolValue)] || [value boolValue];
  }

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
  // Win32 and Toolbox will paint this look later (page 4, section 10).
  // Move these values to common when a second rail paints them.
  const double kFrostTopAlpha = 0.55;
  const double kFrostBottomAlpha = 0.08;
  const double kFrostHairlineAlpha = 0.22;

  void PaintStaticFrost(NSRect bounds)
  {
    loka::macos::MacPaintGround(loka::app::SURFACE_GROUND_WINDOW, bounds);
    const int rows = static_cast<int>(std::ceil(static_cast<double>(bounds.size.height)));
    for (int row = 0; row < rows; ++row)
    {
      const double t = rows > 1 ? static_cast<double>(row) / (rows - 1) : 0.0;
      [[NSColor colorWithCalibratedWhite:1.0
                                   alpha:static_cast<CGFloat>(kFrostTopAlpha * (1.0 - t) + kFrostBottomAlpha * t)] set];
      NSRectFillUsingOperation(NSMakeRect(NSMinX(bounds), NSMinY(bounds) + row, NSWidth(bounds), 1),
                               LOKA_MAC_COMPOSITING_SOURCE_OVER);
    }
    [[NSColor colorWithCalibratedWhite:0.0 alpha:kFrostHairlineAlpha] set];
    NSRectFillUsingOperation(NSMakeRect(NSMinX(bounds), NSMaxY(bounds) - 1, NSWidth(bounds), 1),
                             LOKA_MAC_COMPOSITING_SOURCE_OVER);
  }

  void PaintBlurTint(NSRect bounds)
  {
    NSColor *ground = nil;
    loka::macos::QueryMacGroundColor(loka::app::SURFACE_GROUND_WINDOW, ground);
    NSColor *rgb = [ground colorUsingColorSpaceName:NSCalibratedRGBColorSpace];
    if (!rgb)
      rgb = [NSColor colorWithCalibratedWhite:0.93 alpha:1.0];
    [[rgb colorWithAlphaComponent:0.6] set];
    NSRectFillUsingOperation(bounds, LOKA_MAC_COMPOSITING_SOURCE_OVER);
  }

  /** One observed visible domain. Pixel dimensions/origin are integral;
      local points preserve fractional mapping. The window is identity only. */
  struct MacPhotoGeometry
  {
    NSRect local;
    int width, height;
    long windowX, windowY;
    NSWindow *window;
    bool operator==(const MacPhotoGeometry &other) const
    {
      return NSEqualRects(this->local, other.local) && this->width == other.width && this->height == other.height
             && this->windowX == other.windowX && this->windowY == other.windowY && this->window == other.window;
    }
  };

  /** Layout and screen draws query once; AppKit owns ancestor conversion. */
  MacPhotoGeometry PhotoGeometry(NSView *view)
  {
    MacPhotoGeometry geometry = {};
    geometry.local = [view visibleRect];
    geometry.window = [view window];
    NSRect pixels = geometry.local;
    if ([view respondsToSelector:@selector(convertRectToBacking:)])
      pixels = [view convertRectToBacking:pixels];
    pixels = NSIntegralRect(pixels);
    NSRect windowRect = [view convertRect:geometry.local toView:nil];
    if ([geometry.window respondsToSelector:@selector(convertRectToBacking:)])
      windowRect = [geometry.window convertRectToBacking:windowRect];
    windowRect = NSIntegralRect(windowRect);
    // Reject out-of-range/NaN dimensions before any integer conversion. A
    // nonempty unsupported extent reaches capture and fails dimension validation.
    if (pixels.size.width > 0 && static_cast<double>(pixels.size.width) <= INT_MAX / 4 && pixels.size.height > 0
        && static_cast<double>(pixels.size.height) <= INT_MAX)
    {
      geometry.width = static_cast<int>(pixels.size.width);
      geometry.height = static_cast<int>(pixels.size.height);
    }
    if (static_cast<double>(windowRect.origin.x) >= INT_MIN && static_cast<double>(windowRect.origin.x) <= INT_MAX
        && static_cast<double>(windowRect.origin.y) >= INT_MIN && static_cast<double>(windowRect.origin.y) <= INT_MAX)
    {
      geometry.windowX = static_cast<long>(windowRect.origin.x);
      geometry.windowY = static_cast<long>(windowRect.origin.y);
    }
    return geometry;
  }

  /** Every draw walks only its own AppKit dirty rectangles, without rounding. */
  bool CoversPhoto(NSView *view, NSRect local)
  {
    const NSRect *rects = NULL;
    NSInteger count = 0;
    [view getRectsBeingDrawn:&rects count:&count];
    for (NSInteger i = 0; rects && i < count; ++i)
      if (NSContainsRect(rects[i], local))
        return true;
    return false;
  }

  /** Zero-initialized view-owned storage, explicitly reset before ObjC dealloc.
      No C++ constructor/destructor runtime support is required. READY is derived. */
  class MacPhotoLifetime
  {
  public:
    bool reconcile(bool attached, const MacPhotoGeometry &geometry)
    {
      if (attached && (NSIsEmptyRect(geometry.local) || this->geometry_ == geometry))
        return false;
      this->resetLifetime();
      this->geometry_ = geometry;
      return true;
    }
    void resetLifetime()
    {
      [this->photo_ release];
      this->photo_ = nil;
      this->phase_ = EMPTY;
      const MacPhotoGeometry empty = {};
      this->geometry_ = empty;
    }
    void installCapture(NSBitmapImageRep *rep)
    {
      [rep retain];
      [this->photo_ release];
      this->photo_ = rep;
      this->phase_ = rep ? EMPTY : DISABLED;
    }
    bool admitsCapture(bool policyAllowsWhenReady) const
    {
      return this->phase_ != DISABLED && (policyAllowsWhenReady || !this->photo_);
    }
    NSBitmapImageRep *photo() const
    {
      return this->photo_;
    }
    const MacPhotoGeometry &geometry() const
    {
      return this->geometry_;
    }

  private:
    NSBitmapImageRep *photo_;
    enum Phase
    {
      EMPTY,
      DISABLED
    } phase_;
    MacPhotoGeometry geometry_;
  };

  /** One capture opportunity, O(pixel count), walking only the captured pixels.
      Returns a +1 premultiplied RGBA8 bitmap in its natural unflipped order. */
  NSBitmapImageRep *CapturePhoto(const MacPhotoGeometry &geometry)
  {
#ifdef TEST_BUILD
    if (loka::testing::declineMacPhotoCapture())
      return nil;
#endif
    NSBitmapImageRep *source = [[NSBitmapImageRep alloc] initWithFocusedViewRect:geometry.local];
    if (!source)
      return nil;
    const long bpp = [source bitsPerPixel];
    const long spp = [source samplesPerPixel];
    const long alpha = [source hasAlpha];
    const long planar = [source isPlanar];
    const long row = [source bytesPerRow];
    const long pixelWidth = [source pixelsWide];
    const long pixelHeight = [source pixelsHigh];
    const NSUInteger format = [source bitmapFormat]; // Available since Tiger.
    NSString *space = [source colorSpaceName];
    const bool rgb = [space isEqualToString:NSDeviceRGBColorSpace] || [space isEqualToString:NSCalibratedRGBColorSpace];
    const bool rgb24 = bpp == 24 && spp == 3 && !alpha;
    const bool rgba32 = bpp == 32 && spp == 4 && alpha;
    // Only the nonpremultiplied-alpha bit is convertible; reject alpha-first,
    // floating-point and non-native byte-order encodings rather than guessing.
    if (!rgb || planar || [source bitsPerSample] != 8 || (!rgb24 && !rgba32)
        || (format & ~static_cast<NSUInteger>(NSAlphaNonpremultipliedBitmapFormat)) || pixelWidth != geometry.width
        || pixelHeight != geometry.height || pixelWidth <= 0 || pixelWidth > INT_MAX / 4 || pixelHeight <= 0
        || pixelHeight > INT_MAX || row < pixelWidth * spp || ![source bitmapData]
        || static_cast<size_t>(row) > static_cast<size_t>(-1) / static_cast<size_t>(pixelHeight))
    {
      [source release];
      return nil;
    }
    const int width = static_cast<int>(pixelWidth), height = static_cast<int>(pixelHeight);
    const size_t scratchBytes = loka::app::StackBlurScratchBytes(width, height);
    if (!scratchBytes)
    {
      [source release];
      return nil;
    }
    NSBitmapImageRep *photo = source;
    if (!rgba32 || row != width * 4 || format != 0)
    {
      photo = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:NULL
                                                      pixelsWide:width
                                                      pixelsHigh:height
                                                   bitsPerSample:8
                                                 samplesPerPixel:4
                                                        hasAlpha:YES
                                                        isPlanar:NO
                                                  colorSpaceName:space
                                                     bytesPerRow:width * 4
                                                    bitsPerPixel:32];
      if (!photo || ![photo bitmapData] || [photo bytesPerRow] != width * 4)
      {
        [photo release];
        [source release];
        return nil;
      }
      for (int y = 0; y < height; ++y)
      {
        const unsigned char *src = [source bitmapData] + static_cast<size_t>(y) * row;
        unsigned char *dst = [photo bitmapData] + static_cast<size_t>(y) * width * 4;
        for (int x = 0; x < width; ++x, src += spp, dst += 4)
        {
          const unsigned alpha = rgba32 ? src[3] : 255;
          for (int c = 0; c < 3; ++c)
            dst[c] = format & NSAlphaNonpremultipliedBitmapFormat
                         ? static_cast<unsigned char>((src[c] * alpha + 127) / 255)
                         : src[c];
          dst[3] = static_cast<unsigned char>(alpha);
        }
      }
      [source release];
    }
    unsigned char *scratch = static_cast<unsigned char *>(std::malloc(scratchBytes));
    const bool ok =
        scratch && loka::app::StackBlur([photo bitmapData], width, height, width * 4, 8, scratch, scratchBytes);
    std::free(scratch);
    if (!ok)
    {
      [photo release];
      return nil;
    }
    return photo;
  }

#if defined(__clang__)
#pragma clang diagnostic pop
#endif
} // namespace

@interface LokaStackEffectUnitView : LokaStackUnitView
@end
@implementation LokaStackEffectUnitView
- (BOOL)isOpaque
{
  return NO;
}
- (void)drawRect:(NSRect)rect
{
  (void)rect; // The bottom effect subview supplies the ground.
}
@end

/** Owns one photo and its appearance lifetime; never requests a redraw. */
@interface LokaStackPhotoUnitView : LokaStackUnitView
{
  MacPhotoLifetime photo_;
}
- (MacPhotoLifetime &)lokaPhotoLifetime;
- (BOOL)lokaCapturesWhenReady;
- (void)lokaReconcileAttached:(BOOL)attached;
- (void)lokaReplayPhoto;
@end
@implementation LokaStackPhotoUnitView
- (MacPhotoLifetime &)lokaPhotoLifetime
{
  return photo_;
}
- (BOOL)isOpaque
{
  return NO;
}
- (BOOL)lokaCapturesWhenReady
{
  return NO;
}
/** Owner placement/lifecycle calls once per unit; no foreign row walk. */
- (void)lokaReconcileAttached:(BOOL)attached
{
  [self lokaPhotoLifetime].reconcile(attached != NO, PhotoGeometry(self));
}
- (void)viewDidMoveToWindow
{
  if ([LokaStackUnitView instancesRespondToSelector:@selector(viewDidMoveToWindow)])
    [super viewDidMoveToWindow];
  [self lokaPhotoLifetime].resetLifetime();
}
- (void)viewDidChangeBackingProperties
{
  if ([LokaStackUnitView instancesRespondToSelector:@selector(viewDidChangeBackingProperties)])
    [super viewDidChangeBackingProperties];
  [self lokaPhotoLifetime].resetLifetime();
}
- (void)dealloc
{
  [self lokaPhotoLifetime].resetLifetime();
  [super dealloc];
}
/** Draw-local replay uses only the stored photo and its matching destination. */
- (void)lokaReplayPhoto
{
  const MacPhotoLifetime &lifetime = [self lokaPhotoLifetime];
  if (!lifetime.photo())
  {
    PaintStaticFrost([self bounds]);
    return;
  }
  const NSRect local = lifetime.geometry().local;
  [NSGraphicsContext saveGraphicsState];
  NSAffineTransform *flip = [NSAffineTransform transform];
  BOOL replay = NO;
  if (flip)
  {
    [flip translateXBy:0 yBy:NSMinY(local) + NSMaxY(local)];
    [flip scaleXBy:1 yBy:-1];
    [flip concat];
    replay = [lifetime.photo() drawInRect:local];
  }
  [NSGraphicsContext restoreGraphicsState];
  if (replay)
    PaintBlurTint(local);
  else
    PaintStaticFrost(local);
}
/** AppKit calls per delivered draw: own dirty rects, then at most one capture. */
- (void)drawRect:(NSRect)rect
{
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  // A screen draw is one whose current context is the window's own: on 10.9
  // cacheDisplayInRect: still reports isDrawingToScreen, but draws through a
  // bitmap context, so only the port comparison tells the two apart.
  NSGraphicsContext *current = [NSGraphicsContext currentContext];
  NSGraphicsContext *windowContext = [[self window] graphicsContext];
  const bool attached = [self window] != nil;
  const bool screen = attached && [current isDrawingToScreen] != NO && windowContext != nil
                      && [current graphicsPort] == [windowContext graphicsPort];
  if (!(screen && attached))
  {
    [self lokaReplayPhoto];
    [pool release];
    return;
  }
  const MacPhotoGeometry geometry = PhotoGeometry(self);
  if (NSIsEmptyRect(geometry.local))
  {
    PaintStaticFrost(rect);
    [pool release];
    return;
  }
  MacPhotoLifetime &lifetime = [self lokaPhotoLifetime];
  lifetime.reconcile(true, geometry);
  const bool layer = [self respondsToSelector:@selector(layer)] && [self layer] != nil;
  if (CoversPhoto(self, geometry.local) && !layer && MenuBarTransparency()
      && lifetime.admitsCapture([self lokaCapturesWhenReady] != NO))
  {
    NSBitmapImageRep *photo = CapturePhoto(geometry);
    lifetime.installCapture(photo);
    [photo release];
  }
  [self lokaReplayPhoto];
  [pool release];
}
@end

/** Retakes the photo at each admitted full draw. */
@interface LokaStackBandUnitView : LokaStackPhotoUnitView
@end
@implementation LokaStackBandUnitView
- (BOOL)lokaCapturesWhenReady
{
  return YES;
}
@end

/** Keeps the first photo until observed geometry or appearance changes. */
@interface LokaStackStaticUnitView : LokaStackPhotoUnitView
@end
@implementation LokaStackStaticUnitView
@end

#ifdef TEST_BUILD
namespace loka
{
  namespace testing
  {
    NSBitmapImageRep *macStackPhoto(NSView *view)
    {
      return [(LokaStackPhotoUnitView *)view lokaPhotoLifetime].photo();
    }
    bool macStackPhotoDisabled(NSView *view)
    {
      return ![(LokaStackPhotoUnitView *)view lokaPhotoLifetime].admitsCapture(true);
    }
    NSRect macStackPhotoRect(NSView *view)
    {
      return [(LokaStackPhotoUnitView *)view lokaPhotoLifetime].geometry().local;
    }
  } // namespace testing
} // namespace loka
#endif

namespace
{
  /** Unit admission calls once per missing view; constant work, no row walk.
      Returns a retained view, whose subviews own the native effect. */
  NSView *CreateBlurUnit(NSView *parent, NSRect frame, loka::app::ZStackUnitGround ground)
  {
    Class effectClass = NSClassFromString(@"NSVisualEffectView");
    if (!effectClass)
    {
      if (ground == loka::app::ZSTACK_UNIT_GROUND_EXP_STATIC_BLUR)
        return [[LokaStackStaticUnitView alloc] initWithFrame:frame];
      return [[LokaStackBandUnitView alloc] initWithFrame:frame];
    }
    NSView *view = [[LokaStackEffectUnitView alloc] initWithFrame:frame];
    if (!view)
      return nil;
    NSView *effect = [[effectClass alloc] initWithFrame:[view bounds]];
    if (!effect)
    {
      [view release];
      return nil;
    }
    // 10.13 raises an exception for HUD (13); select by running AppKit.
    if ([effect respondsToSelector:@selector(setMaterial:)])
      [effect setMaterial:NSAppKitVersionNumber >= 1671 ? 13 : 3];
    if ([effect respondsToSelector:@selector(setBlendingMode:)])
      [effect setBlendingMode:1];
    if ([effect respondsToSelector:@selector(setState:)])
      [effect setState:0];
    [effect setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
    // Old SDK builds do not implicitly layer-back within-window blending.
    if ([parent respondsToSelector:@selector(setWantsLayer:)])
      [parent setWantsLayer:YES];
    if ([view respondsToSelector:@selector(setWantsLayer:)])
      [view setWantsLayer:YES];
    if ([effect respondsToSelector:@selector(setWantsLayer:)])
      [effect setWantsLayer:YES];
    [view addSubview:effect positioned:NSWindowBelow relativeTo:nil];
    [effect release];
    return view;
  }
} // namespace

MacZStackContext::Unit::Unit(unsigned index)
    : index_(index),
      next(0),
      view_(0)
{
}

bool MacZStackContext::Unit::ensure(
    void *parent, const loka::macos::MacRect &seat, void *below, void *above, loka::app::ZStackUnitGround ground)
{
  if (this->view_)
    return true;
#ifdef TEST_BUILD
  if (loka::testing::declineMacStackUnitView())
    return false;
#endif
  // Only the first unit (second child) may blur; visibility and height never
  // promote a later unit from its ordinary window ground.
  NSView *view =
      (ground == loka::app::ZSTACK_UNIT_GROUND_EXP_BLUR || ground == loka::app::ZSTACK_UNIT_GROUND_EXP_STATIC_BLUR)
              && this->index_ == 1
          ? CreateBlurUnit((NSView *)parent, seat.r, ground)
          : [[LokaStackUnitView alloc] initWithFrame:seat.r];
  if (!view)
    return false;
  this->view_ = view;
  if (below)
    [(NSView *)parent addSubview:view positioned:NSWindowBelow relativeTo:(NSView *)below];
  else
    [(NSView *)parent addSubview:view positioned:NSWindowAbove relativeTo:(NSView *)above];
  return true;
}

void MacZStackContext::Unit::place(const loka::macos::MacRect &seat)
{
  NSView *view = (NSView *)this->view_;
  loka::macos::SetMacFrame(view, seat);
  if ([view isKindOfClass:[LokaStackPhotoUnitView class]])
    [(LokaStackPhotoUnitView *)view lokaReconcileAttached:YES];
}

MacZStackContext::MacZStackContext(MacScenePlatformController *controller, loka::app::ZStackUnitGround ground)
    : MacRetirableContext(controller),
      ground_(ground),
      units_(0)
{
}

bool MacZStackContext::ensureUnit(Unit &unit, void *parent, const loka::macos::MacRect &seat, void *below, void *above)
{
  return unit.ensure(parent, seat, below, above, this->ground_);
}

void MacZStackContext::onPropsApplied()
{
#ifdef LOKA_LIFECYCLE_AUDIT
  const loka::app::ZStackNode *stack = this->owner() ? this->owner()->asZStackNode() : 0;
  assert(stack && stack->props.unitGround == this->ground_
         && "ZStack unitGround is chosen at first projection; switch grounds through a seat (Show/Match arm or Keyed) "
            "for a new ZStack identity");
#endif
}

MacZStackContext::~MacZStackContext()
{
  while (this->units_)
  {
    Unit *unit = this->units_;
    this->units_ = unit->next;
    assert(!unit->view_ && "terminal fact must queue every unit before context reclaim");
    delete unit;
  }
}

MacZStackContext::Unit *MacZStackContext::unitFor(unsigned index, Unit **&cursor)
{
  if (*cursor && (*cursor)->serves(index))
  {
    Unit *unit = *cursor;
    cursor = &unit->next;
    return unit;
  }
  Unit *unit = new (std::nothrow) Unit(index);
  if (!unit)
    return 0;
  unit->next = *cursor;
  *cursor = unit;
  cursor = &unit->next;
  return unit;
}

void MacZStackContext::lowerNewGroundViews(void *parent, void *previousLast)
{
  Unit *first = this->units_;
  while (first && !first->view())
    first = first->next;
  if (!first)
    return;
  if (!previousLast)
  {
    assert(false && "existing units require a ground-pass subview snapshot");
    return;
  }
  NSView *host = (NSView *)parent;
  // Retirement removal cannot run during this layout pass: the safe point
  // follows layout, so the recorded view must still belong to this parent.
  NSArray *subviews = [host subviews];
  const NSUInteger index = [subviews indexOfObject:(NSView *)previousLast];
  if (index == NSNotFound)
  {
    assert(false && "ground-pass snapshot must remain parented until the safe point");
    return;
  }
  const NSUInteger count = [subviews count] - 1 - index;
  NSView *anchor = (NSView *)first->view();
  // Units are inserted at creation and previousLast is at or above unit 0,
  // so no own unit can occur in this suffix. Its fixed size bounds repair.
  for (NSUInteger moved = 0; moved < count; ++moved)
  {
    NSView *view = [[host subviews] lastObject];
    for (Unit *unit = this->units_; unit; unit = unit->next)
      if (unit->view() == view)
      {
        assert(false && "ground repair must not move an own unit");
        return;
      }
    [host addSubview:view positioned:NSWindowBelow relativeTo:anchor];
    anchor = view;
  }
}

void MacZStackContext::onFactChanged(loka::app::scene::NodeLifecycleFact, loka::app::scene::NodeLifecycleFact next)
{
  for (Unit *unit = this->units_; unit; unit = unit->next)
  {
    NSView *view = (NSView *)unit->view_;
    [view setHidden:next != loka::app::scene::NODE_FACT_ATTACHED];
    if ([view isKindOfClass:[LokaStackPhotoUnitView class]])
      [(LokaStackPhotoUnitView *)view lokaReconcileAttached:next == loka::app::scene::NODE_FACT_ATTACHED];
    if (next == loka::app::scene::NODE_FACT_RETIRED)
    {
      [view removeFromSuperview];
      this->retireNativeObject(unit->view_);
    }
  }
}
