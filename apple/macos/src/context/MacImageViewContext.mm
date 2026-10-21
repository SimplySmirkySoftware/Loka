#include "MacImageViewContext.hpp"
#include <cassert>
#include <climits>
#include <cstring>
#include <limits>
#include "../MacScenePlatformController.hpp"
#include "../MacObjCCompat.hpp"
#include "../platform/MacNativeGeometry.hpp"
#include "app/layout/FallbackControlMetrics.hpp"
#include "app/scene/projection/RetainedNodeHandler.hpp"
#include <AppKit/AppKit.h>

@interface LokaImageView : NSView
{
  NSImage *image_;
  int fitMode_;
}
- (void)setImage:(NSImage *)image;
- (void)setFitMode:(int)fitMode;
@end

namespace
{
  /** Returns an owned device-RGB image, or nil to preserve the source. */
  NSImage *CreateDeviceRGBImage(NSImage *image)
  {
    if (!image)
    {
      return nil;
    }
    NSArray *reps = [image representations];
    NSBitmapImageRep *source = nil;
    for (NSUInteger i = 0; i < [reps count]; ++i)
    {
      NSImageRep *rep = [reps objectAtIndex:i];
      if (![rep isKindOfClass:[NSBitmapImageRep class]])
      {
        return nil;
      }
      if (!source)
      {
        source = (NSBitmapImageRep *)rep;
      }
    }
    const NSSize size = [image size];
    const NSInteger width = [source pixelsWide];
    const NSInteger height = [source pixelsHigh];
    // Tiger's bitmap initializer takes int dimensions and row bytes.
    if (!source || !(size.width > 0) || !(size.height > 0) || width <= 0 || height <= 0 || width > INT_MAX / 4
        || height > INT_MAX)
    {
      return nil;
    }
    const size_t rowBytes = static_cast<size_t>(width) * 4;
    if (static_cast<size_t>(height) > std::numeric_limits<size_t>::max() / rowBytes)
    {
      return nil;
    }
    NSBitmapImageRep *device = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:NULL
                                                                       pixelsWide:width
                                                                       pixelsHigh:height
                                                                    bitsPerSample:8
                                                                  samplesPerPixel:4
                                                                         hasAlpha:YES
                                                                         isPlanar:NO
                                                                   colorSpaceName:NSDeviceRGBColorSpace
                                                                      bytesPerRow:width * 4
                                                                     bitsPerPixel:32];
    if (!device || ![device bitmapData])
    {
      [device release];
      return nil;
    }
    NSGraphicsContext *context = [NSGraphicsContext graphicsContextWithBitmapImageRep:device];
    if (!context)
    {
      [device release];
      return nil;
    }
    // Source-over must start on transparent pixels for images with alpha.
    std::memset([device bitmapData], 0, rowBytes * static_cast<size_t>(height));
    [NSGraphicsContext saveGraphicsState];
    [NSGraphicsContext setCurrentContext:context];
    [context setImageInterpolation:NSImageInterpolationHigh];
    [image drawInRect:NSMakeRect(0, 0, width, height)
             fromRect:NSZeroRect
            operation:LOKA_MAC_COMPOSITING_SOURCE_OVER
             fraction:1.0];
    [NSGraphicsContext restoreGraphicsState];

