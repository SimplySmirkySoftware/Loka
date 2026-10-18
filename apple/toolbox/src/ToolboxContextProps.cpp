// Shared by the controller and host fixture.
#include "ToolboxPropsRefresh.hpp"
#include "ToolboxPaintSuppressScope.hpp"

void ToolboxScenePlatformController::refreshContextProps(loka::app::scene::Node *node, short buttonResourceId)
{
  if (!node || !node->getContext())
    return;
  loka::app::scene::NodeContext *context = node->getContext();
  loka::core::State<loka::core::String> *previousText = 0;
  loka::core::State<loka::core::String> *liveText = 0;
  loka::core::State<bool> *previousEnabled = 0;
  loka::core::State<bool> *enabled = 0;

  // Props apply only reconciles existing projections of this kind.
  if (node->kind() == loka::app::scene::NODE_KIND_TEXT)
  {
    for (size_t i = 0; i < this->hitLedger_.textHits_.size(); ++i)
    {
      TextHit &hit = this->hitLedger_.textHits_[i];
      if (hit.context != context)
        continue;
      previousText = hit.text;
      hit.text = hit.context->projectedTextState();
      liveText = hit.context->liveTextState();
    }
  }
  else if (node->kind() == loka::app::scene::NODE_KIND_CELL)
  {
    for (size_t i = 0; i < this->hitLedger_.cellHits_.size(); ++i)
    {
      CellHit &hit = this->hitLedger_.cellHits_[i];
      if (hit.context != context)
        continue;
      previousText = hit.text;
      hit.text = node->asCellNode()->props.text_;
      hit.emitter = node->asCellNode()->props.onClick_;
      liveText = hit.context->liveTextState();
    }
  }
  else if (node->kind() == loka::app::scene::NODE_KIND_EDIT_TEXT)
  {
    for (size_t i = 0; i < this->hitLedger_.editHits_.size(); ++i)
    {
      EditHit &hit = this->hitLedger_.editHits_[i];
      if (hit.context != context)
        continue;
      previousText = hit.text;
      hit.text = hit.context->projectedTextState();
      liveText = hit.text;
    }
    size_t editIndex = 0;
    if (this->editControls_.find(context, editIndex))
    {
      EditTextControlBinding &binding = this->editControls_[editIndex];
      previousText = binding.text;
      binding.text = context->projectedTextState();
      binding.textSeat = static_cast<ToolboxEditTextContext *>(context)->projectedWriteSeat();
      liveText = binding.text;
      if (previousText != liveText)
        this->syncEditTextFromState(binding);
    }
  }
  else if (node->kind() == loka::app::scene::NODE_KIND_BUTTON)
  {
    for (size_t i = 0; i < this->hitLedger_.buttonHits_.size(); ++i)
    {
      ButtonHit &hit = this->hitLedger_.buttonHits_[i];
      if (hit.context != context)
        continue;
      previousEnabled = hit.enabled;
      hit.enabled = node->asButtonNode()->props.enabled_;
      hit.emitter = node->asButtonNode()->props.onClick_;
      enabled = hit.enabled;
    }
    for (size_t i = 0; buttonResourceId && i < this->buttonControls_.size(); ++i)
    {
      ButtonControlBinding &binding = this->buttonControls_[i];
      if (binding.resourceId != buttonResourceId)
        continue;
      previousEnabled = binding.enabled;
      binding.enabled = node->asButtonNode()->props.enabled_;
      binding.emitter = node->asButtonNode()->props.onClick_;
      enabled = binding.enabled;
      const loka::app::ButtonProps &props = node->asButtonNode()->props;
      // Retained props reconcile outside the walk; the setters must not paint
      // here either, or the control would paint above later drawers and again
      // at its composition position. Same scope as ensureButtonControl.
      ToolboxPaintSuppressScope suppress(this->paintSuppressClipRgn_);
      ReconcileToolboxButtonControl(binding.control,
                                    props.text_ ? props.text_->get() : loka::core::String::Literal("Button"),
                                    binding.enabled,
                                    binding.label);
    }
  }
  else if (node->kind() == loka::app::scene::NODE_KIND_POPUP_MENU)
  {
    for (size_t i = 0; i < this->hitLedger_.popupHits_.size(); ++i)
    {
      PopupHit &hit = this->hitLedger_.popupHits_[i];
      if (hit.context != context)
        continue;
      const loka::app::PopupMenuProps &props = node->asPopupMenuNode()->props;
      previousEnabled = hit.enabled;
      hit.enabled = props.enabled_;
      enabled = hit.enabled;
    }
  }
  ReconcileToolboxTextSubscription(*this, previousText, liveText);
  if (previousEnabled != enabled)
  {
    this->bindEnabledState(enabled);
    if (previousEnabled && !this->hasLiveBinding(previousEnabled))
      this->unbindEnabledState(previousEnabled);
  }
}
