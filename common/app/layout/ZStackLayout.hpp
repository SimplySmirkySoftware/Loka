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
