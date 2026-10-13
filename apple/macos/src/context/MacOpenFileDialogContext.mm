#include "MacOpenFileDialogContext.hpp"
#include <cassert>
#include "../MacScenePlatformController.hpp"
#include "../MacWindow.hpp"
#include "MacObjCCompat.hpp"
#include "MacSaveFileDialogSetup.hpp"
#include "app/scene/projection/RetainedNodeHandler.hpp"
#include "Utf8String.hpp"
#import <AppKit/AppKit.h>

// These declarations also cover old/mixed SDK headers, as in MacObjCCompat.
// They supply types only; the runtime capability checks below remain required.
@interface NSSavePanel (LokaSavePanelCompat)
- (void)setNameFieldStringValue:(NSString *)value;
- (NSInteger)runModal;
- (NSURL *)URL;
@end

#ifdef TEST_BUILD
void *MacOpenFileDialogContext::scheduledTimerForTesting() const
{
  return [(LokaMacDeferredDialogPresenter *)this->deferredPresenter_ scheduledTimerForTesting];
}
#endif

namespace
{
  class MacOpenFileDialogNodeHandler
      : public loka::app::scene::RetainedNodeHandler<MacOpenFileDialogNodeHandler,
                                                     loka::app::OpenFileDialogNode,
                                                     MacOpenFileDialogContext>
  {
  public:
    static loka::app::OpenFileDialogNode *cast(loka::app::scene::Node *node)
    {
      return node ? node->asOpenFileDialogNode() : 0;
    }

    static MacOpenFileDialogContext *create(loka::app::OpenFileDialogNode *dialog,
                                            loka::app::scene::IPlatformController *controller,
                                            const loka::app::scene::LayoutState &state)
    {
      (void)state;
      MacScenePlatformController *mac = static_cast<MacScenePlatformController *>(controller);
      return new MacOpenFileDialogContext(mac, mac->rootView(), dialog);
    }

    static void refresh(MacOpenFileDialogContext *ctx, const loka::app::scene::LayoutState &)
    {
      ctx->presentIfNeeded();
    }

    static void afterAttach(MacOpenFileDialogContext *ctx)
    {
      // Keep presentation in the shared after-attach slot; see RetainedNodeHandler.
      ctx->presentIfNeeded();
    }
  };

  MacOpenFileDialogNodeHandler gMacOpenFileDialogNodeHandler;
} // namespace

MacOpenFileDialogContext::MacOpenFileDialogContext(MacScenePlatformController *controller,
                                                   void *parentView,
                                                   loka::app::OpenFileDialogNode *node)
    : MacRetirableContext(controller),
      node_(node),
      transport_(0),
      presentation_(),
      deferredPresenter_(0),
      registration_(0)
{
  MacWindow *window = MacWindow::fromRootView(parentView);
  this->transport_ = window ? &window->dialogResults() : 0;
  deferredPresenter_ = [[LokaMacDeferredDialogPresenter alloc] initWithOwner:this];
}

MacOpenFileDialogContext::~MacOpenFileDialogContext()
{
  assert(!deferredPresenter_ && !registration_ && "terminal fact delivery must queue the presenter before context reclaim");
}

void MacOpenFileDialogContext::readLifecycleFactOnAttach()
{
  if (this->node_ && this->node_->lifecycleFact() == loka::app::scene::NODE_FACT_ATTACHED)
  {
    this->applyAttachedPresentation();
  }
}

void MacOpenFileDialogContext::onFactChanged(loka::app::scene::NodeLifecycleFact previous,
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
      // Same terminal pair as MacConfirmDialogContext; timer mechanics are shared.
      [(LokaMacDeferredDialogPresenter *)this->deferredPresenter_ detachOwner];
      this->retireNativeObject(this->deferredPresenter_);
      this->node_ = 0;
    }
  }
}

void MacOpenFileDialogContext::applyAttachedPresentation()
{
  this->presentation_.markDetached();
}

void MacOpenFileDialogContext::applyDetachedPresentation()
{
  presentation_.markDetached();
  if (deferredPresenter_)
  {
    [(LokaMacDeferredDialogPresenter *)deferredPresenter_ cancelPresent];
  }
  this->disposeDialog();
}

void MacOpenFileDialogContext::onPropsApplied()
{
  if (this->registration_ && this->node_ && !this->registration_->matches(this->node_->props))
  {
    this->applyDetachedPresentation();
    // Like Win32, retarget abandons this operation; reattach permits a new one.
    this->presentation_.markPresented();
  }
}

