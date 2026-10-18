#include "context/ToolboxLayoutUtil.hpp"

/** Included by the controller and the host fixture: one native installation path. */
TEHandle ToolboxScenePlatformController::ensureTextEditorControl(ToolboxTextEditorContext *context,
                                                                 const Rect &rect,
                                                                 loka::app::scene::NativeLifetimeHint hint)
{
  // The binding's rect is the visible box (hit testing, invalidation); only
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
    binding.rect = visibleBox;
    (**binding.te).viewRect = clipped;
    return binding.te;
  }
  ToolboxTextMeasureScope fontScope(*this);
  // Plain TextEdit captures the port font and its metrics at creation.
  TextFont(4); // Monaco; Universal Interfaces omit the legacy monaco constant.
  TextSize(9);
  TextFace(0);
  TEHandle te = TENew(&textRect, &clipped);
  if (!te)
    return 0;
  EditTextControlBinding entry;
  entry.ownerContext = context;
  entry.editor = context;
  entry.text = 0;
  entry.te = te;
  entry.rect = visibleBox;
  entry.usedThisFrame = true;
  entry.lifetimeHint = hint;
  this->editControls_.add(entry);
  TEAutoView(true, te);
  return te;
}

/** Activation only. An absent ledger position clears native focus. */
void ToolboxScenePlatformController::activateEditControl(std::size_t index)
{
  const EditTextControlBinding *old = this->editControls_.focused();
  const TEHandle previousTE = old ? old->te : 0;
  const TEHandle nextTE = index < this->editControls_.size() ? this->editControls_[index].te : 0;
  GrafPtr previousPort = 0;
  GetPort(&previousPort);
  SetPort(this->window_->window());
  if (previousTE)
  {
    TEDeactivate(previousTE);
    this->invalidateUnitOverlap(old->rect);
  }
  this->editControls_.clearFocus();
  if (nextTE)
  {
    this->editControls_.focus(index);
    this->fallbackFocus_.cut();
    TEActivate(nextTE);
    this->invalidateUnitOverlap(this->editControls_[index].rect);
  }
  SetPort(previousPort);
}

/** Click owns hit testing and caret work; requests share only activation. */
bool ToolboxScenePlatformController::handleEditClick(const Point &point)
{
  // FindControl declines inactive and invisible controls. Only their placed,
  // projection-clipped rows consume a press after projection scopes have ended.
  for (std::size_t i = 0; i < this->scrollBarLedger_.scrollBarControls_.size(); ++i)
  {
    const ScrollBarControlBinding &row = this->scrollBarLedger_.scrollBarControls_[i];
    if (row.usedThisFrame && row.editor.editor() && PtInRect(point, &row.rect))
      return true;
  }
  for (size_t i = 0; i < this->editControls_.size(); ++i)
  {
    EditTextControlBinding &binding = this->editControls_[i];
    if (binding.te && PtInRect(point, &binding.rect))
    {
      this->activateEditControl(i);
      if (binding.editor) binding.editor->click(point);
      else
      {
        TEClick(point, false, binding.te);
        this->invalidateUnitOverlap(binding.rect);
      }
      return true;
    }
  }
  this->activateEditControl(this->editControls_.size());
  return false;
}

void ToolboxScenePlatformController::idleTextEdits()
{
  for (size_t i = 0; i < editControls_.size(); ++i)
  {
    if (editControls_[i].editor) editControls_[i].editor->retryProjection();
    if (&editControls_[i] == editControls_.focused() && editControls_[i].te)
    {
      if (editControls_[i].editor)
        editControls_[i].editor->idleTick();
      else
      {
        TEIdle(editControls_[i].te);
        this->invalidateUnitOverlap(editControls_[i].rect);
      }
    }
  }
}
