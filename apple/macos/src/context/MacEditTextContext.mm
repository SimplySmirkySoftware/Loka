#include "MacEditTextContext.hpp"
#include "../MacInputDoor.hpp"
#include <cassert>
#include "../MacScenePlatformController.hpp"
#include "../platform/MacNativeGeometry.hpp"
#include "MacObjCCompat.hpp"
#include "app/layout/FallbackControlMetrics.hpp"
#include "app/scene/boundary/Boundary.hpp"
#include "app/scene/projection/RetainedNodeHandler.hpp"
#include "Utf8String.hpp"
#include <AppKit/AppKit.h>
#include "app/nodes/controls/EditText.hpp"
#include "core/State.hpp"
#include "platform/StringUTF8.hpp"

namespace
{
  class MacEditTextNodeHandler
      : public loka::app::scene::RetainedNodeHandler<MacEditTextNodeHandler,
                                                     loka::app::EditTextNode,
                                                     MacEditTextContext>
  {
  public:
    static loka::app::EditTextNode *cast(loka::app::scene::Node *node)
    {
      return node ? node->asEditTextNode() : 0;
    }

    static MacEditTextContext *create(loka::app::EditTextNode *edit,
                                      loka::app::scene::IPlatformController *controller,
                                      const loka::app::scene::LayoutState &state)
    {
      MacScenePlatformController *mac = static_cast<MacScenePlatformController *>(controller);
      return new MacEditTextContext(
          mac, mac->projectionParentView(), state.x, state.y, state.width, state.height, edit);
    }

    static void refresh(MacEditTextContext *ctx, const loka::app::scene::LayoutState &state)
    {
      ctx->relayout(state.x, state.y, state.width, state.height);
    }
  };

  MacEditTextNodeHandler gMacEditTextNodeHandler;
} // namespace

/** Native participant mark; behavior remains NSTextField's. */
@interface LokaEditTextField : NSTextField
@end
@implementation LokaEditTextField
@end

@interface LokaTextFieldDelegate : NSObject
{
  MacEditTextContext *owner_;
}
@property(nonatomic, assign) MacEditTextContext *owner;
@end

@implementation LokaTextFieldDelegate
@synthesize owner = owner_;
- (void)controlTextDidChange:(NSNotification *)notification
{
  (void)notification;
  if (self.owner)
  {
    MacInputDoor::editTextChange(*[self owner]);
  }
}
@end

MacEditTextContext *MacEditTextContext::fromNativeFocus(void *responder)
{
  id field = (id)responder;
  if ([field isKindOfClass:[NSTextView class]])
    field = [(NSTextView *)field delegate];
  if (![field isKindOfClass:[LokaEditTextField class]])
    return 0;
  id delegate = [(LokaEditTextField *)field delegate];
  return [delegate isKindOfClass:[LokaTextFieldDelegate class]]
             ? [(LokaTextFieldDelegate *)delegate owner] : 0;
}

MacEditTextContext::MacEditTextContext(MacScenePlatformController *controller,
                                       void *parentView,
                                       int x,
                                       int y,
                                       int width,
                                       int height,
                                       loka::app::EditTextNode *node)
    : MacRetirableContext(controller),
      node_(node),
      field_(0),
      delegate_(0),
      textState_(0),
      applyingFromState_(false),
      updatingFromControl_(false)
{
  NSView *parent = (NSView *)parentView;
  const loka::macos::MacRect frame =
      this->controller()->projection().projectFrame(loka::core::Frame(x, y, width, height));
  NSTextField *field = [[LokaEditTextField alloc] initWithFrame:frame.r];
  [field setEditable:YES];
  [field setSelectable:YES];
  [field setBezeled:YES];
  [field setDrawsBackground:YES];
  if (node_)
  {
    [field setTag:node_->props.controlTag_];
  }

  LokaTextFieldDelegate *delegate = [[LokaTextFieldDelegate alloc] init];
  delegate.owner = this;
  [field setDelegate:(id)delegate];

  if (parent)
  {
    [parent addSubview:field];
  }

  field_ = (void *)field;
  delegate_ = (void *)delegate;
  bindText();
}

