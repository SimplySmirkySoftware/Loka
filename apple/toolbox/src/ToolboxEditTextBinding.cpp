#include "platform/ToolboxPascalText.hpp"
#include "ToolboxPortScope.hpp"
#include "ToolboxEditDoor.hpp"
#include <climits>
#include "ToolboxPaintSuppressScope.hpp"
#include <cstring>
namespace
{
  // Like TextEditor's displaced layout, narrow only after checking every edge.
  bool ToolboxMoveEditRect(const Rect &source, long dx, long dy, Rect &out)
  {
    const long left = static_cast<long>(source.left) + dx;
    const long right = static_cast<long>(source.right) + dx;
    const long top = static_cast<long>(source.top) + dy;
    const long bottom = static_cast<long>(source.bottom) + dy;
    if (left < SHRT_MIN || left > SHRT_MAX || right < SHRT_MIN || right > SHRT_MAX || top < SHRT_MIN || top > SHRT_MAX
        || bottom < SHRT_MIN || bottom > SHRT_MAX)
      return false;
    out.left = static_cast<short>(left);
    out.right = static_cast<short>(right);
    out.top = static_cast<short>(top);
    out.bottom = static_cast<short>(bottom);
    return true;
  }
} // namespace

/** Shared production binding path, also compiled by the host TextEdit fixture. */
ToolboxEditPresentation ToolboxScenePlatformController::ensureEditTextControl(ToolboxEditTextContext *ownerContext,
                                                               const Rect &rect,
                                                               loka::core::State<loka::core::String> *text,
                                                               loka::app::scene::NativeLifetimeHint lifetimeHint)
{
  // A row exists only with its owner window: every native interval on it opens that window's door.
  if (!text || !ownerContext || !this->window_ || !this->window_->window())
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
    if (!this->editControls_.prepareAppend())
      return ToolboxEditPresentation();
    ToolboxPortScope port(this->window_->window());
    TEHandle te = 0;
    if (textEditBucket_.tryAcquire(te))
    {
      // Restore the fresh-created baseline: pooled records keep their last
      // text and rects; the new binding starts with no installation certificate.
      TESetText(static_cast<const void *>(""), 0, te);
      (**te).destRect = rect;
      (**te).viewRect = rect;
    }
    else
    {
      te = TENew(&rect, &rect);
      if (!te)
      {
        this->editControls_.cancel();
        return ToolboxEditPresentation();
      }
    }
    TECalText(te);
    TEAutoView(true, te);
    EditTextControlBinding entry(ownerContext, 0, text);
    entry.textSeat = ownerContext->projectedWriteSeat();
    entry.te = te;
    entry.placement = ToolboxNativePlacement(rect, controlRect);
    entry.usedThisFrame = true;
    entry.installed.revoke();
    entry.lifetimeHint = lifetimeHint;
    editControls_.commitAppend(entry);
    binding = &editControls_.back();
    syncEditTextFromState(*binding);
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
  const Rect &previous = binding->placement.full();
  if (!EqualRect(&previous, &rect) && binding->te)
  {
    ToolboxPortScope port(this->window_->window());
    Rect dest = rect, view = rect;
    // Size changes reset displacement, even when the origin also changes.
    if (static_cast<long>(previous.right) - previous.left == static_cast<long>(rect.right) - rect.left
        && static_cast<long>(previous.bottom) - previous.top == static_cast<long>(rect.bottom) - rect.top)
    {
      const long dx = static_cast<long>(rect.left) - previous.left;
      const long dy = static_cast<long>(rect.top) - previous.top;
      Rect movedDest, movedView;
      if (ToolboxMoveEditRect((**binding->te).destRect, dx, dy, movedDest)
          && ToolboxMoveEditRect((**binding->te).viewRect, dx, dy, movedView))
      {
        dest = movedDest;
        view = movedView;
      }
    }
    (**binding->te).destRect = dest;
    (**binding->te).viewRect = view;
    TECalText(binding->te);
  }
  binding->placement = ToolboxNativePlacement(rect, controlRect);
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
  if (binding.unicode)
  {
    ToolboxUnicodeEditorResource *resource = this->unicodeEditors_.take(binding.ownerContext);
    this->unicodeEditors_.retire(resource);
    binding.unicode = 0;
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
  if (binding.editor || binding.unicode || !binding.te || !*binding.te || !binding.text)
    return;
  const loka::core::String source = binding.text->get();
  if (binding.installed.holds(source)) return;
  ToolboxNativeText candidate;
  // Pre-install refusals preserve both the native value and its certificate (#1026).
  if (!candidate.build(source) || candidate.size() > 32767) return;
  bool verified = false;
  {
    ToolboxEditDoor door(this->window_->window(), binding.placement);
    TESetText(candidate.size() ? static_cast<const void *>(candidate.data()) : "",
              static_cast<long>(candidate.size()),
              binding.te);
    CharsHandle handle = TEGetText(binding.te);
    verified = handle && *handle && (**binding.te).teLength >= 0
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
      TESetSelect(static_cast<short>(candidate.size()), static_cast<short>(candidate.size()), binding.te);
    }
  }
  if (verified)
    binding.installed.commit(source);
  else
  {
    binding.installed.revoke();
    if (binding.ownerContext)
      static_cast<ToolboxEditTextContext *>(binding.ownerContext)->invalidateNativePresentation();
  }
}
