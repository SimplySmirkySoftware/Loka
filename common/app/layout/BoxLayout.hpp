#ifndef LOKA_APP_LAYOUT_BOX_LAYOUT_HPP
#define LOKA_APP_LAYOUT_BOX_LAYOUT_HPP

#include "app/nodes/nestable/Box.hpp"
#include "app/layout/LayoutHeuristics.hpp"
#include "app/layout/StackGaps.hpp"
#include "dsl/composition/CompositionList.hpp"

namespace loka
{
  namespace app
  {
    namespace layout
    {
      /** An inset offer's extent: subtracted in int and clamped to 0 before any caller narrows it,
          so an inset wider than the offer gives an empty offer instead of a wrapped one. */
      inline int insetExtent(int outer, int before, int after)
      {
        const int extent = outer - before - after;
        return extent < 0 ? 0 : extent;
      }

      template <typename LayoutStateT>
      int computeInsetLayoutResultY(loka::app::scene::Node *childrenHead,
                                    size_t childCount,
                                    const FrameInsets &padding,
                                    bool hasFixedWidth,
                                    bool hasFixedHeight,
                                    int fixedWidth,
                                    int fixedHeight,
                                    const LayoutStateT &state,
                                    void *context,
                                    int (*layoutChild)(void *, loka::app::scene::Node *, const LayoutStateT &),
                                    int gap,
                                    bool (*acceptY)(void *, int))
      {
        LayoutStateT childState = state;
        childState.x = layoutCoordinate<LayoutStateT>(state.x + padding.left);
        int currentY = state.y + padding.top;
        childState.width = layoutCoordinate<LayoutStateT>(
            insetExtent(hasFixedWidth ? fixedWidth : state.width, padding.left, padding.right));
        childState.height = layoutCoordinate<LayoutStateT>(
            insetExtent(hasFixedHeight ? fixedHeight : state.height, padding.top, padding.bottom));

        StackGaps gaps(gap);
        loka::dsl::CompositionCursor<loka::app::scene::Node> it(childrenHead, childCount);
        for (loka::app::scene::Node *child = it.next(); child; child = it.next())
        {
          const int childY = currentY + gaps.before(child);
          if (acceptY && !acceptY(context, childY))
            return currentY;
          childState.y = layoutCoordinate<LayoutStateT>(childY);
          currentY = layoutChild(context, child, childState);
        }
        const int resultY = currentY + padding.bottom;
        return hasFixedHeight ? state.y + fixedHeight : resultY;
      }
      /** Resolves the Box width once for both its content offer and, when requested,
          the rail's reported width. The output is borrowed only for this call. */
      template <typename LayoutStateT>
      int computeBoxLayoutResultY(loka::app::BoxNode *box,
                                  const RailSpacing &spacing,
                                  const LayoutStateT &state,
                                  void *context,
                                  int (*layoutChild)(void *, loka::app::scene::Node *, const LayoutStateT &),
                                  int gap,
                                  bool (*acceptY)(void *, int),
                                  int *resolvedWidth = 0)
      {
        if (!box)
          return state.y;
        const int width = box->props.effectiveWidth();
        if (resolvedWidth)
          *resolvedWidth = width;
        const int padding = box->props.padding.resolve(0, spacing.standardWindowMargin());
        const int height = box->props.height;
        // Box-only preflight (#950 page 4); framed layout shares the helper and keeps its own rules.
        // The helper computes the same extents through insetExtent, so both agree by construction.
        if (insetExtent(width > 0 ? width : state.width, padding, padding) > SHRT_MAX
            || insetExtent(height > 0 ? height : state.height, padding, padding) > SHRT_MAX)
          return state.y;
        if (acceptY && (!acceptY(context, state.x + padding) || !acceptY(context, state.y + padding)))
          return state.y;
        return computeInsetLayoutResultY(box->childrenHead(),
                                         box->childrenCount(),
                                         FrameInsets(padding),
                                         width > 0,
                                         height > 0,
                                         width,
                                         height,
                                         state,
                                         context,
                                         layoutChild,
                                         gap,
                                         acceptY);
      }

    } // namespace layout
  } // namespace app
} // namespace loka

#endif // LOKA_APP_LAYOUT_BOX_LAYOUT_HPP
