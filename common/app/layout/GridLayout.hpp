#ifndef LOKA_APP_LAYOUT_GRID_LAYOUT_HPP
#define LOKA_APP_LAYOUT_GRID_LAYOUT_HPP

#include "app/nodes/nestable/Grid.hpp"
#include "app/layout/LayoutHeuristics.hpp"
#include "dsl/composition/CompositionList.hpp"

namespace loka
{
  namespace app
  {
    namespace layout
    {
      struct GridLayoutMetrics
      {
        int gapX;
        int gapY;

        GridLayoutMetrics()
            : gapX(0),
              gapY(0)
        {
        }
      };

      /** Optional synchronous admission of each offered cell before narrowing.
          Null preserves the caller's existing coordinate policy. Refusal stops
          this and later children, preserving the greatest dispatched bottom.
          Cost: computation calls the borrowed callback at most four times per
          examined child, O(1) each; neither callback nor context is retained.
          Arithmetic safety for Web assumes short state and zero gaps; arbitrary
          int coordinates and gaps are not protected against arithmetic overflow. */
      template <typename LayoutStateT>
      int computeGridLayoutResultY(loka::app::GridNode *grid,
                                   const LayoutStateT &state,
                                   const GridLayoutMetrics &metrics,
                                   void *context,
                                   int (*layoutChild)(void *, loka::app::scene::Node *, const LayoutStateT &),
                                   bool (*acceptCoordinate)(void *, int) = 0)
      {
        if (!grid)
        {
          return state.y;
        }

        const int rows = grid->props.rows > 0 ? grid->props.rows : 1;
        const int cols = grid->props.cols > 0 ? grid->props.cols : 1;
        const int availableWidth = state.width - metrics.gapX * (cols - 1);
        const int availableHeight = state.height - metrics.gapY * (rows - 1);
        const int cellWidth = availableWidth > 0 ? availableWidth / cols : 0;
        const int cellHeight = availableHeight > 0 ? availableHeight / rows : 0;
        int maxY = state.y;

        if (loka::app::scene::INestable *nestable = grid->asNestable())
        {
          const size_t childCount = nestable->childrenCount();
          const size_t maxCount = static_cast<size_t>(rows * cols);
          size_t index = 0;
          loka::dsl::CompositionCursor<loka::app::scene::Node> it(nestable->childrenHead(), childCount);
          for (loka::app::scene::Node *child = it.next(); child && index < maxCount; child = it.next(), ++index)
          {
            const int rowIndex = static_cast<int>(index / cols);
            const int colIndex = static_cast<int>(index % cols);
            const int childX = state.x + colIndex * (cellWidth + metrics.gapX);
            const int childY = state.y + rowIndex * (cellHeight + metrics.gapY);
            if (acceptCoordinate
                && (!acceptCoordinate(context, childX) || !acceptCoordinate(context, childX + cellWidth)
                    || !acceptCoordinate(context, childY) || !acceptCoordinate(context, childY + cellHeight)))
              return maxY;
            LayoutStateT childState = state;
            childState.x = layoutCoordinate<LayoutStateT>(childX);
            childState.y = layoutCoordinate<LayoutStateT>(childY);
            childState.width = layoutCoordinate<LayoutStateT>(cellWidth);
            childState.height = layoutCoordinate<LayoutStateT>(cellHeight);
            const int bottom = layoutChild(context, child, childState);
            if (bottom > maxY)
            {
              maxY = bottom;
            }
          }
        }
        return maxY;
      }
    } // namespace layout
  } // namespace app
} // namespace loka

#endif // LOKA_APP_LAYOUT_GRID_LAYOUT_HPP
