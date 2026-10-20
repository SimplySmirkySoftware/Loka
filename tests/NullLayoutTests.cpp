#include <climits>
#include "app/nodes/nestable/ScrollView.hpp"
#include "app/nodes/nestable/Canvas.hpp"
#include "NullLayoutTests.hpp"
#include "support/TestVerify.hpp"

#include <cassert>

#include "app/layout/FallbackControlMetrics.hpp"
#include "app/nodes/nestable/Box.hpp"
#include "app/nodes/controls/Button.hpp"
#include "app/nodes/nestable/Fragment.hpp"
#include "app/nodes/nestable/Grid.hpp"
#include "app/nodes/nestable/RowColumn.hpp"
#include "app/nodes/nestable/ZStack.hpp"
#include "platform/null/NullScenePlatformController.hpp"

namespace
{
  class FixedLayoutProbeNode : public loka::app::scene::Node,
                               public loka::app::scene::IProjectedLayoutNode
  {
  public:
    explicit FixedLayoutProbeNode(short fixedHeight)
        : fixedHeight_(fixedHeight),
          wasLaidOut_(false),
          geometry_()
    {
    }

    virtual loka::app::scene::IProjectedLayoutNode *asProjectedLayoutNode()
    {
      return this;
    }

    virtual short layoutProjected(loka::app::scene::IPlatformController *,
                                  loka::app::scene::LayoutState &state)
    {
      this->wasLaidOut_ = true;
      this->geometry_ = state;
      return static_cast<short>(state.y + this->fixedHeight_);
    }

    bool wasLaidOut() const
    {
      return this->wasLaidOut_;
    }

    const loka::app::scene::LayoutState &geometry() const
    {
      return this->geometry_;
    }

  private:
    short fixedHeight_;
    bool wasLaidOut_;
    loka::app::scene::LayoutState geometry_;
  };

  /** Captures the Row offer before a native Button context consumes its own height. */
  class RowButtonSeatProbe : public loka::app::ButtonNode
  {
  public:
    RowButtonSeatProbe()
        : loka::app::ButtonNode(loka::app::ButtonProps()),
          geometry_()
    {
    }

    virtual short layoutProjected(loka::app::scene::IPlatformController *, loka::app::scene::LayoutState &state)
    {
      this->geometry_ = state;
      return static_cast<short>(state.y + state.height);
    }

    const loka::app::scene::LayoutState &geometry() const
    {
      return this->geometry_;
    }

  private:
    loka::app::scene::LayoutState geometry_;
  };

  loka::app::scene::LayoutState makeState(short x,
                                          short y,
                                          short width,
                                          short height)
  {
    loka::app::scene::LayoutState state;
    state.x = x;
    state.y = y;
    state.width = width;
    state.height = height;
    state.lineHeight = 10;
    return state;
  }

  void assertGeometry(const FixedLayoutProbeNode *probe,
                      short x,
                      short y,
                      short width,
                      short height)
  {
    (void)probe;
    assert(probe);
    LOKA_VERIFY(probe->wasLaidOut());
    (void)x;
    assert(probe->geometry().x == x); // loka-assert-ok: geometry() only returns the stored observation.
    (void)y;
    assert(probe->geometry().y == y); // loka-assert-ok: geometry() only returns the stored observation.
    (void)width;
    assert(probe->geometry().width == width); // loka-assert-ok: geometry() only returns the stored observation.
    (void)height;
    assert(probe->geometry().height == height); // loka-assert-ok: geometry() only returns the stored observation.
  }

  void verifyMetric(int actual, int expected)
  {
    LOKA_VERIFY(actual == expected);
  }
} // namespace

void testFallbackControlMetricsContract()
{
  typedef loka::app::layout::FallbackControlMetrics Metrics;
  verifyMetric(Metrics::kButtonHeight, 32);
  verifyMetric(Metrics::kEditTextHeight, 24);
  verifyMetric(Metrics::kPopupMenuHeight, 26);
  verifyMetric(Metrics::kTextHeight, 20);
  verifyMetric(Metrics::kCellHeight, 20);
  verifyMetric(Metrics::kVerticalSpacing, 12);
  verifyMetric(Metrics::kHorizontalSpacing, 12);
  verifyMetric(Metrics::kImageFallbackHeight, 160);

  const loka::app::layout::RowLayoutMetrics row = Metrics::rowLayout();
  LOKA_VERIFY(row.fallbackHeight == Metrics::kTextHeight);
  LOKA_VERIFY(row.buttonHeight == Metrics::kButtonHeight);
  LOKA_VERIFY(row.editTextHeight == Metrics::kEditTextHeight);
  LOKA_VERIFY(row.popupMenuHeight == Metrics::kPopupMenuHeight);
  LOKA_VERIFY(row.textHeight == Metrics::kTextHeight);
  LOKA_VERIFY(row.imageFallbackHeight == Metrics::kImageFallbackHeight);
}

