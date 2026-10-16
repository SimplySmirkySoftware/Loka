#include "ToolboxLayoutMetrics.hpp"
#include "app/layout/StackGaps.hpp"
// Included by the controller and the Toolbox host fixture.
namespace
{
  loka::core::LokaAllocationSite ScrollViewSpansSite()
  {
    return loka::core::LokaAllocationSite("ToolboxScrollView", "StackSpans");
  }
  void ReleaseScrollViewSpans(loka::app::layout::StackSpans *spans, void *)
  {
    loka::core::LokaDelete(spans, ScrollViewSpansSite());
  }
  loka::core::Managed<loka::app::layout::StackSpans> BuildScrollViewSpans()
  {
    loka::app::layout::StackSpans *spans =
        loka::core::LokaNew<loka::app::layout::StackSpans>(ScrollViewSpansSite());
    if (!spans)
      return loka::core::Managed<loka::app::layout::StackSpans>();
    const loka::core::Managed<loka::app::layout::StackSpans> result =
        loka::core::Managed<loka::app::layout::StackSpans>::TryWrap(spans, ReleaseScrollViewSpans, 0);
    if (!result.isValid())
      ReleaseScrollViewSpans(spans, 0);
    return result;
  }
}

short ToolboxScenePlatformController::layoutScrollView(
    loka::app::ScrollViewNode *scrollView,
    loka::app::scene::LayoutState &state,
    loka::app::scene::BoundaryNode *currentBoundary)
{
  const loka::app::scene::Node::LayoutInputsCheckpoint checkpoint(*scrollView);
  const loka::app::scene::NodeDirtyFlags inputs = scrollView->takeLayoutInputs();
  loka::core::Managed<loka::app::layout::StackSpans> spans;
  for (std::size_t i = 0; i < this->scrollBarLedger_.viewportScrollBars_.size(); ++i)
  {
    ViewportScrollBarBinding &binding = this->scrollBarLedger_.viewportScrollBars_[i];
    if (binding.scrollView != scrollView)
      continue;
    spans = binding.spans;
    if (spans.isValid() &&
        (inputs != loka::app::scene::NODE_DIRTY_LAYOUT ||
         ToolboxScrollViewChildWidth(binding.rect.right - binding.rect.left) !=
             ToolboxScrollViewChildWidth(state.width)))
      spans->invalidate();
    break;
  }
  if (this->projectionParentScopes_.activeDepth() != 0)
  {
    // V1 admits one viewport. The inner subtree materializes nothing, while
    // the outer measurement scope remains usable for following siblings.
    checkpoint.requeue();
    if (spans.isValid()) spans->invalidate();
    return 0;
  }
  if (!this->scrollViewClipRgn_)
  {
    // NewRgn can refuse under Classic low memory. Without the clip region the
    // render pass cannot draw the subtree, so refuse the whole seat here -
    // before any ledger, CDEF, or hit registration - instead of leaving a
    // scrollbar over invisible interactive content.
    checkpoint.requeue();
    if (spans.isValid()) spans->invalidate();
    return 0;
  }

  const int viewportRight = static_cast<int>(state.x) + state.width;
  const int viewportBottom = static_cast<int>(state.y) + state.height;
  if (state.width < 0 || state.height < 0 ||
      viewportRight > SHRT_MAX || viewportBottom > SHRT_MAX)
  {
    // Rect and LayoutState edges are both short on Toolbox. Refuse the seat
    // before any child context or CDEF is installed from a narrowed rect.
    checkpoint.requeue();
    if (spans.isValid()) spans->invalidate();
    return 0;
  }

  Rect viewportRect;
  viewportRect.left = state.x;
  viewportRect.top = state.y;
  viewportRect.right = static_cast<short>(viewportRight);
  viewportRect.bottom = static_cast<short>(viewportBottom);

  const int requestedFact = scrollView->requestedOrigin().y;
  const int projectedOffset = requestedFact;

  short contentWidth = 0;
  const bool widthKnown =
      loka::app::layout::queryContentOverflowWidth(scrollView, ToolboxLayoutMetrics::spacing(), this, contentWidth);
  // An unknown range must not scroll: otherwise content cannot be brought back.
  const ToolboxScrollViewMetrics horizontal = ToolboxScrollViewResolveMetrics(
      widthKnown ? contentWidth : 0, ToolboxScrollViewChildWidth(state.width), scrollView->requestedOrigin().x);
  const bool horizontalBar = horizontal.maximum > 0 && state.height > 0;
  // The vertical bar always reserves width: horizontal range has no feedback from horizontalBar.
  // An X step revisits every row; the next pass recaptures the band.
  if (spans.isValid() && spans->valid() && spans->placedViewport().x != horizontal.clampedOffset)
    spans->invalidate();

  const Rect contentRect = ToolboxScrollViewContentRect(viewportRect, horizontalBar);
  const loka::core::Frame viewportClip(
      contentRect.left, contentRect.top, contentRect.right - contentRect.left, contentRect.bottom - contentRect.top);
  loka::app::scene::ProjectionParentScope childScope;
  const loka::app::scene::ProjectionParentScope &parentScope =
      this->projectionParentScopes_.current();
  if (!parentScope.deriveScrolled(
          parentScope.nativeParent, horizontal.clampedOffset, projectedOffset, viewportClip, childScope))
  {
    checkpoint.requeue();
    if (spans.isValid()) spans->invalidate();
    return 0;
  }

  loka::dsl::CompositionCursor<loka::app::scene::Node> direct(
      scrollView->childrenHead(), scrollView->childrenCount());
  loka::app::scene::Node *column = direct.next();
  if (scrollView->childrenCount() != 1 || !column ||
      !loka::app::layout::mayBandColumn(column->asStackNode()))
  {
    if (spans.isValid()) spans->invalidate();
    column = 0;
  }
  else if (!spans.isValid())
    spans = BuildScrollViewSpans();

  const loka::core::Frame newViewport(horizontal.clampedOffset, projectedOffset, state.width, state.height);
  loka::app::layout::LazyWindow band = {0, 0};
  const loka::app::layout::LazyWindow *range = 0;
  if (column && spans.isValid() && spans->valid() && inputs == loka::app::scene::NODE_DIRTY_LAYOUT)
  {
    band = spans->indicesIn(spans->placedViewport());
    const loka::app::layout::LazyWindow entering = spans->indicesIn(newViewport);
    if (!band.count)
      band = entering;
    else if (entering.count)
    {
      const unsigned end = std::max(band.first + band.count, entering.first + entering.count);
      band.first = std::min(band.first, entering.first);
      band.count = end - band.first;
    }
    range = &band;
  }

  const short seatY = state.y;
  int contentHeight = 0;
  bool shortRangeRefused = false;
  {
    loka::app::scene::ProjectionParentScopeGuard scopeGuard(
        this->projectionParentScopes_, childScope);
    if (!scopeGuard.isActive())
    {
      checkpoint.requeue();
      if (spans.isValid()) spans->invalidate();
      return 0;
    }

    loka::app::scene::LayoutState childState = state;
    childState.height = static_cast<short>(contentRect.bottom - contentRect.top);
    childState.width = static_cast<short>(
        ToolboxScrollViewChildWidth(state.width));
    const std::size_t ledgerMark = this->rectSurfaceExtentLedger_.mark();
    loka::app::layout::StackGaps gaps(state.spacing);
    loka::dsl::CompositionCursor<loka::app::scene::Node> it(
        scrollView->childrenHead(), scrollView->childrenCount());
    for (loka::app::scene::Node *child = it.next(); child; child = it.next())
    {
      const int childStartY = childState.y;
      const int childY = childStartY + gaps.before(child);
      if (this->refuseNarrowingInScrollScope(childY))
        break;
      childState.y = static_cast<short>(childY);
      LayoutNode(child, childState, this, currentBoundary,
                 child == column ? range : 0,
                 child == column && spans.isValid() ? spans.get() : 0);
      if (this->projectionParentScopes_.current().hasShortRangeRefusal())
      {
        break;
      }
      const int nextY = childState.y;
      if (!this->projectionParentScopes_.current().tryAccumulateContentHeight(
              childStartY, nextY))
      {
        this->refuseScrollViewShortRange();
        break;
      }
    }
    contentHeight = this->projectionParentScopes_.current().contentHeight();
    shortRangeRefused =
        this->projectionParentScopes_.current().hasShortRangeRefusal();
    if (shortRangeRefused)
    {
      // Seats recorded under a refused scope are not facts.
      this->rectSurfaceExtentLedger_.discardSince(ledgerMark);
    }
  }

  if (!shortRangeRefused && state.height <= 0 && static_cast<int>(seatY) + contentHeight > SHRT_MAX)
    shortRangeRefused = true;
  if (shortRangeRefused)
  {
    checkpoint.requeue();
    if (spans.isValid()) spans->invalidate();
  }
  if (!shortRangeRefused)
  {
    if (spans.isValid() && spans->valid())
    {
      contentHeight = spans->total();
      spans->placed(newViewport);
    }
    const int clampedOffset = this->ensureViewportScrollBarControl(viewportRect,
                                                                   scrollView,
                                                                   contentHeight,
                                                                   contentRect.bottom - contentRect.top,
                                                                   requestedFact,
                                                                   horizontal,
                                                                   horizontalBar);
    for (std::size_t i = 0; i < this->scrollBarLedger_.viewportScrollBars_.size(); ++i)
      if (this->scrollBarLedger_.viewportScrollBars_[i].scrollView == scrollView)
      {
        this->scrollBarLedger_.viewportScrollBars_[i].spans = spans;
        this->scrollBarLedger_.viewportScrollBars_[i].contentRect = contentRect;
        break;
      }
    // Publish only after successful layout and after the scope has popped.
    scrollView->publishOrigin(horizontal.clampedOffset, clampedOffset);
  }

  if (state.height > 0)
  {
    state.y = static_cast<short>(viewportBottom);
  }
  else if (!shortRangeRefused &&
           static_cast<int>(seatY) + contentHeight <= SHRT_MAX)
  {
    // The channel caps contentHeight at SHRT_MAX, but the seat origin rides
    // on top of it: the measured bottom must also fit the short before it
    // narrows (the same wrap the Win32 arm refuses on its heightless path).
    state.y = static_cast<short>(seatY + contentHeight);
  }
  else
  {
    if (!shortRangeRefused)
    {
      this->refuseScrollViewShortRange();
    }
    state.y = seatY;
  }
  return state.width;
}
