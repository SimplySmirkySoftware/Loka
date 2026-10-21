#include "MacCellContext.hpp"
#include "../MacGround.hpp"
#include "../MacInputDoor.hpp"
#include <cassert>
#include "../MacScenePlatformController.hpp"
#include "../platform/MacNativeGeometry.hpp"
#include "app/layout/FallbackControlMetrics.hpp"
#include "app/scene/projection/RetainedNodeHandler.hpp"
#include "Utf8String.hpp"
#include <AppKit/AppKit.h>
#include "app/nodes/controls/Cell.hpp"
#include "core/State.hpp"
#include "platform/StringUTF8.hpp"

class MacCellContext;

namespace
{
  NSColor *MacCellBorderColor()
  {
    return [NSColor colorWithCalibratedWhite:0.45 alpha:1.0];
  }

  class MacCellNodeHandler
      : public loka::app::scene::RetainedNodeHandler<MacCellNodeHandler,
                                                     loka::app::CellNode,
                                                     MacCellContext>
  {
  public:
    static loka::app::CellNode *cast(loka::app::scene::Node *node)
    {
      return node ? node->asCellNode() : 0;
    }

    static MacCellContext *create(loka::app::CellNode *cell,
                                  loka::app::scene::IPlatformController *controller,
                                  const loka::app::scene::LayoutState &state)
    {
      MacScenePlatformController *mac = static_cast<MacScenePlatformController *>(controller);
      return new MacCellContext(
          mac, mac->projectionParentView(), state.x, state.y, state.width, state.height, cell);
    }

    static void refresh(MacCellContext *ctx, const loka::app::scene::LayoutState &state)
    {
      ctx->relayout(state.x, state.y, state.width, state.height);
    }
  };

  MacCellNodeHandler gMacCellNodeHandler;
} // namespace

@interface LokaCellView : NSView
{
  NSString *text_;
  MacCellContext *context_;
}
- (NSString *)text;
- (void)setText:(NSString *)value;
- (MacCellContext *)context;
- (void)setContext:(MacCellContext *)value;
- (void)continueMouseDown:(NSEvent *)event;
@end

@implementation LokaCellView
- (NSString *)text
{
  return text_;
}

- (void)setText:(NSString *)value
{
  if (text_ != value)
  {
    [text_ release];
    text_ = [value retain];
  }
}

- (MacCellContext *)context
{
  return context_;
}

- (void)setContext:(MacCellContext *)value
{
  context_ = value;
}

- (void)dealloc
{
  [text_ release];
  text_ = nil;
  [super dealloc];
}

- (void)drawRect:(NSRect)dirtyRect
{
  [super drawRect:dirtyRect];
  loka::macos::MacPaintGround(
      loka::app::GroundForKind(loka::app::scene::NODE_KIND_CELL), self.bounds);
  // NSFrameRect draws with the fill color, not the stroke color.
  [MacCellBorderColor() setFill];
  NSFrameRect(self.bounds);

  if ([self text])
  {
    NSDictionary *attrs = [NSDictionary dictionaryWithObjectsAndKeys:[NSFont systemFontOfSize:12.0],
                                                                     NSFontAttributeName,
                                                                     loka::macos::MacTextRoleColor(),
                                                                     NSForegroundColorAttributeName,
                                                                     nil];
    NSSize textSize = [[self text] sizeWithAttributes:attrs];
    NSRect bounds = self.bounds;
    NSRect textRect = NSMakeRect(bounds.origin.x + (bounds.size.width - textSize.width) * 0.5,
                                 bounds.origin.y + (bounds.size.height - textSize.height) * 0.5,
                                 textSize.width,
                                 textSize.height);
    [[self text] drawInRect:textRect withAttributes:attrs];
  }
}

- (void)mouseDown:(NSEvent *)event
{
  if (self.context)
  {
    MacInputDoor::cellClick(*[self context], event);
    return;
  }
  [super mouseDown:event];
}

- (void)continueMouseDown:(NSEvent *)event
{
  [super mouseDown:event];
}
@end

