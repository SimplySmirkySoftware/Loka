#include "WebStackLayoutHandler.hpp"
using namespace loka::app;
using namespace loka::app::scene;
int WebStackLayoutHandler::layoutNode(Node *node,
                                      const LayoutState &state,
                                      IPlatformLayoutTraversal *traversal,
                                      const layout::LazyWindow *range,
                                      layout::StackSpans *spans)
{
  StackNode *stack = node ? node->asStackNode() : 0;
  if (!stack || !traversal)
    return state.y;
  layout::RowLayoutMetrics metrics;
  if (stack->props.effectiveAxis() == STACK_AXIS_ROW && stack->props.hasVerticalAlignment_)
  {
    short line = 0, button = 0;
    const int status = WebTextLayout::rowEstimate(line, button);
    if (status != WEB_MEASURE_OK)
    {
      if (status == WEB_MEASURE_REFUSED)
        this->controller_.requestRelayout();
    }
    else
    {
      metrics.textHeight = metrics.fallbackHeight = line;
      metrics.buttonHeight = button;
    }
  }
  return layout::LayoutStack(stack, state, metrics, this->controller_.spacing(), traversal, range, spans);
}
