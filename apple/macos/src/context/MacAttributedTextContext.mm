#include "MacAttributedTextContext.hpp"
#include "../MacScenePlatformController.hpp"
#include "../MacObjCCompat.hpp"
#ifndef LOKA_MAC_HAS_CALAYER
#error "include MacObjCCompat.hpp first"
#endif
#if LOKA_MAC_HAS_CALAYER
#include "../MacCardImage.hpp"
#endif
#include <cmath>
#include "../MacGround.hpp"
#include "../platform/MacNativeGeometry.hpp"
#include "app/scene/projection/RetainedNodeHandler.hpp"
#include "platform/StringUTF8.hpp"
#include "core/Utf8Decode.hpp"
#include <AppKit/AppKit.h>
#include <cassert>
#include <climits>
#include <new>
#include <limits>

#ifdef TEST_BUILD
namespace loka
{
  namespace testing
  {
    bool refuseMacTextKitAllocation();
    bool refuseMacTextPresentationStep();
  }
} // namespace loka
#endif

namespace
{
  const CGFloat kMacTextContainerHeight = 1.0e7;
  const NSPoint kMacTextOrigin = {0, 0};

  /** One constant-time half-open test per bounds, used line, or selection rect examined. */
  bool ContainsHitPoint(NSRect rect, NSPoint p)
  {
    return p.x >= NSMinX(rect) && p.x < NSMaxX(rect) && p.y >= NSMinY(rect) && p.y < NSMaxY(rect);
  }

  id AllocateTextKitObject(Class type)
  {
#ifdef TEST_BUILD
    if (loka::testing::refuseMacTextKitAllocation())
      return nil;
#endif
    return [type alloc];
  }

  bool MakeMacTextKitSet(NSTextStorage *&storage, NSLayoutManager *&manager, NSTextContainer *&container)
  {
    storage = [AllocateTextKitObject([NSTextStorage class]) init];
    if (!storage)
      return false;
    manager = [AllocateTextKitObject([NSLayoutManager class]) init];
    if (!manager)
      return false;
    container =
        [AllocateTextKitObject([NSTextContainer class]) initWithContainerSize:NSMakeSize(0, kMacTextContainerHeight)];
    if (!container)
      return false;
    [container setLineFragmentPadding:2];
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
    [manager setUsesScreenFonts:NO];
    [manager setHyphenationFactor:0];
    [manager setBackgroundLayoutEnabled:NO];
#ifdef __clang__
#pragma clang diagnostic pop
#endif
    [manager addTextContainer:container];
    [storage addLayoutManager:manager];
    return true;
  }
} // namespace

bool MacTextLayerCompiledIn()
{
  return LOKA_MAC_HAS_CALAYER != 0;
}
bool MacTextLayerCapable()
{
#if LOKA_MAC_HAS_CALAYER
  return [NSView instancesRespondToSelector:@selector(wantsUpdateLayer)];
#else
  return false;
#endif
}

namespace
{
#if LOKA_MAC_HAS_CALAYER
  /** A single read of the model layer and view/window geometry at preparation. */
  struct MacLayerState
  {
    CALayer *layer;
    id contents;
    CGFloat scale, windowScale;
    NSRect bounds;
    CGRect contentsRect;
    NSString *gravity;
    bool identity;
  };

  /** One guarded read per display or preparation; absence cannot certify a scale. */
  CGFloat WindowScale(NSWindow *window)
  {
    return [window respondsToSelector:@selector(backingScaleFactor)] ? [window backingScaleFactor] : 0;
  }

  /** At most once per preparation: read only this layer, its view and its window. */
  MacLayerState ReadLayerState(NSView *view, CALayer *layer, NSWindow *window)
  {
    const MacLayerState now = {layer,
                               [layer contents],
                               [layer contentsScale],
                               WindowScale(window),
                               [view bounds],
                               [layer contentsRect],
                               [layer contentsGravity],
                               CATransform3DIsIdentity([layer transform]) != 0};
    return now;
  }

  /** O(1) per display or policy query. Ordered bounds reject NaN and infinity before conversion. */
  bool WithinMacTextBudget(NSSize size, CGFloat scale, size_t &width, size_t &height)
  {
    if (!(scale > 0 && scale <= std::numeric_limits<CGFloat>::max()))
      return false;
    const CGFloat w = size.width * scale, h = size.height * scale;
    if (!(w > 0 && w <= 4096 && h > 0 && h <= 4096))
      return false;
    width = static_cast<size_t>(std::ceil(w));
    height = static_cast<size_t>(std::ceil(h));
    return width * 4 * height <= 16u * 1024u * 1024u;
  }

  /** One borrowed flipped wrapper per allocated text bitmap, with no row walk. */
  NSGraphicsContext *FlippedBitmapContext(CGContextRef context)
  {
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
    return [NSGraphicsContext graphicsContextWithGraphicsPort:context flipped:YES];
#ifdef __clang__
#pragma clang diagnostic pop
#endif
  }
#endif