    NSImage *converted = [[NSImage alloc] initWithSize:size];
    [device setSize:size];
    [converted addRepresentation:device];
    [device release];
    return converted;
  }

  int ResolveImageLayoutWidth(const loka::app::ImageViewNode *node, int fallbackWidth,
                              const loka::macos::MacProjection &projection)
  {
    if (!node)
    {
      return fallbackWidth;
    }
    int sizePolicy = loka::app::IMAGE_VIEW_SIZE_AUTO;
    if (node->props.hasAttr_ && node->props.attr_.hasSizePolicyValue_)
    {
      sizePolicy = static_cast<int>(node->props.attr_.sizePolicyValue_);
    }
    const bool hasExplicitWidth = node->props.width_ > 0;
    int srcWidth = 0;
    if (node->props.image_)
    {
      const loka::core::resource::Image current = node->props.image_->get();
      srcWidth = current.width();
    }
    if (hasExplicitWidth)
    {
      return node->props.width_;
    }
    if (sizePolicy == loka::app::IMAGE_VIEW_SIZE_INTRINSIC && srcWidth > 0)
    {
      // Keep conversion aligned with MacScenePlatformController::queryNaturalWidth.
      return projection.intrinsicPixelsToLu(srcWidth);
    }
    if (sizePolicy == loka::app::IMAGE_VIEW_SIZE_FILL_PARENT)
    {
      return fallbackWidth;
    }
    return fallbackWidth;
  }

  int ResolveImageLayoutHeight(const loka::app::ImageViewNode *node, int resolvedWidth, int fallbackHeight,
                               const loka::macos::MacProjection &projection)
  {
    if (!node)
    {
      return fallbackHeight > 0 ? fallbackHeight : loka::app::layout::FallbackControlMetrics::kImageFallbackHeight;
    }
    int sizePolicy = loka::app::IMAGE_VIEW_SIZE_AUTO;
    if (node->props.hasAttr_ && node->props.attr_.hasSizePolicyValue_)
    {
      sizePolicy = static_cast<int>(node->props.attr_.sizePolicyValue_);
    }
    const bool hasExplicitHeight = node->props.height_ > 0;
    int srcWidth = 0;
    int srcHeight = 0;
    if (node->props.image_)
    {
      const loka::core::resource::Image current = node->props.image_->get();
      srcWidth = current.width();
      srcHeight = current.height();
    }
    if (hasExplicitHeight)
    {
      return node->props.height_;
    }
    if (sizePolicy == loka::app::IMAGE_VIEW_SIZE_FILL_PARENT && fallbackHeight > 0)
    {
      return fallbackHeight;
    }
    if (sizePolicy == loka::app::IMAGE_VIEW_SIZE_INTRINSIC
        && node->props.width_ <= 0 && srcHeight > 0)
    {
      return projection.intrinsicPixelsToLu(srcHeight);
    }
    if (srcWidth > 0 && srcHeight > 0 && resolvedWidth > 0)
    {
      return (resolvedWidth * srcHeight) / srcWidth;
    }
    if (srcHeight > 0)
    {
      return projection.intrinsicPixelsToLu(srcHeight);
    }
    if (fallbackHeight > 0)
    {
      return fallbackHeight;
    }
    return loka::app::layout::FallbackControlMetrics::kImageFallbackHeight;
  }

  class MacImageViewNodeHandler
      : public loka::app::scene::RetainedNodeHandler<MacImageViewNodeHandler,
                                                     loka::app::ImageViewNode,
                                                     MacImageViewContext>
  {
  public:
    static loka::app::ImageViewNode *cast(loka::app::scene::Node *node)
    {
      return node ? node->asImageViewNode() : 0;
    }

    static MacImageViewContext *create(loka::app::ImageViewNode *image,
                                       loka::app::scene::IPlatformController *controller,
                                       const loka::app::scene::LayoutState &state)
    {
      MacScenePlatformController *mac = static_cast<MacScenePlatformController *>(controller);
      return new MacImageViewContext(
          mac, mac->projectionParentView(), state.x, state.y, state.width, state.height, image);
    }

    static void refresh(MacImageViewContext *ctx, const loka::app::scene::LayoutState &state)
    {
      ctx->relayout(state.x, state.y, state.width, state.height);
    }
  };

  MacImageViewNodeHandler gMacImageViewNodeHandler;
} // namespace

@implementation LokaImageView
- (id)initWithFrame:(NSRect)frame
{
  self = [super initWithFrame:frame];
  if (self)
  {
    image_ = 0;
    fitMode_ = loka::app::IMAGE_FIT_STRETCH;
  }
  return self;
}

