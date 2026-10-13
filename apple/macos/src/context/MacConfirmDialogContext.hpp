#ifndef LOKA_MAC_CONFIRM_DIALOG_CONTEXT_HPP
#define LOKA_MAC_CONFIRM_DIALOG_CONTEXT_HPP

#include "MacRetirableContext.hpp"
#include "MacDeferredDialogPresenter.hpp"
#include "app/core/DialogResultTransport.hpp"
#include "MacObjCCompat.hpp"

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

class MacScenePlatformController;
class MacConfirmDialogResultTestAccess;

/** Confirm twin of MacOpenFileDialogContext on the same Window ledger. */
class MacConfirmDialogContext : public MacRetirableContext, public MacDeferredDialogOwner
{
public:
  MacConfirmDialogContext(MacScenePlatformController *controller,
                           void *parentView,
                           loka::app::ConfirmDialogNode *node);
  virtual ~MacConfirmDialogContext();
  /** Attach-time read (late-subscriber rule): presentation from the current
      fact, called by the installing handler right after setContext. */
  void readLifecycleFactOnAttach();
  virtual void onFactChanged(loka::app::scene::NodeLifecycleFact previous,
                             loka::app::scene::NodeLifecycleFact next);
  void presentIfNeeded();
  virtual void presentDeferred();
  virtual void onPropsApplied();
  /** Positional NSAlert response mapping; unknown responses keep their code. */
  static loka::app::ConfirmResult mapNativeResult(NSInteger response);

private:
  void applyAttachedPresentation();
  void applyDetachedPresentation();
  void presentDialog();
  void disposeDialog();
  friend class MacConfirmDialogResultTestAccess;
#ifdef TEST_BUILD
  /** Borrow the presenter's scheduled timer for cancellation verification. */
  void *scheduledTimerForTesting() const;
#endif
  MacConfirmDialogContext(const MacConfirmDialogContext &);
  MacConfirmDialogContext &operator=(const MacConfirmDialogContext &);

  loka::app::ConfirmDialogNode *node_;
  loka::app::DialogResultTransport *transport_;
  loka::app::OpenFileDialogPresentationPhase presentation_;
  void *deferredPresenter_;
  loka::app::DialogResultTransport::ConfirmRegistration *registration_;
};

void RegisterMacConfirmDialogNodeHandler(loka::app::scene::PlatformNodeHandlerRegistry &registry);

#endif // LOKA_MAC_CONFIRM_DIALOG_CONTEXT_HPP
