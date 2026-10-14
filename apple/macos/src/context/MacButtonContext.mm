#include "MacButtonContext.hpp"
#include "../MacInputDoor.hpp"
#include <cassert>
#include "../MacScenePlatformController.hpp"
#include "../MacObjCCompat.hpp"
#include "../platform/MacNativeGeometry.hpp"
#include "../platform/MacPushButton.hpp"
#include "app/layout/FallbackControlMetrics.hpp"
#include "app/scene/projection/RetainedNodeHandler.hpp"
#include "Utf8String.hpp"
#include <AppKit/AppKit.h>
#include "app/nodes/controls/Button.hpp"
#include "core/State.hpp"
#include "platform/StringUTF8.hpp"
#include "core/resource/Image.hpp"

namespace
{
  class MacButtonNodeHandler
      : public loka::app::scene::RetainedNodeHandler<MacButtonNodeHandler,
                                                     loka::app::ButtonNode,
                                                     MacButtonContext>
  {
  public:
    static loka::app::ButtonNode *cast(loka::app::scene::Node *node)
    {
      return node ? node->asButtonNode() : 0;
    }

    static MacButtonContext *create(loka::app::ButtonNode *button,
                                    loka::app::scene::IPlatformController *controller,
                                    const loka::app::scene::LayoutState &state)
    {
      MacScenePlatformController *mac = static_cast<MacScenePlatformController *>(controller);
      return new MacButtonContext(
          mac, mac->projectionParentView(), state.x, state.y, state.width, state.height, button);
    }

    static void refresh(MacButtonContext *ctx, const loka::app::scene::LayoutState &state)
    {
      ctx->relayout(state.x, state.y, state.width, state.height);
    }
  };

  MacButtonNodeHandler gMacButtonNodeHandler;
} // namespace

@interface LokaButtonTarget : NSObject
{
  MacButtonContext *owner_;
}
@property(nonatomic, assign) MacButtonContext *owner;
- (IBAction)buttonPressed:(id)sender;
@end

@implementation LokaButtonTarget
@synthesize owner = owner_;
- (IBAction)buttonPressed:(id)sender
{
  (void)sender;
  if (self.owner)
  {
    MacInputDoor::buttonPress(*[self owner]);
  }
}
@end

MacButtonContext::MacButtonContext(MacScenePlatformController *controller,
                                   void *parentView,
                                   int x,
                                   int y,
                                   int width,
                                   int height,
                                   loka::app::ButtonNode *node)
    : MacRetirableContext(controller),
      node_(node),
      button_(0),
      target_(0),
      textState_(0),
      textStateBound_(false),
      enabledState_(0)
{
  NSView *parent = (NSView *)parentView;
  const loka::macos::MacRect frame =
      this->controller()->projection().projectFrame(loka::core::Frame(x, y, width, height));
  NSButton *button = [[NSButton alloc] initWithFrame:frame.r];
  loka::macos::ConfigureMacPushButton(button);

  LokaButtonTarget *target = [[LokaButtonTarget alloc] init];
  target.owner = this;
  [button setTarget:target];
  [button setAction:@selector(buttonPressed:)];

  if (parent)
  {
    [parent addSubview:button];
  }

  button_ = (void *)button;
  target_ = (void *)target;
  bindText();
  bindEnabled();
}

MacButtonContext::~MacButtonContext()
{
  assert(!button_ && !target_ && "terminal fact delivery must queue native objects before context reclaim");
}

void MacButtonContext::readLifecycleFactOnAttach()
{
  if (this->node_ && this->node_->lifecycleFact() == loka::app::scene::NODE_FACT_ATTACHED)
  {
    this->applyAttachedPresentation();
  }
}

