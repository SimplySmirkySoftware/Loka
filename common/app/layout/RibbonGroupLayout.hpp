#ifndef LOKA_APP_LAYOUT_RIBBON_GROUP_LAYOUT_HPP
#define LOKA_APP_LAYOUT_RIBBON_GROUP_LAYOUT_HPP

#include "app/layout/BoxLayout.hpp"
#include "app/nodes/controls/Ribbon.hpp"

namespace loka
{
  namespace app
  {
    namespace layout
    {
      template <typename LayoutStateT>
      int computeRibbonGroupLayoutResultY(loka::app::RibbonGroupNode *group,
                                          const LayoutStateT &state,
                                          void *context,
                                          int (*layoutChild)(void *, loka::app::scene::Node *, const LayoutStateT &),
                                          bool (*acceptY)(void *, int))
      {
        if (!group)
          return state.y;
        const int resultY = computeInsetLayoutResultY(group->childrenHead(),
                                                      group->childrenCount() ? 1 : 0,
                                                      RibbonGroupNode::kFrameInset,
                                                      false,
                                                      0,
                                                      0,
                                                      state,
                                                      context,
                                                      layoutChild,
                                                      0,
                                                      acceptY);
        const int width = state.width;
        const int height = resultY - state.y;
        loka::dsl::CompositionCursor<loka::app::scene::Node> children(group->childrenHead(), group->childrenCount());
        children.next(); // Content alone determines the outer extent.
        for (loka::app::scene::Node *child = children.next(); child; child = children.next())
        {
          RibbonGroupEdgeNode *edge = child->asRibbonGroupEdgeNode();
          if (!edge)
            continue;
          LayoutStateT strip = state;
          strip.width = 0;
          strip.height = 0;
          if (width >= 2 && height >= 2)
          {
            switch (edge->props.edge)
            {
            case RIBBON_GROUP_EDGE_LEFT:
              strip.width = 1;
              strip.height = layoutCoordinate<LayoutStateT>(height - 1);
              break;
            case RIBBON_GROUP_EDGE_RIGHT:
              strip.x = layoutCoordinate<LayoutStateT>(state.x + width - 1);
              strip.width = 1;
              strip.height = layoutCoordinate<LayoutStateT>(height - 1);
              break;
            case RIBBON_GROUP_EDGE_BOTTOM:
              strip.y = layoutCoordinate<LayoutStateT>(state.y + height - 1);
              strip.width = layoutCoordinate<LayoutStateT>(width);
              strip.height = 1;
              break;
            }
          }
          layoutChild(context, child, strip);
        }
        return resultY;
      }
    } // namespace layout
  } // namespace app
} // namespace loka

#endif // LOKA_APP_LAYOUT_RIBBON_GROUP_LAYOUT_HPP
