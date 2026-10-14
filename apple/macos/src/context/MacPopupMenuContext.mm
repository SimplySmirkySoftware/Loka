#include "MacPopupMenuContext.hpp"
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
#include "platform/StringUTF8.hpp"

namespace
{
  class MacPopupMenuNodeHandler
      : public loka::app::scene::RetainedNodeHandler<MacPopupMenuNodeHandler,
                                                     loka::app::PopupMenuNode,
                                                     MacPopupMenuContext>
  {
  public:
    static loka::app::PopupMenuNode *cast(loka::app::scene::Node *node)
    {
      return node ? node->asPopupMenuNode() : 0;
    }

    static MacPopupMenuContext *create(loka::app::PopupMenuNode *popup,
                                       loka::app::scene::IPlatformController *controller,
                                       const loka::app::scene::LayoutState &state)
    {
      MacScenePlatformController *mac = static_cast<MacScenePlatformController *>(controller);
      return new MacPopupMenuContext(
          mac, mac->projectionParentView(), state.x, state.y, state.width, state.height, popup);
    }

    static void refresh(MacPopupMenuContext *ctx, const loka::app::scene::LayoutState &state)
    {
      ctx->relayout(state.x, state.y, state.width, state.height);
    }
  };

  MacPopupMenuNodeHandler gMacPopupMenuNodeHandler;
} // namespace

@interface LokaPopupMenuTarget : NSObject
{
  MacPopupMenuContext *owner_;
}
@property(nonatomic, assign) MacPopupMenuContext *owner;
- (IBAction)popupChanged:(id)sender;
@end

@implementation LokaPopupMenuTarget
@synthesize owner = owner_;
- (IBAction)popupChanged:(id)sender
{
  (void)sender;
  if (self.owner)
  {
    MacInputDoor::popupChange(*[self owner]);
  }
}
@end

MacPopupMenuContext::MacPopupMenuContext(MacScenePlatformController *controller,
                                         void *parentView,
                                         int x,
                                         int y,
                                         int width,
                                         int height,
                                         loka::app::PopupMenuNode *node)
    : MacRetirableContext(controller),
      node_(node),
      popup_(0),
      target_(0),
      selectionState_(0),
      enabledState_(0),
      applyingFromState_(false),
      updatingFromControl_(false)
{
  NSView *parent = (NSView *)parentView;
  const loka::macos::MacRect frame =
      this->controller()->projection().projectFrame(loka::core::Frame(x, y, width, height));
  NSPopUpButton *popup = [[NSPopUpButton alloc] initWithFrame:frame.r pullsDown:NO];

  LokaPopupMenuTarget *target = [[LokaPopupMenuTarget alloc] init];
  target.owner = this;
  [popup setTarget:target];
  [popup setAction:@selector(popupChanged:)];

  if (parent)
  {
    [parent addSubview:popup];
  }

  popup_ = (void *)popup;
  target_ = (void *)target;

  applyItems();
  bindSelection();
  bindEnabled();
}

MacPopupMenuContext::~MacPopupMenuContext()
{
  assert(!popup_ && !target_ && "terminal fact delivery must queue native objects before context reclaim");
}

void MacPopupMenuContext::readLifecycleFactOnAttach()
{
  if (this->node_ && this->node_->lifecycleFact() == loka::app::scene::NODE_FACT_ATTACHED)
  {
    this->applyAttachedPresentation();
  }
}

void MacPopupMenuContext::onFactChanged(loka::app::scene::NodeLifecycleFact previous,
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
      this->unbindSelection();
      this->unbindEnabled();
      NSPopUpButton *popup = (NSPopUpButton *)this->popup_;
      [popup setTarget:nil];
      [popup setAction:nil];
      [popup removeFromSuperview];
      [(LokaPopupMenuTarget *)this->target_ setOwner:0];
      this->retireNativeObjects(this->popup_, this->target_);
      this->node_ = 0;
    }
  }
}

void MacPopupMenuContext::applyAttachedPresentation()
{
  NSPopUpButton *popup = (NSPopUpButton *)popup_;
  if (popup)
  {
    [popup setHidden:NO];
  }
}

void MacPopupMenuContext::applyDetachedPresentation()
{
  NSPopUpButton *popup = (NSPopUpButton *)popup_;
  if (popup)
  {
    [popup setHidden:YES];
  }
}

short MacPopupMenuContext::layout(loka::app::scene::IPlatformController *, loka::app::scene::LayoutState &state)
{
  this->relayout(state.x, state.y, state.width, loka::app::layout::FallbackControlMetrics::kPopupMenuHeight);
  state.height = static_cast<short>(loka::app::layout::FallbackControlMetrics::kPopupMenuHeight);
  return static_cast<short>(state.y + loka::app::layout::FallbackControlMetrics::kPopupMenuHeight);
}

