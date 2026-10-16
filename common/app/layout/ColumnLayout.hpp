#ifndef LOKA_APP_LAYOUT_COLUMN_LAYOUT_HPP
#define LOKA_APP_LAYOUT_COLUMN_LAYOUT_HPP

#include "app/nodes/nestable/RowColumn.hpp"
#include "app/layout/LayoutHeuristics.hpp"
#include "app/layout/StackSpans.hpp"
#include "app/layout/StackGaps.hpp"
#include "dsl/composition/CompositionList.hpp"

namespace loka
{
  namespace app
  {
    namespace layout
    {
      template <typename LayoutStateT>
      int computeColumnLayoutResultY(loka::app::StackNode *column,
                                     const LayoutStateT &state,
                                     void *context,
                                     int (*layoutChild)(void *, loka::app::scene::Node *, const LayoutStateT &),
                                     int gap,
                                     bool (*acceptY)(void *, int),
                                     const LazyWindow *range = 0,
                                     StackSpans *spans = 0)
      {
        if (!column)
        {
          return state.y;
        }

        if (spans && !mayBandColumn(column))
        {
          spans->invalidate();
          spans = 0;
        }
        const unsigned count = static_cast<unsigned>(column->childrenCount());
        bool band = range && spans && spans->validForGap(gap) && spans->size() == count && range->first <= count
                    && range->count <= count - range->first;
        // A mismatch repairs from the top in this call, before presentation.
        for (;;)
        {
          const unsigned first = band ? range->first : 0;
          const unsigned last = band ? first + range->count : count;
          if (!band && spans)
            spans->begin(count, gap);
          int currentY = state.y + (band && first < count ? spans->start(first) : 0);
          StackGaps gaps(gap, first > 0);
          unsigned index = 0;
          bool mismatch = false;
          loka::dsl::CompositionCursor<loka::app::scene::Node> it(column->childrenHead(), column->childrenCount());
          for (loka::app::scene::Node *child = it.next(); child; child = it.next())
          {
            if (index >= last)
              break;
            if (index++ < first)
              continue;
            const int startY = currentY;
            const int childY = currentY + gaps.before(child);
            if (acceptY && !acceptY(context, childY))
            {
              if (spans)
                spans->invalidate();
              return currentY;
            }
            currentY = childY;
            LayoutStateT childState = state;
            childState.y = layoutCoordinate<LayoutStateT>(currentY);
            if (state.height > 0)
            {
              childState.height = layoutCoordinate<LayoutStateT>(
                  loka::app::layout::remainingChildHeightForColumn(state.height, state.y, currentY));
            }

            int childWidth = state.width;
            int childOffset = 0;
            if (column->props.hasHorizontalAlignment_)
            {
              childWidth = loka::app::layout::preferredChildWidthForColumn(child, state.width);
              const int remain = state.width - childWidth;
              if (remain > 0)
              {
                if (column->props.horizontalAlignment_ == loka::app::HORIZONTAL_ALIGNMENT_CENTER)
                {
                  childOffset = remain / 2;
                }
                else if (column->props.horizontalAlignment_ == loka::app::HORIZONTAL_ALIGNMENT_TRAILING)
                {
                  childOffset = remain;
                }
              }
            }
            childState.x = layoutCoordinate<LayoutStateT>(state.x + childOffset);
            childState.width = layoutCoordinate<LayoutStateT>(childWidth);
            currentY = layoutChild(context, child, childState);
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
            return state.y + spans->total();
          if (spans)
            spans->finish();
          return currentY;
        }
      }
    } // namespace layout
  } // namespace app
} // namespace loka

#endif // LOKA_APP_LAYOUT_COLUMN_LAYOUT_HPP
