#include "ToolboxPendingDialogs.hpp"

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
  if (this->hasPendingDialogs())
  {
    // Delivery may remove windows from the group; App reclamation follows this pass.
    const AppComponentGroup::Components components = this->group_->getComponents();
    std::vector<AppComponent *> dialogs;
    dialogs.reserve(components.size());
    for (std::size_t i = 0; i < components.size(); ++i)
      dialogs.push_back(components[i]);
    for (std::size_t i = 0; i < dialogs.size(); ++i)
    {
      // A delivery may quit the app; later windows then present nothing.
      if (!this->running_)
        break;
      Window *window = dialogs[i] ? dialogs[i]->asWindow() : 0;
      if (!window || this->isWindowClosePending(window))
        continue;
      ToolboxWindow *toolboxWindow = window->asToolboxWindow();
      ToolboxScenePlatformController *controller = toolboxWindow ? toolboxWindow->scenePlatformController() : 0;
      if (!controller || controller->borrowPhase().open())
        continue;
      loka::app::OpenFileDialogProps props;
      if (controller->pendingDialogs().take(props))
      {
        const loka::app::FileChooserResult result = RunToolboxFileDialog(props.options_);
        this->cursorOwner_.reconcile();
        DeliverOpenFileDialogResult(props.result_, props.onResult_, result);
      }
    }
  }
  this->reconcileFocus();
  turn.close();
  this->reclaimWindows();
}

/** Allocation-free check before the dialog pass snapshots the group. */
bool ToolboxApp::hasPendingDialogs() const
{
  if (!this->running_ || !this->group_)
    return false;
  const AppComponentGroup::Components comps = this->group_->getComponents();
  for (std::size_t i = 0; i < comps.size(); ++i)
  {
    Window *window = comps[i] ? comps[i]->asWindow() : 0;
    ToolboxWindow *toolboxWindow = window ? window->asToolboxWindow() : 0;
    ToolboxScenePlatformController *controller = toolboxWindow ? toolboxWindow->scenePlatformController() : 0;
    if (controller && !controller->pendingDialogs().empty())
      return true;
  }
  return false;
}
