#include "ToolboxLayoutMetrics.hpp"
/** Included by the production controller and Linux host fixture. */
void ToolboxScenePlatformController::render()
{
  PROFILE_FUNC();
  ++debugStats_.renderCalls;
  ++debugStats_.totalRenderCalls;
  if (!window_ || !window_->window() || !rootNode_)
  {
    return;
  }
  {
    // Identity rule: an auto id stays with its context for the context's
    // lifetime. Observed explicit tags only ever RAISE the auto range —
    // resetting the counter here made lazily-allocating contexts collide
    // after a Show reveal (issue #120).
    controlIds_.raiseBaseAbove(MaxExplicitControlId(rootNode_));
  }
  {
    hitLedger_.buttonHits_.clear();
    hitLedger_.cellHits_.clear();
    for (size_t i = 0; i < buttonControls_.size(); ++i)
    {
      buttonControls_[i].usedThisFrame = false;
    }
    for (size_t i = 0; i < scrollBarLedger_.scrollBarControls_.size(); ++i)
    {
      scrollBarLedger_.scrollBarControls_[i].usedThisFrame = false;
    }
    for (size_t i = 0; i < scrollBarLedger_.viewportScrollBars_.size(); ++i)
    {
      scrollBarLedger_.viewportScrollBars_[i].usedThisFrame = false;
    }
    hitLedger_.editHits_.clear();
    for (size_t i = 0; i < editControls_.size(); ++i)
    {
      editControls_[i].usedThisFrame = false;
    }
    hitLedger_.textHits_.clear();
    hitLedger_.popupHits_.clear();
    clearEnabledBindings();
    pendingTextStates_.clear();
    pendingDirtyRects_.clear();
  }
  loka::app::scene::LayoutState state;
  state.x = 12;
  state.y = 12;
  state.lineHeight = 14;
  state.spacing = static_cast<short>(ToolboxLayoutMetrics::spacing().verticalGap());
  {
    Rect port = window_->window()->portRect;
    short width = static_cast<short>(port.right - port.left - state.x * 2);
    short height = static_cast<short>(port.bottom - port.top - state.y * 2);
    if (width < 0)
    {
      width = 0;
    }
    if (height < 0)
    {
      height = 0;
    }
    state.width = width;
    state.height = height;
  }
  assert(this->projectionParentScopes_.activeDepth() == 0 &&
         "a Toolbox projection pass must begin at the root scope");
  const Rect rootPort = window_->window()->portRect;
  const loka::core::Frame rootClip(
      rootPort.left,
      rootPort.top,
      rootPort.right - rootPort.left,
      rootPort.bottom - rootPort.top);
  if (!this->projectionParentScopes_.resetRoot(
          static_cast<void *>(window_->window()), rootClip))
  {
    return;
  }
  PROFILE_SECTION("layout");
  LayoutNode(rootNode_, state, this, 0);
  assert(this->projectionParentScopes_.activeDepth() == 0 &&
         "a Toolbox projection pass must restore the root scope");
  this->rectSurfaceExtentLedger_.flush();
  RenderNode(rootNode_, this);
  debugStats_.refreshHitCounts(static_cast<int>(hitLedger_.buttonHits_.size()),
                               static_cast<int>(hitLedger_.cellHits_.size()),
                               static_cast<int>(hitLedger_.editHits_.size()),
                               static_cast<int>(hitLedger_.textHits_.size()),
                               static_cast<int>(hitLedger_.popupHits_.size()));
  {
    for (size_t i = 0; i < buttonControls_.size(); ++i)
    {
      if (!buttonControls_[i].usedThisFrame && buttonControls_[i].control)
      {
        HideControl(buttonControls_[i].control);
      }
    }
    for (size_t i = 0; i < scrollBarLedger_.scrollBarControls_.size(); ++i)
    {
      if (!scrollBarLedger_.scrollBarControls_[i].usedThisFrame && scrollBarLedger_.scrollBarControls_[i].control)
      {
        HideControl(scrollBarLedger_.scrollBarControls_[i].control);
      }
    }
    for (size_t i = 0; i < editControls_.size();)
    {
      if (!editControls_[i].usedThisFrame)
      {
        this->retireEditTextControlAt(i, editControls_[i].lifetimeHint);
        continue;
      }
      ++i;
    }
  }
}