- (void)dealloc
{
  if (image_)
  {
    [image_ release];
    image_ = 0;
  }
  [super dealloc];
}

- (void)setImage:(NSImage *)image
{
  if (image_ == image)
  {
    return;
  }
  // The slot remembers only the drawn image. Repeating the original input
  // after conversion converts again; no separate source identity is cached.
  NSImage *replacement = CreateDeviceRGBImage(image);
  if (!replacement)
  {
    replacement = [image retain];
  }
  if (image_)
  {
    [image_ release];
  }
  image_ = replacement;
  [self setNeedsDisplay:YES];
}

- (void)setFitMode:(int)fitMode
{
  if (fitMode_ == fitMode)
  {
    return;
  }
  fitMode_ = fitMode;
  [self setNeedsDisplay:YES];
}

- (void)drawRect:(NSRect)dirtyRect
{
  (void)dirtyRect;
  if (image_)
  {
    NSRect bounds = [self bounds];
    NSRect dstRect = bounds;
    NSRect srcRect = NSZeroRect;
    const NSSize imageSize = [image_ size];
    const CGFloat srcW = imageSize.width;
    const CGFloat srcH = imageSize.height;
    const CGFloat dstW = bounds.size.width;
    const CGFloat dstH = bounds.size.height;

    if (srcW > 0 && srcH > 0 && dstW > 0 && dstH > 0)
    {
      if (fitMode_ == loka::app::IMAGE_FIT_NONE)
      {
        dstRect.size.width = srcW;
        dstRect.size.height = srcH;
      }
      else if (fitMode_ == loka::app::IMAGE_FIT_CONTAIN)
      {
        const CGFloat srcAspect = srcW / srcH;
        const CGFloat dstAspect = dstW / dstH;
        if (srcAspect > dstAspect)
        {
          dstRect.size.height = dstW / srcAspect;
        }
        else
        {
          dstRect.size.width = dstH * srcAspect;
        }
        dstRect.origin.x = bounds.origin.x + (dstW - dstRect.size.width) * 0.5f;
        dstRect.origin.y = bounds.origin.y + (dstH - dstRect.size.height) * 0.5f;
      }
      else if (fitMode_ == loka::app::IMAGE_FIT_COVER)
      {
        srcRect = NSMakeRect(0, 0, srcW, srcH);
        const CGFloat srcAspect = srcW / srcH;
        const CGFloat dstAspect = dstW / dstH;
        if (srcAspect > dstAspect)
        {
          const CGFloat cropW = srcH * dstAspect;
          srcRect.origin.x = (srcW - cropW) * 0.5f;
          srcRect.size.width = cropW;
        }
        else
        {
          const CGFloat cropH = srcW / dstAspect;
          srcRect.origin.y = (srcH - cropH) * 0.5f;
          srcRect.size.height = cropH;
        }
      }
    }

    NSCompositingOperation op = LOKA_MAC_COMPOSITING_SOURCE_OVER;
    // The newer respectFlipped/hints variant crashes with some PDF-backed
    // images on older runtimes/compatibility builds. Keep the classic draw
    // path for broadest compatibility.
    [image_ drawInRect:dstRect fromRect:srcRect operation:op fraction:1.0];
  }
}
@end

MacImageViewContext::MacImageViewContext(MacScenePlatformController *controller,
                                         void *parentView,
                                         int x,
                                         int y,
                                         int width,
                                         int height,
                                         loka::app::ImageViewNode *node)
    : MacRetirableContext(controller),
      node_(node),
      imageView_(0),
      imageState_(0),
      image_()
{
  NSView *parent = (NSView *)parentView;
  const loka::macos::MacRect frame =
      this->controller()->projection().projectFrame(loka::core::Frame(x, y, width, height));
  LokaImageView *view = [[LokaImageView alloc] initWithFrame:frame.r];
  if (parent)
  {
    [parent addSubview:view];
  }
  imageView_ = (void *)view;
  bindImage();
}

