#include "ToolboxScrollViewContractTests.hpp"

#include "support/TestVerify.hpp"
#include <climits>
#include "app/nodes/nestable/ScrollView.hpp"

#include "../apple/toolbox/src/ToolboxScrollViewDecisions.hpp"
#include "../apple/toolbox/src/ToolboxWindowGeometry.hpp"

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
  const ToolboxScrollViewRect content = ToolboxScrollViewContentRect(ToolboxScrollViewRect(10, 20, 110, 120), false);
  LOKA_VERIFY(content.left == 10 && content.top == 20 && content.right == 94 && content.bottom == 120);
  const ToolboxScrollViewRect narrow = ToolboxScrollViewContentRect(ToolboxScrollViewRect(10, 20, 18, 120), false);
  LOKA_VERIFY(narrow.left == 10 && narrow.right == 10);
  const ToolboxScrollViewRect empty = ToolboxScrollViewContentRect(ToolboxScrollViewRect(10, 20, 10, 20), false);
  LOKA_VERIFY(empty.left == 10 && empty.right == 10 && empty.top == 20 && empty.bottom == 20);
}

void testToolboxScrollViewTwoBarGeometry()
{
  const ToolboxScrollViewRect port(0, 0, 420, 260);
  const ToolboxScrollViewRect sizeBox = ToolboxWindowSizeBoxRect(port);
  LOKA_VERIFY(sizeBox.left == 405 && sizeBox.top == 245 && sizeBox.right == 420 && sizeBox.bottom == 260);
  const ToolboxScrollViewRect edgeBar =
      ToolboxScrollViewVerticalBarRect(port, false, sizeBox, ToolboxScrollViewFrameEdges(port, port, true));
  LOKA_VERIFY(edgeBar.left == 405 && edgeBar.top == -1 && edgeBar.right == 421 && edgeBar.bottom == 246);
  const ToolboxScrollViewRect marginSeat(12, 12, 408, 248);
  LOKA_VERIFY(
      ToolboxScrollViewVerticalBarRect(marginSeat, false, sizeBox, ToolboxScrollViewEdges(false, false, false, false))
          .bottom
      == 246);
  // Touching edges do not intersect; neither axis clips a distant bar.
  const ToolboxScrollViewRect touching(0, 0, 405, 260);
  LOKA_VERIFY(
      ToolboxScrollViewVerticalBarRect(touching, false, sizeBox, ToolboxScrollViewEdges(false, false, false, false))
          .bottom
      == 260);
  const ToolboxScrollViewRect emptyVertical(410, 250, 410, 260);
  LOKA_VERIFY(ToolboxScrollViewVerticalBarRect(
                  emptyVertical, false, sizeBox, ToolboxScrollViewEdges(false, false, false, false))
                  .bottom
              == 260);
  const ToolboxScrollViewRect emptyHorizontal(0, 250, 450, 250);
  LOKA_VERIFY(
      ToolboxScrollViewHorizontalBarRect(emptyHorizontal, sizeBox, ToolboxScrollViewEdges(false, false, false, false))
          .right
      == 434);
  const ToolboxScrollViewRect above(0, 0, 450, 245);
  LOKA_VERIFY(
      ToolboxScrollViewHorizontalBarRect(above, sizeBox, ToolboxScrollViewEdges(false, false, false, false)).right
      == 434);

  const ToolboxScrollViewRect noSizeBox;
  const ToolboxScrollViewRect seat(10, 20, 110, 54);
  const ToolboxScrollViewRect content = ToolboxScrollViewContentRect(seat, true);
  const ToolboxScrollViewRect vertical =
      ToolboxScrollViewVerticalBarRect(seat, true, noSizeBox, ToolboxScrollViewEdges(false, false, false, false));
  const ToolboxScrollViewRect horizontal =
      ToolboxScrollViewHorizontalBarRect(seat, noSizeBox, ToolboxScrollViewEdges(false, false, false, false));
  LOKA_VERIFY(content.left == 10 && content.top == 20 && content.right == 94 && content.bottom == 38);
  LOKA_VERIFY(vertical.left == 94 && vertical.top == 20 && vertical.right == 110 && vertical.bottom == 38);
  LOKA_VERIFY(horizontal.left == 10 && horizontal.top == 38 && horizontal.right == 94 && horizontal.bottom == 54);
  const ToolboxScrollViewRect fullVertical =
      ToolboxScrollViewVerticalBarRect(seat, false, noSizeBox, ToolboxScrollViewEdges(false, false, false, false));
  LOKA_VERIFY(fullVertical.left == 94 && fullVertical.top == 20 && fullVertical.right == 110
              && fullVertical.bottom == 54);
  ToolboxScrollViewRect out;
  const ToolboxScrollViewRect corner(94, 38, 110, 54);
  LOKA_VERIFY(!ToolboxScrollViewIntersectHitRect(vertical, horizontal, out));
  LOKA_VERIFY(!ToolboxScrollViewIntersectHitRect(vertical, corner, out));
  LOKA_VERIFY(!ToolboxScrollViewIntersectHitRect(horizontal, corner, out));
  for (int width = 0; width <= 16; ++width)
    for (int height = 0; height <= 16; ++height)
    {
      const ToolboxScrollViewRect small(10, 20, 10 + width, 20 + height);
      const ToolboxScrollViewRect c = ToolboxScrollViewContentRect(small, true);
      const ToolboxScrollViewRect v =
          ToolboxScrollViewVerticalBarRect(small, true, noSizeBox, ToolboxScrollViewEdges(false, false, false, false));
      const ToolboxScrollViewRect h =
          ToolboxScrollViewHorizontalBarRect(small, noSizeBox, ToolboxScrollViewEdges(false, false, false, false));
      LOKA_VERIFY(c.right >= c.left && c.bottom >= c.top);
      LOKA_VERIFY(v.right >= v.left && v.bottom >= v.top);
      LOKA_VERIFY(h.right >= h.left && h.bottom >= h.top);
    }
}

