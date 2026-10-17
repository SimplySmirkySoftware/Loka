#include "ToolboxPlatformLayoutHandlers.hpp"
#include "ToolboxLayoutMetrics.hpp"

#include "app/nodes/nestable/Box.hpp"
#include "app/nodes/nestable/Grid.hpp"
#include "app/RectSurface.hpp"
#include "app/nodes/ImageView.hpp"
#include "app/nodes/nestable/RowColumn.hpp"
#include "app/nodes/nestable/ZStack.hpp"
#include "app/nodes/nestable/Layer.hpp"
#include "app/layout/LayoutHeuristics.hpp"
#include "app/layout/BoxLayout.hpp"
#include "app/layout/FramedLayout.hpp"
#include "dsl/composition/CompositionList.hpp"

// Out-of-class definitions for ToolboxLayoutMetrics (see its header). A
// conditional with two lvalue operands binds the member by reference, and
// C++98 then requires a definition; GCC 4.2 at -O0 links against it.
const short ToolboxLayoutMetrics::kDefaultGap;
const short ToolboxLayoutMetrics::kRootInset;
const short ToolboxLayoutMetrics::kDefaultLineHeight;
const short ToolboxLayoutMetrics::kImageFallbackHeight;
const short ToolboxLayoutMetrics::kControlAscentInset;
const short ToolboxLayoutMetrics::kControlDescent;
const short ToolboxLayoutMetrics::kPushButtonTitleInset;
const short ToolboxLayoutMetrics::kEditTextDescent;

namespace
{
  inline int
  DispatchTraversalLayoutChild(void *context, loka::app::scene::Node *child, const loka::app::scene::LayoutState &state)
  {
    loka::app::scene::IPlatformLayoutTraversal *traversal =
        static_cast<loka::app::scene::IPlatformLayoutTraversal *>(context);
    if (!traversal)
    {
      return 0;
    }
    return traversal->layoutChild(child, state);
  }

  bool AcceptTraversalY(void *context, int y)
  {
    return !static_cast<loka::app::scene::IPlatformLayoutTraversal *>(context)->refuseLayoutResultY(y);
  }

  /** For containers whose width is their offered seat, forward only the child result Y. */
  int DispatchTraversalLayoutChildResultY(void *context,
                                          loka::app::scene::Node *child,
                                          const loka::app::scene::LayoutState &state)
  {
    loka::app::scene::IPlatformLayoutTraversal *traversal =
        static_cast<loka::app::scene::IPlatformLayoutTraversal *>(context);
    traversal->layoutChild(child, state);
    return traversal->layoutResultY();
  }

  /** Stack-local adapter for Toolbox's width return and separate result Y. */
  struct ToolboxBoxTraversal
  {
    loka::app::scene::IPlatformLayoutTraversal &traversal;
    short maxWidth;

    explicit ToolboxBoxTraversal(loka::app::scene::IPlatformLayoutTraversal &value)
        : traversal(value), maxWidth(0) {}

    static bool acceptY(void *context, int y)
    {
      return !static_cast<ToolboxBoxTraversal *>(context)->traversal.refuseLayoutResultY(y);
    }

    static int layoutChild(void *context, loka::app::scene::Node *child,
                           const loka::app::scene::LayoutState &state)
    {
      ToolboxBoxTraversal &self = *static_cast<ToolboxBoxTraversal *>(context);
      const int width = self.traversal.layoutChild(child, state);
      if (width > self.maxWidth)
      {
        self.maxWidth = static_cast<short>(width);
      }
      return self.traversal.layoutResultY();
    }
  };

  class ToolboxBoxLayoutHandler : public loka::app::scene::IPlatformLayoutHandler
  {
  public:
    virtual const void *nodeTypeKey() const
    {
      return loka::app::scene::NodeTypeToken<loka::app::BoxNode>();
    }

    virtual int layoutNode(loka::app::scene::Node *node,
                           const loka::app::scene::LayoutState &state,
                           loka::app::scene::IPlatformLayoutTraversal *traversal,
                               const loka::app::layout::LazyWindow * = 0,
                               loka::app::layout::StackSpans * = 0)
    {
      return ComputeToolboxBoxLayout(node ? node->asBoxNode() : 0, state, traversal);
    }
  };

  template <typename NodeT> class ToolboxFramedLayoutHandler : public loka::app::scene::IPlatformLayoutHandler
  {
  public:
    virtual const void *nodeTypeKey() const
    {
      return loka::app::scene::NodeTypeToken<NodeT>();
    }

    virtual int layoutNode(loka::app::scene::Node *node,
                           const loka::app::scene::LayoutState &state,
                           loka::app::scene::IPlatformLayoutTraversal *traversal,
                           const loka::app::layout::LazyWindow * = 0,
                           loka::app::layout::StackSpans * = 0)
    {
      return ComputeToolboxFramedLayout(node, state, traversal);
    }
  };