void MacPopupMenuContext::handleSelectionChange()
{
#ifdef LOKA_LIFECYCLE_AUDIT
  assert(this->controller()->borrowPhase().open());
#endif
  if (!applyingFromState_)
  {
    syncStateFromControl();
  }
}

void MacPopupMenuContext::relayout(int x, int y, int width, int height)
{
  NSPopUpButton *popup = (NSPopUpButton *)popup_;
  if (!popup)
  {
    return;
  }
  loka::macos::SetMacFrame(popup,
      this->controller()->projection().projectFrame(loka::core::Frame(x, y, width, height)));
}

void MacPopupMenuContext::bindSelection()
{
  if (!node_)
  {
    return;
  }
  selectionState_ = node_->props.selectedIndex_.state();
  selectionSeat_ = node_->props.selectedIndex_;
  if (selectionState_)
  {
    selectionState_->deferBind(&MacPopupMenuContext::SelectionChangedThunk, this);
    applySelection();
  }
}

void MacPopupMenuContext::unbindSelection()
{
  if (selectionState_)
  {
    selectionState_->deferUnbind(&MacPopupMenuContext::SelectionChangedThunk, this);
    selectionState_ = 0;
    selectionSeat_ = loka::app::scene::WriteSeat<int>();
  }
}

void MacPopupMenuContext::bindEnabled()
{
  if (!node_)
  {
    return;
  }
  enabledState_ = static_cast<loka::core::State<bool> *>(node_->props.enabled_);
  if (enabledState_)
  {
    enabledState_->deferBind(&MacPopupMenuContext::EnabledChangedThunk, this);
    applyEnabled();
  }
}

void MacPopupMenuContext::unbindEnabled()
{
  if (enabledState_)
  {
    enabledState_->deferUnbind(&MacPopupMenuContext::EnabledChangedThunk, this);
    enabledState_ = 0;
  }
}

void MacPopupMenuContext::applyItems()
{
  NSPopUpButton *popup = (NSPopUpButton *)popup_;
  if (!popup || !node_)
  {
    return;
  }
  [popup removeAllItems];
  const loka::Vector<loka::core::String> *items = node_->props.items_;
  if (!items)
  {
    return;
  }
  for (std::size_t i = 0; i < items->size(); ++i)
  {
    std::string utf8;
    if (loka::platform::CollectUtf8((*items)[i], utf8))
    {
      [popup addItemWithTitle:loka::macos::CreateNSStringFromUtf8(utf8)];
    }
  }
}

void MacPopupMenuContext::applySelection()
{
  NSPopUpButton *popup = (NSPopUpButton *)popup_;
  if (!popup || !selectionState_)
  {
    return;
  }
  NSInteger itemCount = [popup numberOfItems];
  if (itemCount <= 0)
  {
    return;
  }
  int index = selectionState_->get();
  if (index < 0)
  {
    index = 0;
  }
  if (index >= itemCount)
  {
    index = static_cast<int>(itemCount) - 1;
  }
  applyingFromState_ = true;
  [popup selectItemAtIndex:index];
  applyingFromState_ = false;
}

void MacPopupMenuContext::applyEnabled()
{
  NSPopUpButton *popup = (NSPopUpButton *)popup_;
  if (!popup || !enabledState_)
  {
    return;
  }
  [popup setEnabled:enabledState_->get() ? YES : NO];
}

void MacPopupMenuContext::syncStateFromControl()
{
  NSPopUpButton *popup = (NSPopUpButton *)popup_;
  if (!popup || !selectionState_)
  {
    return;
  }
  updatingFromControl_ = true;
  NSInteger index = [popup indexOfSelectedItem];
  selectionSeat_.set(static_cast<int>(index), true);
  updatingFromControl_ = false;
  if (node_ && node_->props.onChange_)
  {
    node_->props.onChange_->emit();
  }
}

void MacPopupMenuContext::SelectionChangedThunk(void *userData)
{
  MacPopupMenuContext *self = static_cast<MacPopupMenuContext *>(userData);
  if (self)
  {
    self->applySelection();
  }
}

void MacPopupMenuContext::EnabledChangedThunk(void *userData)
{
  MacPopupMenuContext *self = static_cast<MacPopupMenuContext *>(userData);
  if (self)
  {
    self->applyEnabled();
  }
}

void RegisterMacPopupMenuNodeHandler(loka::app::scene::PlatformNodeHandlerRegistry &registry)
{
  registry.registerHandler(&gMacPopupMenuNodeHandler);
}