namespace
{
  struct ShortRect
  {
    short left, top, right, bottom;
  };

  void verifyScrollRect(const ToolboxScrollViewRect &actual, const ToolboxScrollViewRect &expected)
  {
    LOKA_VERIFY(actual.left == expected.left && actual.top == expected.top && actual.right == expected.right
                && actual.bottom == expected.bottom);
  }
} // namespace

void testToolboxScrollViewFrameContacts()
{
  const ToolboxScrollViewRect port(0, 0, 360, 260);
  for (unsigned mask = 0; mask != 16; ++mask)
  {
    const ToolboxScrollViewRect seat(mask & 1 ? 0 : 12, mask & 2 ? 0 : 12, mask & 4 ? 360 : 348, mask & 8 ? 260 : 248);
    for (unsigned present = 0; present != 2; ++present)
    {
      const ToolboxScrollViewEdges edges = ToolboxScrollViewFrameEdges(seat, port, present != 0);
      LOKA_VERIFY(edges.left == (present && (mask & 1)));
      LOKA_VERIFY(edges.top == (present && (mask & 2)));
      LOKA_VERIFY(edges.right == (present && (mask & 4)));
      LOKA_VERIFY(edges.bottom == (present && (mask & 8)));
    }
  }
  const ToolboxScrollViewRect box(345, 245, 360, 260);
  const ToolboxScrollViewRect seats[] = {port,
                                         ToolboxScrollViewRect(12, 12, 348, 248),
                                         port,
                                         ToolboxScrollViewRect(12, 12, 348, 248),
                                         ToolboxScrollViewRect(0, 0, 370, 260),
                                         port};
  const int widths[] = {345, 320, 345, 320, 354, 344};
  const ToolboxScrollViewRect contents[] = {ToolboxScrollViewRect(0, 0, 345, 260),
                                            ToolboxScrollViewRect(12, 12, 332, 248),
                                            ToolboxScrollViewRect(0, 0, 345, 245),
                                            ToolboxScrollViewRect(12, 12, 332, 232),
                                            ToolboxScrollViewRect(0, 0, 354, 245),
                                            ToolboxScrollViewRect(0, 0, 344, 244)};
  const ToolboxScrollViewRect verticals[] = {ToolboxScrollViewRect(345, -1, 361, 246),
                                             ToolboxScrollViewRect(332, 12, 348, 246),
                                             ToolboxScrollViewRect(345, -1, 361, 246),
                                             ToolboxScrollViewRect(332, 12, 348, 232),
                                             ToolboxScrollViewRect(354, -1, 370, 246),
                                             ToolboxScrollViewRect(344, 0, 360, 244)};
  const ToolboxScrollViewRect horizontals[] = {ToolboxScrollViewRect(-1, 245, 346, 261),
                                               ToolboxScrollViewRect(12, 232, 332, 248),
                                               ToolboxScrollViewRect(-1, 245, 346, 261),
                                               ToolboxScrollViewRect(0, 244, 344, 260)};
  for (unsigned i = 0; i != 6; ++i)
  {
    const bool horizontal = i >= 2;
    const ToolboxScrollViewEdges edges = ToolboxScrollViewFrameEdges(seats[i], port, i != 5);
    const ToolboxScrollViewRect sizeBox = i != 5 ? box : ToolboxScrollViewRect();
    LOKA_VERIFY(ToolboxScrollViewChildWidth(seats[i], edges) == widths[i]);
    const ToolboxScrollViewRect content =
        ToolboxScrollViewContentRect(ToolboxScrollViewFrameRect(seats[i], horizontal, edges), horizontal);
    verifyScrollRect(content, contents[i]);
    verifyScrollRect(ToolboxScrollViewVerticalBarRect(seats[i], horizontal, sizeBox, edges), verticals[i]);
    if (horizontal)
      verifyScrollRect(ToolboxScrollViewHorizontalBarRect(seats[i], sizeBox, edges), horizontals[i - 2]);
    if (i == 0 || i == 2)
      LOKA_VERIFY(content.bottom - content.top - 1 == (i == 0 ? 259 : 244));
  }
  // No size box: bars remain disjoint and leave the corner as window ground.
  const ToolboxScrollViewEdges edges = ToolboxScrollViewFrameEdges(port, port, true);
  const ToolboxScrollViewRect absent;
  verifyScrollRect(ToolboxScrollViewVerticalBarRect(port, true, absent, edges),
                   ToolboxScrollViewRect(345, -1, 361, 245));
  verifyScrollRect(ToolboxScrollViewHorizontalBarRect(port, absent, edges), ToolboxScrollViewRect(-1, 245, 345, 261));
  // No port and no size box: vertical-only content also stays at its old bottom.
  const ToolboxScrollViewEdges none = ToolboxScrollViewFrameEdges(port, port, false);
  verifyScrollRect(ToolboxScrollViewContentRect(ToolboxScrollViewFrameRect(port, false, none), false),
                   ToolboxScrollViewRect(0, 0, 344, 260));
  verifyScrollRect(ToolboxScrollViewVerticalBarRect(port, false, absent, none),
                   ToolboxScrollViewRect(344, 0, 360, 260));
}

