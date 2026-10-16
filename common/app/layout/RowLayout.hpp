#ifndef LOKA_APP_LAYOUT_ROW_LAYOUT_HPP
#define LOKA_APP_LAYOUT_ROW_LAYOUT_HPP

#include "app/nodes/nestable/RowColumn.hpp"
#include "app/layout/LayoutHeuristics.hpp"
#include "dsl/composition/CompositionList.hpp"

namespace loka
{
  namespace app
  {
    namespace layout
    {
      struct RowLayoutMetrics
      {
        int fallbackHeight;
        int buttonHeight;
        int editTextHeight;
        int popupMenuHeight;
        int textHeight;
        int imageFallbackHeight;

        RowLayoutMetrics()
            : fallbackHeight(0),
              buttonHeight(0),
              editTextHeight(0),
              popupMenuHeight(0),
              textHeight(0),
              imageFallbackHeight(0)
        {
        }
      };

      template <typename LayoutStateT>
      int computeRowLayoutResultY(loka::app::StackNode *row,
                                  const LayoutStateT &state,
                                  const RowLayoutMetrics &metrics,
                                  const RailSpacing &spacing,
                                  const INaturalWidthSource *source,
                                  void *context,
                                  int (*layoutChild)(void *, loka::app::scene::Node *, const LayoutStateT &),
                                  int gap,
                                  bool (*acceptCoordinate)(void *, int))
      {
        if (!row)
        {
          return state.y;
        }

        const size_t childCount = row->childrenCount();
        if (row->childrenHead() == 0 || childCount == 0)
        {
          return state.y;
        }

        RowWidthConsultation widths(
            row->childrenHead(), childCount, state.width, gap, spacing, row->props.rowUndeclaredWidth_, source);
        int rowHeight = state.height > 0 ? state.height : 0;
        if (row->props.hasVerticalAlignment_)
        {
          rowHeight = 0;
          loka::dsl::CompositionCursor<loka::app::scene::Node> measure(row->childrenHead(), childCount);
          for (loka::app::scene::Node *child = measure.next(); child; child = measure.next())
          {
            const int h = loka::app::layout::preferredChildHeightForRow(child,
                                                                        metrics.fallbackHeight,
                                                                        metrics.buttonHeight,
                                                                        metrics.editTextHeight,
                                                                        metrics.popupMenuHeight,
                                                                        metrics.textHeight,
                                                                        metrics.imageFallbackHeight);
            if (h > rowHeight)
            {
              rowHeight = h;
            }
          }
          if (rowHeight <= 0)
          {
            rowHeight = metrics.textHeight;
          }
        }

        int currentX = state.x;
        int maxY = state.y;
        loka::dsl::CompositionCursor<loka::app::scene::Node> it(row->childrenHead(), childCount);
        for (loka::app::scene::Node *child = it.next(); child; child = it.next())
        {
          LayoutStateT childState = state;
          const RowChildWidth allocation = widths.next(child);
          if (allocation.hasGapBefore())
          {
            currentX += gap;
          }
          if (acceptCoordinate
              && (!acceptCoordinate(context, currentX) || !acceptCoordinate(context, currentX + allocation.width())))
            return maxY;
          childState.x = layoutCoordinate<LayoutStateT>(currentX);
          childState.y = state.y;
          childState.width = layoutCoordinate<LayoutStateT>(allocation.width());
          if (row->props.hasVerticalAlignment_)
          {
            const int childHeight = loka::app::layout::preferredChildHeightForRow(child,
                                                                                  rowHeight,
                                                                                  metrics.buttonHeight,
                                                                                  metrics.editTextHeight,
                                                                                  metrics.popupMenuHeight,
                                                                                  metrics.textHeight,
                                                                                  metrics.imageFallbackHeight);
            int offset = 0;
            const int remain = rowHeight - childHeight;
            if (remain > 0)
            {
              if (row->props.verticalAlignment_ == loka::app::VERTICAL_ALIGNMENT_CENTER)
              {
                offset = remain / 2;
              }
              else if (row->props.verticalAlignment_ == loka::app::VERTICAL_ALIGNMENT_BOTTOM)
              {
                offset = remain;
              }
            }
            childState.y = layoutCoordinate<LayoutStateT>(state.y + offset);
            childState.height = layoutCoordinate<LayoutStateT>(childHeight);
          }
          const int childY = layoutChild(context, child, childState);
          if (childY > maxY)
          {
            maxY = childY;
          }
          if (allocation.isLiveSeat())
          {
            currentX += allocation.width();
          }
        }
        return maxY;
      }
    } // namespace layout
  } // namespace app
} // namespace loka

#endif // LOKA_APP_LAYOUT_ROW_LAYOUT_HPP
