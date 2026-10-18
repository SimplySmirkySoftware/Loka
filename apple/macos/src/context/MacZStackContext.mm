#include "MacZStackContext.hpp"
#include "../MacGround.hpp"
#include "../platform/MacNativeGeometry.hpp"
#include <cassert>
#include <new>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include "app/style/StackBlur.hpp"

#ifdef TEST_BUILD
namespace loka
{
  namespace testing
  {
    bool declineMacStackUnitView();
    bool forceMacStackCPUBlur();
    int macStackMenuBarTransparency();
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

// Runtime declarations keep pre-10.10 SDKs independent of the effect type.
@interface NSView (LokaStackVisualEffect)
- (void)setBlendingMode:(NSInteger)mode;
- (void)setMaterial:(NSInteger)material;
- (void)setState:(NSInteger)state;
- (void)setWantsLayer:(BOOL)value;
- (NSRect)convertRectToBacking:(NSRect)rect;
@end

namespace
{
  bool MenuBarTransparency()
  {
#ifdef TEST_BUILD
    const int overrideValue = loka::testing::macStackMenuBarTransparency();
    if (overrideValue >= 0)
      return overrideValue != 0;
#endif
    NSDictionary *global = [[NSUserDefaults standardUserDefaults] persistentDomainForName:NSGlobalDomain];
    id value = [global objectForKey:@"AppleEnableMenuBarTransparency"];
    return !value || ![value respondsToSelector:@selector(boolValue)] || [value boolValue];
  }

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
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

  /** One prefix walk of the parent's native z order per CPU draw. Each visible
      intersecting lower sibling renders once into our full-bounds bitmap.
      The unit and higher siblings never participate in their own underlay. */
  void RenderUnderlay(NSView *unit, NSGraphicsContext *context)
  {
    const NSRect bounds = [unit bounds];
    loka::macos::MacPaintGround(loka::app::SURFACE_GROUND_WINDOW,
                                NSMakeRect(0, 0, bounds.size.width, bounds.size.height));
    NSView *parent = [unit superview];
    NSArray *siblings = [parent subviews];
    for (NSUInteger i = 0; i < [siblings count]; ++i)
    {
      NSView *sibling = [siblings objectAtIndex:i];
      if (sibling == unit)
        break;
      if ([sibling isHidden])
        continue;
      const NSRect clip = NSIntersectionRect([sibling bounds], [sibling convertRect:bounds fromView:unit]);
      if (NSIsEmptyRect(clip))
        continue;
      const NSRect frame = [unit convertRect:[sibling frame] fromView:parent];
      [NSGraphicsContext saveGraphicsState];
      // An external context applies the view's bounds transform, not its
      // frame/ancestor transforms. Supply an unflipped frame here; AppKit
      // handles the sibling's own flipped/unflipped bounds and descendants.
      NSAffineTransform *placement = [NSAffineTransform transform];
      [placement translateXBy:NSMinX(frame) - NSMinX(bounds) yBy:NSMaxY(bounds) - NSMaxY(frame)];
      [placement concat];
      [sibling displayRectIgnoringOpacity:clip inContext:context];
      [NSGraphicsContext restoreGraphicsState];
    }
  }