  /** View-owned deliberate twin of scene::PaintFact<T> and Toolbox presented_.
      Invalidation releases the retained image. Last value release can destroy this text's segments. */
  class MacTextPresentation
  {
  public:
    MacTextPresentation()
        : installed_(NOTHING_INSTALLED),
#if LOKA_MAC_HAS_CALAYER
          image_(0),
#endif
#ifdef LOKA_LIFECYCLE_AUDIT
          occupied_(false),
#endif
          width_(0)
    {
    }
    ~MacTextPresentation()
    {
      this->dropImage();
    }
    /** One check before each invalidating mutation or guarded interval. */
    void assertIdle() const
    {
#ifdef LOKA_LIFECYCLE_AUDIT
      assert(!this->occupied_);
#endif
    }
    /** One lexical interval per display or checked preparation. No callbacks or event pump. */
    class Interval
    {
    public:
      explicit Interval(MacTextPresentation &owner)
#ifdef LOKA_LIFECYCLE_AUDIT
          : owner_(owner)
#endif
      {
        owner.assertIdle();
#ifdef LOKA_LIFECYCLE_AUDIT
        owner.occupied_ = true;
#endif
      }
      ~Interval()
      {
#ifdef LOKA_LIFECYCLE_AUDIT
        this->owner_.occupied_ = false;
#endif
      }

    private:
#ifdef LOKA_LIFECYCLE_AUDIT
      MacTextPresentation &owner_;
#endif
      Interval(const Interval &);
      Interval &operator=(const Interval &);
    };
    /** One handle release per invalidation, replacement or deallocation. */
    void dropImage()
    {
#if LOKA_MAC_HAS_CALAYER
      if (this->image_)
        CGImageRelease(this->image_);
      this->image_ = 0;
#endif
    }
#ifdef TEST_BUILD
    /** O(1) test observation of the retained record, independently of native contents. */
    bool hasImage() const
    {
#if LOKA_MAC_HAS_CALAYER
      return this->image_ != 0;
#else
      return false;
#endif
    }
#endif
    /** Once before storage replacement, releasing any previous image and value. */
    void dropInstalled()
    {
      this->dropImage();
      this->installed_ = NOTHING_INSTALLED;
      this->value_ = loka::app::AttributedString();
    }
    /** Once after successful measurement, sharing the projection's immutable handle. */
    void completeInstall(const loka::app::AttributedString &value)
    {
      this->value_ = value;
      this->installed_ = VALUE_INSTALLED;
    }
    /** Constant-time precondition per display. */
    bool installed() const
    {
      return this->installed_ == VALUE_INSTALLED;
    }
    /** One scalar read per width comparison or hit. */
    CGFloat width() const
    {
      return this->width_;
    }
    /** One scalar write per changed container width. */
    void publishWidth(CGFloat width)
    {
      this->width_ = width;
    }
#if LOKA_MAC_HAS_CALAYER
    /** Once per successful display, retain before releasing the previous image. */
    void commitLayer(CGImageRef image, NSSize size, CGFloat scale, NSString *gravity)
    {
      assert(this->installed());
      CGImageRetain(image);
      this->dropImage();
      this->image_ = image;
      this->size_ = size;
      this->scale_ = scale;
      this->gravity_ = gravity;
    }
    /** One tuple comparison per checked preparation. No segment or pixel walk. */
    bool queryFact(const MacLayerState &now, MacPresentedText &out) const
    {
      if (!this->image_ || !now.layer || now.contents != (id)this->image_ || now.scale != this->scale_
          || now.windowScale != this->scale_ || !NSEqualSizes(now.bounds.size, this->size_)
          || !NSEqualPoints(now.bounds.origin, NSZeroPoint)
          || !CGRectEqualToRect(now.contentsRect, CGRectMake(0, 0, 1, 1))
          || ![now.gravity isEqualToString:this->gravity_] || !now.identity)
        return false;
      out.value = this->value_;
      out.width = this->width_;
      return true;
    }
#endif
  private:
    enum
    {
      NOTHING_INSTALLED,
      VALUE_INSTALLED
    } installed_;
    loka::app::AttributedString value_;
#if LOKA_MAC_HAS_CALAYER
    CGImageRef image_;
    NSSize size_;
    CGFloat scale_;
    NSString *gravity_; // Borrowed CA constant.
#endif
#ifdef LOKA_LIFECYCLE_AUDIT
    bool occupied_;
#endif
    CGFloat width_;
    MacTextPresentation(const MacTextPresentation &);
    MacTextPresentation &operator=(const MacTextPresentation &);
  };
} // namespace

#ifdef TEST_BUILD
/** One budget calculation for each synthetic size; no allocation or native sends. */
bool MacTestTextBudget(NSSize size, CGFloat scale)
{
#if LOKA_MAC_HAS_CALAYER
  size_t width, height;
  return WithinMacTextBudget(size, scale, width, height);
#else
  (void)size;
  (void)scale;
  return false;
#endif
}
#endif

/** Passive attributed leaf. Its container is shared by measurement and painting. */
@interface LokaAttributedTextView : NSView
{
@private
  NSTextStorage *storage_;
  NSLayoutManager *manager_;
  NSTextContainer *container_;
  MacTextPresentation *presentation_;
}
/** One size write and record invalidation per changed width. */
- (void)applyContainerWidth:(CGFloat)width;
/** One storage replacement and forced layout of this leaf per install. */
- (BOOL)installValue:(NSAttributedString *)value
            snapshot:(const loka::app::AttributedString *)snapshot
              extent:(CGFloat *)extent;
@end

