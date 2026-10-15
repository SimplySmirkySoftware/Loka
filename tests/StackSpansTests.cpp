#include "app/layout/ColumnLayout.hpp"
#include "support/LayoutY.hpp"
#include "StackSpansTests.hpp"
#include "support/TestVerify.hpp"
#include "support/LokaAllocFailure.hpp"
#include "app/layout/StackSpans.hpp"
#include "app/nodes/nestable/ScrollView.hpp"
#include "app/nodes/boundary/StdComposition.hpp"
#include "app/nodes/controls/Button.hpp"
#include "platform/null/NullScenePlatformController.hpp"
#include "platform/null/context/NullTextContext.hpp"
#include "app/nodes/AttributedText.hpp"

namespace
{
  using namespace loka::app;
  using namespace loka::app::scene;
  using namespace loka::app::layout;
  using namespace loka::core;

  struct SpanTraversal
  {
    NullScenePlatformController &platform;
    int *origins;
    unsigned calls;
    SpanTraversal(NullScenePlatformController &p, int *o)
        : platform(p),
          origins(o),
          calls(0)
    {
    }
    static int child(void *context, Node *node, const LayoutState &state)
    {
      SpanTraversal &self = *static_cast<SpanTraversal *>(context);
      if (self.origins)
        self.origins[self.calls] = state.y;
      ++self.calls;
      return self.platform.projectLayoutForTesting(node, state);
    }
  };

  int layoutSpanColumn(NullScenePlatformController &platform,
                       StackNode *column,
                       const LayoutState &state,
                       const LazyWindow *range,
                       StackSpans &spans,
                       int *origins = 0)
  {
    SpanTraversal traversal(platform, origins);
    return computeColumnLayoutResultY(
        column, state, &traversal, &SpanTraversal::child, 3, &loka::testing::acceptShortLayoutY, range, &spans);
  }

  void window(const StackSpans &spans, int top, int bottom, unsigned first, unsigned count)
  {
    const LazyWindow result = spans.indicesIn(top, bottom);
    LOKA_VERIFY(result.first == first);
    LOKA_VERIFY(result.count == count);
  }
} // namespace

void testStackSpansSelection()
{
  StackSpans spans;
  LOKA_VERIFY(!spans.valid());
  LOKA_VERIFY(spans.begin(5));
  LOKA_VERIFY(spans.append(0, 10));
  LOKA_VERIFY(spans.append(10, 10));
  LOKA_VERIFY(spans.append(10, 25));
  LOKA_VERIFY(spans.append(25, 40));
  LOKA_VERIFY(spans.append(40, 40));
  LOKA_VERIFY(!spans.valid());
  LOKA_VERIFY(spans.finish());
  LOKA_VERIFY(spans.total() == 40);
  window(spans, 0, 10, 0, 1);
  window(spans, 10, 25, 2, 1);
  window(spans, 9, 11, 0, 3);
  window(spans, 25, 40, 3, 1);
  window(spans, 39, 41, 3, 2);
  window(spans, 40, 50, 0, 0);
  window(spans, 12, 12, 0, 0);
  window(spans, -20, 0, 0, 0);
  spans.placed(Frame(0, 5, 80, 20));
  LOKA_VERIFY(spans.placedViewport().y == 5);
  spans.invalidate();
  LOKA_VERIFY(!spans.valid());
  LOKA_VERIFY(spans.placedViewport().height == 0);
  LOKA_VERIFY(spans.begin(0));
  LOKA_VERIFY(spans.finish());
  window(spans, 0, 20, 0, 0);
  LOKA_VERIFY(spans.begin(1));
  LOKA_VERIFY(!spans.append(0, SHRT_MAX + 1));
  LOKA_VERIFY(!spans.valid());
}

void testStackSpansAllocationRefusal()
{
  {
    StackSpans spans;
    loka::core::testing::failLokaAllocRaw("TextLineBreaker", "Table", 1);
    const bool built = spans.begin(100);
    loka::core::testing::failLokaAllocRaw("TextLineBreaker", "Table", 0);
    LOKA_VERIFY(!built);
    LOKA_VERIFY(!spans.valid());
    LOKA_VERIFY(spans.begin(100));
    for (int i = 0; i < 100; ++i)
      LOKA_VERIFY(spans.append(i, i + 1));
    LOKA_VERIFY(spans.finish());
  }
  loka::core::testing::allowLokaAllocRaw();
}

