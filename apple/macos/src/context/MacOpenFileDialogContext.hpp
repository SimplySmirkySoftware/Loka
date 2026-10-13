#ifndef LOKA_MAC_OPEN_FILE_DIALOG_CONTEXT_HPP
#define LOKA_MAC_OPEN_FILE_DIALOG_CONTEXT_HPP

#include "MacRetirableContext.hpp"
#include "MacDeferredDialogPresenter.hpp"
#include "app/core/DialogResultTransport.hpp"
#include "core/String.hpp"

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
class MacDialogResultTestAccess;

class MacOpenFileDialogContext : public MacRetirableContext, public MacDeferredDialogOwner
{
public:
  MacOpenFileDialogContext(MacScenePlatformController *controller,
                           void *parentView,
                           loka::app::OpenFileDialogNode *node);
  virtual ~MacOpenFileDialogContext();
  /** Attach-time read (late-subscriber rule): presentation from the current
      fact, called by the installing handler right after setContext. */
  void readLifecycleFactOnAttach();
  virtual void onFactChanged(loka::app::scene::NodeLifecycleFact previous,
                             loka::app::scene::NodeLifecycleFact next);
  void presentIfNeeded();
  virtual void presentDeferred();
  virtual void onPropsApplied();

private:
  void applyAttachedPresentation();
  void applyDetachedPresentation();
  void presentDialog();
  void disposeDialog();
  friend class MacDialogResultTestAccess;
#ifdef TEST_BUILD
  /** Borrow the presenter's scheduled timer for cancellation verification. */
  void *scheduledTimerForTesting() const;
#endif
  MacOpenFileDialogContext(const MacOpenFileDialogContext &);
  MacOpenFileDialogContext &operator=(const MacOpenFileDialogContext &);

  loka::app::OpenFileDialogNode *node_;
  loka::app::DialogResultTransport *transport_;
  loka::app::OpenFileDialogPresentationPhase presentation_;
  void *deferredPresenter_;
  loka::app::DialogResultTransport::Registration *registration_;
};

void RegisterMacOpenFileDialogNodeHandler(loka::app::scene::PlatformNodeHandlerRegistry &registry);

#endif // LOKA_MAC_OPEN_FILE_DIALOG_CONTEXT_HPP
