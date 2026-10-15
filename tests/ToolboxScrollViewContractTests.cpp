#include "ToolboxScrollViewContractTests.hpp"

#include "support/TestVerify.hpp"
#include "app/nodes/nestable/ScrollView.hpp"

#include "../apple/toolbox/src/ToolboxScrollViewDecisions.hpp"

void testToolboxScrollViewChildWidthAlwaysReservesScrollbar()
{
  LOKA_VERIFY(ToolboxScrollViewChildWidth(100) == 84);
  LOKA_VERIFY(ToolboxScrollViewChildWidth(16) == 0);
  LOKA_VERIFY(ToolboxScrollViewChildWidth(8) == 0);
}

void testToolboxScrollViewHitIntersectionGatesViewport()
{
  const ToolboxScrollViewRect viewport(10, 20, 110, 120);
  ToolboxScrollViewRect result;

  LOKA_VERIFY(ToolboxScrollViewIntersectHitRect(
      ToolboxScrollViewRect(20, 30, 40, 50), viewport, result));
  LOKA_VERIFY(result.left == 20 && result.top == 30 &&
              result.right == 40 && result.bottom == 50);

  LOKA_VERIFY(ToolboxScrollViewIntersectHitRect(
      ToolboxScrollViewRect(0, 10, 30, 40), viewport, result));
  LOKA_VERIFY(result.left == 10 && result.top == 20 &&
              result.right == 30 && result.bottom == 40);

  LOKA_VERIFY(!ToolboxScrollViewIntersectHitRect(
      ToolboxScrollViewRect(0, 20, 10, 40), viewport, result));
  LOKA_VERIFY(!ToolboxScrollViewIntersectHitRect(
      ToolboxScrollViewRect(20, 120, 40, 140), viewport, result));
}

namespace
{
  void countClampWrites(void *context)
  {
    ++*static_cast<int *>(context);
  }
} // namespace

void testToolboxScrollViewOffsetClampRepublishesOnlyChangedFacts()
{
  using loka::core::Frame;
  loka::core::MutableState<Frame> storage;
  loka::app::scene::NodeState<Frame> fact(&storage, 0);
  loka::app::ScrollViewNode node((loka::app::ScrollViewProps(fact)));
  const int requested[] = {-7, 75, 50000, 40};
  const int expected[] = {0, 75, 200, 0};
  for (unsigned i = 0; i < 4; ++i)
  {
    fact.set(Frame(0, requested[i], 0, 0));
    int writes = 0;
    storage.bind(&countClampWrites, &writes, false);
    const ToolboxScrollViewMetrics metrics =
        ToolboxScrollViewResolveMetrics(i == 3 ? 120 : 300, i == 3 ? 200 : 100, requested[i]);
    LOKA_VERIFY(metrics.maximum == (i == 3 ? 0 : 200));
    LOKA_VERIFY(metrics.clampedOffset == expected[i]);
    node.publishOrigin(0, metrics.clampedOffset);
    LOKA_VERIFY(writes == (i == 1 ? 0 : 1));
    LOKA_VERIFY(fact.get().y == expected[i]);
    storage.unbind(&countClampWrites, &writes);
  }
}

void testToolboxScrollViewRangeMapsContentMinusViewport()
{
  LOKA_VERIFY(ToolboxScrollViewMaximum(300, 100) == 200);
  LOKA_VERIFY(ToolboxScrollViewMaximum(100, 100) == 0);
  LOKA_VERIFY(ToolboxScrollViewMaximum(80, 100) == 0);
}

void testToolboxScrollViewContentRectExcludesScrollbar()
{
  const ToolboxScrollViewRect content = ToolboxScrollViewContentRect(ToolboxScrollViewRect(10, 20, 110, 120));
  LOKA_VERIFY(content.left == 10 && content.top == 20 && content.right == 94 && content.bottom == 120);
  const ToolboxScrollViewRect narrow = ToolboxScrollViewContentRect(ToolboxScrollViewRect(10, 20, 18, 120));
  LOKA_VERIFY(narrow.left == 10 && narrow.right == 10);
  const ToolboxScrollViewRect empty = ToolboxScrollViewContentRect(ToolboxScrollViewRect(10, 20, 10, 20));
  LOKA_VERIFY(empty.left == 10 && empty.right == 10 && empty.top == 20 && empty.bottom == 20);
}