void MacButtonContext::onFactChanged(loka::app::scene::NodeLifecycleFact previous,
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
      this->unbindEnabled();
      NSButton *button = (NSButton *)this->button_;
      [button setTarget:nil];
      [button setAction:nil];
      [button removeFromSuperview];
      [(LokaButtonTarget *)this->target_ setOwner:0];
      this->retireNativeObjects(this->button_, this->target_);
      this->node_ = 0;
    }
  }
}

void MacButtonContext::applyAttachedPresentation()
{
  NSButton *button = (NSButton *)button_;
  if (button)
  {
    [button setHidden:NO];
  }
}

void MacButtonContext::applyDetachedPresentation()
{
  NSButton *button = (NSButton *)button_;
  if (button)
  {
    [button setHidden:YES];
  }
}

bool MacButtonContext::captureBitmap(loka::core::resource::Image &out) const
{
  return this->controller()->captureViewBitmap(this->button_, out);
}

short MacButtonContext::layout(loka::app::scene::IPlatformController *, loka::app::scene::LayoutState &state)
{
  // The height a Row aligns this button by, so the frame cannot disagree with it.
  const short height = static_cast<short>(this->controller()->rowLayoutMetrics().buttonHeight);
  this->relayout(state.x, state.y, state.width, height);
  state.height = height;
  return static_cast<short>(state.y + height);
}

void MacButtonContext::handlePress()
{
#ifdef LOKA_LIFECYCLE_AUDIT
  assert(this->controller()->borrowPhase().open());
#endif
  if (node_ && node_->props.onClick_)
  {
    node_->props.onClick_->emit();
  }
}

void MacButtonContext::relayout(int x, int y, int width, int height)
{
  NSButton *button = (NSButton *)button_;
  if (!button)
  {
    return;
  }
  loka::macos::SetMacFrame(button,
      this->controller()->projection().projectFrame(loka::core::Frame(x, y, width, height)));
}

void MacButtonContext::bindText()
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
      textState_->deferBind(&MacButtonContext::TextChangedThunk, this);
      textStateBound_ = true;
    }
    applyText();
  }
}

void MacButtonContext::unbindText()
{
  if (textState_)
  {
    if (textStateBound_)
    {
      textState_->deferUnbind(&MacButtonContext::TextChangedThunk, this);
    }
    textState_ = 0;
    textStateBound_ = false;
  }
}

void MacButtonContext::bindEnabled()
{
  if (!node_)
  {
    return;
  }
  enabledState_ = static_cast<loka::core::State<bool> *>(node_->props.enabled_);
  if (enabledState_)
  {
    enabledState_->deferBind(&MacButtonContext::EnabledChangedThunk, this);
    applyEnabled();
  }
}

void MacButtonContext::unbindEnabled()
{
  if (enabledState_)
  {
    enabledState_->deferUnbind(&MacButtonContext::EnabledChangedThunk, this);
    enabledState_ = 0;
  }
}

void MacButtonContext::applyText()
{
  NSButton *button = (NSButton *)button_;
  if (!button || !textState_)
  {
    return;
  }
  std::string utf8;
  if (loka::platform::CollectUtf8(textState_->get(), utf8))
  {
    [button setTitle:loka::macos::CreateNSStringFromUtf8(utf8)];
  }
}

void RegisterMacButtonNodeHandler(loka::app::scene::PlatformNodeHandlerRegistry &registry)
{
  registry.registerHandler(&gMacButtonNodeHandler);
}

void MacButtonContext::applyEnabled()
{
  NSButton *button = (NSButton *)button_;
  if (!button || !enabledState_)
  {
    return;
  }
  [button setEnabled:enabledState_->get() ? YES : NO];
}

void MacButtonContext::TextChangedThunk(void *userData)
{
  MacButtonContext *self = static_cast<MacButtonContext *>(userData);
  if (self)
  {
    self->applyText();
  }
}

void MacButtonContext::EnabledChangedThunk(void *userData)
{
  MacButtonContext *self = static_cast<MacButtonContext *>(userData);
  if (self)
  {
    self->applyEnabled();
  }
}
