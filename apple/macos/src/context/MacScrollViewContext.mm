#include "MacScrollViewContext.hpp"
#include "../MacInputDoor.hpp"
#include "../MacObjCCompat.hpp"
#include "../platform/MacNativeGeometry.hpp"
#include "../MacScenePlatformController.hpp"

#include <cassert>
#include <AppKit/AppKit.h>

@interface LokaScrollDocumentView : NSView
@end

@implementation LokaScrollDocumentView
- (BOOL)isFlipped
{
  return YES;
}
@end

@interface LokaScrollView : NSScrollView
{
  MacScrollViewContext *context_;
}
- (void)setContext:(MacScrollViewContext *)value;
- (void)clipViewBoundsChanged:(NSNotification *)notification;
@end

@implementation LokaScrollView
- (void)setContext:(MacScrollViewContext *)value
{
  context_ = value;
}

- (void)clipViewBoundsChanged:(NSNotification *)notification
{
  (void)notification;
  if (context_)
  {
    MacInputDoor::scrollBoundsChange(*context_);
  }
}
@end

MacScrollViewContext::MacScrollViewContext(MacScenePlatformController *controller,
                                           void *parentView,
                                           int x,
                                           int y,
                                           int width,
                                           int height,
                                           loka::app::ScrollViewNode *node)
    : MacRetirableContext(controller),
      node_(node),
      scrollView_(0)
{
  NSView *parent = (NSView *)parentView;
  const loka::macos::MacRect frame =
      this->controller()->projection().projectFrame(loka::core::Frame(x, y, width, height));
  LokaScrollView *scrollView = [[LokaScrollView alloc] initWithFrame:frame.r];
  if (!scrollView)
  {
    return;
  }
  [scrollView setDrawsBackground:NO];
  [scrollView setBorderType:NSNoBorder];
  [scrollView setHasHorizontalScroller:NO];
  [scrollView setHasVerticalScroller:YES];
  [scrollView setAutohidesScrollers:NO];

  const NSSize contentSize = [scrollView contentSize];
  LokaScrollDocumentView *documentView =
      [[LokaScrollDocumentView alloc] initWithFrame:NSMakeRect(0, 0, contentSize.width, 0)];
  if (!documentView)
  {
    [scrollView release];
    return;
  }
  [scrollView setDocumentView:documentView];
  [documentView release];

  [scrollView setContext:this];
  NSClipView *clipView = [scrollView contentView];
  [clipView setPostsBoundsChangedNotifications:YES];
  [[NSNotificationCenter defaultCenter] addObserver:scrollView
                                           selector:@selector(clipViewBoundsChanged:)
                                               name:NSViewBoundsDidChangeNotification
                                             object:clipView];
  if (parent)
  {
    [parent addSubview:scrollView];
  }
  this->scrollView_ = scrollView;
}

MacScrollViewContext::~MacScrollViewContext()
{
  assert(!this->scrollView_ && "terminal fact delivery must queue the native ScrollView before context reclaim");
}

void MacScrollViewContext::readLifecycleFactOnAttach()
{
  if (this->node_ && this->node_->lifecycleFact() == loka::app::scene::NODE_FACT_ATTACHED)
  {
    this->applyAttachedPresentation();
  }
}

void MacScrollViewContext::onFactChanged(loka::app::scene::NodeLifecycleFact previous,
                                         loka::app::scene::NodeLifecycleFact next)
{
  (void)previous;
  if (next == loka::app::scene::NODE_FACT_ATTACHED)
  {
    this->applyAttachedPresentation();
  }
  else
  {
    this->applyDetachedPresentation();
    if (next == loka::app::scene::NODE_FACT_RETIRED)
    {
      LokaScrollView *scrollView = (LokaScrollView *)this->scrollView_;
      NSClipView *clipView = [scrollView contentView];
      [[NSNotificationCenter defaultCenter] removeObserver:scrollView
                                                      name:NSViewBoundsDidChangeNotification
                                                    object:clipView];
      [scrollView setContext:0];
      [scrollView removeFromSuperview];
      this->retireNativeObject(this->scrollView_);
      this->node_ = 0;
    }
  }
}

bool MacScrollViewContext::isValid() const
{
  return this->scrollView_ != 0;
}

void *MacScrollViewContext::documentView() const
{
  NSScrollView *scrollView = (NSScrollView *)this->scrollView_;
  return scrollView ? (void *)[scrollView documentView] : 0;
}

int MacScrollViewContext::visibleWidth() const
{
  NSScrollView *scrollView = (NSScrollView *)this->scrollView_;
  if (!scrollView)
  {
    return 0;
  }
  const NSSize size = [scrollView contentSize];
  return size.width > 0 ? this->controller()->projection().clientCapacityToLu(size.width) : 0;
}