void testStackSpansColumnCollection()
{
  NullScenePlatformController platform;
  ScrollViewNode scroll((ScrollViewProps()));
  StackNode *column = new StackNode(StackProps(STACK_AXIS_COLUMN));
  scroll.addChild(column);
  for (int i = 0; i < 4; ++i)
    column->addChild(Text("text").create());
  LayoutState state;
  state.x = 3;
  state.y = 7;
  state.width = 100;
  state.height = 80;
  // Fixture gap 3 is supplied to the Column, not to leaf advances.
  StackSpans spans;
  int origins[4];
  LOKA_VERIFY(layoutSpanColumn(platform, column, state, 0, spans, origins) == 64);
  LOKA_VERIFY(spans.valid());
  LOKA_VERIFY(spans.size() == 4);
  unsigned index = 0;
  for (Node *child = column->childrenHead(); child; child = child->nextInComposition, ++index)
  {
    NullTextContext *context = static_cast<NullTextContext *>(child->getContext());
    LOKA_VERIFY(context != 0);
    // Each span starts at the previous bottom; its gap precedes this child.
    LOKA_VERIFY(spans.start(index) == origins[index] - state.y - (index ? 3 : 0));
    LOKA_VERIFY(spans.end(index) == origins[index] - state.y + context->measurement().height());
  }
  LOKA_VERIFY(spans.total() == 57);
  const unsigned long before = platform.leafLayoutVisitsForTesting();
  const LazyWindow range = {1, 2};
  int restarted[2];
  layoutSpanColumn(platform, column, state, &range, spans, restarted);
  LOKA_VERIFY(platform.leafLayoutVisitsForTesting() - before == 2);
  LOKA_VERIFY(restarted[0] == origins[1] && restarted[1] == origins[2]);
  // A changed visited advance repairs all four leaves in this same call.
  TextDefinition taller(TextProps("text\ntext"));
  LOKA_VERIFY(taller.applyPropsToNode(column->childrenHead()->nextInComposition));
  const unsigned long beforeRepair = platform.leafLayoutVisitsForTesting();
  layoutSpanColumn(platform, column, state, &range, spans);
  LOKA_VERIFY(platform.leafLayoutVisitsForTesting() - beforeRepair == 5);
  LOKA_VERIFY(spans.valid() && spans.total() == 69);
  column->addChild(new TextNode(TextProps("new child")));
  const unsigned long beforeStructure = platform.leafLayoutVisitsForTesting();
  layoutSpanColumn(platform, column, state, &range, spans);
  LOKA_VERIFY(platform.leafLayoutVisitsForTesting() - beforeStructure == 5);
  LOKA_VERIFY(spans.valid() && spans.size() == 5 && spans.total() == 84);
  // A refused viewport must not leave a table from a prior successful pass.
  LayoutState refused = state;
  refused.y = SHRT_MAX;
  platform.projectLayoutForTesting(&scroll, refused, &range, &spans);
  LOKA_VERIFY(!spans.valid());
  platform.projectLayoutForTesting(&scroll, state, 0, &spans);
  LOKA_VERIFY(spans.valid());
  Node *detached = scroll.detachChildren();
  platform.projectLayoutForTesting(&scroll, state, &range, &spans);
  LOKA_VERIFY(!spans.valid());
  scroll.addChild(detached);
  platform.projectLayoutForTesting(&scroll, state, 0, &spans);
  LOKA_VERIFY(spans.valid());
  column->addChild(new ButtonNode(Button("button").props));
  platform.projectLayoutForTesting(&scroll, state, &range, &spans);
  LOKA_VERIFY(!spans.valid());
}

void testStackSpansEligibility()
{
  NullScenePlatformController platform;
  StackNode column((StackProps(STACK_AXIS_COLUMN)));
  column.addChild(new TextNode(TextProps("text")));
  column.addChild(new AttributedTextNode(AttributedTextProps(Styled(String::Literal("styled"), TextStyle()))));
  LOKA_VERIFY(mayBandColumn(&column));
  column.addChild(new ButtonNode(Button("button").props));
  LOKA_VERIFY(!mayBandColumn(&column));
  StackNode row((StackProps(STACK_AXIS_ROW)));
  LOKA_VERIFY(!mayBandColumn(&row));
}

void testScrollViewOwnLayoutInputs()
{
  NullScenePlatformController platform;
  MutableState<loka::core::Frame> storage(loka::core::Frame(0, 0, 0, 0));
  PushStateTracker tracker;
  tracker.addState(&storage);
  NodeState<loka::core::Frame> offset(&storage, &tracker);
  StdCompositionNode owner((StdCompositionProps()));
  ScrollViewNode scroll((ScrollViewProps(offset)));
  scroll.setPropsTypeId(ScrollViewProps::staticTypeId());
  BoundaryNode::declareBoundaryDirtySources(&scroll, &owner);
  scroll.takeLayoutInputs();
  scroll.publishOrigin(0, 10);
  LOKA_VERIFY(scroll.takeLayoutInputs() == NODE_DIRTY_LAYOUT);
  LOKA_VERIFY(scroll.takeLayoutInputs() == NODE_DIRTY_NONE);
  ScrollViewDefinition next((ScrollViewProps(offset)));
  LOKA_VERIFY(next.applyPropsToNode(&scroll));
  LOKA_VERIFY(scroll.takeLayoutInputs() == (NODE_DIRTY_PROPS | NODE_DIRTY_LAYOUT));
  LOKA_VERIFY(scroll.takeLayoutInputs() == NODE_DIRTY_NONE);
  owner.clearObservedStateEntries();
}