MacEditTextContext::~MacEditTextContext()
{
  assert(!field_ && !delegate_ && "terminal fact delivery must queue native objects before context reclaim");
}

void MacEditTextContext::readLifecycleFactOnAttach()
{
  if (this->node_ && this->node_->lifecycleFact() == loka::app::scene::NODE_FACT_ATTACHED)
  {
    this->applyAttachedPresentation();
  }
}

void MacEditTextContext::onFactChanged(loka::app::scene::NodeLifecycleFact previous,
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
      NSTextField *field = (NSTextField *)this->field_;
      [field setDelegate:nil];
      [field removeFromSuperview];
      [(LokaTextFieldDelegate *)this->delegate_ setOwner:0];
      this->retireNativeObjects(this->field_, this->delegate_);
      this->node_ = 0;
    }
  }
}

void MacEditTextContext::applyAttachedPresentation()
{
  NSTextField *field = (NSTextField *)field_;
  if (field)
  {
    [field setHidden:NO];
  }
}

void MacEditTextContext::applyDetachedPresentation()
{
  NSTextField *field = (NSTextField *)field_;
  if (field)
  {
    [field setHidden:YES];
  }
}

short MacEditTextContext::layout(loka::app::scene::IPlatformController *, loka::app::scene::LayoutState &state)
{
  this->relayout(state.x, state.y, state.width, loka::app::layout::FallbackControlMetrics::kEditTextHeight);
  state.height = static_cast<short>(loka::app::layout::FallbackControlMetrics::kEditTextHeight);
  return static_cast<short>(state.y + loka::app::layout::FallbackControlMetrics::kEditTextHeight);
}

void MacEditTextContext::handleTextDidChange()
{
#ifdef LOKA_LIFECYCLE_AUDIT
  assert(this->controller()->borrowPhase().open());
#endif
  if (!applyingFromState_)
  {
    syncStateFromControl();
  }
}

void *MacEditTextContext::nativeField() const
{
  return field_;
}

void MacEditTextContext::relayout(int x, int y, int width, int height)
{
  NSTextField *field = (NSTextField *)field_;
  if (!field)
  {
    return;
  }
  loka::macos::SetMacFrame(field,
      this->controller()->projection().projectFrame(loka::core::Frame(x, y, width, height)));
}

void MacEditTextContext::bindText()
{
  if (!node_)
  {
    return;
  }
  textState_ = node_->props.text_.state();
  textSeat_ = node_->props.text_;
  if (textState_)
  {
    textState_->deferBind(&MacEditTextContext::TextChangedThunk, this);
    applyText();
  }
}

void MacEditTextContext::unbindText()
{
  if (textState_)
  {
    textState_->deferUnbind(&MacEditTextContext::TextChangedThunk, this);
    textState_ = 0;
    textSeat_ = loka::app::scene::WriteSeat<loka::core::String>();
  }
}

void MacEditTextContext::applyText()
{
  NSTextField *field = (NSTextField *)field_;
  if (!field || !textState_)
  {
    return;
  }
  if (updatingFromControl_)
  {
    return;
  }
  std::string desired;
  if (!loka::platform::CollectUtf8(textState_->get(), desired))
  {
    desired.clear();
  }
  std::string current = loka::macos::Utf8FromNSString([field stringValue]);
  if (current == desired)
  {
    return;
  }
  applyingFromState_ = true;
  [field setStringValue:loka::macos::CreateNSStringFromUtf8(desired)];
  applyingFromState_ = false;
}

void MacEditTextContext::syncStateFromControl()
{
  NSTextField *field = (NSTextField *)field_;
  if (!textState_ || !field)
  {
    return;
  }
  updatingFromControl_ = true;
  std::string utf8 = loka::macos::Utf8FromNSString([field stringValue]);
  textSeat_.set(loka::core::String(utf8), true);
  updatingFromControl_ = false;
}

void MacEditTextContext::TextChangedThunk(void *userData)
{
  MacEditTextContext *self = static_cast<MacEditTextContext *>(userData);
  if (self)
  {
    self->applyText();
  }
}

void RegisterMacEditTextNodeHandler(loka::app::scene::PlatformNodeHandlerRegistry &registry)
{
  registry.registerHandler(&gMacEditTextNodeHandler);
}