@interface LokaAttributedTextView (Private)
- (NSTextStorage *)lokaTextStorage;
- (NSLayoutManager *)lokaLayoutManager;
- (NSTextContainer *)lokaTextContainer;
- (CGFloat)appliedWidth;
/** Reports the closed character range at a view-coordinate point, writing out only on SOURCE.
    At most once per context preparation. Queries this view's layout (TextKit may lay out),
    converts three ranges, reads up to one cluster of visibility flags, one style and at most
    one truncation range, then scans the candidate's selection rects once. */
- (MacTextHitAnswer)queryHitCharacters:(NSPoint)point range:(NSRange *)out;
/** Borrow the owner once per checked preparation or retirement assertion. */
- (MacTextPresentation *)presentation;
/** Read one native tuple per checked preparation. No glyph walk. */
- (BOOL)queryPresentedFact:(MacPresentedText *)out;
/** One dirty band or full bitmap per draw; walks only this view's own glyphs. */
- (void)drawTextInRect:(NSRect)band;
@end

@implementation LokaAttributedTextView
- (id)initWithFrame:(NSRect)frame
{
  self = [super initWithFrame:frame];
  if (self)
  {
    if (!MakeMacTextKitSet(storage_, manager_, container_))
    {
      [self release];
      return nil;
    }
#ifdef TEST_BUILD
    if (!loka::testing::refuseMacTextKitAllocation())
#endif
      presentation_ = new (std::nothrow) MacTextPresentation;
    if (!presentation_)
    {
      [self release];
      return nil;
    }
#if LOKA_MAC_HAS_CALAYER
    if (MacTextLayerCapable())
    {
      [self setWantsLayer:YES];
      if ([self respondsToSelector:@selector(setLayerContentsPlacement:)])
        [self setLayerContentsPlacement:LOKA_MAC_LAYER_TOP_LEFT];
    }
#endif
    if ([self respondsToSelector:@selector(setClipsToBounds:)])
      [self setClipsToBounds:YES];
    if ([self respondsToSelector:@selector(setLayerContentsRedrawPolicy:)])
      [self setLayerContentsRedrawPolicy:LOKA_MAC_LAYER_REDRAW_DURING_VIEW_RESIZE];
  }
  return self;
}
- (void)dealloc
{
  delete presentation_;
  presentation_ = 0;
  [storage_ release];
  [manager_ release];
  [container_ release];
  [super dealloc];
}
- (BOOL)isFlipped
{
  return YES;
}
- (BOOL)acceptsFirstMouse:(NSEvent *)event
{
  return YES;
}
- (void)addSubview:(NSView *)view
{
  assert(false && "AttributedText cannot host children");
}
- (void)addSubview:(NSView *)view positioned:(NSWindowOrderingMode)order relativeTo:(NSView *)sibling
{
  assert(false && "AttributedText cannot host children");
}
- (void)setSubviews:(NSArray *)views
{
  if ([views count])
  {
    assert(false && "AttributedText cannot host children");
    return;
  }
  if ([NSView instancesRespondToSelector:@selector(setSubviews:)])
    [super setSubviews:views];
}
- (NSTextStorage *)lokaTextStorage
{
  return storage_;
}
- (NSLayoutManager *)lokaLayoutManager
{
  return manager_;
}
- (NSTextContainer *)lokaTextContainer
{
  return container_;
}
- (MacTextPresentation *)presentation
{
  return presentation_;
}
#ifdef TEST_BUILD
/** Isolate the absent-layer comparison while all other captured fields still match; O(1) per test. */
- (BOOL)lokaFactWithAbsentLayer
{
#if LOKA_MAC_HAS_CALAYER
  if (!MacTextLayerCapable() || ![[self layer] respondsToSelector:@selector(contentsScale)])
    return NO;
  MacLayerState now = ReadLayerState(self, [self layer], [self window]);
  now.layer = nil;
  MacPresentedText fact;
  return presentation_->queryFact(now, fact);
#else
  return NO;
#endif
}
/** One owner-tag read per fixture observation. */
- (BOOL)lokaHasInstalledText
{
  return presentation_->installed();
}
/** One retained-image read per fixture observation. */
- (BOOL)lokaHasPresentationImage
{
  return presentation_->hasImage();
}
#endif
/** One invalidation per AppKit window transition, including removal. */
- (void)viewWillMoveToWindow:(NSWindow *)window
{
  if (presentation_)
  {
    presentation_->assertIdle();
    presentation_->dropImage();
  }
  [super viewWillMoveToWindow:window];
}
- (BOOL)queryPresentedFact:(MacPresentedText *)out
{
#if LOKA_MAC_HAS_CALAYER
  if (!MacTextLayerCapable())
    return NO;
  NSWindow *window = [self window];
  if (!window)
    return NO;
  CALayer *layer = [self layer];
  if (!layer || ![layer respondsToSelector:@selector(contentsScale)])
    return NO;
  const MacLayerState now = ReadLayerState(self, layer, window);
  return presentation_->queryFact(now, *out);
#else
  (void)out;
  return NO;
#endif
}
#if LOKA_MAC_HAS_CALAYER
/** One constant-time area and budget query per AppKit request. */
- (BOOL)wantsUpdateLayer
{
  size_t w, h;
  return MacTextLayerCapable() && WithinMacTextBudget([self bounds].size, WindowScale([self window]), w, h);
}
/** Once per AppKit display, cache or render request. Draw this leaf's visible glyphs and seal its bitmap. */
- (void)updateLayer
{
  if (!presentation_)
    return;
  MacTextPresentation::Interval interval(*presentation_);
  if (!MacTextLayerCapable() || !presentation_->installed())
  {
    presentation_->dropImage();
    return;
  }
  CALayer *layer = [self layer];
  NSWindow *window = [self window];
  if (!layer || !window)
  {
    presentation_->dropImage();
    return;
  }
  const NSRect bounds = [self bounds];
  const CGFloat scale = WindowScale(window);
  size_t width, height;
  if (!NSEqualPoints(bounds.origin, NSZeroPoint) || !WithinMacTextBudget(bounds.size, scale, width, height)
      || ![layer respondsToSelector:@selector(contentsAreFlipped)]
      || ![layer respondsToSelector:@selector(setContentsScale:)])
  {
    presentation_->dropImage();
    return;
  }
  CardImage bitmap(static_cast<int>(width), static_cast<int>(height));
  CGContextRef graphics = bitmap.context();
#ifdef TEST_BUILD
  if (loka::testing::refuseMacTextPresentationStep())
    graphics = 0;
#endif
  NSGraphicsContext *wrapper = graphics ? FlippedBitmapContext(graphics) : nil;
#ifdef TEST_BUILD
  if (loka::testing::refuseMacTextPresentationStep())
    wrapper = nil;
#endif
  if (!wrapper)
  {
    presentation_->dropImage();
    return;
  }
  CGContextTranslateCTM(graphics, 0, height);
  CGContextScaleCTM(graphics, scale, -scale);
  [NSGraphicsContext saveGraphicsState];
  [NSGraphicsContext setCurrentContext:wrapper];
  const NSRect canonical = {NSZeroPoint, bounds.size};
  NSRectClip(canonical);
  [self drawTextInRect:canonical];
  [NSGraphicsContext restoreGraphicsState];
  CGImageRef image = bitmap.seal();
#ifdef TEST_BUILD
  if (loka::testing::refuseMacTextPresentationStep())
    image = 0;
#endif
  if (!image)
  {
    presentation_->dropImage();
    return;
  }
  NSString *gravity = [layer contentsAreFlipped] ? kCAGravityBottomLeft : kCAGravityTopLeft;
  [CATransaction begin];
  [CATransaction setValue:(id)kCFBooleanTrue forKey:kCATransactionDisableActions];
  [layer setContentsGravity:gravity];
  [layer setContents:(id)image];
  [layer setContentsScale:scale];
  [CATransaction commit];
  presentation_->commitLayer(image, bounds.size, scale, gravity);
}
/** One display request per native appearance change. */
- (void)viewDidChangeEffectiveAppearance
{
  if ([NSView instancesRespondToSelector:@selector(viewDidChangeEffectiveAppearance)])
    [super viewDidChangeEffectiveAppearance];
  if (MacTextLayerCapable())
    [self setNeedsDisplay:YES];
}
#endif
- (CGFloat)appliedWidth
{
  return presentation_->width();
}
- (void)applyContainerWidth:(CGFloat)width
{
  if ([self appliedWidth] != width)
  {
    presentation_->assertIdle();
    presentation_->dropImage();
    [[self lokaTextContainer] setContainerSize:NSMakeSize(width, kMacTextContainerHeight)];
    presentation_->publishWidth(width);
  }
}
- (BOOL)installValue:(NSAttributedString *)value
            snapshot:(const loka::app::AttributedString *)snapshot
              extent:(CGFloat *)extent
{
  presentation_->assertIdle();
  presentation_->dropInstalled();
  NSTextStorage *storage = [self lokaTextStorage];
  if (!value)
  {
    [storage deleteCharactersInRange:NSMakeRange(0, [storage length])];
    return YES;
  }
  [storage setAttributedString:value];
#ifdef TEST_BUILD
  if (loka::testing::refuseMacTextPresentationStep())
    return NO;
#endif
  NSLayoutManager *manager = [self lokaLayoutManager];
  NSTextContainer *container = [self lokaTextContainer];
  [manager glyphRangeForTextContainer:container];
  if ([value length])
    *extent = [manager usedRectForTextContainer:container].size.height;
  else
  {
    NSFont *font = [NSFont systemFontOfSize:[NSFont systemFontSize]];
    if (!font)
      return NO;
    *extent = [manager defaultLineHeightForFont:font];
  }
  presentation_->completeInstall(*snapshot);
  return YES;
}
- (MacTextHitAnswer)queryHitCharacters:(NSPoint)point range:(NSRange *)out
{
  if (!ContainsHitPoint([self bounds], point))
    return MAC_HIT_NONE;
  NSLayoutManager *manager = [self lokaLayoutManager];
  NSTextContainer *container = [self lokaTextContainer];
  const NSUInteger n = [manager numberOfGlyphs];
  if (!NSEqualRanges([manager glyphRangeForTextContainer:container], NSMakeRange(0, n)))
    return MAC_HIT_REFUSED;
  if (!n)
    return MAC_HIT_NONE;

  const NSPoint p = NSMakePoint(point.x - kMacTextOrigin.x, point.y - kMacTextOrigin.y);
  const NSUInteger g = [manager glyphIndexForPoint:p inTextContainer:container fractionOfDistanceThroughGlyph:NULL];
  if (g >= n)
    return MAC_HIT_NONE;
  NSRange glyphs, actual;
  const NSRange chars = [manager characterRangeForGlyphRange:NSMakeRange(g, 1) actualGlyphRange:&glyphs];
  const NSRange roundTrip = [manager glyphRangeForCharacterRange:chars actualCharacterRange:&actual];
  if (!NSEqualRanges(roundTrip, glyphs) || !NSEqualRanges(actual, chars)
      || !NSEqualRanges([manager characterRangeForGlyphRange:glyphs actualGlyphRange:NULL], chars))
    return MAC_HIT_REFUSED;
  if (!chars.length)
    return MAC_HIT_NONE;

  NSUInteger visible = glyphs.location;
  while (visible < NSMaxRange(glyphs) && [manager notShownAttributeForGlyphAtIndex:visible])
    ++visible;
  if (visible == NSMaxRange(glyphs))
    return MAC_HIT_NONE;

  NSParagraphStyle *style = [[self lokaTextStorage] attribute:NSParagraphStyleAttributeName
                                                      atIndex:chars.location
                                               effectiveRange:NULL];
  if ([style lineBreakMode] == NSLineBreakByTruncatingTail)
  {
    if (![manager respondsToSelector:@selector(truncatedGlyphRangeInLineFragmentForGlyphAtIndex:)])
      return MAC_HIT_REFUSED;
    const NSRange truncated = [manager truncatedGlyphRangeInLineFragmentForGlyphAtIndex:g];
    if (truncated.location != NSNotFound && NSIntersectionRange(truncated, glyphs).length)
      return MAC_HIT_NONE;
  }

  const NSRect fragment = [manager lineFragmentRectForGlyphAtIndex:g effectiveRange:NULL];
  const NSRect line = [manager lineFragmentUsedRectForGlyphAtIndex:g effectiveRange:NULL];
  if (p.x >= NSMaxX(fragment) - [container lineFragmentPadding])
    return MAC_HIT_NONE;
  NSUInteger k = 0;
  NSRectArray rects = [manager rectArrayForCharacterRange:chars
                             withinSelectedCharacterRange:NSMakeRange(NSNotFound, 0)
                                          inTextContainer:container
                                                rectCount:&k];
  NSUInteger i = 0;
  while (i < k && !ContainsHitPoint(rects[i], p))
    ++i;
  if (i == k || !ContainsHitPoint(line, p))
    return MAC_HIT_NONE;

  *out = chars;
  return MAC_HIT_SOURCE;
}
- (void)drawRect:(NSRect)dirty
{
  const NSRect bounds = [self bounds];
  NSRectClip(bounds);
  const NSRect band = NSOffsetRect(NSIntersectionRect(dirty, bounds), -kMacTextOrigin.x, -kMacTextOrigin.y);
  [self drawTextInRect:band];
}
- (void)drawTextInRect:(NSRect)band
{
  if (NSIsEmptyRect(band))
    return;
  NSLayoutManager *manager = [self lokaLayoutManager];
  const NSRange range = [manager glyphRangeForBoundingRect:band inTextContainer:[self lokaTextContainer]];
  if (range.length)
  {
    [manager drawBackgroundForGlyphRange:range atPoint:kMacTextOrigin];
    [manager drawGlyphsForGlyphRange:range atPoint:kMacTextOrigin];
  }
}
@end

