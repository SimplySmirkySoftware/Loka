#ifndef LOKA_WIN32_CONFIRM_DIALOG_CONTEXT_HPP
#define LOKA_WIN32_CONFIRM_DIALOG_CONTEXT_HPP

#include <windows.h>
#include "app/scene/projection/NativeNodeContext.hpp"
#include "app/core/DialogResultTransport.hpp"

class Win32ConfirmDialogResultTestAccess;
class Win32Window;

namespace loka
{
  namespace app
  {
    namespace scene
    {
      class PlatformNodeHandlerRegistry;
    }
  } // namespace app
} // namespace loka

/** Confirm twin of Win32OpenFileDialogContext; both use the Window ledger. */
class Win32ConfirmDialogContext : public loka::app::scene::NativeNodeContext
{
public:
  Win32ConfirmDialogContext(HWND parent, loka::app::ConfirmDialogNode *node,
                             Win32Window *window = 0);
  virtual ~Win32ConfirmDialogContext();
  /** Attach-time read (late-subscriber rule): presentation from the current
      fact, called by the installing handler right after setContext. */
  void readLifecycleFactOnAttach();
  virtual void onFactChanged(loka::app::scene::NodeLifecycleFact previous,
                             loka::app::scene::NodeLifecycleFact next);
  void presentIfNeeded();
  virtual void onPropsApplied();
  /** Maps a MessageBoxW return and its immediately captured error code. */
  static loka::app::ConfirmResult mapNativeResult(int button, DWORD error);

private:
  void applyAttachedPresentation();
  void applyDetachedPresentation();
  void presentDialog();
  static void queueDeferredResult(loka::app::DialogResultTransport::ConfirmReturnPort &port,
                                  const loka::app::ConfirmResult &result);
  void detachOwnedDialog();
  friend class Win32ConfirmDialogResultTestAccess;
  Win32ConfirmDialogContext(const Win32ConfirmDialogContext &);
  Win32ConfirmDialogContext &operator=(const Win32ConfirmDialogContext &);

  HWND parent_;
  loka::app::ConfirmDialogNode *node_;
  loka::app::DialogResultTransport *transport_;
  loka::app::OpenFileDialogPresentationPhase presentation_;
  loka::app::DialogResultTransport::ConfirmRegistration *registration_;
};

void RegisterWin32ConfirmDialogNodeHandler(loka::app::scene::PlatformNodeHandlerRegistry &registry);

#endif // LOKA_WIN32_CONFIRM_DIALOG_CONTEXT_HPP
