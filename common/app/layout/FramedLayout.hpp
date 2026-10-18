#ifndef LOKA_APP_LAYOUT_FRAMED_LAYOUT_HPP
#define LOKA_APP_LAYOUT_FRAMED_LAYOUT_HPP

#include "app/layout/BoxLayout.hpp"
#include "app/nodes/controls/Ribbon.hpp"
#include "app/nodes/controls/Card.hpp"
#include "core/Frame.hpp"
#include <climits>

namespace loka
{
  namespace app
  {
    namespace layout
    {
      template <typename LayoutStateT>
      int computeFramedLayoutResultY(loka::app::scene::Node *node,
                                     const LayoutStateT &state,
                                     void *context,
                                     int (*layoutChild)(void *, loka::app::scene::Node *, const LayoutStateT &),
                                     bool (*acceptY)(void *, int))
      {
        if (!node || !node->asFramedNode())
          return state.y;
        const FrameGeometry geometry = node->asFramedNode()->frameGeometry();
        loka::app::scene::INestable *group = node->asNestable();
        if (!group)
          return state.y;
        int resultY = computeInsetLayoutResultY(group->childrenHead(),
                                                group->childrenCount() ? 1 : 0,
                                                geometry.insets,
                                                false,
                                                false,
                                                0,
                                                0,
                                                state,
                                                context,
                                                layoutChild,
                                                0,
                                                acceptY);
        switch (geometry.placement)
        {
        case FRAME_PLACEMENT_CARD_BANDS:
        {
          if (resultY < state.y + geometry.minimumHeight)
            resultY = state.y + geometry.minimumHeight;
          const int left = state.x;
          const int right = state.x + state.width;
          const int top = state.y;
          const int bottom = resultY;
          const FrameInsets &i = geometry.insets;
          const loka::core::Frame bands[CARD_BAND_COUNT] = {
              loka::core::Frame(left, top, right - i.right - left, i.top),
              loka::core::Frame(left, top + i.top, i.left, bottom - top - i.top),
              loka::core::Frame(right - i.right, top, i.right, bottom - i.bottom - top),
              loka::core::Frame(left + i.left, bottom - i.bottom, right - left - i.left, i.bottom)};
          bool enabled = state.width >= geometry.minimumWidth;
          for (int band = 0; band < CARD_BAND_COUNT; ++band)
          {
            const loka::core::Frame &rect = bands[band];
            enabled = enabled && rect.x >= SHRT_MIN && rect.x <= SHRT_MAX && rect.y >= SHRT_MIN && rect.y <= SHRT_MAX
                      && rect.width >= 0 && rect.width <= SHRT_MAX && rect.height >= 0 && rect.height <= SHRT_MAX
                      && rect.x + rect.width >= SHRT_MIN && rect.x + rect.width <= SHRT_MAX
                      && rect.y + rect.height >= SHRT_MIN && rect.y + rect.height <= SHRT_MAX;
          }
          loka::dsl::CompositionCursor<loka::app::scene::Node> children(group->childrenHead(), group->childrenCount());
          children.next();
          for (loka::app::scene::Node *child = children.next(); child; child = children.next())
          {
            CardBandNode *band = child->asCardBandNode();
            if (!band)
              continue;
            LayoutStateT strip = state;
            strip.width = 0;
            strip.height = 0;
            if (enabled && band->props.role != CARD_BAND_COUNT)
            {
              const loka::core::Frame &rect = bands[band->props.role];
              strip.x = layoutCoordinate<LayoutStateT>(rect.x);
              strip.y = layoutCoordinate<LayoutStateT>(rect.y);
              strip.width = layoutCoordinate<LayoutStateT>(rect.width);
              strip.height = layoutCoordinate<LayoutStateT>(rect.height);
            }
            layoutChild(context, child, strip);
          }
          break;
        }
        case FRAME_PLACEMENT_RIBBON_GROUP_EDGES:
        {
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
          break;
        }
        }
        return resultY;
      }
    } // namespace layout
  } // namespace app
} // namespace loka

#endif // LOKA_APP_LAYOUT_FRAMED_LAYOUT_HPP