void testNullLayoutRowProducesFixedChildGeometry()
{
  NullScenePlatformController platform;
  loka::app::StackNode row((loka::app::StackProps(loka::app::STACK_AXIS_ROW)));
  FixedLayoutProbeNode *first = new FixedLayoutProbeNode(7);
  FixedLayoutProbeNode *second = new FixedLayoutProbeNode(11);
  row.addChild(first);
  row.addChild(second);

  const int resultY = platform.projectLayoutForTesting(&row, makeState(10, 20, 101, 30));

  assertGeometry(first, 10, 20, 49, 30);
  assertGeometry(second, 63, 20, 48, 30);
  (void)resultY;
  assert(resultY == 31);
}

void testNullLayoutColumnProducesFixedChildGeometry()
{
  NullScenePlatformController platform;
  loka::app::StackNode column((loka::app::StackProps(loka::app::STACK_AXIS_COLUMN)));
  FixedLayoutProbeNode *first = new FixedLayoutProbeNode(7);
  FixedLayoutProbeNode *second = new FixedLayoutProbeNode(11);
  column.addChild(first);
  column.addChild(second);

  const int resultY = platform.projectLayoutForTesting(&column, makeState(10, 20, 80, 40));

  assertGeometry(first, 10, 20, 80, 40);
  assertGeometry(second, 10, 39, 80, 21);
  (void)resultY;
  assert(resultY == 50);
}

void testNullLayoutFragmentAdvancesAcrossChildren()
{
  NullScenePlatformController platform;
  loka::app::FragmentNode fragment((loka::app::FragmentProps()));
  FixedLayoutProbeNode *first = new FixedLayoutProbeNode(7);
  FixedLayoutProbeNode *second = new FixedLayoutProbeNode(11);
  fragment.addChild(first);
  fragment.addChild(second);

  const int resultY = platform.projectLayoutForTesting(&fragment, makeState(10, 20, 80, 40));

  assertGeometry(first, 10, 20, 80, 40);
  assertGeometry(second, 10, 39, 80, 40);
  (void)resultY;
  assert(resultY == 50);
}

void testNullLayoutBoxProducesFixedChildGeometry()
{
  NullScenePlatformController platform;
  loka::app::BoxProps props;
  props.setPadding(5);
  loka::app::BoxNode box(props);
  FixedLayoutProbeNode *first = new FixedLayoutProbeNode(7);
  FixedLayoutProbeNode *second = new FixedLayoutProbeNode(11);
  box.addChild(first);
  box.addChild(second);

  const int resultY = platform.projectLayoutForTesting(&box, makeState(10, 20, 80, 40));

  assertGeometry(first, 15, 25, 70, 30);
  assertGeometry(second, 15, 44, 70, 30);
  (void)resultY;
  assert(resultY == 60);
}

void testNullLayoutFixedBoxOwnsDeclaredExtent()
{
  NullScenePlatformController platform;
  loka::app::BoxProps props;
  props.setPadding(5).setSize(300, 170);
  loka::app::BoxNode box(props);
  FixedLayoutProbeNode *child = new FixedLayoutProbeNode(7);
  box.addChild(child);

  const int resultY = platform.projectLayoutForTesting(&box, makeState(10, 20, 80, 40));

  assertGeometry(child, 15, 25, 290, 160);
  (void)resultY;
  assert(resultY == 190);
}

void testNullLayoutGridProducesFixedChildGeometry()
{
  NullScenePlatformController platform;
  loka::app::GridNode grid((loka::app::GridProps(2, 2)));
  FixedLayoutProbeNode *first = new FixedLayoutProbeNode(7);
  FixedLayoutProbeNode *second = new FixedLayoutProbeNode(11);
  FixedLayoutProbeNode *third = new FixedLayoutProbeNode(13);
  FixedLayoutProbeNode *fourth = new FixedLayoutProbeNode(17);
  grid.addChild(first);
  grid.addChild(second);
  grid.addChild(third);
  grid.addChild(fourth);

  platform.projectLayoutForTesting(&grid, makeState(10, 20, 83, 44));

  assertGeometry(first, 10, 20, 40, 20);
  assertGeometry(second, 52, 20, 40, 20);
  assertGeometry(third, 10, 44, 40, 20);
  assertGeometry(fourth, 52, 44, 40, 20);
}

