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

ToolboxScrollViewLayoutResult
ToolboxScenePlatformController::layoutScrollView(loka::app::ScrollViewNode *scrollView,
                                                 loka::app::scene::LayoutState &state,
                                                 loka::app::scene::BoundaryNode *currentBoundary)
{
  const loka::app::scene::Node::LayoutInputsCheckpoint checkpoint(*scrollView);
  const loka::app::scene::NodeDirtyFlags inputs = scrollView->takeLayoutInputs();
  Rect port = Rect();
  const bool hasPort = this->window_ && this->window_->queryPortRect(port);
  const ToolboxScrollViewRect seat(
      state.x, state.y, static_cast<int>(state.x) + state.width, static_cast<int>(state.y) + state.height);
  const ToolboxScrollViewRect portEdges(port.left, port.top, port.right, port.bottom);
  const ToolboxScrollViewEdges edges = ToolboxScrollViewFrameEdges(seat, portEdges, hasPort);
  const int childWidth = ToolboxScrollViewChildWidth(seat, edges);
  loka::core::Managed<loka::app::layout::StackSpans> spans;
  for (std::size_t i = 0; i < this->scrollBarLedger_.viewportScrollBars_.size(); ++i)
  {
    ViewportScrollBarBinding &binding = this->scrollBarLedger_.viewportScrollBars_[i];
    if (binding.scrollView != scrollView)
      continue;
    spans = binding.spans;
    // A resized port can change contact while the stored seat stays fixed.
    if (spans.isValid()
        && (inputs != loka::app::scene::NODE_DIRTY_LAYOUT
            || ToolboxScrollViewChildWidth(binding.rect, ToolboxScrollViewFrameEdges(binding.rect, port, hasPort))
                   != childWidth
            || binding.contentRect.right - binding.contentRect.left != childWidth))
      spans->invalidate();
    break;
  }
  if (this->projectionParentScopes_.insideViewport() || this->projectionParentScopes_.insideUnit())
  {
    // V1 admits one viewport. The inner subtree materializes nothing, while
    // the outer measurement scope remains usable for following siblings.
    checkpoint.requeue();
    if (spans.isValid()) spans->invalidate();
    return ToolboxScrollViewLayoutResult();
  }
  if (!this->scrollViewClipRgn_)
  {
    // NewRgn can refuse under Classic low memory. Without the clip region the
    // render pass cannot draw the subtree, so refuse the whole seat here -
    // before any ledger, CDEF, or hit registration - instead of leaving a
    // scrollbar over invisible interactive content.
    checkpoint.requeue();
    if (spans.isValid()) spans->invalidate();
    return ToolboxScrollViewLayoutResult();
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
    return ToolboxScrollViewLayoutResult();
  }

  Rect viewportRect;
  viewportRect.left = state.x;
  viewportRect.top = state.y;
  viewportRect.right = static_cast<short>(viewportRight);
  viewportRect.bottom = static_cast<short>(viewportBottom);

  const int requestedExtent = scrollView->requestedOrigin().y;
  const int translation = scrollView->contentTranslation().y;

  short contentWidth = 0;
  const bool widthKnown =
      loka::app::layout::queryContentOverflowWidth(scrollView, ToolboxLayoutMetrics::spacing(), this, contentWidth);
  // An unknown range must not scroll: otherwise content cannot be brought back.
  const ToolboxScrollViewMetrics horizontal =
      ToolboxScrollViewResolveMetrics(widthKnown ? contentWidth : 0, childWidth, scrollView->requestedOrigin().x);
  const bool horizontalBar = horizontal.maximum > 0 && state.height > 0;
  if (viewportRight + edges.right > SHRT_MAX || viewportBottom + edges.bottom > SHRT_MAX
      || static_cast<int>(state.y) - edges.top < SHRT_MIN
      || (horizontalBar && static_cast<int>(state.x) - edges.left < SHRT_MIN))
  {
    // Frame-sharing bars must fit QuickDraw's shorts just like the seat itself.
    checkpoint.requeue();
    if (spans.isValid())
      spans->invalidate();
    return ToolboxScrollViewLayoutResult();
  }
  // The vertical bar always reserves width: horizontal range has no feedback from horizontalBar.
  // An X step revisits every row; the next pass recaptures the band.
  if (spans.isValid() && spans->valid() && spans->placedViewport().x != horizontal.clampedOffset)
    spans->invalidate();

  const Rect contentRect =
      ToolboxScrollViewContentRect(ToolboxScrollViewFrameRect(viewportRect, horizontalBar, edges), horizontalBar);
  Rect clippedViewport = contentRect;
  if (this->projectionParentScopes_.currentIsClip())
    this->intersectWithProjectionClip(contentRect, clippedViewport);
  const loka::core::Frame viewportClip(clippedViewport.left,
                                       clippedViewport.top,
                                       clippedViewport.right - clippedViewport.left,
                                       clippedViewport.bottom - clippedViewport.top);
  loka::app::scene::ProjectionParentScope childScope;
  const loka::app::scene::ProjectionParentScope &parentScope =
      this->projectionParentScopes_.current();
  if (!parentScope.deriveScrolled(
          parentScope.nativeParent, horizontal.clampedOffset, translation, viewportClip, childScope))
  {
    checkpoint.requeue();
    if (spans.isValid()) spans->invalidate();
    return ToolboxScrollViewLayoutResult();
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

  const loka::core::Frame newViewport(horizontal.clampedOffset, translation, state.width, state.height);
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
  const std::size_t ledgerMark = this->rectSurfaceExtentLedger_.mark();
  const ToolboxUnitLedger::Mark unitMark = this->unitLedger_.mark();
  {
    loka::app::scene::ProjectionViewportScopeGuard scopeGuard(this->projectionParentScopes_, childScope);
    if (!scopeGuard.isActive())
    {
      checkpoint.requeue();
      if (spans.isValid()) spans->invalidate();
      return ToolboxScrollViewLayoutResult();
    }

    loka::app::scene::LayoutState childState = state;
    childState.height = static_cast<short>(contentRect.bottom - contentRect.top);
    childState.width = static_cast<short>(childWidth);
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
  }

  if (!shortRangeRefused && spans.isValid() && spans->valid())
    contentHeight = spans->total();
  const int extent = contentHeight + scrollView->props.contentInsets_.extentExtra();
  // The extent itself must fit the short the bar's maximum is stored in, and
  // on the heightless path the seat origin rides on top of it.
  if (!shortRangeRefused && (extent > SHRT_MAX || (state.height <= 0 && static_cast<int>(seatY) + extent > SHRT_MAX)))
    shortRangeRefused = true;
  if (shortRangeRefused)
  {
    // Seats recorded under a refused scope are not facts.
    this->rectSurfaceExtentLedger_.discardSince(ledgerMark);
    this->unitLedger_.discard(unitMark);
    checkpoint.requeue();
    if (spans.isValid()) spans->invalidate();
  }
  ToolboxViewportRows rows;
  if (!shortRangeRefused)
  {
    if (spans.isValid() && spans->valid())
      spans->placed(newViewport);
    rows = this->ensureViewportScrollBarControl(viewportRect,
                                                scrollView,
                                                extent,
                                                contentRect.bottom - contentRect.top,
                                                requestedExtent,
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
    scrollView->publishViewport(
        horizontal.clampedOffset, rows.verticalOffset(), childWidth, contentRect.bottom - contentRect.top);
  }

  if (state.height > 0)
  {
    state.y = static_cast<short>(viewportBottom);
  }
  else if (!shortRangeRefused && static_cast<int>(seatY) + extent <= SHRT_MAX)
  {
    // The measured bottom includes both insets and must fit the short before
    // narrowing (the Win32 heightless arm refuses the same wrap).
    state.y = static_cast<short>(seatY + extent);
  }
  else
  {
    if (!shortRangeRefused)
    {
      this->refuseScrollViewShortRange();
    }
    state.y = seatY;
  }
  return shortRangeRefused ? ToolboxScrollViewLayoutResult() : ToolboxScrollViewLayoutResult(state.width, rows);
}

#include "ToolboxScrollViewSettlement.hpp"
#include "ToolboxPaintSuppressScope.hpp"
loka::app::scene::RequestApplication<loka::core::Frame, loka::app::ScrollRequestResult>
ToolboxScrollViewSeat::apply(loka::app::scene::Node &base, const loka::app::ScrollTarget &pending)
{
  loka::app::ScrollViewNode &node = static_cast<loka::app::ScrollViewNode &>(base);
  const loka::core::Frame target = node.normalizedOrigin(pending);
  {
    ToolboxPaintSuppressScope suppress(this->controller_.paintSuppressClipRgn_);
    std::size_t index;
    if (this->rows_.queryRow(loka::app::ScrollViewVisit::X, index))
      SetControlValue(this->controller_.scrollBarLedger_.scrollBarControls_[index].control,
                      static_cast<short>(target.x));
    if (this->rows_.queryRow(loka::app::ScrollViewVisit::Y, index))
      SetControlValue(this->controller_.scrollBarLedger_.scrollBarControls_[index].control,
                      static_cast<short>(target.y));
  }
  this->controller_.commitViewportScrollBarValue(node, this->rows_, 0);
  return loka::app::scene::RequestApplication<loka::core::Frame, loka::app::ScrollRequestResult>(
      node.props.viewport_.state()->get(), loka::app::SCROLL_OK);
}
