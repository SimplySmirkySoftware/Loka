// The probe deliberately exercises legacy OpenGL on modern SDKs too.
#ifndef GL_SILENCE_DEPRECATION
#define GL_SILENCE_DEPRECATION
#endif
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <ApplicationServices/ApplicationServices.h>
#import <QuartzCore/CIFilter.h>
#include "MacZStackContext.hpp"
#include "../MacObjCCompat.hpp"
#include "../MacGround.hpp"
#include "../platform/MacNativeGeometry.hpp"
#include <cassert>
// CALayer comes after the Loka headers: seen earlier, its -setFrame:(CGRect) wins
// over NSView's for an id receiver under 32-bit GCC 4.2 (NSRect is not CGRect there).
// Core Animation starts at 10.5, so a 10.4 SDK build has no masked layer.
#if defined(MAC_OS_X_VERSION_10_5) && MAC_OS_X_VERSION_MAX_ALLOWED >= MAC_OS_X_VERSION_10_5
#import <QuartzCore/CALayer.h>
#define LOKA_PROBE_MASKED_LAYER 1
#endif
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <climits>
#include <mach/mach_time.h>

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

#ifdef LOKA_PROBE_MASKED_LAYER
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

#endif

namespace
{
  bool ProbeSoftBlurEnabled()
  {
    static const bool enabled = std::getenv("LOKA_PROBE_SOFT_BLUR") && !NSClassFromString(@"NSVisualEffectView");
    return enabled;
  }

  int ProbeSoftBlurRadius()
  {
    const char *value = std::getenv("LOKA_PROBE_BLUR_RADIUS");
    if (!value)
      return 8;
    char *end = 0;
    const double radius = std::strtod(value, &end);
    if (end == value || *end != '\0' || radius != radius)
      return 8;
    return radius < 1.0 ? 1 : radius > 32.0 ? 32 : static_cast<int>(radius);
  }

  static const unsigned char *ProbeBlurPixel(const unsigned char *line, int index, int length, size_t step)
  {
    const int clamped = index < 0 ? 0 : index >= length ? length - 1 : index;
    return line + static_cast<size_t>(clamped) * step;
  }

  // Original implementation of Klingemann's StackBlur triangular running sums.
  // The source pass stays immutable, so it supplies the moving stack's samples
  // directly. Each axis uses weights r+1-|offset| and replicated edge pixels.
  static void ProbeStackBlurPass(const unsigned char *source, unsigned char *destination,
                                 int length, int lines, size_t sourceStep, size_t sourceLineStep,
                                 size_t destinationStep, size_t destinationLineStep, int radius)
  {
    const int divisor = (radius + 1) * (radius + 1);
    for (int line = 0; line < lines; ++line)
    {
      const unsigned char *input = source + static_cast<size_t>(line) * sourceLineStep;
      unsigned char *output = destination + static_cast<size_t>(line) * destinationLineStep;
      int weighted[4] = {0, 0, 0, 0};
      int outgoing[4] = {0, 0, 0, 0};
      int incoming[4] = {0, 0, 0, 0};
      for (int offset = -radius; offset <= radius + 1; ++offset)
      {
        const unsigned char *pixel = ProbeBlurPixel(input, offset, length, sourceStep);
        const int weight = radius + 1 - (offset < 0 ? -offset : offset);
        for (int channel = 0; channel < 4; ++channel)
        {
          weighted[channel] += pixel[channel] * weight;
          if (offset <= 0)
            outgoing[channel] += pixel[channel];
          else
            incoming[channel] += pixel[channel];
        }
      }
      for (int position = 0; position < length; ++position)
      {
        for (int channel = 0; channel < 4; ++channel)
          output[channel] = static_cast<unsigned char>(weighted[channel] / divisor);
        if (position + 1 == length)
          break;
        const unsigned char *departing = ProbeBlurPixel(input, position - radius, length, sourceStep);
        const unsigned char *middle = ProbeBlurPixel(input, position + 1, length, sourceStep);
        const unsigned char *arriving = ProbeBlurPixel(input, position + radius + 2, length, sourceStep);
        for (int channel = 0; channel < 4; ++channel)
        {
          weighted[channel] += incoming[channel] - outgoing[channel];
          outgoing[channel] += middle[channel] - departing[channel];
          incoming[channel] += arriving[channel] - middle[channel];
        }
        output += destinationStep;
      }
    }
  }