  template <typename NodeT> class ToolboxZStackLayoutHandler : public loka::app::scene::IPlatformLayoutHandler
  {
  public:
    virtual const void *nodeTypeKey() const
    {
      return loka::app::scene::NodeTypeToken<NodeT>();
    }

    virtual int layoutNode(loka::app::scene::Node *node,
                           const loka::app::scene::LayoutState &state,
                           loka::app::scene::IPlatformLayoutTraversal *traversal,
                               const loka::app::layout::LazyWindow * = 0,
                               loka::app::layout::StackSpans * = 0)
    {
      return ComputeToolboxZStackLayout(node ? node->asNestable() : 0, state, traversal);
    }
  };

  class ToolboxStackLayoutHandler : public loka::app::scene::IPlatformLayoutHandler
  {
  public:
    virtual const void *nodeTypeKey() const
    {
      return loka::app::scene::NodeTypeToken<loka::app::StackNode>();
    }

    virtual int layoutNode(loka::app::scene::Node *node,
                           const loka::app::scene::LayoutState &state,
                           loka::app::scene::IPlatformLayoutTraversal *traversal,
                           const loka::app::layout::LazyWindow *range = 0,
                           loka::app::layout::StackSpans *spans = 0)
    {
      loka::app::StackNode *stack = node ? node->asStackNode() : 0;
      if (!stack || !traversal)
      {
        return 0;
      }

      if (stack->props.effectiveAxis() == loka::app::STACK_AXIS_COLUMN)
      {
        return ComputeToolboxColumnLayout(stack, state, traversal, range, spans);
      }

      return ComputeToolboxRowLayout(stack, state, traversal);
    }
  };

  class ToolboxGridLayoutHandler : public loka::app::scene::IPlatformLayoutHandler
  {
  public:
    virtual const void *nodeTypeKey() const
    {
      return loka::app::scene::NodeTypeToken<loka::app::GridNode>();
    }

    virtual int layoutNode(loka::app::scene::Node *node,
                           const loka::app::scene::LayoutState &state,
                           loka::app::scene::IPlatformLayoutTraversal *traversal,
                           const loka::app::layout::LazyWindow * = 0,
                           loka::app::layout::StackSpans * = 0)
    {
      return ComputeToolboxGridLayout(node ? node->asGridNode() : 0, state, traversal);
    }
  };
} // namespace

int ComputeToolboxColumnLayout(loka::app::StackNode *column,
                               const loka::app::scene::LayoutState &state,
                               loka::app::scene::IPlatformLayoutTraversal *traversal,
                               const loka::app::layout::LazyWindow *range,
                               loka::app::layout::StackSpans *spans)
{
  if (!column || !traversal)
  {
    return 0;
  }

  if (spans && !loka::app::layout::mayBandColumn(column))
  {
    spans->invalidate();
    spans = 0;
  }
  const unsigned count = static_cast<unsigned>(column->childrenCount());
  bool band = range && spans && spans->validForGap(state.spacing) && spans->size() == count && range->first <= count
              && range->count <= count - range->first;
  // Keep selection/repair paired with common ColumnLayout.hpp; this rail
  // additionally returns the maximum width through its traversal channel.
  for (;;)
  {
    const unsigned first = band ? range->first : 0;
    const unsigned last = band ? first + range->count : count;
    if (!band && spans)
      spans->begin(count, state.spacing);
    // Every cached start is nonnegative and bounded by total(). Check
    // the full advance before narrowing either the start or the result.
    if (band && traversal->refuseLayoutResultY(state.y + spans->total()))
    {
      return 0;
    }
    short width = 0;
    short currentY = static_cast<short>(state.y + (band && first < count ? spans->start(first) : 0));
    loka::app::layout::StackGaps gaps(state.spacing, first > 0);
    unsigned index = 0;
    bool mismatch = false;
    loka::dsl::CompositionCursor<loka::app::scene::Node> it(column->childrenHead(), column->childrenCount());
    for (loka::app::scene::Node *child = it.next(); child; child = it.next())
    {
      if (index >= last)
        break;
      if (index++ < first)
        continue;
      const short startY = currentY;
      const int childY = static_cast<int>(currentY) + gaps.before(child);
      if (traversal->refuseLayoutResultY(childY))
      {
        if (spans)
          spans->invalidate();
        return 0;
      }
      currentY = static_cast<short>(childY);
      loka::app::scene::LayoutState childState = state;
      childState.y = currentY;
      if (state.height > 0)
      {
        childState.height =
            static_cast<short>(loka::app::layout::remainingChildHeightForColumn(state.height, state.y, currentY));
      }
      short childWidth = state.width;
      short childOffset = 0;
      if (column->props.hasHorizontalAlignment_)
      {
        childWidth = static_cast<short>(loka::app::layout::preferredChildWidthForColumn(child, state.width));
        short remain = static_cast<short>(state.width - childWidth);
        if (remain > 0)
        {
          if (column->props.horizontalAlignment_ == loka::app::HORIZONTAL_ALIGNMENT_CENTER)
          {
            childOffset = static_cast<short>(remain / 2);
          }
          else if (column->props.horizontalAlignment_ == loka::app::HORIZONTAL_ALIGNMENT_TRAILING)
          {
            childOffset = remain;
          }
        }
      }
      childState.x = static_cast<short>(state.x + childOffset);
      childState.width = childWidth;
      const int childWidthUsed = DispatchTraversalLayoutChild(traversal, child, childState);
      if (childWidthUsed > width)
      {
        width = static_cast<short>(childWidthUsed);
      }
      currentY = traversal->layoutResultY();
      if (band)
      {
        if (currentY - state.y != spans->end(index - 1))
        {
          spans->invalidate();
          mismatch = true;
          break;
        }
      }
      else if (spans)
        spans->append(startY - state.y, currentY - state.y);
    }
    if (mismatch)
    {
      band = false;
      continue;
    }
    if (band)
      currentY = static_cast<short>(state.y + spans->total());
    else if (spans)
      spans->finish();
    traversal->setLayoutResultY(currentY);
    return width;
  }
}

