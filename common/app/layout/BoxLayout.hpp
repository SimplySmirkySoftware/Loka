#ifndef LOKA_APP_LAYOUT_BOX_LAYOUT_HPP
#define LOKA_APP_LAYOUT_BOX_LAYOUT_HPP

#include "app/nodes/nestable/Box.hpp"
#include "app/layout/LayoutHeuristics.hpp"
#include "app/layout/FallbackControlMetrics.hpp"
#include "dsl/composition/CompositionList.hpp"

namespace loka
{
  namespace app
  {
    namespace layout
    {
      template <typename LayoutStateT>
      int computeInsetLayoutResultY(loka::app::scene::Node *childrenHead,
                                    size_t childCount,
                                    int padding,
                                    bool hasFixedSize,
                                    int fixedWidth,
                                    int fixedHeight,
                                    const LayoutStateT &state,
                                    void *context,
                                    int (*layoutChild)(void *, loka::app::scene::Node *, const LayoutStateT &),
                                    int gap = FallbackControlMetrics::kVerticalSpacing)
      {
        LayoutStateT childState = state;
        childState.x = layoutCoordinate<LayoutStateT>(state.x + padding);
        childState.y = layoutCoordinate<LayoutStateT>(state.y + padding);
        childState.width = layoutCoordinate<LayoutStateT>((hasFixedSize ? fixedWidth : state.width) - padding * 2);
        if (childState.width < 0)
        {
          childState.width = 0;
        }
        childState.height = layoutCoordinate<LayoutStateT>((hasFixedSize ? fixedHeight : state.height) - padding * 2);
        if (childState.height < 0)
        {
          childState.height = 0;
        }

        bool seatSeen = false;
        loka::dsl::CompositionCursor<loka::app::scene::Node> it(childrenHead, childCount);
        for (loka::app::scene::Node *child = it.next(); child; child = it.next())
        {
          const bool takesSeat = childTakesLayoutSeat(child);
          if (takesSeat && seatSeen)
            childState.y = layoutCoordinate<LayoutStateT>(childState.y + gap);
          seatSeen = seatSeen || takesSeat;
          childState.y = layoutCoordinate<LayoutStateT>(layoutChild(context, child, childState));
        }
        const int resultY = childState.y + padding;
        return hasFixedSize ? state.y + fixedHeight : resultY;
      }
      template <typename LayoutStateT>
      int computeBoxLayoutResultY(loka::app::BoxNode *box,
                                  const LayoutStateT &state,
                                  void *context,
                                  int (*layoutChild)(void *, loka::app::scene::Node *, const LayoutStateT &),
                                  int gap = FallbackControlMetrics::kVerticalSpacing)
      {
        if (!box)
          return state.y;
        return computeInsetLayoutResultY(box->childrenHead(),
                                         box->childrenCount(),
                                         box->props.padding,
                                         box->props.hasFixedSize(),
                                         box->props.effectiveWidth(),
                                         box->props.height,
                                         state,
                                         context,
                                         layoutChild,
                                         gap);
      }

    } // namespace layout
  } // namespace app
} // namespace loka

#endif // LOKA_APP_LAYOUT_BOX_LAYOUT_HPP