namespace
{
  NSLineBreakMode LineBreakMode(const loka::app::BlockStyle &block)
  {
    if (block.hasWrap_ && block.wrap_ != loka::app::TEXT_WRAP_NONE)
      return block.wrap_ == loka::app::TEXT_WRAP_CHAR ? NSLineBreakByCharWrapping : NSLineBreakByWordWrapping;
    return block.hasTruncation_ && block.truncation_ == loka::app::TEXT_TRUNCATION_ELLIPSIS
               ? NSLineBreakByTruncatingTail
               : NSLineBreakByClipping;
  }

  /** Byte bounds are construction-local and relative to the current style run. */
  struct ByteBound
  {
    std::size_t begin, end;
  };

  /** Validate the whole run before publishing any source interval. NSString may
      discard a leading BOM; the display remains native even when sources differ. */
  bool AgreesWithUtf16(const std::string &bytes, NSString *run)
  {
    const NSUInteger length = [run length];
    NSUInteger unit = 0;
    for (std::size_t byte = 0; byte < bytes.size();)
    {
      unsigned int scalar = 0;
      if (!loka::core::detail::DecodeUtf8Codepoint(bytes.data(), bytes.size(), byte, scalar))
        return false;
      // Twin: core/TextUnits.hpp's scalar-to-UTF-16 pair formula.
      const NSUInteger count = scalar > 0xFFFF ? 2 : 1;
      const unsigned int pair = scalar > 0xFFFF ? scalar - 0x10000 : 0;
      for (NSUInteger k = 0; k < count; ++k)
      {
        const unichar expected = static_cast<unichar>(count == 1 ? scalar
                                                      : k == 0   ? 0xD800 + (pair >> 10)
                                                                 : 0xDC00 + (pair & 0x3FF));
        if (unit >= length || [run characterAtIndex:unit] != expected)
          return false;
        ++unit;
      }
    }
    return unit == length;
  }

