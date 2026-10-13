#include "Win32ConfirmDialogContext.hpp"
#include "../Win32ScenePlatformController.hpp"
#include "../Win32Window.hpp"
#include "app/scene/projection/RetainedNodeHandler.hpp"
#include <vector>
#include <string>
#include "Win32ThreadModalDialogScope.hpp"
#include "platform/Win32String.hpp"
#include "Win32DialogResultWake.hpp"
#include <cassert>

namespace
{
  class Win32ConfirmDialogNodeHandler
      : public loka::app::scene::RetainedNodeHandler<Win32ConfirmDialogNodeHandler,
                                                     loka::app::ConfirmDialogNode,
                                                     Win32ConfirmDialogContext>
  {
  public:
    static loka::app::ConfirmDialogNode *cast(loka::app::scene::Node *node)
    {
      return node ? node->asConfirmDialogNode() : 0;
    }

    static Win32ConfirmDialogContext *create(loka::app::ConfirmDialogNode *dialog,
                                              loka::app::scene::IPlatformController *controller,
                                              const loka::app::scene::LayoutState &state)
    {
      (void)state;
      Win32ScenePlatformController *win32 = static_cast<Win32ScenePlatformController *>(controller);
      Win32Window *window = win32->rootHwnd()
                           ? reinterpret_cast<Win32Window *>(GetWindowLongPtr(win32->rootHwnd(), GWLP_USERDATA))
                           : 0;
      return new Win32ConfirmDialogContext(win32->rootHwnd(), dialog, window);
    }

    static void refresh(Win32ConfirmDialogContext *ctx, const loka::app::scene::LayoutState &)
    {
      ctx->presentIfNeeded();
    }

    static void afterAttach(Win32ConfirmDialogContext *ctx)
    {
      // Keep presentation in the shared after-attach slot; see RetainedNodeHandler.
      ctx->presentIfNeeded();
    }
  };

  Win32ConfirmDialogNodeHandler gWin32ConfirmDialogNodeHandler;
} // namespace

Win32ConfirmDialogContext::Win32ConfirmDialogContext(HWND parent, loka::app::ConfirmDialogNode *node,
                                                     Win32Window *window)
    : parent_(parent), node_(node), transport_(window ? &window->dialogResults() : 0),
      presentation_(), registration_(0)
{
}

// Lifecycle/props sequencing deliberately mirrors Win32OpenFileDialogContext.
void Win32ConfirmDialogContext::onPropsApplied()
{
  if (this->registration_ && this->node_ && !this->registration_->matches(this->node_->props))
    this->detachOwnedDialog();
}

Win32ConfirmDialogContext::~Win32ConfirmDialogContext()
{
  this->detachOwnedDialog();
}

void Win32ConfirmDialogContext::readLifecycleFactOnAttach()
{
  if (this->node_ && this->node_->lifecycleFact() == loka::app::scene::NODE_FACT_ATTACHED)
  {
    this->applyAttachedPresentation();
  }
}

void Win32ConfirmDialogContext::onFactChanged(loka::app::scene::NodeLifecycleFact previous,
                                               loka::app::scene::NodeLifecycleFact next)
{
  (void)previous;
  if (next == loka::app::scene::NODE_FACT_ATTACHED)
  {
    this->applyAttachedPresentation();
  }
  else
  {
    // Retained detach abandons the old operation just like terminal retirement.
    this->applyDetachedPresentation();
  }
}

void Win32ConfirmDialogContext::applyAttachedPresentation()
{
  // Record intent here; afterAttach/refresh owns the presentation attempt.
  this->presentation_.markDetached();
}

void Win32ConfirmDialogContext::applyDetachedPresentation()
{
  this->presentation_.markDetached();
  this->detachOwnedDialog();
}

void Win32ConfirmDialogContext::presentIfNeeded()
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
  this->presentDialog();
}

loka::app::ConfirmResult Win32ConfirmDialogContext::mapNativeResult(int button, DWORD error)
{
  switch (button)
  {
  case IDYES:
    return loka::app::ConfirmResult::Save();
  case IDNO:
    return loka::app::ConfirmResult::Discard();
  case IDCANCEL:
    return loka::app::ConfirmResult::Cancel();
  case 0:
    return loka::app::ConfirmResult::Error(static_cast<int>(error));
  }
  assert(false && "Unexpected MessageBoxW result for MB_YESNOCANCEL");
  return loka::app::ConfirmResult::Error(static_cast<int>(error));
}

void Win32ConfirmDialogContext::presentDialog()
{
  loka::app::DialogResultTransport::ConfirmReturnPort port(this->registration_);
  const HWND parent = this->parent_;
  const loka::app::ConfirmDialogOptions options = this->node_->props.options_;
  this->presentation_.markPresented();

  // Same UTF-16 materialization as Win32OpenFileDialogContext's default name.
  std::wstring message;
  if (!loka::win32::MaterializeWideString(options.message(), message))
  {
    queueDeferredResult(port, loka::app::ConfirmResult::Error(ERROR_NO_UNICODE_TRANSLATION));
    return;
  }
  // Copy the owner title before native code, as ReadEditTextWide copies text.
  const int titleLength = GetWindowTextLengthW(parent);
  std::vector<wchar_t> title(static_cast<std::size_t>(titleLength) + 1, L'\0');
  GetWindowTextW(parent, &title[0], titleLength + 1);

  int button;
  DWORD error = 0;
  {
    loka::win32::ThreadModalDialogScope threadModal(parent);
    button = MessageBoxW(parent, message.c_str(), &title[0], MB_YESNOCANCEL | MB_ICONWARNING);
    // Scope teardown calls native APIs too; capture failure before restoring windows.
    if (button == 0)
      error = GetLastError();
  }

  // Native return has only the revocable stack port; the context may be gone.
  queueDeferredResult(port, mapNativeResult(button, error));
}

void Win32ConfirmDialogContext::queueDeferredResult(loka::app::DialogResultTransport::ConfirmReturnPort &port,
                                                    const loka::app::ConfirmResult &result)
{
  loka::win32::QueueDeferredDialogResult(port, result);
}

void Win32ConfirmDialogContext::detachOwnedDialog()
{
  loka::app::DialogResultTransport::ConfirmRegistration *registration = this->registration_;
  this->registration_ = 0;
  delete registration;
}

void RegisterWin32ConfirmDialogNodeHandler(loka::app::scene::PlatformNodeHandlerRegistry &registry)
{
  registry.registerHandler(&gWin32ConfirmDialogNodeHandler);
}
