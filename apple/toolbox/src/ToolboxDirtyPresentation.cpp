/** Shared dirty replay routing; rows are owned by this controller. */
void ToolboxScenePlatformController::renderDirty(const Rect &rect)
{
  ++debugStats_.renderDirtyCalls;
  ++debugStats_.totalRenderDirtyCalls;
  if (!window_ || !window_->window() || !rootNode_)
  {
    return;
  }
  if (forceFullRedraw_)
  {
    forceFullRedraw_ = false;
    render();
    return;
  }
  // Text-like contexts supply this fact through their attach/retire
  // membership. A transparent ImageView (#1199) also replays after surfaces
  // regardless of composition order, so a ZStack window holding one replays
  // in composition order too; that part walks the projection tree once per
  // dirty delivery, like the ZStack check below.
  const bool compositionReplay = this->compositionReplay_.required()
      || (HasImageViewNode(rootNode_) && ToolboxTreeHasKind(rootNode_, loka::app::scene::NODE_KIND_ZSTACK));
  if (!compositionReplay && hitLedger_.textHits_.empty() && hitLedger_.popupHits_.empty() && hitLedger_.cellHits_.empty()
      && buttonControls_.empty() && scrollBarLedger_.scrollBarControls_.empty() && editControls_.empty())
  {
    if (HasRectSurfaceNode(rootNode_) || HasImageViewNode(rootNode_))
    {
      RenderDirtyRectSurfaces(rootNode_, this, rect);
      RenderDirtyImageViews(rootNode_, this, rect);
    }
    else
    {
      render();
    }
    return;
  }
  // Any drawer whose kind order the replay below does not preserve (text-like
  // drawers replay after surfaces and images regardless of composition order)
  // sends a ZStack window through the clipped full render instead.
  bool dirtyIntersectsText = compositionReplay;
  for (size_t i = 0; i < hitLedger_.textHits_.size() && !dirtyIntersectsText; ++i)
    dirtyIntersectsText = RectsIntersect(rect, hitLedger_.textHits_[i].rect);
  for (size_t i = 0; i < editControls_.size() && !dirtyIntersectsText; ++i)
    dirtyIntersectsText = editControls_[i].te && RectsIntersect(rect, editControls_[i].rect);
  for (size_t i = 0; i < hitLedger_.cellHits_.size() && !dirtyIntersectsText; ++i)
    dirtyIntersectsText = RectsIntersect(rect, hitLedger_.cellHits_[i].rect);
  for (size_t i = 0; i < hitLedger_.popupHits_.size() && !dirtyIntersectsText; ++i)
    dirtyIntersectsText = RectsIntersect(rect, hitLedger_.popupHits_[i].rect);
  if (compositionReplay || (dirtyIntersectsText && ToolboxTreeHasKind(rootNode_, loka::app::scene::NODE_KIND_ZSTACK)))
  {
    ToolboxRenderDirtyInCompositionOrder(*this, rect);
    return;
  }
  if (HasRectSurfaceNode(rootNode_))
  {
    RenderDirtyRectSurfaces(rootNode_, this, rect);
  }
  RenderDirtyImageViews(rootNode_, this, rect);
  for (size_t i = 0; i < hitLedger_.popupHits_.size(); ++i)
  {
    PopupHit &hit = hitLedger_.popupHits_[i];
    if (!RectsIntersect(rect, hit.rect))
    {
      continue;
    }
    redrawPopupHit(hit);
  }
  // Replay over a frozen prefix, by value: registration belongs to the render
  // walk (#315), so the registry must not change under this loop. The frozen
  // bound and the copied entry keep a regressed registrar from turning this
  // into an unbounded loop or a dangling reference even where the assert is
  // compiled out; the assert makes the contract loud where it is not.
  const size_t cellReplayCount = hitLedger_.cellHits_.size();
  for (size_t i = 0; i < cellReplayCount; ++i)
  {
    CellHit hit = hitLedger_.cellHits_[i];
    if (!hit.context)
    {
      continue;
    }
    if (!RectsIntersect(rect, hit.rect))
    {
      continue;
    }
    hit.context->draw(this);
    assert(hitLedger_.cellHits_.size() == cellReplayCount
           && "cell hits register on the render walk; the dirty replay must not grow the registry it iterates (#315)");
  }
  for (size_t i = 0; i < hitLedger_.textHits_.size(); ++i)
  {
    TextHit &hit = hitLedger_.textHits_[i];
    if (!RectsIntersect(rect, hit.rect))
    {
      continue;
    }
    redrawTextHit(hit);
  }
  for (size_t i = 0; i < editControls_.size(); ++i)
  {
    EditTextControlBinding &binding = editControls_[i];
    if (!binding.ownerContext || !binding.te || !binding.usedThisFrame)
    {
      continue;
    }
    // An EditText's binding.rect is the inset text rect that TEUpdate needs and
    // draw() frames the outer rect, so the region that has to trigger a redraw
    // is the outer one: gating on the inner rect would skip a dirty strip
    // covering only the chrome and leave the frame erased. A TextEditor's
    // binding.rect excludes the bar column; chromeRect() covers the whole frame.
    if (!RectsIntersect(rect, (binding.editor ? binding.editor->chromeRect() : static_cast<ToolboxEditTextContext *>(binding.ownerContext)->chromeRect())))
    {
      continue;
    }
    // Replay borrows established TE placement; it never reprojects or changes
    // the registry after the viewport's projection scope has popped.
    if (binding.editor) binding.editor->repaint(binding.te);
    else static_cast<ToolboxEditTextContext *>(binding.ownerContext)->repaint(
        ToolboxEditPresentation(binding.te, binding.installed));
  }
  drawControlsInRect(rect);
}
