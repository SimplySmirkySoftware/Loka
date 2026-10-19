#include "platform/ToolboxPascalText.hpp"
#include "ToolboxPortScope.hpp"
#include "ToolboxPaintSuppressScope.hpp"
#include <cstring>
/** Shared production binding path, also compiled by the host TextEdit fixture. */
ToolboxEditPresentation ToolboxScenePlatformController::ensureEditTextControl(ToolboxEditTextContext *ownerContext,
                                                               const Rect &rect,
                                                               loka::core::State<loka::core::String> *text,
                                                               loka::app::scene::NativeLifetimeHint lifetimeHint)
{
  if (!text || !ownerContext)
  {
    return ToolboxEditPresentation();
  }
  Rect controlRect;
  if (!this->intersectWithProjectionClip(rect, controlRect))
  {
    return ToolboxEditPresentation();
  }
  EditTextControlBinding *binding = 0;
  loka::core::State<loka::core::String> *previousText = 0;
  size_t bindingIndex = 0;
  if (editControls_.find(ownerContext, bindingIndex))
  {
    binding = &editControls_[bindingIndex];
    previousText = binding->text;
    binding->text = text;
    binding->textSeat = ownerContext->projectedWriteSeat();
  }
  if (!binding)
  {
    TEHandle te = 0;
    if (textEditBucket_.tryAcquire(te))
    {
      // Restore the fresh-created baseline: pooled records keep their last
      // text and rects; the new binding starts with no installation certificate.
      TESetText(static_cast<const void *>(""), 0, te);
      (**te).destRect = controlRect;
      (**te).viewRect = controlRect;
      TECalText(te);
    }
    else
    {
      te = TENew(&controlRect, &controlRect);
      if (!te)
      {
        return ToolboxEditPresentation();
      }
    }
    EditTextControlBinding entry;
    entry.ownerContext = ownerContext;
    entry.editor = 0;
    entry.text = text;
    entry.textSeat = ownerContext->projectedWriteSeat();
    entry.te = te;
    entry.rect = controlRect;
    entry.usedThisFrame = true;
    entry.installed.revoke();
    entry.lifetimeHint = lifetimeHint;
    editControls_.add(entry);
    binding = &editControls_.back();
    syncEditTextFromState(*binding);
    TEAutoView(true, binding->te);
  }
  if (this->fallbackFocusContext() == ownerContext)
    this->fallbackFocus_.cut();
  // A native EditText is a live String projection just like Text and the
  // fallback EditHit. Register it at the same seam so programmatic writes can
  // reach TESetText even when no render walk follows the write.
  bindTextState(text);
  if (previousText && previousText != text && !this->hasLiveBinding(previousText))
  {
    unbindTextState(previousText);
  }
  binding->usedThisFrame = true;
  binding->lifetimeHint = lifetimeHint;
  if (binding->rect.left != controlRect.left || binding->rect.top != controlRect.top || binding->rect.right != controlRect.right
      || binding->rect.bottom != controlRect.bottom)
  {
    binding->rect = controlRect;
    if (binding->te)
    {
      (**binding->te).destRect = controlRect;
      (**binding->te).viewRect = controlRect;
      TECalText(binding->te);
      TEAutoView(true, binding->te);
    }
  }
  if (!inBatchUpdate_)
  {
    syncEditTextFromState(*binding);
  }
  return ToolboxEditPresentation(binding->te, binding->installed);
}

void ToolboxScenePlatformController::retireEditTextBinding(
    EditTextControlBinding &binding,
    loka::app::scene::NativeLifetimeHint lifetimeHint)
{
  binding.installed.revoke();
  if (binding.editor)
    binding.editor->invalidateNativePresentation();
  else if (binding.ownerContext)
    static_cast<ToolboxEditTextContext *>(binding.ownerContext)->invalidateNativePresentation();
  if (binding.te)
  {
    if (this->window_ && this->window_->window())
    {
      ToolboxPortScope port(this->window_->window());
      ToolboxPaintSuppressScope suppress(this->paintSuppressClipRgn_);
      TEDeactivate(binding.te);
    }
    // The EditText churn pool accepts only ordinary EditText records. Editors
    // capture Monaco 9 and a whole-document layout; dispose at the same clock
    // safe point instead of offering their incompatible recipe to that pool.
    // Without a window no safe deactivation port exists. Never pool an active
    // record for reuse; disposal still follows the native retirement clock.
    queueRetiredTextEdit(binding.te,
                         binding.editor || !this->window_ || !this->window_->window()
                             ? loka::app::scene::NATIVE_HINT_EAGER_RELEASE
                             : lifetimeHint);
    binding.te = 0;
  }
  binding.textSeat = loka::app::scene::WriteSeat<loka::core::String>();
}

void ToolboxScenePlatformController::retireEditTextControlAt(
    std::size_t index,
    loka::app::scene::NativeLifetimeHint lifetimeHint)
{
  EditTextControlBinding &binding = editControls_[index];
  loka::core::State<loka::core::String> *retiredText = binding.text;
  this->retireEditTextBinding(binding, lifetimeHint);
  this->pressLedger_.forget(PressRow::EDIT_CONTROL, index);
  editControls_.erase(index);
  if (retiredText && !this->hasLiveBinding(retiredText))
  {
    unbindTextState(retiredText);
  }
}

void ToolboxScenePlatformController::retireEditTextControl(
    loka::app::scene::NodeContext *ownerContext,
    loka::app::scene::NativeLifetimeHint lifetimeHint)
{
  std::size_t index = 0;
  if (editControls_.find(ownerContext, index))
  {
    this->retireEditTextControlAt(index, lifetimeHint);
  }
}

void ToolboxScenePlatformController::syncEditTextFromState(EditTextControlBinding &binding)
{
  if (binding.editor || !binding.te || !*binding.te || !binding.text) return;
  const loka::core::String source = binding.text->get();
  if (binding.installed.holds(source)) return;
  ToolboxNativeText candidate;
  // Pre-install refusals preserve both the native value and its certificate (#1026).
  if (!candidate.build(source) || candidate.size() > 32767) return;
  TESetText(candidate.size() ? static_cast<const void *>(candidate.data()) : "",
            static_cast<long>(candidate.size()), binding.te);
  CharsHandle handle = TEGetText(binding.te);
  bool verified = handle && *handle && (**binding.te).teLength >= 0
      && static_cast<std::size_t>((**binding.te).teLength) == candidate.size();
  if (verified)
  {
    const signed char state = HGetState(reinterpret_cast<Handle>(handle));
    HLock(reinterpret_cast<Handle>(handle));
    verified = !candidate.size() || std::memcmp(*handle, candidate.data(), candidate.size()) == 0;
    HSetState(reinterpret_cast<Handle>(handle), state);
  }
  if (verified)
  {
    binding.installed.commit(source);
    TESetSelect(static_cast<short>(candidate.size()), static_cast<short>(candidate.size()), binding.te);
  }
  else
  {
    binding.installed.revoke();
    if (binding.ownerContext)
      static_cast<ToolboxEditTextContext *>(binding.ownerContext)->invalidateNativePresentation();
  }
}
