// Included by the shared scrollbar implementation, including the host fixture.
#include "ToolboxPortScope.hpp"
#include "ToolboxPaintSuppressScope.hpp"
#include "context/ToolboxTextEditorContext.hpp"

bool ToolboxScenePlatformController::presentEditorScrollBar(short id,
                                                            const ToolboxEditorScrollBarPresentation &presentation)
{
  if (!this->window_ || !this->window_->window())
    return false;
  for (std::size_t i = 0; i < this->scrollBarLedger_.scrollBarControls_.size(); ++i)
  {
    ScrollBarControlBinding &row = this->scrollBarLedger_.scrollBarControls_[i];
    if (row.resourceId != id || !row.control)
      continue;
    if (presentation.shown && (!row.usedThisFrame || !row.editor.editor()))
      return false;
    ToolboxPortScope port(this->window_->window());
    const bool visible = (**row.control).contrlVis != 0;
    bool changed = visible != presentation.shown;
    {
      if (!this->paintSuppressClipRgn_)
        LOKA_PAINT_REFUSE(this->observation_, TOOLBOX_PAINT_REFUSAL_CLIP_ALLOCATION);
      ToolboxPaintSuppressScope suppress(this->paintSuppressClipRgn_);
      if (presentation.shown)
      {
        const bool active = presentation.maximum > 0;
        changed = changed || row.minimum != 0 || row.maximum != presentation.maximum
                  || row.appliedValue != presentation.value || GetControlValue(row.control) != presentation.value
                  || row.active != active;
        row.minimum = 0;
        row.maximum = presentation.maximum;
        row.appliedValue = presentation.value;
        row.lineStep = 1;
        row.pageStep = presentation.pageStep;
        row.active = active;
        SetControlMinimum(row.control, 0);
        SetControlMaximum(row.control, presentation.maximum);
        SetControlValue(row.control, presentation.value);
        HiliteControl(row.control, active ? 0 : 255);
        ShowControl(row.control);
      }
      else
        HideControl(row.control);
    }
    Rect visibleRect;
    if (changed && row.placement.queryVisible(visibleRect))
      this->window_->requestInvalidateRect(visibleRect);
    return true;
  }
  return false;
}

bool ToolboxScenePlatformController::ensureEditorScrollBar(short id,
                                                           const Rect &rect,
                                                           ToolboxTextEditorContext *editor,
                                                           const ToolboxEditorScrollBarPresentation &presentation,
                                                           loka::app::scene::NativeLifetimeHint hint)
{
  if (!this->window_ || !this->window_->window())
    return false;
  if (EmptyRect(&rect))
  {
    this->presentEditorScrollBar(id, ToolboxEditorScrollBarPresentation());
    return false;
  }
  ToolboxPortScope port(this->window_->window());
  ScrollBarControlBinding *row = 0;
  bool acquired = false;
  {
    ToolboxPaintSuppressScope suppress(this->paintSuppressClipRgn_);
    if (!EmptyRect(&rect))
      acquired = this->ensureScrollBarBinding(id,
                                              rect,
                                              0,
                                              presentation.shown ? presentation.maximum : 0,
                                              1,
                                              presentation.shown ? presentation.pageStep : 1,
                                              hint,
                                              row);
  }
  if (!row)
  {
    if (!acquired)
      LOKA_PAINT_REFUSE(this->observation_, TOOLBOX_PAINT_REFUSAL_NATIVE_ACQUISITION);
    this->presentEditorScrollBar(id, ToolboxEditorScrollBarPresentation());
    return false;
  }
  if (!this->paintSuppressClipRgn_)
    LOKA_PAINT_REFUSE(this->observation_, TOOLBOX_PAINT_REFUSAL_CLIP_ALLOCATION);
  row->editor.install(editor);
  return true;
}

void ToolboxScenePlatformController::revokeEditorScrollBar(short id)
{
  for (std::size_t i = 0; i < this->scrollBarLedger_.scrollBarControls_.size(); ++i)
  {
    ScrollBarControlBinding &row = this->scrollBarLedger_.scrollBarControls_[i];
    if (row.resourceId == id)
    {
      row.editor.revoke();
      this->presentEditorScrollBar(id, ToolboxEditorScrollBarPresentation());
      return;
    }
  }
}
