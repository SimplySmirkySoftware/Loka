#include "ToolboxInputDoor.hpp"
/** Shared foreground idle and deferred native-drain entry. */
void ToolboxWindow::idleControls(ActivationPhase phase)
{
  extern bool gLokaProbeIdleMadePending;
  extern unsigned long gLokaProbeIdlePendingIterations;
  const bool probePendingBefore = this->hasPendingInvalidate(); // PROBE (not for merge)
  if (scenePlatformController_)
  {
    // Caret blink and other text-edit idle work is foreground-only; native
    // handle reclamation runs in every phase.
    if (phase == ACTIVATION_FOREGROUND)
    {
      ToolboxInputDoor::idleTextEdits(*scenePlatformController_);
    }
    scenePlatformController_->flushRetiredNativeHandles();
  }
  if (!probePendingBefore && this->hasPendingInvalidate())
  {
    gLokaProbeIdleMadePending = true;
    ++gLokaProbeIdlePendingIterations;
  }
}
