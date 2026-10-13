#include "MacConfirmDialogContext.hpp"
#include <cassert>
#include "../MacScenePlatformController.hpp"
#include "../MacWindow.hpp"
#include "MacObjCCompat.hpp"
#include "platform/StringUTF8.hpp"
#include "app/scene/projection/RetainedNodeHandler.hpp"
#include "Utf8String.hpp"
#import <AppKit/AppKit.h>

#ifdef TEST_BUILD
void *MacConfirmDialogContext::scheduledTimerForTesting() const
{
  return [(LokaMacDeferredDialogPresenter *)this->deferredPresenter_ scheduledTimerForTesting];
}
#endif

namespace
{
  class MacConfirmDialogNodeHandler
      : public loka::app::scene::RetainedNodeHandler<MacConfirmDialogNodeHandler,
                                                     loka::app::ConfirmDialogNode,
                                                     MacConfirmDialogContext>
  {
  public:
    static loka::app::ConfirmDialogNode *cast(loka::app::scene::Node *node)
    {
      return node ? node->asConfirmDialogNode() : 0;
    }

    static MacConfirmDialogContext *create(loka::app::ConfirmDialogNode *dialog,
                                            loka::app::scene::IPlatformController *controller,
                                            const loka::app::scene::LayoutState &state)
    {
      (void)state;
      MacScenePlatformController *mac = static_cast<MacScenePlatformController *>(controller);
      return new MacConfirmDialogContext(mac, mac->rootView(), dialog);
    }

    static void refresh(MacConfirmDialogContext *ctx, const loka::app::scene::LayoutState &)
    {
      ctx->presentIfNeeded();
    }

    static void afterAttach(MacConfirmDialogContext *ctx)
    {
      // Keep presentation in the shared after-attach slot; see RetainedNodeHandler.
      ctx->presentIfNeeded();
    }
  };

  MacConfirmDialogNodeHandler gMacConfirmDialogNodeHandler;
} // namespace

MacConfirmDialogContext::MacConfirmDialogContext(MacScenePlatformController *controller,
                                                   void *parentView,
                                                   loka::app::ConfirmDialogNode *node)
    : MacRetirableContext(controller),
      node_(node),
      transport_(0),
      presentation_(),
      deferredPresenter_(0),
      registration_(0)
{
  MacWindow *window = MacWindow::fromRootView(parentView);
  this->transport_ = window ? &window->dialogResults() : 0;
  this->deferredPresenter_ = [[LokaMacDeferredDialogPresenter alloc] initWithOwner:this];
}

MacConfirmDialogContext::~MacConfirmDialogContext()
{
  assert(!this->deferredPresenter_ && !this->registration_ && "terminal fact delivery must queue the presenter before context reclaim");
}

void MacConfirmDialogContext::readLifecycleFactOnAttach()
{
  if (this->node_ && this->node_->lifecycleFact() == loka::app::scene::NODE_FACT_ATTACHED)
  {
    this->applyAttachedPresentation();
  }
}

void MacConfirmDialogContext::onFactChanged(loka::app::scene::NodeLifecycleFact previous,
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
      // Same terminal pair as MacOpenFileDialogContext; timer mechanics are shared.
      [(LokaMacDeferredDialogPresenter *)this->deferredPresenter_ detachOwner];
      this->retireNativeObject(this->deferredPresenter_);
      this->node_ = 0;
    }
  }
}

void MacConfirmDialogContext::applyAttachedPresentation()
{
  this->presentation_.markDetached();
}

void MacConfirmDialogContext::applyDetachedPresentation()
{
  this->presentation_.markDetached();
  if (this->deferredPresenter_)
  {
    [(LokaMacDeferredDialogPresenter *)this->deferredPresenter_ cancelPresent];
  }
  this->disposeDialog();
}

void MacConfirmDialogContext::onPropsApplied()
{
  if (this->registration_ && this->node_ && !this->registration_->matches(this->node_->props))
  {
    this->applyDetachedPresentation();
    // Like Win32, retarget abandons this operation; reattach permits a new one.
    this->presentation_.markPresented();
  }
}

void MacConfirmDialogContext::presentIfNeeded()
{
  if (this->registration_ || !this->transport_ || !this->node_
      || this->node_->lifecycleFact() != loka::app::scene::NODE_FACT_ATTACHED)
    return;
  if (!this->presentation_.beginPresent())
    return;
  this->registration_ = this->transport_->reserve(this->node_->props);
  if (!this->registration_)
  {
    this->presentation_.markDetached();
    return;
  }
  if (this->deferredPresenter_)
  {
    [(LokaMacDeferredDialogPresenter *)this->deferredPresenter_ schedulePresent];
    return;
  }
  this->presentDialog();
}

void MacConfirmDialogContext::presentDeferred()
{
  this->presentDialog();
}

void MacConfirmDialogContext::presentDialog()
{
  // Native close revokes the captured props before Scene teardown cancels
  // the timer. Refuse that queued presenter as well as a retargeted operation.
  if (!this->presentation_.isPresenting() || !this->registration_ || !this->node_
      || !this->registration_->matches(this->node_->props))
  {
    return;
  }
  loka::app::DialogResultTransport::ConfirmReturnPort port(this->registration_);
  this->presentation_.markPresented();
  const loka::app::ConfirmDialogOptions options(this->node_->props.options_);
  // Materialize the message exactly as the file twin's default-name setup.
  std::string utf8;
  if (!loka::platform::CollectUtf8(options.message(), utf8))
  {
    port.seal(loka::app::ConfirmResult::Error(1));
    return;
  }
  NSString *message = loka::macos::CreateNSStringFromUtf8(utf8);
  NSAlert *alert = [[NSAlert alloc] init];
  if (!alert)
  {
    port.seal(loka::app::ConfirmResult::Error(1));
    return;
  }
  [alert setMessageText:message];
  [alert setInformativeText:@""];
  // The legacy spelling keeps the 10.3+ API usable with the old SDKs.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
  [alert setAlertStyle:NSWarningAlertStyle];
#pragma clang diagnostic pop
  [alert addButtonWithTitle:@"Save"];
  [alert addButtonWithTitle:@"Cancel"];
  [alert addButtonWithTitle:@"Don't Save"];
  const NSInteger response = [alert runModal];
  [alert release];
  // Native return owns only stack values: the context may already be reclaimed.
  // MacApp's admission timer delivers the sealed fact, as for the file twin.
  port.seal(MacConfirmDialogContext::mapNativeResult(response));
}

loka::app::ConfirmResult MacConfirmDialogContext::mapNativeResult(NSInteger response)
{
  switch (response)
  {
  case NSAlertFirstButtonReturn:
    return loka::app::ConfirmResult::Save();
  case NSAlertSecondButtonReturn:
    return loka::app::ConfirmResult::Cancel();
  case NSAlertThirdButtonReturn:
    return loka::app::ConfirmResult::Discard();
  }
  return loka::app::ConfirmResult::Error(static_cast<int>(response));
}

void MacConfirmDialogContext::disposeDialog()
{
  loka::app::DialogResultTransport::ConfirmRegistration *registration = this->registration_;
  this->registration_ = 0;
  delete registration;
}

void RegisterMacConfirmDialogNodeHandler(loka::app::scene::PlatformNodeHandlerRegistry &registry)
{
  registry.registerHandler(&gMacConfirmDialogNodeHandler);
}