void testNullLayoutGridReturnedHeight()
{
  NullScenePlatformController platform;
  loka::app::GridNode grid((loka::app::GridProps(2, 2)));
  grid.addChild(new FixedLayoutProbeNode(7));
  grid.addChild(new FixedLayoutProbeNode(11));
  grid.addChild(new FixedLayoutProbeNode(13));
  grid.addChild(new FixedLayoutProbeNode(17));
  LOKA_VERIFY(platform.projectLayoutForTesting(&grid, makeState(10, 20, 83, 44)) == 61);
}

void testNullLayoutZStackProducesFixedChildGeometry()
{
  for (int mark = 0; mark != 2; ++mark)
  {
    NullScenePlatformController platform;
    loka::app::ZStack definition;
    if (mark)
      definition.expBlurUnit();
    loka::app::ZStackNode stack(definition.props);
    FixedLayoutProbeNode *back = new FixedLayoutProbeNode(7);
    FixedLayoutProbeNode *front = new FixedLayoutProbeNode(13);
    stack.addChild(back);
    stack.addChild(front);

    const int resultY = platform.projectLayoutForTesting(&stack, makeState(10, 20, 80, 40));

    assertGeometry(back, 10, 20, 80, 40);
    assertGeometry(front, 10, 20, 80, 40);
    (void)resultY;
    LOKA_VERIFY(resultY == 33);
  }
}

void testNullLayoutNestedBoxAndRowProduceFixedChildGeometry()
{
  NullScenePlatformController platform;
  loka::app::BoxProps boxProps;
  boxProps.setPadding(2);
  loka::app::BoxNode box(boxProps);
  loka::app::StackNode *row =
      new loka::app::StackNode((loka::app::StackProps(loka::app::STACK_AXIS_ROW)));
  FixedLayoutProbeNode *first = new FixedLayoutProbeNode(7);
  FixedLayoutProbeNode *second = new FixedLayoutProbeNode(11);
  row->addChild(first);
  row->addChild(second);
  box.addChild(row);

  const int resultY = platform.projectLayoutForTesting(&box, makeState(10, 20, 100, 40));

  assertGeometry(first, 12, 22, 46, 36);
  assertGeometry(second, 62, 22, 46, 36);
  (void)resultY;
  assert(resultY == 35);
}

void testNullLayoutOnChangeUsesRegisteredTraversal()
{
  NullScenePlatformController platform;
  loka::app::StackNode row((loka::app::StackProps(loka::app::STACK_AXIS_ROW)));
  FixedLayoutProbeNode *first = new FixedLayoutProbeNode(7);
  FixedLayoutProbeNode *second = new FixedLayoutProbeNode(11);
  row.addChild(first);
  row.addChild(second);

  platform.onChange(&row, loka::app::scene::NODE_DIRTY_LAYOUT, false);

  assertGeometry(first, 0, 0, 48, 20);
  assertGeometry(second, 52, 0, 48, 20);
}