  static bool ProbeStackBlur(unsigned char *pixels, int width, int height, size_t rowBytes, int radius)
  {
    if (!pixels || width <= 0 || height <= 0 || width > INT_MAX / 4 || height > INT_MAX - 64 ||
        radius < 1 || radius > 32)
      return false;
    const size_t packedRow = static_cast<size_t>(width) * 4;
    if (rowBytes < packedRow || rowBytes > static_cast<size_t>(-1) / static_cast<size_t>(height))
      return false;
    const size_t bytes = packedRow * static_cast<size_t>(height);
    unsigned char *temporary = static_cast<unsigned char *>(std::malloc(bytes));
    if (!temporary)
      return false;
    ProbeStackBlurPass(pixels, temporary, width, height, 4, rowBytes, 4, packedRow, radius);
    ProbeStackBlurPass(temporary, pixels, height, width, packedRow, 4, rowBytes, 4, radius);
    std::free(temporary);
    return true;
  }

  void ProbePaintBlurTint(NSRect bounds)
  {
    NSColor *ground = nil;
    if (loka::macos::QueryMacGroundColor(loka::app::SURFACE_GROUND_WINDOW, ground))
    {
      // Preserve the RGB fix and the existing raw/no-tint diagnostic switches.
      if (std::getenv("LOKA_PROBE_NO_TINT"))
        return;
      NSColor *rgb = std::getenv("LOKA_PROBE_TINT_RAW") ? ground : [ground colorUsingColorSpaceName:NSCalibratedRGBColorSpace];
      if (!rgb)
        rgb = [NSColor colorWithCalibratedWhite:0.93 alpha:1.0];
      [[rgb colorWithAlphaComponent:0.6] set];
      NSRectFillUsingOperation(bounds, LOKA_MAC_COMPOSITING_SOURCE_OVER);
    }
  }