  class MacAttributedTextNodeHandler : public loka::app::scene::RetainedNodeHandler<MacAttributedTextNodeHandler,
                                                                                    loka::app::AttributedTextNode,
                                                                                    MacAttributedTextContext>
  {
  public:
    static loka::app::AttributedTextNode *cast(loka::app::scene::Node *node)
    {
      return node ? node->asAttributedTextNode() : 0;
    }
    static MacAttributedTextContext *create(loka::app::AttributedTextNode *node,
                                            loka::app::scene::IPlatformController *controller,
                                            const loka::app::scene::LayoutState &)
    {
      MacScenePlatformController *mac = static_cast<MacScenePlatformController *>(controller);
      MacAttributedTextContext *context =
          new (std::nothrow) MacAttributedTextContext(mac, mac->projectionParentView(), node);
      // A context whose only native view could not be created would be
      // installed forever and never retried; refuse it so a later ensure
      // can build a complete one (AGENTS "Failure-atomicity policy").
      if (context && !context->hasNativeView())
      {
        delete context;
        return 0;
      }
      return context;
    }
  };
  MacAttributedTextNodeHandler handler;

  short Coordinate(int value)
  {
    return static_cast<short>(value > SHRT_MAX ? SHRT_MAX : value < SHRT_MIN ? SHRT_MIN : value);
  }
} // namespace

