/** Included in the Unicode context implementation's closed native branch.
    Each render inspects its controller's rows once; reuse never installs. */
ToolboxUnicodeEditorResource *ToolboxScenePlatformController::ensureUnicodeTextEditorControl(
    ToolboxUnicodeTextEditorContext *context, const Rect &rect, loka::app::scene::NativeLifetimeHint hint)
{
  Rect port = Rect(), sizeBox = Rect();
  if (!this->window_ || !this->window_->queryPortRect(port))
    return 0;
  this->window_->querySizeBoxRect(sizeBox);
  const Rect frame = ToolboxUnicodeTextEditorFrame(rect, port, true, sizeBox);
  Rect permitted, visible, clipped;
  if (EmptyRect(&frame) || !SectRect(&frame, &port, &permitted) || !this->intersectWithProjectionClip(frame, clipped)
      || !SectRect(&clipped, &port, &visible) || !EqualRect(&visible, &permitted))
    return 0;
  const ToolboxNativePlacement placement(frame, visible);
  std::size_t index = 0;
  if (this->editControls_.find(context, index))
  {
    EditTextControlBinding &row = this->editControls_[index];
    if (!EqualRect(&frame, &row.placement.full()))
      this->unicodeEditors_.place(row.unicode, ToolboxUnicodeLogicalFrame(frame));
    row.placement = placement;
    row.usedThisFrame = true;
    return row.unicode;
  }
  ToolboxUnicodeEditorLedger::Prepared prepared(context);
  if (!prepared.resource() || !this->editControls_.prepareAppend())
    return 0;
  EditTextControlBinding row(context, context, 0);
  row.unicode = prepared.resource();
  row.placement = placement;
  row.usedThisFrame = true;
  row.lifetimeHint = hint;
  if (!this->unicodeEditors_.install(
          prepared.resource(), this->window_, ToolboxUnicodeLogicalFrame(frame), ToolboxUnicodeEditorOptions()))
  {
    this->editControls_.cancel();
    return 0;
  }
  this->editControls_.commitAppend(row);
  this->unicodeEditors_.link(prepared.release());
  return row.unicode;
}
