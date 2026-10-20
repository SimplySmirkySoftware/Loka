#ifndef LOKA_APP_LAYOUT_ZSTACK_LAYOUT_HPP
#define LOKA_APP_LAYOUT_ZSTACK_LAYOUT_HPP

#include "app/nodes/nestable/ZStack.hpp"
#include "dsl/composition/CompositionList.hpp"

namespace loka
{
  namespace app
  {
    namespace layout
    {
      /** Band extent only: children retain their natural layout. A collapsed
          Show has no band. Shared by Null/Win32/macOS and the traversal twin
          apple/toolbox/src/ToolboxZStack.cpp. O(1) per upper child. */
      inline int zStackUnitBottom(ZStackUnitGround ground, unsigned index, int top, int childBottom, int groundBottom)
      {
        return ground == ZSTACK_UNIT_GROUND_EXP_STATIC_BLUR && index == 1 && childBottom > top
                       && childBottom < groundBottom
                   ? groundBottom
                   : childBottom;
      }

      // Deliberate platform-seam twins: Win32ScenePlatformController and
      // MacScenePlatformController own this
      // seat/max-bottom loop to insert opaque native units for upper children.
      template <typename LayoutStateT>
      int computeZStackLayoutResultY(loka::app::ZStackNode *stack,
                                     const LayoutStateT &state,
                                     void *context,
                                     int (*layoutChild)(void *, loka::app::scene::Node *, const LayoutStateT &))
      {
        if (!stack)
        {
          return state.y;
        }

        int maxY = state.y;
        if (loka::app::scene::INestable *nestable = stack->asNestable())
        {
          loka::dsl::CompositionCursor<loka::app::scene::Node> it(nestable->childrenHead(), nestable->childrenCount());
          for (loka::app::scene::Node *child = it.next(); child; child = it.next())
          {
            LayoutStateT childState = state;
            const int childY = layoutChild(context, child, childState);
            if (childY > maxY)
            {
              maxY = childY;
            }
          }
        }
        return maxY;
      }
    } // namespace layout
  } // namespace app
} // namespace loka

#endif // LOKA_APP_LAYOUT_ZSTACK_LAYOUT_HPP
