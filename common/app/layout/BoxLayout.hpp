#ifndef LOKA_APP_LAYOUT_BOX_LAYOUT_HPP
#define LOKA_APP_LAYOUT_BOX_LAYOUT_HPP

#include "app/nodes/nestable/Box.hpp"
#include "app/nodes/controls/Ribbon.hpp"
#include "app/layout/LayoutHeuristics.hpp"
#include "dsl/composition/CompositionList.hpp"

namespace loka
{
  namespace app
  {
    namespace layout
    {
      template <typename LayoutStateT>
      int computeInsetLayoutResultY(loka::app::scene::INestable *nestable,
                                    int padding,
                                    bool hasFixedSize,
                                    int fixedWidth,
                                    int fixedHeight,
                                    const LayoutStateT &state,
                                    void *context,
                                    int (*layoutChild)(void *, loka::app::scene::Node *, const LayoutStateT &))
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

        int resultY = childState.y;
        if (nestable)
        {
          loka::dsl::CompositionCursor<loka::app::scene::Node> it(nestable->childrenHead(), nestable->childrenCount());
          for (loka::app::scene::Node *child = it.next(); child; child = it.next())
          {
            childState.y = layoutCoordinate<LayoutStateT>(layoutChild(context, child, childState));
          }
          resultY = childState.y + padding;
        }
        else
        {
          resultY = state.y + padding * 2;
        }
        return hasFixedSize ? state.y + fixedHeight : resultY;
      }
      template <typename LayoutStateT>
      int computeBoxLayoutResultY(loka::app::BoxNode *box,
                                  const LayoutStateT &state,
                                  void *context,
                                  int (*layoutChild)(void *, loka::app::scene::Node *, const LayoutStateT &))
      {
        if (!box)
          return state.y;
        return computeInsetLayoutResultY(box->asNestable(),
                                         box->props.padding,
                                         box->props.hasFixedSize(),
                                         box->props.effectiveWidth(),
                                         box->props.height,
                                         state,
                                         context,
                                         layoutChild);
      }

      template <typename LayoutStateT>
      int computeRibbonGroupLayoutResultY(loka::app::RibbonGroupNode *group,
                                          const LayoutStateT &state,
                                          void *context,
                                          int (*layoutChild)(void *, loka::app::scene::Node *, const LayoutStateT &))
      {
        if (!group)
          return state.y;
        return computeInsetLayoutResultY(
            group->asNestable(), RibbonGroupNode::kFrameInset, false, 0, 0, state, context, layoutChild);
      }
    } // namespace layout
  } // namespace app
} // namespace loka

#endif // LOKA_APP_LAYOUT_BOX_LAYOUT_HPP