void testToolboxScrollViewSizeBoxSharedLines()
{
  // Native rects use shorts; the one-pixel contact probe must not wrap.
  const ShortRect shortSeat = {0, 0, SHRT_MAX, SHRT_MAX};
  const ShortRect shortBox = {SHRT_MAX - 15, SHRT_MAX - 15, SHRT_MAX, SHRT_MAX};
  const ShortRect vertical =
      ToolboxScrollViewVerticalBarRect(shortSeat, false, shortBox, ToolboxScrollViewEdges(false, false, false, false));
  LOKA_VERIFY(vertical.bottom == SHRT_MAX - 14);

  const ToolboxScrollViewEdges none(false, false, false, false);
  const ToolboxScrollViewRect seat(0, 0, 100, 100);
  // Ending on the box line, intersecting it, and stopping one pixel apart.
  const int starts[] = {100, 99, 101};
  const int verticalEnds[] = {101, 100, 100};
  const int horizontalEnds[] = {85, 84, 84};
  for (unsigned i = 0; i != 3; ++i)
  {
    verifyScrollRect(
        ToolboxScrollViewVerticalBarRect(seat, false, ToolboxScrollViewRect(90, starts[i], 110, 120), none),
        ToolboxScrollViewRect(84, 0, 100, verticalEnds[i]));
    verifyScrollRect(
        ToolboxScrollViewHorizontalBarRect(seat, ToolboxScrollViewRect(starts[i] - 16, 90, 120, 110), none),
        ToolboxScrollViewRect(0, 84, horizontalEnds[i], 100));
  }
  verifyScrollRect(ToolboxScrollViewVerticalBarRect(seat, false, ToolboxScrollViewRect(90, -10, 110, 120), none),
                   ToolboxScrollViewRect(84, 0, 100, 0));
  verifyScrollRect(ToolboxScrollViewHorizontalBarRect(seat, ToolboxScrollViewRect(-10, 90, 120, 110), none),
                   ToolboxScrollViewRect(0, 84, 0, 100));
}