void MacScrollViewContext::applyAttachedPresentation()
{
  NSScrollView *scrollView = (NSScrollView *)this->scrollView_;
  if (scrollView)
  {
    [scrollView setHidden:NO];
  }
}

void MacScrollViewContext::applyDetachedPresentation()
{
  NSScrollView *scrollView = (NSScrollView *)this->scrollView_;
  if (scrollView)
  {
    [scrollView setHidden:YES];
  }
}

void MacScrollViewContext::relayout(int x, int y, int width, int height)
{
  NSScrollView *scrollView = (NSScrollView *)this->scrollView_;
  if (!scrollView)
  {
    return;
  }
  loka::macos::SetMacFrame(scrollView,
      this->controller()->projection().projectFrame(loka::core::Frame(x, y, width, height)));
  NSView *documentView = [scrollView documentView];
  if (documentView)
  {
    NSRect frame = [documentView frame];
    const CGFloat clipWidth = [scrollView contentSize].width;
    if (frame.size.width < clipWidth)
      frame.size.width = clipWidth;
    [documentView setFrame:frame];
  }
}

int MacScrollViewContext::commitScrollMetrics(
    int contentWidth, int contentHeight, int viewportHeight, int clampedX, int requestedY, bool horizontalBar)
{
  if (contentHeight < 0)
  {
    contentHeight = 0;
  }
  if (viewportHeight < 0)
  {
    viewportHeight = 0;
  }
  int maximum = contentHeight - viewportHeight;
  if (maximum < 0)
  {
    maximum = 0;
  }
  const int clamped = this->clampOffset(requestedY, maximum);

  NSScrollView *scrollView = (NSScrollView *)this->scrollView_;
  NSView *documentView = scrollView ? [scrollView documentView] : nil;
  if (!scrollView || !documentView)
  {
    return clamped;
  }
  // Native tiling and document resizing can notify intermediate origins.
  // Publish only the completed two-axis commit at the caller.
  NSClipView *clipView = [scrollView contentView];
  const BOOL postsBounds = [clipView postsBoundsChangedNotifications];
  [clipView setPostsBoundsChangedNotifications:NO];
  [scrollView setHasHorizontalScroller:horizontalBar];
  const loka::macos::MacProjection &projection = this->controller()->projection();
  const CGFloat width = projection.projectLength(0, contentWidth).pt;
  const CGFloat clipWidth = [scrollView contentSize].width;
  loka::macos::SetMacDocumentSize(
      scrollView, width > clipWidth ? width : clipWidth, projection.projectLength(0, contentHeight));
  loka::macos::ScrollMacDocument(
      documentView, projection.scrollOffsetToNative(clampedX), projection.scrollOffsetToNative(clamped));
  [clipView setPostsBoundsChangedNotifications:postsBounds];
  return clamped;
}

void MacScrollViewContext::publishClipViewBoundsOrigin()
{
#ifdef LOKA_LIFECYCLE_AUDIT
  assert(this->controller()->borrowPhase().open());
#endif
  NSScrollView *scrollView = (NSScrollView *)this->scrollView_;
  if (!scrollView)
  {
    return;
  }
  const NSRect bounds = [[scrollView contentView] bounds];
  const loka::macos::MacProjection &projection = this->controller()->projection();
  const int x = this->clampOffset(projection.scrollPositionToLu(bounds.origin.x), this->maximumOffset(true));
  const int y = this->clampOffset(projection.scrollPositionToLu(bounds.origin.y), this->maximumOffset(false));
  // Repeated native bounds notifications, including any delayed layout echo,
  // are same-value no-ops inside publishOrigin, the echo-loop breaker.
  if (this->node_)
    this->node_->publishOrigin(x, y);
}

int MacScrollViewContext::clampOffset(int value, int maximum) const
{
  if (value < 0)
  {
    return 0;
  }
  if (value > maximum)
  {
    return maximum;
  }
  return value;
}

int MacScrollViewContext::maximumOffset(bool horizontal) const
{
  NSScrollView *scrollView = (NSScrollView *)this->scrollView_;
  NSView *documentView = scrollView ? [scrollView documentView] : nil;
  if (!scrollView || !documentView)
  {
    return 0;
  }
  const NSRect documentFrame = [documentView frame];
  const NSRect clipBounds = [[scrollView contentView] bounds];
  const loka::macos::MacProjection &projection = this->controller()->projection();
  const int content = projection.capacityToLu(horizontal ? documentFrame.size.width : documentFrame.size.height);
  const int viewport = projection.clientCapacityToLu(horizontal ? clipBounds.size.width : clipBounds.size.height);
  return content > viewport ? content - viewport : 0;
}