int ComputeToolboxZStackLayout(loka::app::scene::INestable *stack,
                               const loka::app::scene::LayoutState &state,
                               loka::app::scene::IPlatformLayoutTraversal *traversal)
{
  if (!stack || !traversal)
  {
    return 0;
  }

  short maxWidth = 0;
  short maxY = state.y;
  loka::dsl::CompositionCursor<loka::app::scene::Node> it(stack->childrenHead(), stack->childrenCount());
  for (loka::app::scene::Node *child = it.next(); child; child = it.next())
  {
    loka::app::scene::LayoutState childState = state;
    const int width = DispatchTraversalLayoutChild(traversal, child, childState);
    if (width > maxWidth)
    {
      maxWidth = static_cast<short>(width);
    }
    const short childResultY = traversal->layoutResultY();
    if (childResultY > maxY)
    {
      maxY = childResultY;
    }
  }
  traversal->setLayoutResultY(maxY);
  return maxWidth;
}

int ComputeToolboxGridLayout(loka::app::GridNode *grid,
                             const loka::app::scene::LayoutState &state,
                             loka::app::scene::IPlatformLayoutTraversal *traversal)
{
  if (!grid || !traversal)
  {
    return 0;
  }

  const short rows = grid->props.rows > 0 ? grid->props.rows : 1;
  const short cols = grid->props.cols > 0 ? grid->props.cols : 1;
  const short gap = 0;
  short availableWidth = state.width;
  if (availableWidth > 0)
  {
    availableWidth = static_cast<short>(availableWidth - gap * (cols - 1));
    if (availableWidth < 0)
    {
      availableWidth = 0;
    }
  }
  short availableHeight = state.height;
  if (availableHeight > 0)
  {
    availableHeight = static_cast<short>(availableHeight - gap * (rows - 1));
    if (availableHeight < 0)
    {
      availableHeight = 0;
    }
  }
  const short cellWidth = cols > 0 ? static_cast<short>(availableWidth / cols) : 0;
  const short cellHeight = rows > 0 ? static_cast<short>(availableHeight / rows) : 0;
  short maxWidth = static_cast<short>(cellWidth * cols + gap * (cols > 0 ? cols - 1 : 0));
  const int bottom = state.y + (cellHeight > 0 ? cellHeight * rows + gap * (rows - 1) : 0);
  // Positive-height cell origins lie between state.y and bottom. Check first.
  if (traversal->refuseLayoutResultY(bottom))
  {
    return 0;
  }
  short maxY = state.y;
  if (loka::app::scene::INestable *nestable = grid->asNestable())
  {
    const size_t childCount = nestable->childrenCount();
    const size_t maxCount = static_cast<size_t>(rows * cols);
    size_t index = 0;
    loka::dsl::CompositionCursor<loka::app::scene::Node> it(nestable->childrenHead(), childCount);
    for (loka::app::scene::Node *child = it.next(); child && index < maxCount; child = it.next(), ++index)
    {
      const short row = static_cast<short>(index / cols);
      const short col = static_cast<short>(index % cols);
      loka::app::scene::LayoutState cellState = state;
      cellState.x = static_cast<short>(state.x + col * (cellWidth + gap));
      cellState.y = static_cast<short>(state.y + row * (cellHeight + gap));
      cellState.width = cellWidth;
      cellState.height = cellHeight;
      const int width = DispatchTraversalLayoutChild(traversal, child, cellState);
      if (width > maxWidth)
      {
        maxWidth = static_cast<short>(width);
      }
      const short childResultY = traversal->layoutResultY();
      if (childResultY > maxY)
      {
        maxY = childResultY;
      }
    }
  }
  if (cellHeight > 0)
  {
    if (bottom > maxY)
    {
      maxY = static_cast<short>(bottom);
    }
  }
  traversal->setLayoutResultY(maxY);
  return maxWidth;
}

