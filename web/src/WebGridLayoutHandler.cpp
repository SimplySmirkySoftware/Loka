#include "WebGridLayoutHandler.hpp"
using namespace loka::app;
using namespace loka::app::scene;
int WebGridLayoutHandler::layoutNode(Node *node,
                                     const LayoutState &state,
                                     IPlatformLayoutTraversal *traversal,
                                     const layout::LazyWindow *,
                                     layout::StackSpans *)
{
  GridNode *grid = node ? node->asGridNode() : 0;
  if (!grid || !traversal)
    return state.y;
  const layout::GridLayoutMetrics metrics;
  return layout::computeGridLayoutResultY(
      grid, state, metrics, traversal, &layout::DispatchTraversalLayoutChild, &layout::AcceptTraversalLayoutY);
}