bool MacAttributedTextContext::MacTextSourceTable::queryProvenance(NSUInteger start,
                                                                   NSUInteger end,
                                                                   loka::app::TextSegmentRange &out) const
{
  if (start >= end)
    return false;
  std::size_t first = 0, last = 0;
  for (NSUInteger unit = start; unit < end;)
  {
    // Upper bound: the last start <= unit wins ties with preceding empties.
    // Search again at each covered boundary; never walk intervening empty slots.
    std::size_t low = 0, high = this->slots_.size();
    while (low < high)
    {
      const std::size_t middle = low + (high - low) / 2;
      if (this->slots_[middle].start <= unit)
        low = middle + 1;
      else
        high = middle;
    }
    if (!low || unit >= this->slots_[low - 1].end)
      return false;
    last = low - 1;
    if (unit == start)
      first = last;
    unit = this->slots_[last].end;
  }
  out = loka::app::TextSegmentRange(first).through(last);
  return true;
}

bool MacAttributedTextContext::Projection::queryProvenance(NSUInteger start,
                                                           NSUInteger end,
                                                           loka::app::TextSegmentRange &out) const
{
  if (!this->value_ || start >= end)
    return false;
  // Coverage also enforces end <= native length: no slot can extend beyond it.
  // This keeps the query native-call-free without retaining a duplicate length.
  return this->sources_.queryProvenance(start, end, out);
}

MacAttributedTextContext::Projection::Projection()
    : value_(0)
{
}
MacAttributedTextContext::Projection::~Projection()
{
  this->clear();
}

void MacAttributedTextContext::Projection::clear()
{
  this->snapshot_ = loka::app::AttributedString();
  this->sources_.clear();
  this->measurement_.invalidate();
  [(NSAttributedString *)this->value_ release];
  this->value_ = 0;
}

bool MacAttributedTextContext::Projection::build(const loka::app::AttributedString &value,
                                                 const loka::app::BlockStyle &block,
                                                 const MacScenePlatformController &controller)
{
  // One failure exit: fill's locals (bounds, byte buffers) are released when it
  // returns, and a refused fill leaves no partial value, measurement or slots.
  this->clear();
  if (this->fill(value, block, controller))
  {
    this->snapshot_ = value;
    return true;
  }
  this->clear();
  return false;
}