  // Focus is already locked by AppKit's drawRect. All reps are draw-local.
  bool ProbeDrawSoftBlur(NSRect bounds, int &width, int &height)
  {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#elif defined(__GNUC__) && (__GNUC__ * 100 + __GNUC_MINOR__ >= 406)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
    NSBitmapImageRep *capture = [[NSBitmapImageRep alloc] initWithFocusedViewRect:bounds];
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__) && (__GNUC__ * 100 + __GNUC_MINOR__ >= 406)
#pragma GCC diagnostic pop
#endif
    const long capturedWidth = [capture pixelsWide];
    const long capturedHeight = [capture pixelsHigh];
    if (!capture || capturedWidth <= 0 || capturedHeight <= 0 ||
        capturedWidth > INT_MAX / 4 || capturedHeight > INT_MAX - 64 ||
        static_cast<size_t>(capturedWidth) * 4 > static_cast<size_t>(-1) / static_cast<size_t>(capturedHeight))
    {
      [capture release];
      return false;
    }
    width = static_cast<int>(capturedWidth);
    height = static_cast<int>(capturedHeight);
    // The legacy initializer specifies alpha-last, interleaved RGBA bytes;
    // byte channels avoid endian-dependent packed-word tricks on PPC.
    NSBitmapImageRep *rgba = [[NSBitmapImageRep alloc]
        initWithBitmapDataPlanes:NULL pixelsWide:width pixelsHigh:height
        bitsPerSample:8 samplesPerPixel:4 hasAlpha:YES isPlanar:NO
        colorSpaceName:NSCalibratedRGBColorSpace bytesPerRow:width * 4 bitsPerPixel:32];
    NSGraphicsContext *context = rgba ? [NSGraphicsContext graphicsContextWithBitmapImageRep:rgba] : nil;
    bool drawn = false;
    const long actualRowBytes = [rgba bytesPerRow];
    if (context && [rgba bitmapData] && actualRowBytes >= width * 4 &&
        static_cast<size_t>(actualRowBytes) <= static_cast<size_t>(-1) / static_cast<size_t>(height))
    {
      const size_t rowBytes = static_cast<size_t>(actualRowBytes);
      std::memset([rgba bitmapData], 0, rowBytes * static_cast<size_t>(height));
      [NSGraphicsContext saveGraphicsState];
      [NSGraphicsContext setCurrentContext:context];
      const BOOL converted = [capture drawInRect:NSMakeRect(0, 0, width, height)];
      [NSGraphicsContext restoreGraphicsState];
      if (converted && ProbeStackBlur([rgba bitmapData], width, height, rowBytes, ProbeSoftBlurRadius()))
        drawn = [rgba drawInRect:bounds] != NO;
    }
    [rgba release];
    [capture release];
    return drawn;
  }

  /** Process-wide draw measurements, updated only by the main-thread paint door. */
  class ProbeSoftBlurTiming
  {
  public:
    ProbeSoftBlurTiming() : draws_(0), totalMicroseconds_(0), maximumMicroseconds_(0), microsecondsPerTick_(0)
    {
      mach_timebase_info_data_t timebase = {0, 0};
      if (mach_timebase_info(&timebase) == KERN_SUCCESS && timebase.denom)
        this->microsecondsPerTick_ = static_cast<double>(timebase.numer) / timebase.denom / 1000.0;
    }
    void record(uint64_t ticks, int width, int height)
    {
      const double elapsed = static_cast<double>(ticks) * this->microsecondsPerTick_;
      ++this->draws_;
      this->totalMicroseconds_ += elapsed;
      if (elapsed > this->maximumMicroseconds_)
        this->maximumMicroseconds_ = elapsed;
      if (this->draws_ % 20 == 0)
        std::fprintf(stderr, "probe: soft blur draws=%lu avg=%.0f max=%.0f px=%dx%d\n", this->draws_,
                     this->totalMicroseconds_ / this->draws_, this->maximumMicroseconds_, width, height);
    }
  private:
    unsigned long draws_;
    double totalMicroseconds_;
    double maximumMicroseconds_;
    double microsecondsPerTick_;
  };
} // namespace

@interface LokaStackBlurUnitView : NSView
@end
@implementation LokaStackBlurUnitView
#ifdef LOKA_PROBE_MASKED_LAYER
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
  if (layer && !std::getenv("LOKA_PROBE_NO_SUBST_LAYER") && ![layer isKindOfClass:[LokaMaskedBackingLayer class]])
  {
    CALayer *masked = [LokaMaskedBackingLayer layer];
    [masked setMasksToBounds:YES];
    [super setLayer:masked];
    return;
  }
  [super setLayer:layer];
}
#endif
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
  if (ProbeSoftBlurEnabled())
  {
    static ProbeSoftBlurTiming timing;
    const uint64_t started = mach_absolute_time();
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
    int width = 0;
    int height = 0;
    if (ProbeDrawSoftBlur([self bounds], width, height))
      ProbePaintBlurTint([self bounds]);
    else
    {
      static bool refusedReported = false;
      if (!refusedReported)
      {
        std::fprintf(stderr, "probe: soft blur capture/normalize/blur declined\n");
        refusedReported = true;
      }
      loka::macos::MacPaintGround(loka::app::SURFACE_GROUND_WINDOW, [self bounds]);
    }
    [pool release];
    timing.record(mach_absolute_time() - started, width, height);
    return;
  }
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
    ProbePaintBlurTint([self bounds]);
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
#ifdef LOKA_PROBE_MASKED_LAYER
            if (std::getenv("LOKA_PROBE_MASK"))
              [[view layer] setMasksToBounds:YES];
#endif
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
    else if (ProbeSoftBlurEnabled())
    {
      // Software capture deliberately leaves both parent and unit unlayered.
      view = [[LokaStackBlurUnitView alloc] initWithFrame:seat.r];
      if (view && !reported)
      {
        std::fprintf(stderr, "probe: exp blur soft radius=%d\n", ProbeSoftBlurRadius());
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
