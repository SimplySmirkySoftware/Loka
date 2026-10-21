#include "MacRibbonGroupEdgeContext.hpp"
#include "../MacGround.hpp"
#include "../MacScenePlatformController.hpp"
#include "../platform/MacNativeGeometry.hpp"
#include "app/nodes/controls/Ribbon.hpp"
#include "app/scene/projection/RetainedNodeHandler.hpp"
#include <cassert>
#include <cmath>

namespace
{
  // Deliberate Cell twin: the handler owns no lifecycle or placement policy.
  class MacRibbonGroupEdgeNodeHandler : public loka::app::scene::RetainedNodeHandler<MacRibbonGroupEdgeNodeHandler,
                                                                                     loka::app::RibbonGroupEdgeNode,
                                                                                     MacRibbonGroupEdgeContext>
  {
  public:
    static loka::app::RibbonGroupEdgeNode *cast(loka::app::scene::Node *node)
    {
      return node ? node->asRibbonGroupEdgeNode() : 0;
    }
    static MacRibbonGroupEdgeContext *create(loka::app::RibbonGroupEdgeNode *node,
                                             loka::app::scene::IPlatformController *controller,
                                             const loka::app::scene::LayoutState &state)
    {
      MacScenePlatformController *mac = static_cast<MacScenePlatformController *>(controller);
      return new MacRibbonGroupEdgeContext(
          mac, mac->projectionParentView(), state.x, state.y, state.width, state.height, node);
    }
    static void refresh(MacRibbonGroupEdgeContext *context, const loka::app::scene::LayoutState &state)
    {
      context->relayout(state.x, state.y, state.width, state.height);
    }
  };
  MacRibbonGroupEdgeNodeHandler gMacRibbonGroupEdgeNodeHandler;
} // namespace

@interface LokaRibbonGroupEdgeView : NSView
{
  loka::app::RibbonGroupEdge edge_;
}
- (loka::app::RibbonGroupEdge)edge;
- (void)setEdge:(loka::app::RibbonGroupEdge)value;
@end

@implementation LokaRibbonGroupEdgeView
- (loka::app::RibbonGroupEdge)edge
{
  return edge_;
}
- (void)setEdge:(loka::app::RibbonGroupEdge)value
{
  edge_ = value;
}
- (BOOL)isFlipped
{
  return YES;
}

- (void)drawRect:(NSRect)dirtyRect
{
  (void)dirtyRect;
  const NSRect bounds = [self bounds];
  NSColor *ground = nil;
  loka::macos::QueryMacGroundColor(loka::app::SURFACE_GROUND_WINDOW, ground);
  // The legacy spellings keep the Tiger-era APIs usable with the new SDKs.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
  NSColor *edge = [NSColor controlShadowColor];
  NSColor *edgeRGB = [edge colorUsingColorSpaceName:NSCalibratedRGBColorSpace];
  NSColor *groundRGB = [ground colorUsingColorSpaceName:NSCalibratedRGBColorSpace];
#pragma clang diagnostic pop
  if ([self edge] == loka::app::RIBBON_GROUP_EDGE_BOTTOM || !edgeRGB || !groundRGB)
  {
    [edge setFill];
    NSRectFill(bounds);
    return;
  }
  const int rows = static_cast<int>(std::ceil(bounds.size.height));
  for (int row = 0; row < rows; ++row)
  {
    const CGFloat fraction = rows > 1 ? static_cast<CGFloat>(row) / (rows - 1) : 1;
    [[NSColor colorWithCalibratedRed:[groundRGB redComponent] * (1 - fraction) + [edgeRGB redComponent] * fraction
                               green:[groundRGB greenComponent] * (1 - fraction) + [edgeRGB greenComponent] * fraction
                                blue:[groundRGB blueComponent] * (1 - fraction) + [edgeRGB blueComponent] * fraction
                               alpha:[groundRGB alphaComponent] * (1 - fraction) + [edgeRGB alphaComponent] * fraction]
        setFill];
    const CGFloat remaining = bounds.size.height - row;
    NSRectFill(NSMakeRect(bounds.origin.x, bounds.origin.y + row, bounds.size.width, remaining < 1 ? remaining : 1));
  }
}
@end

MacRibbonGroupEdgeContext::MacRibbonGroupEdgeContext(MacScenePlatformController *controller,
                                                     void *parentView,
                                                     int x,
                                                     int y,
                                                     int width,
                                                     int height,
                                                     loka::app::RibbonGroupEdgeNode *node)
    : MacRetirableContext(controller),
      node_(node),
      view_(0)
{
  const loka::macos::MacRect frame =
      this->controller()->projection().projectFrame(loka::core::Frame(x, y, width, height));
  LokaRibbonGroupEdgeView *view = [[LokaRibbonGroupEdgeView alloc] initWithFrame:frame.r];
  [view setEdge:node->props.edge];
  if (parentView)
    [(NSView *)parentView addSubview:view];
  this->view_ = view;
}

MacRibbonGroupEdgeContext::~MacRibbonGroupEdgeContext()
{
  assert(!this->view_ && "terminal fact delivery must queue the native view before context reclaim");
}

void MacRibbonGroupEdgeContext::readLifecycleFactOnAttach()
{
  if (this->node_ && this->node_->lifecycleFact() == loka::app::scene::NODE_FACT_ATTACHED)
    [(NSView *)this->view_ setHidden:NO];
}

void MacRibbonGroupEdgeContext::onFactChanged(loka::app::scene::NodeLifecycleFact previous,
                                              loka::app::scene::NodeLifecycleFact next)
{
  (void)previous;
  NSView *view = (NSView *)this->view_;
  [view setHidden:next != loka::app::scene::NODE_FACT_ATTACHED];
  if (next == loka::app::scene::NODE_FACT_RETIRED)
  {
    [view removeFromSuperview];
    this->retireNativeObject(this->view_);
    this->node_ = 0;
  }
}

short MacRibbonGroupEdgeContext::layout(loka::app::scene::IPlatformController *, loka::app::scene::LayoutState &state)
{
  this->relayout(state.x, state.y, state.width, state.height);
  return static_cast<short>(state.y + state.height);
}

void MacRibbonGroupEdgeContext::relayout(int x, int y, int width, int height)
{
  NSView *view = (NSView *)this->view_;
  if (!view)
    return;
  if (loka::macos::ApplyMacFrame(
          view, this->controller()->projection().projectFrame(loka::core::Frame(x, y, width, height))))
    [view setNeedsDisplay:YES];
}

void RegisterMacRibbonGroupEdgeNodeHandler(loka::app::scene::PlatformNodeHandlerRegistry &registry)
{
  registry.registerHandler(&gMacRibbonGroupEdgeNodeHandler);
}