MacCellContext::MacCellContext(MacScenePlatformController *controller,
                               void *parentView,
                               int x,
                               int y,
                               int width,
                               int height,
                               loka::app::CellNode *node)
    : MacRetirableContext(controller),
      node_(node),
      view_(0),
      textState_(0),
      textStateBound_(false)
{
  NSView *parent = (NSView *)parentView;
  const loka::macos::MacRect frame =
      this->controller()->projection().projectFrame(loka::core::Frame(x, y, width, height));
  LokaCellView *view = [[LokaCellView alloc] initWithFrame:frame.r];
  if ([view respondsToSelector:@selector(setWantsLayer:)])
  {
    [view setWantsLayer:YES];
  }
  [view setContext:this];

  if (parent)
  {
    [parent addSubview:view];
  }

  view_ = (void *)view;
  bindText();
}

MacCellContext::~MacCellContext()
{
  assert(!view_ && "terminal fact delivery must queue the native view before context reclaim");
}

void MacCellContext::readLifecycleFactOnAttach()
{
  if (this->node_ && this->node_->lifecycleFact() == loka::app::scene::NODE_FACT_ATTACHED)
  {
    this->applyAttachedPresentation();
  }
}

void MacCellContext::onFactChanged(loka::app::scene::NodeLifecycleFact previous,
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
      this->unbindText();
      LokaCellView *view = (LokaCellView *)this->view_;
      [view setContext:0];
      [view removeFromSuperview];
      this->retireNativeObject(this->view_);
      this->node_ = 0;
    }
  }
}

void MacCellContext::applyAttachedPresentation()
{
  LokaCellView *view = (LokaCellView *)view_;
  if (view)
  {
    [view setHidden:NO];
  }
}

void MacCellContext::applyDetachedPresentation()
{
  LokaCellView *view = (LokaCellView *)view_;
  if (view)
  {
    [view setHidden:YES];
  }
}

short MacCellContext::layout(loka::app::scene::IPlatformController *, loka::app::scene::LayoutState &state)
{
  const short requestedHeight = state.height;
  const int cellHeight = requestedHeight > 0 ? requestedHeight
                                             : static_cast<int>(loka::app::layout::FallbackControlMetrics::kCellHeight);
  this->relayout(state.x, state.y, state.width, cellHeight);
  state.height = static_cast<short>(cellHeight);
  return static_cast<short>(state.y + cellHeight);
}

void MacCellContext::relayout(int x, int y, int width, int height)
{
  LokaCellView *view = (LokaCellView *)view_;
  if (!view)
  {
    return;
  }
  if (loka::macos::ApplyMacFrame(view,
                                 this->controller()->projection().projectFrame(loka::core::Frame(x, y, width, height))))
    [view setNeedsDisplay:YES];
}

void MacCellContext::handleClick(void *event)
{
#ifdef LOKA_LIFECYCLE_AUDIT
  assert(this->controller()->borrowPhase().open());
#endif
  if (this->node_ && this->node_->props.onClick_)
    this->node_->props.onClick_->emit();
  [(LokaCellView *)this->view_ continueMouseDown:(NSEvent *)event];
}

void MacCellContext::bindText()
{
  if (!node_)
  {
    return;
  }
  textState_ = static_cast<loka::core::State<loka::core::String> *>(node_->props.text_);
  textStateBound_ = false;
  if (textState_)
  {
    if (!node_->props.ownsText_)
    {
      textState_->deferBind(&MacCellContext::TextChangedThunk, this);
      textStateBound_ = true;
    }
    applyText();
  }
}

void MacCellContext::unbindText()
{
  if (textState_)
  {
    if (textStateBound_)
    {
      textState_->deferUnbind(&MacCellContext::TextChangedThunk, this);
    }
    textState_ = 0;
    textStateBound_ = false;
  }
}

void MacCellContext::applyText()
{
  LokaCellView *view = (LokaCellView *)view_;
  if (!view || !textState_)
  {
    return;
  }
  std::string utf8;
  if (loka::platform::CollectUtf8(textState_->get(), utf8))
  {
    [view setText:loka::macos::CreateNSStringFromUtf8(utf8)];
    [view setNeedsDisplay:YES];
  }
}

void MacCellContext::TextChangedThunk(void *userData)
{
  MacCellContext *self = static_cast<MacCellContext *>(userData);
  if (self)
  {
    self->applyText();
  }
}

void RegisterMacCellNodeHandler(loka::app::scene::PlatformNodeHandlerRegistry &registry)
{
  registry.registerHandler(&gMacCellNodeHandler);
}