void testProjectionMeasurementChannelAndUnitAdmission()
{
  using namespace loka::app::scene;
  ProjectionParentScopeStack scopes;
  LOKA_VERIFY(!scopes.insideUnit());
  LOKA_VERIFY(!scopes.current().channel);
  LOKA_VERIFY(!scopes.current().tryAccumulateContentHeight(0, 10));
  scopes.current().markShortRangeRefused();
  LOKA_VERIFY(!scopes.current().hasShortRangeRefusal());
  ProjectionParentScope viewport;
  LOKA_VERIFY(scopes.pushViewport(viewport));
  ProjectionParentScope &admittedViewport = scopes.current();
  LOKA_VERIFY(!scopes.insideUnit());
  ProjectionParentScope secondViewport;
  LOKA_VERIFY(!scopes.pushViewport(secondViewport));
  ProjectionParentScope unit;
  LOKA_VERIFY(scopes.current().deriveScrolled(0, 10, 20, loka::core::Frame(0, 0, 80, 80), unit));
  LOKA_VERIFY(scopes.pushUnit(unit));
  ProjectionParentScope &admittedUnit = scopes.current();
  LOKA_VERIFY(scopes.insideUnit());
  LOKA_VERIFY(admittedUnit.channel == admittedViewport.channel);
  LOKA_VERIFY(scopes.current().tryAccumulateContentHeight(0, 10));
  LOKA_VERIFY(scopes.current().contentHeight() == 10);
  scopes.current().markShortRangeRefused();
  LOKA_VERIFY(scopes.current().hasShortRangeRefusal());
  LOKA_VERIFY(!scopes.pushUnit(unit));
  LOKA_VERIFY(scopes.pop());
  LOKA_VERIFY(!admittedUnit.channel);
  LOKA_VERIFY(scopes.current().contentHeight() == 10);
  LOKA_VERIFY(scopes.current().hasShortRangeRefusal());
  LOKA_VERIFY(scopes.pop());
  LOKA_VERIFY(!admittedViewport.channel);
  LOKA_VERIFY(!scopes.current().hasShortRangeRefusal());
  LOKA_VERIFY(scopes.pushViewport(viewport));
  LOKA_VERIFY(scopes.current().contentHeight() == 0);
  LOKA_VERIFY(!scopes.current().hasShortRangeRefusal());
  LOKA_VERIFY(scopes.pop());
  ProjectionParentScope rootUnit;
  LOKA_VERIFY(scopes.current().deriveScrolled(0, 10, 20, loka::core::Frame(0, 0, 80, 80), rootUnit));
  LOKA_VERIFY(scopes.pushUnit(rootUnit));
  ProjectionParentScope &admittedRootUnit = scopes.current();
  ProjectionParentScope child;
  LOKA_VERIFY(scopes.current().deriveScrolled(0, 0, 0, loka::core::Frame(), child));
  child.markShortRangeRefused();
  LOKA_VERIFY(scopes.current().hasShortRangeRefusal());
  LOKA_VERIFY(scopes.insideUnit());
  LOKA_VERIFY(!scopes.pushUnit(rootUnit));
  // The root-only viewport admission wall (activeDepth != 0) answers here.
  LOKA_VERIFY(!scopes.pushViewport(secondViewport));
  LayoutState seat;
  seat.x = 10;
  seat.y = 20;
  LayoutState projected;
  LOKA_VERIFY(scopes.current().project(seat, projected));
  LOKA_VERIFY(projected.x == 0 && projected.y == 0);
  LOKA_VERIFY(scopes.pop());
  LOKA_VERIFY(!admittedRootUnit.channel);
  LOKA_VERIFY(!scopes.insideUnit());
  LOKA_VERIFY(!scopes.current().hasShortRangeRefusal());
  {
    ProjectionUnitScopeGuard guard(scopes, rootUnit);
    LOKA_VERIFY(guard.isActive());
    LOKA_VERIFY(scopes.insideUnit());
    LOKA_VERIFY(!scopes.current().hasShortRangeRefusal());
  }
  LOKA_VERIFY(!admittedRootUnit.channel);
  LOKA_VERIFY(!scopes.insideUnit());
}

void testNullLayoutWidthOnlyBoxUnderRootAndZStack()
{
  NullScenePlatformController platform;
  loka::app::BoxProps props;
  props.setWidth(300);
  loka::app::BoxNode rootBox(props);
  FixedLayoutProbeNode *rootChild = new FixedLayoutProbeNode(7);
  rootBox.addChild(rootChild);
  const int rootY = platform.projectLayoutForTesting(&rootBox, makeState(10, 20, 80, 40));
  loka::app::ZStackNode stack((loka::app::ZStackProps()));
  FixedLayoutProbeNode *back = new FixedLayoutProbeNode(7);
  loka::app::BoxNode *front = new loka::app::BoxNode(props);
  FixedLayoutProbeNode *frontChild = new FixedLayoutProbeNode(13);
  front->addChild(frontChild);
  stack.addChild(back);
  stack.addChild(front);
  const int stackY = platform.projectLayoutForTesting(&stack, makeState(10, 20, 80, 40));
  assertGeometry(rootChild, 10, 20, 300, 40);
  LOKA_VERIFY(rootY == 27);
  assertGeometry(back, 10, 20, 80, 40);
  assertGeometry(frontChild, 10, 20, 300, 40);
  LOKA_VERIFY(stackY == 33);
}

