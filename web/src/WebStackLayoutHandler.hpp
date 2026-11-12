#ifndef LOKA_WEB_STACK_LAYOUT_HANDLER_HPP
#define LOKA_WEB_STACK_LAYOUT_HANDLER_HPP
#include "WebScenePlatformController.hpp"
#include "app/layout/PlatformBuiltinLayoutHandlers.hpp"
/** Registry-owned Stack policy; the creating controller outlives this borrowed
    reference. Estimates exist only during a visit, with no retained typography. */
class WebStackLayoutHandler : public loka::app::scene::IPlatformLayoutHandler
{
public:
  /** Cost: controller construction once; borrows that controller, no rows. */
  explicit WebStackLayoutHandler(WebScenePlatformController &controller)
      : controller_(controller)
  {
  }
  /** Cost: registry lookup per visit; fixed type identity, no rows. */
  virtual const void *nodeTypeKey() const
  {
    return loka::app::scene::NodeTypeToken<loka::app::StackNode>();
  }
  /** Cost: once per reached visit; aligned rows fetch one estimate, then common
      layout walks this Stack's children. Estimate refusal degrades in O(1);
      recovery requests the existing Scene-root walk after the current run. */
  virtual int layoutNode(loka::app::scene::Node *node,
                         const loka::app::scene::LayoutState &state,
                         loka::app::scene::IPlatformLayoutTraversal *traversal,
                         const loka::app::layout::LazyWindow *range = 0,
                         loka::app::layout::StackSpans *spans = 0);

private:
  WebScenePlatformController &controller_;
};
#endif
