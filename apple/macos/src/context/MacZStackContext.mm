// The probe deliberately exercises legacy OpenGL on modern SDKs too.
#ifndef GL_SILENCE_DEPRECATION
#define GL_SILENCE_DEPRECATION
#endif
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <ApplicationServices/ApplicationServices.h>
#import <QuartzCore/CIFilter.h>
#import <QuartzCore/CALayer.h>
#include "MacZStackContext.hpp"
#include "../MacObjCCompat.hpp"
#include "../MacGround.hpp"
#include "../platform/MacNativeGeometry.hpp"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

#ifdef TEST_BUILD
namespace loka
{
  namespace testing
  {
    bool declineMacStackUnitView();
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

// Runtime-only declarations keep the probe buildable with pre-10.10 SDKs.
@interface NSView (LokaProbeVisualEffect)
- (void)setBlendingMode:(NSInteger)mode;
- (void)setMaterial:(NSInteger)material;
- (void)setState:(NSInteger)state;
- (void)setWantsLayer:(BOOL)value;
- (void)setLayerUsesCoreImageFilters:(BOOL)value;
- (void)setBackgroundFilters:(NSArray *)filters;
- (NSArray *)backgroundFilters;
@end

// DIAG: AppKit resets masksToBounds on the layer it owns (10.7/10.8 read NO at
// scroll time even after setting YES), and with NO Core Animation blurs the whole
// background layer (CALayer backgroundFilters documentation). A backing layer that
// always answers YES keeps the blur inside the bar. AppKit asks for it from 10.6.
@interface LokaMaskedBackingLayer : CALayer
@end
@implementation LokaMaskedBackingLayer
- (BOOL)masksToBounds
{
  return YES;
}
- (void)setMasksToBounds:(BOOL)value
{
  (void)value;
  [super setMasksToBounds:YES];
}
@end

@interface LokaStackBlurUnitView : NSView
@end
@implementation LokaStackBlurUnitView
- (CALayer *)makeBackingLayer
{
  CALayer *layer = std::getenv("LOKA_PROBE_PLAIN_LAYER") ? [CALayer layer] : [LokaMaskedBackingLayer layer];
  [layer setMasksToBounds:YES];
  std::fprintf(stderr, "probe: makeBackingLayer called\n");
  return layer;
}
// DIAG for 10.5 (no makeBackingLayer): log how AppKit hands the view its layer and,
// with LOKA_PROBE_SUBST_LAYER=1, substitute the always-masking layer there.
- (void)setLayer:(CALayer *)layer
{
  std::fprintf(stderr, "probe: setLayer %s\n", layer ? [NSStringFromClass([layer class]) UTF8String] : "nil");
  if (layer && std::getenv("LOKA_PROBE_SUBST_LAYER") && ![layer isKindOfClass:[LokaMaskedBackingLayer class]])
  {
    CALayer *masked = [LokaMaskedBackingLayer layer];
    [masked setMasksToBounds:YES];
    [super setLayer:masked];
    return;
  }
  [super setLayer:layer];
}
- (BOOL)isFlipped
{
  return YES;
}
- (BOOL)isOpaque
{
  return NO;
}
- (BOOL)acceptsFirstResponder
{
  return NO;
}
- (void)drawRect:(NSRect)rect
{
  (void)rect;
  // Native filter presence is the tint policy; visual-effect units stay clear.
  // DIAG: LOKA_PROBE_TINT_ALWAYS=1 tints without filters, and the first call reports.
  static bool drawReported = false;
  if (!drawReported)
  {
    std::fprintf(stderr, "probe: diag drawRect filters=%d layer=%d\n",
                 [self respondsToSelector:@selector(backgroundFilters)] ? (int)[[self backgroundFilters] count] : -1,
                 [self respondsToSelector:@selector(layer)] && [self performSelector:@selector(layer)] ? 1 : 0);
    drawReported = true;
  }
  if (std::getenv("LOKA_PROBE_TINT_ALWAYS") ||
      ([self respondsToSelector:@selector(backgroundFilters)] && [[self backgroundFilters] count]))
  {
    NSColor *ground = nil;
    if (loka::macos::QueryMacGroundColor(loka::app::SURFACE_GROUND_WINDOW, ground))
    {
      // DIAG: 10.9 painted the bar black: windowBackgroundColor is a pattern
      // color there, and a pattern drawn into a layer-backed view comes out
      // black. Convert to calibrated RGB (pattern colors refuse and fall back to
      // a plain light gray). LOKA_PROBE_NO_TINT=1 skips the tint entirely.
      if (std::getenv("LOKA_PROBE_NO_TINT"))
        return;
      NSColor *rgb = std::getenv("LOKA_PROBE_TINT_RAW") ? ground : [ground colorUsingColorSpaceName:NSCalibratedRGBColorSpace];
      if (!rgb)
        rgb = [NSColor colorWithCalibratedWhite:0.93 alpha:1.0];
      [[rgb colorWithAlphaComponent:0.6] set];
      NSRectFillUsingOperation([self bounds], LOKA_MAC_COMPOSITING_SOURCE_OVER);
    }
  }
}
@end

namespace
{
  bool ProbeAcceleratedFragmentProgram()
  {
    CGLPixelFormatAttribute attributes[] = {
        kCGLPFAAccelerated, kCGLPFANoRecovery, kCGLPFADisplayMask,
        static_cast<CGLPixelFormatAttribute>(CGDisplayIDToOpenGLDisplayMask(CGMainDisplayID())),
        static_cast<CGLPixelFormatAttribute>(0)};
    CGLPixelFormatObj format = 0;
    CGLContextObj context = 0;
    CGLContextObj previous = CGLGetCurrentContext();
    GLint count = 0;
    bool supported = false;
    if (CGLChoosePixelFormat(attributes, &format, &count) == kCGLNoError && format && count > 0 &&
        CGLCreateContext(format, 0, &context) == kCGLNoError && context &&
        CGLSetCurrentContext(context) == kCGLNoError)
    {
      const char *extensions = reinterpret_cast<const char *>(glGetString(GL_EXTENSIONS));
      const char *token = "GL_ARB_fragment_program";
      const size_t length = std::strlen(token);
      const char *match = extensions;
      while (match && (match = std::strstr(match, token)) != 0)
      {
        if ((match == extensions || match[-1] == ' ') &&
            (match[length] == ' ' || match[length] == '\0'))
        {
          supported = true;
          break;
        }
        match += length;
      }
    }
    // One exit pairs every temporary acquisition, including partial failures.
    if (context)
    {
      const CGLError restored = CGLSetCurrentContext(previous);
      assert(restored == kCGLNoError && "probe must restore the caller's CGL context");
      if (restored != kCGLNoError)
        supported = false;
      CGLDestroyContext(context);
    }
    if (format)
      CGLDestroyPixelFormat(format);
    return supported;
  }

