#include "MacCardBandContext.hpp"
#include "../MacCardPainter.hpp"
#include "../MacScenePlatformController.hpp"
#include "../platform/MacNativeGeometry.hpp"
#include "app/nodes/controls/Card.hpp"
#include "app/scene/projection/RetainedNodeHandler.hpp"
#include <cassert>

namespace
{
  // Deliberate ribbon-edge twin: the handler owns no lifecycle or placement policy.
  class MacCardBandNodeHandler : public loka::app::scene::RetainedNodeHandler<MacCardBandNodeHandler,
                                                                              loka::app::CardBandNode,
                                                                              MacCardBandContext>
  {
  public:
    static loka::app::CardBandNode *cast(loka::app::scene::Node *node)
    {
      return node ? node->asCardBandNode() : 0;
    }
    static MacCardBandContext *create(loka::app::CardBandNode *node,
                                      loka::app::scene::IPlatformController *controller,
                                      const loka::app::scene::LayoutState &state)
    {
      MacScenePlatformController *mac = static_cast<MacScenePlatformController *>(controller);
      return new MacCardBandContext(
          mac, mac->projectionParentView(), state.x, state.y, state.width, state.height, node);
    }
  };
  MacCardBandNodeHandler gMacCardBandNodeHandler;
} // namespace

@interface LokaCardBandView : NSView
{
  MacCardBandContext *context_;
}
- (MacCardBandContext *)context;
- (void)setContext:(MacCardBandContext *)value;
@end

@implementation LokaCardBandView
- (MacCardBandContext *)context
{
  return context_;
}
- (void)setContext:(MacCardBandContext *)value
{
  context_ = value;
}
- (BOOL)isFlipped
{
  return YES;
}
- (BOOL)isOpaque
{
  return NO;
}
- (void)drawRect:(NSRect)dirtyRect
{
  (void)dirtyRect;
  if ([self context])
    [self context]->draw();
}
@end

MacCardBandContext::MacCardBandContext(MacScenePlatformController *controller,
                                       void *parentView,
                                       int x,
                                       int y,
                                       int width,
                                       int height,
                                       loka::app::CardBandNode *node)
    : MacRetirableContext(controller),
      node_(node),
      view_(0),
      band_(x, y, width, height),
      props_(node->props)
{
  const loka::macos::MacRect frame =
      this->controller()->projection().projectFrame(loka::core::Frame(x, y, width, height));
  LokaCardBandView *view = [[LokaCardBandView alloc] initWithFrame:frame.r];
  [view setContext:this];
  if (parentView && view)
    [(NSView *)parentView addSubview:view];
  this->view_ = view;
}

MacCardBandContext::~MacCardBandContext()
{
  assert(!this->view_ && "terminal fact delivery must queue the native view before context reclaim");
}

void MacCardBandContext::readLifecycleFactOnAttach()
{
  if (this->node_ && this->node_->lifecycleFact() == loka::app::scene::NODE_FACT_ATTACHED)
    [(NSView *)this->view_ setHidden:NO];
}

void MacCardBandContext::onFactChanged(loka::app::scene::NodeLifecycleFact previous,
                                       loka::app::scene::NodeLifecycleFact next)
{
  (void)previous;
  NSView *view = (NSView *)this->view_;
  [view setHidden:next != loka::app::scene::NODE_FACT_ATTACHED];
  if (next == loka::app::scene::NODE_FACT_RETIRED)
  {
    [(LokaCardBandView *)view setContext:0];
    [view removeFromSuperview];
    this->retireNativeObject(this->view_);
    this->node_ = 0;
  }
}

short MacCardBandContext::layout(loka::app::scene::IPlatformController *, loka::app::scene::LayoutState &state)
{
  this->relayout(state.x, state.y, state.width, state.height);
  return static_cast<short>(state.y + state.height);
}

void MacCardBandContext::relayout(int x, int y, int width, int height)
{
  NSView *view = (NSView *)this->view_;
  if (!view)
    return;
  this->band_ = loka::core::Frame(x, y, width, height);
  if (loka::macos::ApplyMacFrame(view,
                                 this->controller()->projection().projectFrame(loka::core::Frame(x, y, width, height))))
    [view setNeedsDisplay:YES];
}

void RegisterMacCardBandNodeHandler(loka::app::scene::PlatformNodeHandlerRegistry &registry)
{
  registry.registerHandler(&gMacCardBandNodeHandler);
}

bool MacCardBandContext::acceptsInstallation() const
{
  return this->view_ != 0;
}

void MacCardBandContext::onPropsApplied()
{
  if (!this->node_ || !this->view_)
    return;
  LokaCardBandView *view = (LokaCardBandView *)this->view_;
  this->props_ = this->node_->props;
  [view setNeedsDisplay:YES];
}

void MacCardBandContext::draw() const
{
  if (!this->view_ || !this->band_.hasSize())
    return;
  NSView *view = (NSView *)this->view_;
  NSView *root = [[view window] contentView];
  // Unwindowed offscreen trees use their topmost view as content origin.
  if (!root)
  {
    root = view;
    while ([root superview])
      root = [root superview];
  }
  const NSPoint origin = [view convertPoint:NSZeroPoint toView:root];
  // Content views need not be flipped; express phase in top-down points.
  const NSPoint phase =
      NSMakePoint(origin.x - NSMinX([root bounds]),
                  [root isFlipped] ? origin.y - NSMinY([root bounds]) : NSMaxY([root bounds]) - origin.y);
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
  CGContextRef graphics = static_cast<CGContextRef>([[NSGraphicsContext currentContext] graphicsPort]);
#pragma clang diagnostic pop
  loka::macos::PaintCardBandInk(graphics, this->band_, this->props_, this->controller()->projection(), phase);
}