void testBoxHeightDoorPreservesWidthSources()
{
  using namespace loka::app;
  loka::core::MutableState<int> width(125);
  BoxDefinition box = Box().size(200, 10).width(&width).height(40);
  LOKA_VERIFY(box.props.width == 200);
  LOKA_VERIFY(box.props.widthState_ == &width);
  LOKA_VERIFY(box.props.effectiveWidth() == 125);
  LOKA_VERIFY(box.props.height == 40);
  box.width(static_cast<loka::core::State<int> *>(0));
  LOKA_VERIFY(box.props.effectiveWidth() == 200);
  box = Box().size(200, 40).height(0);
  LOKA_VERIFY(box.props.effectiveWidth() == 200);
  LOKA_VERIFY(box.props.height == 0);
  box = Box().height(40).size(0, 0);
  LOKA_VERIFY(box.props.width == 0 && box.props.height == 0);
  LOKA_VERIFY(Box().height(40000).props.height == SHRT_MAX);
  LOKA_VERIFY(Box().height(INT_MIN).props.height == 0);
  LOKA_VERIFY(Box().height(-65535).props.height == 0);
}

void testBoxHeightOnlyEmptyAndPaddedOffer()
{
  using namespace loka::app;
  NullScenePlatformController platform;
  BoxNode empty(Box().height(40).props);
  const int emptyY = platform.projectLayoutForTesting(&empty, makeState(10, 20, 100, 15));
  BoxNode padded(Box().height(40).padding(5).props);
  FixedLayoutProbeNode *content = new FixedLayoutProbeNode(70);
  padded.addChild(content);
  const int paddedY = platform.projectLayoutForTesting(&padded, makeState(10, 20, 100, 15));
  LOKA_VERIFY(emptyY == 60);
  LOKA_VERIFY(paddedY == 60);
  assertGeometry(content, 15, 25, 90, 30);
  padded.props.setHeight(5);
  const int smallY = platform.projectLayoutForTesting(&padded, makeState(10, 20, 100, 15));
  LOKA_VERIFY(content->geometry().height == 0);
  LOKA_VERIFY(smallY == 25);
}

void testBoxHeightOnlyColumnAndScrollExtent()
{
  using namespace loka::app;
  NullScenePlatformController platform;
  StackNode column(Column().gap(4).props);
  column.addChild(new BoxNode(Box().height(40).props));
  FixedLayoutProbeNode *after = new FixedLayoutProbeNode(7);
  column.addChild(after);
  const int columnY = platform.projectLayoutForTesting(&column, makeState(10, 20, 100, 15));
  ScrollViewNode scroll((ScrollViewProps()));
  scroll.addChild(new BoxNode(Box().height(40).props));
  const int scrollY = platform.projectLayoutForTesting(&scroll, makeState(10, 20, 100, 0));
  LOKA_VERIFY(after->geometry().y == 64);
  LOKA_VERIFY(columnY == 71);
  LOKA_VERIFY(scrollY == 60);
}

void testBoxHeightStaysFixedWhenLiveWidthCrossesZero()
{
  using namespace loka::app;
  loka::core::MutableState<int> width(200);
  BoxNode box(Box().size(200, 40).width(&width).props);
  FixedLayoutProbeNode *content = new FixedLayoutProbeNode(7);
  box.addChild(content);
  NullScenePlatformController platform;
  const int widths[] = {200, 0, -100};
  for (unsigned i = 0; i < 3; ++i)
  {
    width.set(widths[i]);
    const int resultY = platform.projectLayoutForTesting(&box, makeState(10, 20, 100, 15));
    LOKA_VERIFY(content->geometry().width == (i == 0 ? 200 : 100));
    LOKA_VERIFY(content->geometry().height == 40);
    LOKA_VERIFY(resultY == 60);
  }
}

void testSharedAlignedRowExtentIsLargerOfMeasurementAndChild()
{
  using namespace loka::app;
  const VerticalAlignment alignments[] = {VERTICAL_ALIGNMENT_TOP, VERTICAL_ALIGNMENT_CENTER, VERTICAL_ALIGNMENT_BOTTOM};
  for (unsigned alignment = 0; alignment < 3; ++alignment)
  {
    for (unsigned fixedWidth = 0; fixedWidth < 2; ++fixedWidth)
    {
      NullScenePlatformController platform;
      StackNode row(Row().alignVertical(alignments[alignment]).props);
      BoxProps props = Box().height(200).padding(5).props;
      if (fixedWidth)
        props.setWidth(300);
      BoxNode *box = new BoxNode(props);
      FixedLayoutProbeNode *content = new FixedLayoutProbeNode(7);
      box->addChild(content);
      row.addChild(box);
      const int resultY = platform.projectLayoutForTesting(&row, makeState(10, 20, 80, 40));
      assertGeometry(content, 15, 25, fixedWidth ? 290 : 70, 190);
      LOKA_VERIFY(resultY == 220);
    }
  }
  NullScenePlatformController platform;
  StackNode shorter(Row().alignVertical(VERTICAL_ALIGNMENT_TOP).props);
  FixedLayoutProbeNode *child = new FixedLayoutProbeNode(7);
  shorter.addChild(child);
  const scene::LayoutState state = makeState(10, 20, 80, 40);
  const int resultY = platform.projectLayoutForTesting(&shorter, state);
  const int measurement = child->geometry().height;
  LOKA_VERIFY(measurement > 7);
  LOKA_VERIFY(resultY == state.y + measurement);
}

