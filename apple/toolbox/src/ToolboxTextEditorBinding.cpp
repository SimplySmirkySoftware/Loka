#include "ToolboxEditDoor.hpp"
#include "context/ToolboxLayoutUtil.hpp"

/** Included by the controller and the host fixture: one native installation path. */
TEHandle ToolboxScenePlatformController::ensureTextEditorControl(ToolboxTextEditorContext *context,
                                                                 const Rect &rect,
                                                                 loka::app::scene::NativeLifetimeHint hint)
{
  // The row's interaction placement uses the hit box; only
  // TextEdit's own rects sit inside the frame.
  Rect port = Rect(), sizeBox = Rect();
  const bool hasPort = this->window_ && this->window_->queryPortRect(port);
  if (this->window_)
    this->window_->querySizeBoxRect(sizeBox);
  const ToolboxTextEditorRects geometry = ToolboxTextEditorGeometry(rect, port, hasPort, sizeBox);
  const Rect textRect = geometry.text;
  Rect visibleBox, clipped;
  if (!this->intersectWithProjectionClip(geometry.hit, visibleBox)
      || !this->intersectWithProjectionClip(textRect, clipped))
    return 0;
  std::size_t index = 0;
  if (this->editControls_.find(context, index))
  {
    EditTextControlBinding &binding = this->editControls_[index];
    binding.usedThisFrame = true;
    binding.placement = ToolboxNativePlacement(geometry.hit, visibleBox);
    (**binding.te).viewRect = clipped;
    return binding.te;
  }
  if (!this->editControls_.prepareAppend())
    return 0;
  ToolboxTextMeasureScope fontScope(*this);
  // Plain TextEdit captures the port font and its metrics at creation.
  TextFont(4); // Monaco; Universal Interfaces omit the legacy monaco constant.
  TextSize(9);
  TextFace(0);
  TEHandle te = TENew(&textRect, &clipped);
  if (!te)
  {
    this->editControls_.cancel();
    return 0;
  }
  EditTextControlBinding entry(context, context, 0);
  entry.te = te;
  entry.placement = ToolboxNativePlacement(geometry.hit, visibleBox);
  entry.usedThisFrame = true;
  entry.lifetimeHint = hint;
  this->editControls_.commitAppend(entry);
  TEAutoView(true, te);
  return te;
}

/** Activation only. An absent ledger position clears native focus. */
void ToolboxScenePlatformController::activateEditControl(std::size_t index)
{
  const EditTextControlBinding *old = this->editControls_.focused();
  const TEHandle previousTE = old ? old->te : 0;
  const TEHandle nextTE = index < this->editControls_.size() ? this->editControls_[index].te : 0;
  ToolboxUnicodeEditorResource *oldUnicode = old ? old->unicode : 0;
  ToolboxUnicodeEditorResource *nextUnicode =
      index < this->editControls_.size() ? this->editControls_[index].unicode : 0;
  GrafPtr previousPort = 0;
  GetPort(&previousPort);
  SetPort(this->window_->window());
  if (previousTE || oldUnicode)
  {
    if (previousTE)
    {
      if (!old->editor && !old->unicode)
      {
        ToolboxEditDoor door(this->window_->window(), old->placement);
        TEDeactivate(previousTE);
      }
      else
        TEDeactivate(previousTE);
    }
    else
      old->editor->focus(false);
    this->invalidateUnitOverlap(old->placement.visible());
  }
  this->editControls_.clearFocus();
  if (nextTE || nextUnicode)
  {
    this->editControls_.focus(index);
    this->fallbackFocus_.cut();
    if (nextTE)
    {
      const EditTextControlBinding &next = this->editControls_[index];
      if (!next.editor && !next.unicode)
      {
        ToolboxEditDoor door(this->window_->window(), next.placement);
        TEActivate(nextTE);
      }
      else
        TEActivate(nextTE);
    }
    else
      this->editControls_[index].editor->focus(true);
    this->invalidateUnitOverlap(this->editControls_[index].placement.visible());
  }
  SetPort(previousPort);
}

void ToolboxScenePlatformController::idleTextEdits()
{
  for (size_t i = 0; i < editControls_.size(); ++i)
  {
    if (editControls_[i].editor) editControls_[i].editor->retryProjection();
    if (&editControls_[i] == editControls_.focused() && (editControls_[i].te || editControls_[i].unicode))
    {
      if (editControls_[i].editor)
        editControls_[i].editor->idleTick();
      else
      {
        const short caretBefore = (**editControls_[i].te).caretState;
        {
          ToolboxEditDoor door(this->window_->window(), editControls_[i].placement);
          TEIdle(editControls_[i].te);
        }
        if (caretBefore != (**editControls_[i].te).caretState)
          this->invalidateUnitOverlap(editControls_[i].placement.visible());
      }
    }
  }
}