int ComputeToolboxBoxLayout(loka::app::BoxNode *box,
                            const loka::app::scene::LayoutState &state,
                            loka::app::scene::IPlatformLayoutTraversal *traversal)
{
  if (!box || !traversal)
  {
    return 0;
  }
  ToolboxBoxTraversal adapter(*traversal);
  // Common BoxLayout owns padding, clamping and child advancement on every rail.
  const int resultY = loka::app::layout::computeBoxLayoutResultY(box,
                                                                 ToolboxLayoutMetrics::spacing(),
                                                                 state,
                                                                 &adapter,
                                                                 &ToolboxBoxTraversal::layoutChild,
                                                                 state.spacing,
                                                                 &ToolboxBoxTraversal::acceptY);
  if (traversal->refuseLayoutResultY(resultY))
  {
    return 0;
  }
  traversal->setLayoutResultY(static_cast<short>(resultY));
  const int width =
      box->props.hasFixedSize()
          ? box->props.effectiveWidth()
          : adapter.maxWidth
                + 2 * box->props.padding.resolve(0, ToolboxLayoutMetrics::spacing().standardWindowMargin());
  if (traversal->refuseLayoutResultY(width))
    return 0;
  return static_cast<short>(width);
}

int ComputeToolboxFramedLayout(loka::app::scene::Node *group,
                               const loka::app::scene::LayoutState &state,
                               loka::app::scene::IPlatformLayoutTraversal *traversal)
{
  if (!group || !group->asFramedNode() || !traversal)
  {
    return 0;
  }
  // Common framed layout owns content inset and strip placement on every rail.
  const int resultY = loka::app::layout::computeFramedLayoutResultY(
      group, state, traversal, &DispatchTraversalLayoutChildResultY, &AcceptTraversalY);
  if (traversal->refuseLayoutResultY(resultY))
  {
    return 0;
  }
  traversal->setLayoutResultY(static_cast<short>(resultY));
  return state.width;
}