MacImageViewContext::~MacImageViewContext()
{
  assert(!imageView_ && "terminal fact delivery must queue the native view before context reclaim");
}

void MacImageViewContext::readLifecycleFactOnAttach()
{
  if (this->node_ && this->node_->lifecycleFact() == loka::app::scene::NODE_FACT_ATTACHED)
  {
    this->applyAttachedPresentation();
  }
}

void MacImageViewContext::onFactChanged(loka::app::scene::NodeLifecycleFact previous,
                                        loka::app::scene::NodeLifecycleFact next)
{
  (void)previous;
  if (next == loka::app::scene::NODE_FACT_ATTACHED)
  {
    this->applyAttachedPresentation();
  }
  else
  {
    // DETACHED_RETAINED hides; terminal RETIRED keeps the same policy
    // (hide before the ritual destroys the native pair).
    this->applyDetachedPresentation();
    if (next == loka::app::scene::NODE_FACT_RETIRED)
    {
      this->unbindImage();
      [(LokaImageView *)this->imageView_ removeFromSuperview];
      this->retireNativeObject(this->imageView_);
      this->node_ = 0;
    }
  }
}

void MacImageViewContext::applyAttachedPresentation()
{
  NSView *view = (NSView *)imageView_;
  if (view)
  {
    [view setHidden:NO];
  }
}

void MacImageViewContext::applyDetachedPresentation()
{
  NSView *view = (NSView *)imageView_;
  if (view)
  {
    [view setHidden:YES];
  }
}

short MacImageViewContext::layout(loka::app::scene::IPlatformController *, loka::app::scene::LayoutState &state)
{
  const int imageWidth = ResolveImageLayoutWidth(this->node_, state.width, this->controller()->projection());
  const int imageHeight = ResolveImageLayoutHeight(this->node_, imageWidth, state.height, this->controller()->projection());
  this->relayout(state.x, state.y, imageWidth, imageHeight);
  state.width = static_cast<short>(imageWidth);
  state.height = static_cast<short>(imageHeight);
  return static_cast<short>(state.y + imageHeight);
}

void MacImageViewContext::relayout(int x, int y, int width, int height)
{
  LokaImageView *view = (LokaImageView *)imageView_;
  if (!view)
  {
    return;
  }
  loka::macos::SetMacFrame(view,
      this->controller()->projection().projectFrame(loka::core::Frame(x, y, width, height)));
  [view setNeedsDisplay:YES];
}

void MacImageViewContext::bindImage()
{
  if (!node_)
  {
    return;
  }
  imageState_ = node_->props.image_;
  if (imageState_)
  {
    imageState_->deferBind(&MacImageViewContext::ImageChangedThunk, this);
    applyImage();
  }
}

void MacImageViewContext::unbindImage()
{
  if (imageState_)
  {
    imageState_->deferUnbind(&MacImageViewContext::ImageChangedThunk, this);
    imageState_ = 0;
  }
}

void MacImageViewContext::applyImage()
{
  LokaImageView *view = (LokaImageView *)imageView_;
  if (!view || !imageState_)
  {
    return;
  }
  image_ = imageState_->get();
  NSImage *image = (NSImage *)image_.nativeHandle();
  int fitMode = loka::app::IMAGE_FIT_STRETCH;
  if (node_ && node_->props.hasAttr_ && node_->props.attr_.hasFitValue_)
  {
    fitMode = static_cast<int>(node_->props.attr_.fitValue_);
  }
  [view setFitMode:fitMode];
  [view setImage:image];
}

void MacImageViewContext::ImageChangedThunk(void *userData)
{
  MacImageViewContext *self = static_cast<MacImageViewContext *>(userData);
  if (self)
  {
    self->applyImage();
  }
}

void RegisterMacImageViewNodeHandler(loka::app::scene::PlatformNodeHandlerRegistry &registry)
{
  registry.registerHandler(&gMacImageViewNodeHandler);
}
