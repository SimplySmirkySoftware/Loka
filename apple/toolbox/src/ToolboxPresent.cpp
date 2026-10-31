#include "ToolboxPendingDialogs.hpp"
#include "ToolboxDialogPresenter.hpp"
#include "ToolboxUnicodeStartup.hpp"

/** Outer admission/render completion, shared with the host fixture. */
void ToolboxApp::present(ActivationPhase phase, loka::core::Operation &turn)
{
  turn.settle();
  this->admitAndApplyWindows();
  if (!this->group_)
  {
    turn.close();
    this->reclaimWindows();
    return;
  }
  const AppComponentGroup::Components comps = group_->getComponents();
  for (std::size_t i = 0; i < comps.size(); ++i)
  {
    Window *w = comps[i]->asWindow();
    ToolboxWindow *toolboxWindow = w ? w->asToolboxWindow() : 0;
    if (toolboxWindow)
    {
      toolboxWindow->flushInvalidate();
    }
  }
  // #503: windows present in every phase; dialogs and focus stay foreground-only.
  if (phase != ACTIVATION_FOREGROUND)
  {
    turn.close();
    this->reclaimWindows();
    return;
  }
  // One group walk and at most one modal for the entire App. No group borrow
  // is read after native code: the modal may have changed the Window group.
  if (this->running_)
  {
    const AppComponentGroup::Components dialogs = this->group_->getComponents();
    for (std::size_t i = 0; i < dialogs.size(); ++i)
    {
      Window *window = dialogs[i] ? dialogs[i]->asWindow() : 0;
      if (!window || this->isWindowClosePending(window))
        continue;
      ToolboxWindow *toolboxWindow = window->asToolboxWindow();
      ToolboxScenePlatformController *controller = toolboxWindow ? toolboxWindow->scenePlatformController() : 0;
      if (!controller || controller->borrowPhase().open())
        continue;
      ToolboxDialogPresenter *dialog = controller->pendingDialogs().take();
      if (dialog)
      {
        this->syncNativeFocusAll(NATIVE_FOCUS_SUPPRESSED);
        loka::toolbox::UnicodeText::scrapToPublic();
        dialog->present(*this);
        loka::toolbox::UnicodeText::scrapFromPublic();
        // One more admission only on the tick that ran a modal: it delivers the
        // sealed value so close/quit callbacks act before the next pass. Never
        // admit inside the dialog walk.
        this->admitAndApplyWindows();
        break;
      }
    }
  }
  this->reconcileFocus();
  turn.close();
  this->reclaimWindows();
}