void testSharedAlignedRowBottomAlignmentMeasuresBoxHeight()
{
  using namespace loka::app;
  NullScenePlatformController platform;
  StackNode baseline(Row().alignVertical(VERTICAL_ALIGNMENT_BOTTOM).props);
  RowButtonSeatProbe *baselineButton = new RowButtonSeatProbe();
  baseline.addChild(baselineButton);
  const scene::LayoutState state = makeState(10, 20, 80, 40);
  platform.projectLayoutForTesting(&baseline, state);
  const int buttonHeight = baselineButton->geometry().height;
  LOKA_VERIFY(buttonHeight > 0 && buttonHeight < 200);

  StackNode row(Row().alignVertical(VERTICAL_ALIGNMENT_BOTTOM).props);
  row.addChild(new BoxNode(Box().height(200).props));
  RowButtonSeatProbe *button = new RowButtonSeatProbe();
  row.addChild(button);
  const int resultY = platform.projectLayoutForTesting(&row, state);
  LOKA_VERIFY(button->geometry().height == buttonHeight);
  LOKA_VERIFY(button->geometry().y == state.y + 200 - buttonHeight);
  LOKA_VERIFY(resultY == 220);
}

void testSharedAlignedRowRefusesOverflowingChildBox()
{
  using namespace loka::app;
  NullScenePlatformController platform;
  // Narrowing refusal is a scroll-scope rule on this rail, so the row sits in
  // a ScrollView. The sibling comes first so nothing but its own painted box
  // can refuse it: bottom-aligned under a SHRT_MAX measurement it would land
  // at a wrapped Y, and the row refuses that box before narrowing.
  ScrollViewNode scroll((ScrollViewProps()));
  StackNode *row = new StackNode(Row().alignVertical(VERTICAL_ALIGNMENT_BOTTOM).props);
  FixedLayoutProbeNode *sibling = new FixedLayoutProbeNode(7);
  row->addChild(sibling);
  row->addChild(new BoxNode(Box().height(SHRT_MAX).props));
  scroll.addChild(row);
  platform.projectLayoutForTesting(&scroll, makeState(10, 20, 80, 40));
  LOKA_VERIFY(!sibling->wasLaidOut());
}

void testBoxHeightExceedsGridAndCanvasCells()
{
  using namespace loka::app;
  NullScenePlatformController platform;
  GridNode grid(GridProps(2, 1));
  BoxNode *box = new BoxNode(Box().height(80).props);
  FixedLayoutProbeNode *content = new FixedLayoutProbeNode(7);
  box->addChild(content);
  grid.addChild(box);
  FixedLayoutProbeNode *second = new FixedLayoutProbeNode(7);
  grid.addChild(second);
  const int gridY = platform.projectLayoutForTesting(&grid, makeState(10, 20, 100, 40));
  LOKA_VERIFY(content->geometry().height == 80);
  LOKA_VERIFY(second->geometry().y == 42);
  LOKA_VERIFY(gridY == 100);
  loka::core::State<loka::core::Frame> viewport(loka::core::Frame(0, 0, 100, 40));
  CanvasNode canvas(CanvasProps(100, 20, &viewport));
  BoxNode *cell = new BoxNode(Box().height(80).props);
  FixedLayoutProbeNode *cellContent = new FixedLayoutProbeNode(7);
  cell->addChild(cellContent);
  canvas.addChild(cell);
  const int canvasY = platform.projectLayoutForTesting(&canvas, makeState(10, 20, 100, 40));
  LOKA_VERIFY(cellContent->geometry().height == 80);
  LOKA_VERIFY(canvasY == 40);
}