void MacOpenFileDialogContext::presentIfNeeded()
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
  // A refused timer (#1295 bot P2) must not strand the reserved operation:
  // present now, exactly as a context without a presenter does.
  if (this->deferredPresenter_
      && [(LokaMacDeferredDialogPresenter *)this->deferredPresenter_ schedulePresent])
    return;
  this->presentDialog();
}

void MacOpenFileDialogContext::presentDeferred()
{
  presentDialog();
}

void MacOpenFileDialogContext::presentDialog()
{
  // Native close revokes the captured props before Scene teardown cancels
  // the timer. Refuse that queued presenter as well as a retargeted operation.
  if (!this->presentation_.isPresenting() || !this->registration_ || !this->node_
      || !this->registration_->matches(this->node_->props))
  {
    return;
  }
  loka::app::DialogResultTransport::ReturnPort port(this->registration_);
  this->presentation_.markPresented();
  const loka::app::FileDialogOptions options(this->node_->props.options_);
  if (options.purpose() == loka::app::FILE_DIALOG_SAVE)
  {
    // All logical inputs are stack-owned before AppKit can run nested events.
    NSSavePanel *panel = [NSSavePanel savePanel]; // Autoreleased; do not release.
    NSString *name = loka::macos::MacSaveFileDialogDefaultName(options);
    if (!panel || !name)
    {
      port.seal(loka::app::FileChooserResult::Error(1));
      return;
    }
    // The 10.3 type API keeps Tiger support; UTType APIs require newer systems.
    // Keep deprecation suppression local to the deliberate compatibility calls.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    [panel setAllowedFileTypes:loka::macos::MacSaveFileDialogAllowedTypes(options)];
    [panel setAllowsOtherFileTypes:YES];
    [panel setCanCreateDirectories:YES];
    NSInteger response;
    if ([panel respondsToSelector:@selector(setNameFieldStringValue:)])
    {
      // Name-field setup and parameterless SAVE runModal arrived in 10.6.
      [panel setNameFieldStringValue:name];
      response = [panel runModal];
    }
    else
    {
      response = [panel runModalForDirectory:nil file:name];
    }
    loka::app::FileChooserResult result = loka::app::FileChooserResult::Canceled();
    if (response == LOKA_MAC_MODAL_RESPONSE_OK)
    {
      NSString *path = [panel respondsToSelector:@selector(URL)]
          ? [[panel URL] path] : [panel filename];
      if (path)
      {
        // Same selected-address construction as OPEN below; no filesystem write.
        loka::file::File file = loka::file::File::FromPath(
            loka::core::String(loka::macos::Utf8FromNSString(path)));
        file.setKind(loka::file::File::KIND_FILE);
        result = loka::app::FileChooserResult::File(file);
      }
      else
      {
        result = loka::app::FileChooserResult::Error(2);
      }
    }
#pragma clang diagnostic pop
    // The context may have detached or been reclaimed during the modal loop.
    port.seal(result);
    return;
  }

  // From here through modal return, only the revocable stack port is borrowed.
  NSOpenPanel *panel = [NSOpenPanel openPanel];
  if (!panel)
  {
    port.seal(loka::app::FileChooserResult::Error(1));
    return;
  }

  [panel setAllowsMultipleSelection:NO];
  [panel setCanChooseDirectories:NO];
  [panel setCanChooseFiles:YES];
  NSInteger response = [panel runModal];
  loka::app::FileChooserResult result = loka::app::FileChooserResult::Canceled();
  if (response == LOKA_MAC_MODAL_RESPONSE_OK)
  {
    NSURL *url = [panel URL];
    if (url)
    {
      std::string path = loka::macos::Utf8FromNSString([url path]);
      loka::file::File file = loka::file::File::FromPath(loka::core::String(path));
      file.setKind(loka::file::File::KIND_FILE);
      result = loka::app::FileChooserResult::File(file);
    }
    else
    {
      result = loka::app::FileChooserResult::Error(2);
    }
  }
  // MacApp's repeating admission timer services this pending fact. No callback,
  // context access or separate wake allocation is needed on the native return.
  port.seal(result);
}

void MacOpenFileDialogContext::disposeDialog()
{
  loka::app::DialogResultTransport::Registration *registration = this->registration_;
  this->registration_ = 0;
  delete registration;
}

void RegisterMacOpenFileDialogNodeHandler(loka::app::scene::PlatformNodeHandlerRegistry &registry)
{
  registry.registerHandler(&gMacOpenFileDialogNodeHandler);
}