bool MacAttributedTextContext::Projection::fill(const loka::app::AttributedString &value,
                                                const loka::app::BlockStyle &block,
                                                const MacScenePlatformController &controller)
{
  if (!value.valid() || !this->sources_.reserve(value.segmentCount()))
    return false;
  loka::app::detail::TextMeasureTable<ByteBound> bounds;
  if (!bounds.allocate(value.segmentCount()))
    return false;
  NSMutableAttributedString *result = [[[NSMutableAttributedString alloc] initWithString:@""] autorelease];
  if (!result)
    return false;
  std::size_t index = 0;
  for (; index < value.segmentCount();)
  {
    const std::size_t first = index;
    const loka::app::TextStyle &style = value.segment(first).style;
    std::string bytes;
    do
    {
      bounds[index].begin = bytes.size();
      std::string part;
      if (!loka::platform::CollectUtf8(value.segment(index).text, part))
        return false;
      bytes.append(part);
      bounds[index].end = bytes.size();
      ++index;
    } while (index < value.segmentCount() && value.segment(index).style == style);
    NSString *run = [[[NSString alloc] initWithBytes:bytes.data() length:bytes.size()
                                            encoding:NSUTF8StringEncoding] autorelease];
    NSFont *font = (NSFont *)controller.textFont(style);
    // Missing fonts refuse even a zero-length run, as in the original builder.
    if (!run || !font)
      return false;
    const NSUInteger base = [result length];
    NSColor *color = nil;
    if (!loka::macos::QueryMacTextColor(style.color_, color))
      color = loka::macos::MacLabelTextColor();
    NSMutableDictionary *attributes = [NSMutableDictionary
        dictionaryWithObjectsAndKeys:font, NSFontAttributeName, color, NSForegroundColorAttributeName, nil];
    if (!attributes)
      return false;
    bool on = false;
    // Twin: TextDraft in MacTextContext.mm uses the same underline attribute.
    if (style.queryUnderline(on) && on)
      [attributes setObject:[NSNumber numberWithInt:NSUnderlineStyleSingle] forKey:NSUnderlineStyleAttributeName];
    NSAttributedString *part = [[[NSAttributedString alloc] initWithString:run attributes:attributes] autorelease];
    if (!part)
      return false;
    [result appendAttributedString:part];
    if (!AgreesWithUtf16(bytes, run))
    {
      for (std::size_t segment = first; segment < index; ++segment)
        this->sources_.slot(segment).start = this->sources_.slot(segment).end = base;
    }
    else
    {
      std::size_t byte = 0;
      NSUInteger unit = 0;
      for (std::size_t segment = first; segment < index; ++segment)
      {
        MacTextSourceSlot &slot = this->sources_.slot(segment);
        slot.start = slot.end = base + unit;
        while (byte < bounds[segment].end)
        {
          const std::size_t begin = byte;
          unsigned int scalar = 0;
          if (!loka::core::detail::DecodeUtf8Codepoint(bytes.data(), bytes.size(), byte, scalar))
            return false;
          const NSUInteger count = scalar > 0xFFFF ? 2 : 1;
          // A scalar crossing any segment edge has no owner (both surrogates).
          if (begin >= bounds[segment].begin && byte <= bounds[segment].end)
            slot.end = base + unit + count;
          unit += count;
        }
      }
    }
    // The next base is result.length, including the native decoder's BOM rule.
  }
  NSMutableParagraphStyle *paragraph = [[[NSMutableParagraphStyle alloc] init] autorelease];
  if (!paragraph)
    return false;
  [paragraph setLineBreakMode:LineBreakMode(block)];
  // Twin: MacTextContext::applyStyle configures the plain label.
  switch (block.hasAlign_ ? block.align_ : loka::app::TEXT_ALIGN_LEFT)
  {
  case loka::app::TEXT_ALIGN_LEFT:
    [paragraph setAlignment:LOKA_MAC_TEXT_ALIGNMENT_LEFT];
    break;
  case loka::app::TEXT_ALIGN_CENTER:
    [paragraph setAlignment:LOKA_MAC_TEXT_ALIGNMENT_CENTER];
    break;
  case loka::app::TEXT_ALIGN_RIGHT:
    [paragraph setAlignment:LOKA_MAC_TEXT_ALIGNMENT_RIGHT];
    break;
  }
  [result addAttribute:NSParagraphStyleAttributeName value:paragraph range:NSMakeRange(0, [result length])];
#ifdef LOKA_LIFECYCLE_AUDIT
  for (std::size_t segment = 0; segment < value.segmentCount(); ++segment)
  {
    const MacTextSourceSlot &slot = this->sources_.slot(segment);
    assert(slot.start <= slot.end && slot.end <= [result length]);
    if (segment)
      assert(this->sources_.slot(segment - 1).end <= slot.start);
  }
#endif
  this->value_ = (void *)[result copy];
  return this->value_ != 0;
}

MacAttributedTextContext::MacAttributedTextContext(MacScenePlatformController *controller,
                                                   void *parentView,
                                                   loka::app::AttributedTextNode *node)
    : MacRetirableContext(controller),
      node_(node),
      view_(0)
{
  assert(controller && controller->textShaping() == loka::app::WHOLE_LINE);
  LokaAttributedTextView *view = [[LokaAttributedTextView alloc] initWithFrame:NSZeroRect];
  this->view_ = view;
  if (parentView && view)
    [(NSView *)parentView addSubview:view];
}

MacAttributedTextContext::~MacAttributedTextContext()
{
  assert(!this->view_ && "retirement must queue the native view before reclaim");
  assert(!this->projection_.value() && "retirement must drop derived text before reclaim");
}

MacTextHitAnswer MacAttributedTextContext::prepareSourceHit(NSPoint viewPoint, MacTextHit &out) const
{
  LokaAttributedTextView *view = (LokaAttributedTextView *)this->view_;
  if (!view || !this->projection_.value())
    return MAC_HIT_REFUSED;
#ifdef LOKA_LIFECYCLE_AUDIT
  assert([[view lokaTextStorage] length] == [(NSAttributedString *)this->projection_.value() length]);
#endif
  NSRange chars;
  const MacTextHitAnswer answer = [view queryHitCharacters:viewPoint range:&chars];
  if (answer != MAC_HIT_SOURCE)
    return answer;
  loka::app::TextSegmentRange source(0);
  if (!this->projection_.queryProvenance(chars.location, NSMaxRange(chars), source))
    return MAC_HIT_REFUSED;
  const MacTextHit hit = {source, [view appliedWidth]};
  out = hit;
  return MAC_HIT_SOURCE;
}

MacTextHitAnswer MacAttributedTextContext::prepareTextHit(NSPoint point, MacTextHit &hit, MacPresentedText &fact) const
{
  LokaAttributedTextView *view = (LokaAttributedTextView *)this->view_;
  if (!view)
    return MAC_HIT_REFUSED;
  MacTextPresentation::Interval interval(*[view presentation]);
  MacPresentedText current;
  if (![view queryPresentedFact:&current])
    return MAC_HIT_REFUSED;
  MacTextHit source = {loka::app::TextSegmentRange(0), 0};
  const MacTextHitAnswer answer = this->prepareSourceHit(point, source);
  if (answer == MAC_HIT_SOURCE)
  {
    hit = source;
    fact = current;
  }
  return answer;
}