int ComputeToolboxRowLayout(loka::app::StackNode *row,
                            const loka::app::scene::LayoutState &state,
                            loka::app::scene::IPlatformLayoutTraversal *traversal)
{
  if (!row || !traversal)
  {
    return 0;
  }

  int rowStartX = state.x;
  int maxHeight = 0;
  const short lineHeight = state.lineHeight > 0 ? state.lineHeight : ToolboxLayoutMetrics::kDefaultLineHeight;
  const short controlAscent = static_cast<short>(lineHeight - ToolboxLayoutMetrics::kControlAscentInset);
  const short controlHeight = static_cast<short>(controlAscent + ToolboxLayoutMetrics::kControlDescent);
  const short editTextHeight = static_cast<short>(controlAscent + ToolboxLayoutMetrics::kEditTextDescent);
  short rowHeight = lineHeight;
  const size_t childCount = row->childrenCount();
  loka::app::layout::RowWidthConsultation widths(row->childrenHead(),
                                                 childCount,
                                                 state.width,
                                                 state.spacing,
                                                 ToolboxLayoutMetrics::spacing(),
                                                 row->props.rowUndeclaredWidth_,
                                                 traversal);
  if (row->props.hasVerticalAlignment_)
  {
    rowHeight = 0;
    loka::dsl::CompositionCursor<loka::app::scene::Node> measure(row->childrenHead(), childCount);
    for (loka::app::scene::Node *child = measure.next(); child; child = measure.next())
    {
      short height = static_cast<short>(loka::app::layout::preferredChildHeightForRow(
          child, lineHeight, controlHeight, editTextHeight, controlHeight, controlHeight,
          ToolboxLayoutMetrics::kImageFallbackHeight));
      if (height > rowHeight)
      {
        rowHeight = height;
      }
    }
    if (rowHeight <= 0)
    {
      rowHeight = ToolboxLayoutMetrics::kDefaultLineHeight;
    }
  }

  loka::dsl::CompositionCursor<loka::app::scene::Node> it(row->childrenHead(), childCount);
  for (loka::app::scene::Node *child = it.next(); child; child = it.next())
  {
    loka::app::scene::LayoutState rowState = state;
    const loka::app::layout::RowChildWidth allocation = widths.next(child);
    if (allocation.hasGapBefore())
    {
      rowStartX += state.spacing;
    }
    // Deliberate twin of common RowLayout: accept both edges before narrowing.
    if (traversal->refuseLayoutResultY(rowStartX) || traversal->refuseLayoutResultY(rowStartX + allocation.width()))
    {
      return 0;
    }
    rowState.x = static_cast<short>(rowStartX);
    rowState.width = static_cast<short>(allocation.width());
    if (row->props.hasVerticalAlignment_)
    {
      short childHeight = static_cast<short>(loka::app::layout::preferredChildHeightForRow(
          child, rowHeight, controlHeight, editTextHeight, controlHeight, controlHeight,
          ToolboxLayoutMetrics::kImageFallbackHeight));
      short remain = static_cast<short>(rowHeight - childHeight);
      short offset = 0;
      if (remain > 0)
      {
        if (row->props.verticalAlignment_ == loka::app::VERTICAL_ALIGNMENT_CENTER)
        {
          offset = static_cast<short>(remain / 2);
        }
        else if (row->props.verticalAlignment_ == loka::app::VERTICAL_ALIGNMENT_BOTTOM)
        {
          offset = remain;
        }
      }
      // Every child receives its painted box top, including text controls.
      const int childY = state.y + offset;
      if (traversal->refuseLayoutResultY(childY))
      {
        return 0;
      }
      rowState.y = static_cast<short>(childY);
      rowState.height = childHeight;
    }
    DispatchTraversalLayoutChild(traversal, child, rowState);
    if (allocation.isLiveSeat())
    {
      rowStartX += allocation.width();
    }
    const short childResultY = traversal->layoutResultY();
    if (childResultY > state.y && static_cast<short>(childResultY - state.y) > maxHeight)
    {
      maxHeight = static_cast<short>(childResultY - state.y);
    }
  }
  if (row->props.hasVerticalAlignment_)
  {
    // An aligned row's extent is the painted box it measured.
    maxHeight = rowHeight;
  }
  const int resultY = state.y + maxHeight;
  if (traversal->refuseLayoutResultY(resultY))
  {
    return 0;
  }
  traversal->setLayoutResultY(static_cast<short>(resultY));
  return static_cast<short>(rowStartX - state.x);
}

bool ApplyToolboxPlatformLayoutHandler(
    loka::app::scene::PlatformLayoutHandlerRegistry &registry,
    loka::app::scene::Node &node,
    loka::app::scene::LayoutState &state,
    loka::app::scene::IPlatformLayoutTraversal &traversal,
    short &width,
    const loka::app::layout::LazyWindow *range,
    loka::app::layout::StackSpans *spans)
{
  loka::app::scene::IPlatformLayoutHandler *handler = registry.find(&node);
  if (!handler)
  {
    return false;
  }
  width = static_cast<short>(handler->layoutNode(&node, state, &traversal, range, spans));
  state.y = traversal.layoutResultY();
  return true;
}

bool RegisterToolboxPlatformLayoutHandlers(loka::app::scene::PlatformLayoutHandlerRegistry &registry)
{
  bool ok = registry.registerHandler(new ToolboxBoxLayoutHandler());
  ok = registry.registerHandler(new ToolboxFramedLayoutHandler<loka::app::RibbonGroupNode>()) && ok;
  ok = registry.registerHandler(new ToolboxFramedLayoutHandler<loka::app::CardNode>()) && ok;
  ok = registry.registerHandler(new ToolboxZStackLayoutHandler<loka::app::ZStackNode>()) && ok;
  ok = registry.registerHandler(new ToolboxZStackLayoutHandler<loka::app::LayerNode>()) && ok;
  ok = registry.registerHandler(new ToolboxStackLayoutHandler()) && ok;
  ok = registry.registerHandler(new ToolboxGridLayoutHandler()) && ok;
  return ok;
}
