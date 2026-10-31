/** Shared dirty replay routing; rows are owned by this controller. */
void ToolboxScenePlatformController::renderDirty(const Rect &rect)
{
  ++debugStats_.renderDirtyCalls;
  ++debugStats_.totalRenderDirtyCalls;
  if (!window_ || !window_->window() || !rootNode_)
  {
    return;
  }
  // O(units) over this controller's facts per delivery. Every touch runs a
  // full layout + composition render, including every LAPTOP.JS scroll step.
  ToolboxUnitLedger::Mark unitCursor = 0;
  Rect unitOverlap;
  if (this->unitLedger_.nextOverlap(rect, unitCursor, unitOverlap))
  {
    ToolboxRenderDirtyInCompositionOrder(*this, rect);
    return;
  }
  if (forceFullRedraw_)
  {
    forceFullRedraw_ = false;
    render();
    return;
  }
  // Text contexts retain their explicit composition replay membership.
  const bool compositionReplay = this->compositionReplay_.required();
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
  if (compositionReplay)
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
    if (!RectsIntersect(rect, hit.rect))
    {
      continue;
    }
    if (!hit.context)
    {
      LOKA_PAINT_REFUSE(this->observation_, TOOLBOX_PAINT_REFUSAL_OBLIGATION_UNMET);
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
    if (!binding.ownerContext || !(binding.te || binding.unicode) || !binding.usedThisFrame)
    {
      continue;
    }
    // An EditText's visible interaction rect excludes its chrome, while
    // draw() frames the outer rect, so the region that has to trigger a redraw
    // is the outer one: gating on the inner rect would skip a dirty strip
    // covering only the chrome and leave the frame erased. A TextEditor's
    // visible interaction rect excludes the bar column; chromeRect() covers the whole frame.
    if (!RectsIntersect(rect, (binding.editor ? binding.editor->chromeRect() : static_cast<ToolboxEditTextContext *>(binding.ownerContext)->chromeRect())))
    {
      continue;
    }
    // Replay borrows established TE placement; it never reprojects or changes
    // the registry after the viewport's projection scope has popped.
    if (binding.editor)
      binding.editor->repaintNative();
    else static_cast<ToolboxEditTextContext *>(binding.ownerContext)->repaint(
        ToolboxEditPresentation(binding.te, binding.installed));
  }
  // Kind-ordered replay has no composition walk to draw its controls and
  // owns the dirty port clip. Only damage eligible for kind-ordered replay
  // reaches this pass; composition replay draws controls during its walk.
  drawControlsInRect(rect);
}