  bool ProbeMenuBarTransparency()
  {
    NSDictionary *global = [[NSUserDefaults standardUserDefaults] persistentDomainForName:NSGlobalDomain];
    id value = [global objectForKey:@"AppleEnableMenuBarTransparency"];
    return !value || ![value respondsToSelector:@selector(boolValue)] || [value boolValue];
  }

  double ProbeBlurRadius()
  {
    const char *value = std::getenv("LOKA_PROBE_BLUR_RADIUS");
    if (!value)
      return 8.0;
    char *end = 0;
    const double radius = std::strtod(value, &end);
    return end != value && *end == '\0' && radius >= 0.0 && radius <= 128.0 ? radius : 8.0;
  }

  // Called only for a missing visual-effect class, once per uncreated unit.
  NSView *CreateProbeFilterUnit(NSView *parent, NSRect frame)
  {
    static const bool gpu = ProbeAcceleratedFragmentProgram();
    const bool menubar = ProbeMenuBarTransparency();
    NSView *view = nil;
    Class filterClass = NSClassFromString(@"CIFilter");
    if (gpu && menubar && [parent respondsToSelector:@selector(setWantsLayer:)] &&
        [filterClass respondsToSelector:@selector(filterWithName:)])
    {
      CIFilter *filter = [filterClass filterWithName:@"CIGaussianBlur"];
      if ([filter respondsToSelector:@selector(setDefaults)] &&
          [filter respondsToSelector:@selector(setValue:forKey:)])
      {
        view = [[LokaStackBlurUnitView alloc] initWithFrame:frame];
        if ([view respondsToSelector:@selector(setWantsLayer:)] &&
            [view respondsToSelector:@selector(setBackgroundFilters:)])
        {
          [filter setDefaults];
          [filter setValue:[NSNumber numberWithDouble:ProbeBlurRadius()] forKey:@"inputRadius"];
          NSArray *filters = [NSArray arrayWithObject:filter];
          // DIAG: LOKA_PROBE_CROP=1 appends a CICrop to the unit's own bounds.
          Class vectorClass = NSClassFromString(@"CIVector");
          if (std::getenv("LOKA_PROBE_CROP") && vectorClass)
          {
            CIFilter *crop = [filterClass filterWithName:@"CICrop"];
            [crop setDefaults];
            [crop setValue:[vectorClass vectorWithX:0 Y:0 Z:frame.size.width W:frame.size.height] forKey:@"inputRectangle"];
            filters = [NSArray arrayWithObjects:filter, crop, nil];
          }
          if (filters)
          {
            // DIAG switches (10.9 real machine painted the bar black, 2026-10-18).
            if (!std::getenv("LOKA_PROBE_NO_PARENT_LAYER"))
              [parent setWantsLayer:YES];
            [view setWantsLayer:YES];
            if (std::getenv("LOKA_PROBE_MASK"))
              [[view layer] setMasksToBounds:YES];
            if (!std::getenv("LOKA_PROBE_NO_CI_FLAG") && [view respondsToSelector:@selector(setLayerUsesCoreImageFilters:)])
              [view setLayerUsesCoreImageFilters:YES];
            if (!std::getenv("LOKA_PROBE_FILTER_SKIP"))
              [view setBackgroundFilters:filters];
            std::fprintf(stderr, "probe: diag parentLayer=%d viewLayer=%d filters=%d\n",
                         [parent performSelector:@selector(layer)] ? 1 : 0, [view performSelector:@selector(layer)] ? 1 : 0,
                         (int)[[view backgroundFilters] count]);
          }
          else
          {
            [view release];
            view = nil;
          }
        }
        else
        {
          [view release];
          view = nil;
        }
      }
    }
    static bool reported = false;
    if (!reported)
    {
      std::fprintf(stderr, "probe: exp blur filters gpu=%d menubar=%d -> %s\n",
                   gpu ? 1 : 0, menubar ? 1 : 0, view ? "applied" : "declined");
      reported = true;
    }
    return view;
  }
} // namespace

MacZStackContext::Unit::Unit(unsigned index)
    : index_(index),
      next(0),
      view_(0)
{
}

bool MacZStackContext::Unit::ensure(void *parent, const loka::macos::MacRect &seat, void *below, void *above)
{
  if (this->view_)
    return true;
#ifdef TEST_BUILD
  if (loka::testing::declineMacStackUnitView())
    return false;
#endif
  NSView *view = nil;
  if (std::getenv("LOKA_PROBE_EXP_BLUR"))
  {
    static bool reported = false;
    Class effectClass = NSClassFromString(@"NSVisualEffectView");
    if (effectClass)
    {
      view = [[LokaStackBlurUnitView alloc] initWithFrame:seat.r];
      if (!view)
        return false;
      NSView *effect = [[effectClass alloc] initWithFrame:[view bounds]];
      if (!effect)
      {
        [view release];
        return false;
      }
      // AppKit raises on a material it does not know (10.13: "Unknown material
      // value: 13"), so the default follows the running AppKit: HUD (13) from
      // 10.14 (AppKit 1671), titlebar (3) on 10.10-10.13, and any value above the
      // 10.10-10.13 range is replaced there.
      const bool mojaveMaterials = NSAppKitVersionNumber >= 1671;
      const char *materialValue = std::getenv("LOKA_PROBE_EXP_BLUR_MATERIAL");
      int material = materialValue ? std::atoi(materialValue) : (mojaveMaterials ? 13 : 3);
      if (!mojaveMaterials && material > 9)
        material = 3;
      if ([effect respondsToSelector:@selector(setBlendingMode:)])
        [effect setBlendingMode:1];
      if ([effect respondsToSelector:@selector(setMaterial:)])
        [effect setMaterial:material];
      if ([effect respondsToSelector:@selector(setState:)])
        [effect setState:0];
      [effect setAutoresizingMask:(NSViewWidthSizable | NSViewHeightSizable)];
      // Within-window blending needs layers: an old-SDK build on 10.10-10.13 is
      // not layer-backed implicitly and AppKit paints a red "requires
      // setWantsLayer:YES" banner instead (10.11 real machine, 2026-10-18). The
      // content below must share the layer tree, so the parent asks for one too.
      if ([(NSView *)parent respondsToSelector:@selector(setWantsLayer:)])
      {
        [(NSView *)parent setWantsLayer:YES];
        [view setWantsLayer:YES];
        [effect setWantsLayer:YES];
      }
      // ensure precedes leaf attachment; the effect is the first subview.
      [view addSubview:effect];
      [effect release];
      if (!reported)
      {
        std::fprintf(stderr, "probe: exp blur unit material=%d\n", material);
        reported = true;
      }
    }
    else if (std::getenv("LOKA_PROBE_LEGACY_FILTERS"))
      view = CreateProbeFilterUnit((NSView *)parent, seat.r);
    else if (!reported)
    {
      // 10.5-10.9 real machines (2026-10-18): 10.9 painted the bar black, 10.6
      // blurred the whole scrolled area. The filter path stays opt-in.
      std::fprintf(stderr, "probe: exp blur declined (no NSVisualEffectView; LOKA_PROBE_LEGACY_FILTERS=1 tries filters)\n");
      reported = true;
    }
  }
  if (!view)
    view = [[LokaStackUnitView alloc] initWithFrame:seat.r];
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

MacZStackContext::MacZStackContext(MacScenePlatformController *controller)
    : MacRetirableContext(controller),
      units_(0)
{
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
