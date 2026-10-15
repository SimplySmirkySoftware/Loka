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
      template <typename LayoutStateT>
      int computeInsetLayoutResultY(loka::app::scene::Node *childrenHead,
                                    size_t childCount,
                                    const FrameInsets &padding,
                                    bool hasFixedSize,
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
        childState.width =
            layoutCoordinate<LayoutStateT>((hasFixedSize ? fixedWidth : state.width) - padding.left - padding.right);
        if (childState.width < 0)
        {
          childState.width = 0;
        }
        childState.height =
            layoutCoordinate<LayoutStateT>((hasFixedSize ? fixedHeight : state.height) - padding.top - padding.bottom);
        if (childState.height < 0)
        {
          childState.height = 0;
        }

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
        return hasFixedSize ? state.y + fixedHeight : resultY;
      }
      template <typename LayoutStateT>
      int computeBoxLayoutResultY(loka::app::BoxNode *box,
                                  const LayoutStateT &state,
                                  void *context,
                                  int (*layoutChild)(void *, loka::app::scene::Node *, const LayoutStateT &),
                                  int gap,
                                  bool (*acceptY)(void *, int))
      {
        if (!box)
          return state.y;
        return computeInsetLayoutResultY(box->childrenHead(),
                                         box->childrenCount(),
                                         FrameInsets(box->props.padding),
                                         box->props.hasFixedSize(),
                                         box->props.effectiveWidth(),
                                         box->props.height,
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
