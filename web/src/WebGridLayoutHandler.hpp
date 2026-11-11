#ifndef LOKA_WEB_GRID_LAYOUT_HANDLER_HPP
#define LOKA_WEB_GRID_LAYOUT_HANDLER_HPP
#include "app/layout/PlatformBuiltinLayoutHandlers.hpp"
/** Registry-owned Web policy for admission of the offered cell, with short
    layout state and zero gaps. No controller or visit state is retained.
    Cost: controller allocates one at construction; registry owns and searches
    its handler entries once for registration. */
class WebGridLayoutHandler : public loka::app::scene::IPlatformLayoutHandler
{
public:
  /** Cost: registry lookup per visit; fixed Grid identity, no child walk. */
  virtual const void *nodeTypeKey() const
  {
    return loka::app::scene::NodeTypeToken<loka::app::GridNode>();
  }
  /** Cost: once per reached visit; common computation walks this Grid's children
      up to min(childCount, rows * cols), stopping at the first refused cell. */
  virtual int layoutNode(loka::app::scene::Node *node,
                         const loka::app::scene::LayoutState &state,
                         loka::app::scene::IPlatformLayoutTraversal *traversal,
                         const loka::app::layout::LazyWindow * = 0,
                         loka::app::layout::StackSpans * = 0);
};
#endif
