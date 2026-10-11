#ifndef LOKA_TOOLBOX_PLATFORM_LAYOUT_HANDLERS_HPP
#define LOKA_TOOLBOX_PLATFORM_LAYOUT_HANDLERS_HPP

#include "app/scene/projection/PlatformLayoutHandler.hpp"

namespace loka
{
  namespace app
  {
    class StackNode;
    class BoxNode;
    class GridNode;
  }
} // namespace loka

/** Applies common Box layout while preserving Toolbox's width/result-Y channels. */
int ComputeToolboxBoxLayout(loka::app::BoxNode *box,
                            const loka::app::scene::LayoutState &state,
                            loka::app::scene::IPlatformLayoutTraversal *traversal);

/** Applies Toolbox Row layout through the supplied traversal. */
int ComputeToolboxRowLayout(loka::app::StackNode *row,
                            const loka::app::scene::LayoutState &state,
                            loka::app::scene::IPlatformLayoutTraversal *traversal);

/** Applies Toolbox Grid layout through the supplied traversal. */
int ComputeToolboxGridLayout(loka::app::GridNode *grid,
                             const loka::app::scene::LayoutState &state,
                             loka::app::scene::IPlatformLayoutTraversal *traversal);

/**
  Applies a registered Toolbox layout handler and commits both of its result
  channels to the caller's layout state.
*/
bool ApplyToolboxPlatformLayoutHandler(
    loka::app::scene::PlatformLayoutHandlerRegistry &registry,
    loka::app::scene::Node &node,
    loka::app::scene::LayoutState &state,
    loka::app::scene::IPlatformLayoutTraversal &traversal,
    short &width,
    const loka::app::layout::LazyWindow *range = 0,
    loka::app::layout::StackSpans *spans = 0);

bool RegisterToolboxPlatformLayoutHandlers(loka::app::scene::PlatformLayoutHandlerRegistry &registry);

#endif // LOKA_TOOLBOX_PLATFORM_LAYOUT_HANDLERS_HPP
