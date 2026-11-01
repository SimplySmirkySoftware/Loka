#include "ToolboxLayoutMetrics.hpp"
#include "app/layout/BoxLayout.hpp"
#include "ToolboxPaintSuppressScope.hpp"
void ToolboxScenePlatformController::noteMissingRenderContext()
{
  LOKA_PAINT_REFUSE(this->observation_, TOOLBOX_PAINT_REFUSAL_OBLIGATION_UNMET);
}

/** Every render exit finalizes and publishes before resetting pass-local facts.
    The ledger settles its own V rows, O(V); publication makes one or two copies. */
ToolboxPresentObservation::Interval::~Interval()
{
  this->observation_.assignEligibility(this->observation_.firstRefusal_.refused()    ? TOOLBOX_PAINT_REFUSED
                                       : this->observation_.phase_ != PAINT_FINISHED ? TOOLBOX_PAINT_REFUSED
                                       : this->coversWindow_                         ? TOOLBOX_PAINT_CLEAN
                                                                                     : TOOLBOX_PAINT_REFUSED);
  this->ledger_.settlePresented(this->observation_.eligibility_);
  this->debugStats_.lastPresentObservation = this->observation_;
  this->debugStats_.lastPresentEntryEligibility = this->entryEligibility_;
  if (this->observation_.viewports() > 0)
  {
    this->debugStats_.lastMovedPresentObservation = this->observation_;
    this->debugStats_.lastMovedEntryEligibility = this->entryEligibility_;
  }
  const unsigned next = this->observation_.nextReasons_;
  this->observation_.reset();
  this->observation_.reasons_ = next;
}

/** Included by the production controller and Linux host fixture. */
void ToolboxScenePlatformController::render(ToolboxFullDrawPresenter *presenter)
{
  PROFILE_FUNC();
  const bool coversWindow =
      this->window_ && this->window_->window() && this->erasedPass_.admitsCapture(this->window_->window()->portRect);
  ToolboxPresentObservation::Interval interval(
      this->observation_, coversWindow, this->debugStats_, this->scrollBarLedger_);
  ToolboxBlurSnapshot<TOOLBOX_BLUR_FROZEN>::LayoutScope snapshotLayout(this->staticBlur_);
  ToolboxBlurSnapshot<TOOLBOX_BLUR_LIVE>::LayoutScope bandLayout(this->bandBlur_);
  BusyBatch batch(this->cursorOwner());
  this->pressLedger_.begin();
  if (!this->scrollViewClipRgn_ || !this->paintSuppressClipRgn_)
    LOKA_PAINT_REFUSE(this->observation_, TOOLBOX_PAINT_REFUSAL_CLIP_ALLOCATION);
  ++debugStats_.renderCalls;
  ++debugStats_.totalRenderCalls;
  if (!window_ || !window_->window() || !rootNode_)
  {
    if (presenter)
      presenter->eraseBeforePaint();
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
    for (size_t i = 0; i < editControls_.size(); ++i)
    {
      editControls_[i].usedThisFrame = false;
    }
    clearEnabledBindings();
    pendingTextStates_.clear();
    pendingDirtyRects_.clear();
  }
  loka::app::scene::LayoutState state;
  const int margin = resolveWindowContentMargin(this->window_, ToolboxLayoutMetrics::spacing());
  state.x = static_cast<short>(margin);
  state.y = static_cast<short>(margin);
  state.lineHeight = 14;
  state.spacing = static_cast<short>(ToolboxLayoutMetrics::spacing().verticalGap());
  {
    const Rect port = this->window_->window()->portRect;
    const int width = loka::app::layout::insetExtent(port.right - port.left, margin, margin);
    const int height = loka::app::layout::insetExtent(port.bottom - port.top, margin, margin);
    // Window ports normally start at zero; refuse even a nonzero-origin port
    // whose difference exceeds the LayoutState short range before narrowing.
    if (width > SHRT_MAX || height > SHRT_MAX)
    {
      if (presenter)
        presenter->eraseBeforePaint();
      return;
    }
    state.width = static_cast<short>(width);
    state.height = static_cast<short>(height);
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
    if (presenter)
      presenter->eraseBeforePaint();
    return;
  }
  this->unitLedger_.clear();
  hitLedger_.buttonHits_.clear();
  hitLedger_.cellHits_.clear();
  hitLedger_.editHits_.clear();
  hitLedger_.textHits_.clear();
  hitLedger_.popupHits_.clear();
  PROFILE_SECTION("layout");
#ifdef TEST_BUILD
  if (presenter)
    presenter->beginLayoutForTesting();
#endif
  LayoutNode(rootNode_, state, this, 0);
#ifdef TEST_BUILD
  if (presenter)
    presenter->endLayoutForTesting();
#endif
  assert(this->projectionParentScopes_.activeDepth() == 0 &&
         "a Toolbox projection pass must restore the root scope");
  // Once per full layout, the unit ledger visits its own rows with one cursor.
  unsigned foregroundUnits = 0;
  if (this->observation_.viewports())
  {
    ToolboxUnitLedger::Mark cursor = 0;
    Rect overlap;
    while (this->unitLedger_.nextOverlap(this->observation_.viewport(), cursor, overlap))
      ++foregroundUnits;
  }
  this->observation_.complete(foregroundUnits);
  const ToolboxUnitLedger::FirstBlurUnits selection = this->unitLedger_.firstBlurUnits();
  snapshotLayout.commit(selection);
  bandLayout.commit(selection);
  this->rectSurfaceExtentLedger_.flush();
  if (presenter)
    presenter->eraseBeforePaint();
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
        if (!this->paintSuppressClipRgn_)
          LOKA_PAINT_REFUSE(this->observation_, TOOLBOX_PAINT_REFUSAL_CLIP_ALLOCATION);
        ToolboxPaintSuppressScope suppress(this->paintSuppressClipRgn_);
        HideControl(buttonControls_[i].control);
      }
    }
    for (size_t i = 0; i < scrollBarLedger_.scrollBarControls_.size(); ++i)
    {
      if (!scrollBarLedger_.scrollBarControls_[i].usedThisFrame && scrollBarLedger_.scrollBarControls_[i].control)
      {
        if (!this->paintSuppressClipRgn_)
          LOKA_PAINT_REFUSE(this->observation_, TOOLBOX_PAINT_REFUSAL_CLIP_ALLOCATION);
        ToolboxPaintSuppressScope suppress(this->paintSuppressClipRgn_);
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
  this->pressLedger_.commit();
  this->observation_.markPaintFinished();
}