  /** Owns every temporary for one full-bounds draw; blur uses the fixed radius
      of eight pixels. Source pixels are rendered afresh, never read from the
      partially refreshed window backing store. */
  bool DrawCPUBlur(NSView *unit)
  {
    const NSRect bounds = [unit bounds];
    NSRect pixels = bounds;
    if ([unit respondsToSelector:@selector(convertRectToBacking:)])
      pixels = [unit convertRectToBacking:bounds];
    const double pixelWidth = std::ceil(static_cast<double>(pixels.size.width));
    const double pixelHeight = std::ceil(static_cast<double>(pixels.size.height));
    if (!(bounds.size.width > 0 && bounds.size.height > 0 && pixelWidth > 0 && pixelWidth <= INT_MAX / 4
          && pixelHeight > 0 && pixelHeight <= INT_MAX - 64))
      return false;
    const int width = static_cast<int>(pixelWidth);
    const int height = static_cast<int>(pixelHeight);
    const size_t bytes = loka::app::StackBlurScratchBytes(width, height);
    if (!bytes)
      return false;
    NSBitmapImageRep *rgba = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:NULL
                                                                     pixelsWide:width
                                                                     pixelsHigh:height
                                                                  bitsPerSample:8
                                                                samplesPerPixel:4
                                                                       hasAlpha:YES
                                                                       isPlanar:NO
                                                                 colorSpaceName:NSCalibratedRGBColorSpace
                                                                    bytesPerRow:width * 4
                                                                   bitsPerPixel:32];
    NSGraphicsContext *context = rgba ? [NSGraphicsContext graphicsContextWithBitmapImageRep:rgba] : nil;
    unsigned char *scratch = static_cast<unsigned char *>(std::malloc(bytes));
    bool drawn = false;
    const long rowBytes = [rgba bytesPerRow];
    if (context && scratch && [rgba bitmapData] && rowBytes >= width * 4
        && static_cast<size_t>(rowBytes) <= static_cast<size_t>(-1) / static_cast<size_t>(height))
    {
      std::memset([rgba bitmapData], 0, static_cast<size_t>(rowBytes) * height);
      [NSGraphicsContext saveGraphicsState];
      [NSGraphicsContext setCurrentContext:context];
      NSAffineTransform *scale = [NSAffineTransform transform];
      [scale scaleXBy:width / bounds.size.width yBy:height / bounds.size.height];
      [scale concat];
      NSRectClip(NSMakeRect(0, 0, bounds.size.width, bounds.size.height));
      RenderUnderlay(unit, context);
      [NSGraphicsContext restoreGraphicsState];
      if (loka::app::StackBlur([rgba bitmapData], width, height, static_cast<size_t>(rowBytes), 8, scratch, bytes))
      {
        // Bitmap reps draw unflipped; restore the flipped unit's orientation.
        [NSGraphicsContext saveGraphicsState];
        NSAffineTransform *flip = [NSAffineTransform transform];
        [flip translateXBy:0 yBy:NSMinY(bounds) + NSMaxY(bounds)];
        [flip scaleXBy:1 yBy:-1];
        [flip concat];
        drawn = [rgba drawInRect:bounds] != NO;
        [NSGraphicsContext restoreGraphicsState];
      }
    }
    std::free(scratch);
    [rgba release];
    return drawn;
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

@interface LokaStackCPUUnitView : LokaStackEffectUnitView
@end
@implementation LokaStackCPUUnitView
- (void)drawRect:(NSRect)rect
{
  (void)rect;
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  if (MenuBarTransparency() && DrawCPUBlur(self))
    PaintBlurTint([self bounds]);
  else
    loka::macos::MacPaintGround(loka::app::SURFACE_GROUND_WINDOW, [self bounds]);
  [pool release];
}
@end

namespace
{
  /** Unit admission calls once per missing view; constant work, no row walk.
      Returns a retained view, whose subviews own the native effect. */
  NSView *CreateBlurUnit(NSView *parent, NSRect frame)
  {
    Class effectClass = NSClassFromString(@"NSVisualEffectView");
#ifdef TEST_BUILD
    if (loka::testing::forceMacStackCPUBlur())
      effectClass = Nil;
#endif
    if (!effectClass)
      return [[LokaStackCPUUnitView alloc] initWithFrame:frame];
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
  NSView *view = ground == loka::app::ZSTACK_UNIT_GROUND_EXP_BLUR ? CreateBlurUnit((NSView *)parent, seat.r)
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
  loka::macos::SetMacFrame((NSView *)this->view_, seat);
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
    if (next == loka::app::scene::NODE_FACT_RETIRED)
    {
      [view removeFromSuperview];
      this->retireNativeObject(unit->view_);
    }
  }
}
