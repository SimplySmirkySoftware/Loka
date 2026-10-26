// Shared by the controller and host fixture.
#include "ToolboxPaintSuppressScope.hpp"
bool ToolboxScenePlatformController::ensureButtonControl(short resourceId,
                                                         const Rect &rect,
                                                         const loka::core::String &label,
                                                         loka::core::EmitterState *emitter,
                                                         loka::core::State<bool> *enabled,
                                                         loka::app::scene::NativeLifetimeHint lifetimeHint,
                                                         ToolboxButtonContext *context,
                                                         ButtonControlBinding *&binding,
                                                         bool *painted)
{
  binding = 0;
  if (painted)
    *painted = false;
  if (!window_ || !window_->window() || resourceId <= 0)
  {
    return false;
  }
  Rect controlRect;
  if (!this->intersectWithProjectionClip(rect, controlRect))
  {
    return true;
  }
  for (size_t i = 0; i < buttonControls_.size(); ++i)
  {
    if (buttonControls_[i].resourceId == resourceId)
    {
      binding = &buttonControls_[i];
      break;
    }
  }
  bool created = false;
  if (!binding)
  {
    ControlRef control = 0;
    if (pushButtonBucket_.tryAcquire(control))
    {
      // Pooled handles keep their last title; restore the fresh-created
      // blank so the label diff below runs against the same baseline.
      Str255 title;
      title[0] = 0;
      SetControlTitle(control, title);
    }
    else
    {
      Rect rectCopy = controlRect;
      Str255 title;
      title[0] = 0;
      control = NewControl(window_->window(), &rectCopy, title, false, 0, 0, 1, pushButProc, 0);
      if (!control)
      {
        return false;
      }
      HideControl(control);
    }
    ButtonControlBinding entry;
    entry.context = context;
    entry.resourceId = resourceId;
    entry.control = control;
    entry.emitter = emitter;
    entry.enabled = enabled;
    entry.usedThisFrame = true;
    entry.rect = controlRect;
    entry.label = "";
    entry.lifetimeHint = lifetimeHint;
    buttonControls_.push_back(entry);
    binding = &buttonControls_.back();
    created = true;
  }
  binding->context = context;
  binding->emitter = emitter;
  binding->enabled = enabled;
  binding->lifetimeHint = lifetimeHint;
  bindEnabledState(enabled);
  binding->usedThisFrame = true;
  const Rect before = binding->rect;
  const bool moved = created || before.left != controlRect.left || before.top != controlRect.top
                     || before.right != controlRect.right || before.bottom != controlRect.bottom;
  const Rect admittedMove = moved ? this->admitOwnControlMove(before, controlRect) : Rect();
  if (moved && !created)
  {
    // Moving and sizing hidden reduces the measured three CDEF draws to one.
    ToolboxPaintSuppressScope suppress(EmptyRect(&admittedMove) ? 0 : this->paintSuppressClipRgn_);
    HideControl(binding->control);
  }
  if (moved)
  {
    MoveControl(binding->control, controlRect.left, controlRect.top);
    SizeControl(binding->control, controlRect.right - controlRect.left, controlRect.bottom - controlRect.top);
    binding->rect = controlRect;
  }
  bool submitted;
  {
    ToolboxPaintSuppressScope suppress(this->paintSuppressClipRgn_);
    submitted = ReconcileToolboxButtonControl(binding->control, label, binding->enabled, binding->label);
  }
  ShowControl(binding->control);
  if (moved)
  {
    this->noteControlDraw();
    if (painted)
      *painted = true;
  }
  // Reconciliation can invalidate the moved control again; discharge last.
  if (moved)
    this->validateOwnControlMove(admittedMove);
  return submitted;
}

void ToolboxScenePlatformController::destroyButtonControl(short resourceId,
                                                          loka::app::scene::NativeLifetimeHint lifetimeHint)
{
  for (size_t i = 0; i < buttonControls_.size(); ++i)
  {
    ButtonControlBinding &binding = buttonControls_[i];
    if (binding.resourceId != resourceId)
    {
      continue;
    }
    if (binding.context)
      binding.context->forgetPresentedControl();
    ControlRef control = binding.control;
    binding.control = 0;
    binding.emitter = 0;
    loka::core::State<bool> *retiredEnabled = binding.enabled;
    binding.enabled = 0;
    this->pressLedger_.forget(PressRow::NATIVE_BUTTON, i);
    buttonControls_.erase(buttonControls_.begin() + i);
    if (retiredEnabled && !this->hasLiveBinding(retiredEnabled))
      this->unbindEnabledState(retiredEnabled);
    controlIds_.release(resourceId);
    if (control)
    {
      // Context destruction can run inside an update pass; disposal waits for
      // the platform safe point like every other retired native handle.
      this->hideRetiredControl(control);
      queueRetiredControl(control, lifetimeHint);
    }
    return;
  }
}