namespace
{
  /** Deliberate twin of Win32 queryEligibleRun. Per SOURCE, compare up to two pairs of this node's values
      and inspect one maximal link run. Comparing distinct handles may collect UTF-8. */
  bool EligibleRun(const MacTextHit &hit,
                   const MacPresentedText &fact,
                   const loka::app::AttributedString &snapshot,
                   const loka::app::AttributedString &current,
                   loka::app::AttributedString::LinkRun &run)
  {
    return fact.value.hasSameSegmentValues(snapshot) && snapshot.hasSameSegmentValues(current)
           && hit.containerWidth == fact.width && current.queryLinkRun(hit.source.first(), run)
           && run.last >= hit.source.last();
  }
} // namespace

#ifdef TEST_BUILD
/** One invocation of the production predicate on a fixture's independently supplied values. */
bool MacTestEligibleRun(const MacTextHit &hit,
                        const MacPresentedText &fact,
                        const loka::app::AttributedString &snapshot,
                        const loka::app::AttributedString &current)
{
  loka::app::AttributedString::LinkRun run;
  return EligibleRun(hit, fact, snapshot, current, run);
}
#endif

bool MacAttributedTextContext::queryPressCandidate(NSPoint point, loka::app::AttributedString::LinkRun &out) const
{
  if (!loka::app::scene::IsAttachedCurrentContext(this) || !this->node_ || !this->node_->props.text_
      || !this->projection_.value())
    return false;
  MacTextHit hit = {loka::app::TextSegmentRange(0), 0};
  MacPresentedText fact;
  loka::app::AttributedString::LinkRun run;
  if (this->prepareTextHit(point, hit, fact) != MAC_HIT_SOURCE
      || !EligibleRun(hit, fact, this->projection_.snapshot(), this->node_->props.text_->get(), run))
    return false;
  out = run;
  return true;
}

void MacAttributedTextContext::clearProjection()
{
  [(LokaAttributedTextView *)this->view_ installValue:nil snapshot:0 extent:0];
  this->projection_.clear();
}

void MacAttributedTextContext::onPropsApplied()
{
  // The #965 props mark also forces a rebuild, including baked paragraph alignment.
  this->clearProjection();
  this->controller()->requestRelayout();
}

void MacAttributedTextContext::readLifecycleFactOnAttach()
{
  [(NSView *)this->view_ setHidden:this->node_->lifecycleFact() != loka::app::scene::NODE_FACT_ATTACHED];
}

void MacAttributedTextContext::onFactChanged(loka::app::scene::NodeLifecycleFact,
                                             loka::app::scene::NodeLifecycleFact next)
{
#ifdef LOKA_LIFECYCLE_AUDIT
  if (next == loka::app::scene::NODE_FACT_RETIRED && this->view_)
    [(LokaAttributedTextView *)this->view_ presentation]->assertIdle();
#endif
  [(NSView *)this->view_ setHidden:next != loka::app::scene::NODE_FACT_ATTACHED];
  if (next == loka::app::scene::NODE_FACT_RETIRED)
  {
    this->clearProjection();
    [(NSView *)this->view_ removeFromSuperview];
    this->retireNativeObject(this->view_);
    this->node_ = 0;
  }
}

bool MacAttributedTextContext::rebuildProjection(const loka::macos::MacLength &constraint)
{
  this->clearProjection();
  LokaAttributedTextView *view = (LokaAttributedTextView *)this->view_;
  if (!view || !this->node_ || !this->node_->props.text_
      || !this->projection_.build(this->node_->props.text_->get(), this->node_->props.blockStyle_, *this->controller()))
    return false;
  [view applyContainerWidth:constraint.pt];
  CGFloat extent;
  if (![view installValue:(NSAttributedString *)this->projection_.value()
                 snapshot:&this->projection_.snapshot()
                   extent:&extent])
    return false;
  this->projection_.commitMeasurement(constraint, this->controller()->projection().measurementToLu(extent));
  return true;
}

short MacAttributedTextContext::layout(loka::app::scene::IPlatformController *, loka::app::scene::LayoutState &state)
{
  const loka::macos::MacProjection &projection = this->controller()->projection();
  const loka::macos::MacLength constraint = projection.projectLength(state.x, state.x + state.width);
  LokaAttributedTextView *view = (LokaAttributedTextView *)this->view_;
  // Per visited leaf: O(1) hit, no run walk or native measurement; always place.
  if (state.inputs != loka::app::scene::NODE_DIRTY_NONE || !this->projection_.reusable(constraint))
  {
    if (!this->rebuildProjection(constraint))
    {
      this->controller()->refuseTextMeasurement(this->node_, state);
      this->clearProjection();
    }
  }
#ifdef LOKA_LIFECYCLE_AUDIT
  else
    assert([view appliedWidth] == constraint.pt);
#endif
  state.height = Coordinate(this->projection_.height());
  loka::macos::SetMacFrame(
      view,
      this->controller()->projection().projectFrame(loka::core::Frame(state.x, state.y, state.width, state.height)));
  [view setNeedsDisplay:YES];
  return Coordinate(state.y + state.height);
}

void RegisterMacAttributedTextNodeHandler(loka::app::scene::PlatformNodeHandlerRegistry &registry)
{
  registry.registerHandler(&handler);
}
