#include "NullLayoutTests.hpp"
#include "support/TestVerify.hpp"

#include <cassert>

#include "app/layout/FallbackControlMetrics.hpp"
#include "app/nodes/nestable/Box.hpp"
#include "app/nodes/nestable/Fragment.hpp"
#include "app/nodes/nestable/Grid.hpp"
#include "app/nodes/nestable/RowColumn.hpp"
#include "app/nodes/nestable/ZStack.hpp"
#include "app/nodes/nestable/Layer.hpp"
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
    assert(probe->wasLaidOut());
    (void)x;
    assert(probe->geometry().x == x);
    (void)y;
    assert(probe->geometry().y == y);
    (void)width;
    assert(probe->geometry().width == width);
    (void)height;
    assert(probe->geometry().height == height);
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
  NullScenePlatformController platform;
  loka::app::ZStackNode stack((loka::app::ZStackProps()));
  FixedLayoutProbeNode *back = new FixedLayoutProbeNode(7);
  FixedLayoutProbeNode *front = new FixedLayoutProbeNode(13);
  stack.addChild(back);
  stack.addChild(front);

  const int resultY = platform.projectLayoutForTesting(&stack, makeState(10, 20, 80, 40));

  assertGeometry(back, 10, 20, 80, 40);
  assertGeometry(front, 10, 20, 80, 40);
  (void)resultY;
  assert(resultY == 33);
}

void testNullLayoutLayerSharesZStackSeat()
{
  using namespace loka::app;
  NullScenePlatformController platform;
  // Compare the same nesting with both containers; no ScrollView participates.
  for (int useLayer = 0; useLayer != 2; ++useLayer)
  {
    ZStackNode stack((ZStackProps()));
    FixedLayoutProbeNode *back = new FixedLayoutProbeNode(31);
    scene::Node *overlay = useLayer ? Layer().create() : ZStack().create();
    scene::Node *box = Box().size(100, 20).create();
    FixedLayoutProbeNode *boxContent = new FixedLayoutProbeNode(7);
    FixedLayoutProbeNode *seat = new FixedLayoutProbeNode(13);
    box->asNestable()->addChild(boxContent);
    overlay->asNestable()->addChild(box);
    overlay->asNestable()->addChild(seat);
    stack.addChild(back);
    stack.addChild(overlay);

    LOKA_VERIFY(platform.projectLayoutForTesting(&stack, makeState(10, 20, 180, 60)) == 51);
    assertGeometry(back, 10, 20, 180, 60);
    assertGeometry(seat, 10, 20, 180, 60);
    // The offer is full width; the fixed Box narrows its own content to 100.
    assertGeometry(boxContent, 10, 20, 100, 20);
    LOKA_VERIFY(platform.projectLayoutForTesting(overlay, makeState(10, 20, 180, 60)) == 40);
  }
}

void testNullLayoutLayerStacksChildren()
{
  NullScenePlatformController platform;
  loka::app::LayerNode layer((loka::app::LayerProps()));
  FixedLayoutProbeNode *back = new FixedLayoutProbeNode(7);
  FixedLayoutProbeNode *front = new FixedLayoutProbeNode(13);
  layer.addChild(back);
  layer.addChild(front);

  LOKA_VERIFY(platform.projectLayoutForTesting(&layer, makeState(10, 20, 80, 40)) == 33);
  assertGeometry(back, 10, 20, 80, 40);
  assertGeometry(front, 10, 20, 80, 40);
}

void testNullLayerIdentityAndPaintSkip()
{
  using namespace loka::app;
  NullScenePlatformController platform;
  LayerNode layer((LayerProps()));
  ZStackNode stack((ZStackProps()));
  scene::Node *node = &layer;
  LOKA_VERIFY(node->asLayerNode() == &layer);
  LOKA_VERIFY(stack.asLayerNode() == 0);
  LOKA_VERIFY(layer.asZStackNode() == 0);
  LOKA_VERIFY(layer.kind() == scene::NODE_KIND_LAYER);
  const scene::PaintQuery query = {platform.paintScope(), scene::PLACEMENT_ELIGIBLE};
  scene::PaintAnswer answer = scene::PaintAnswer::nativeScheduled();
  LOKA_VERIFY(!platform.queryPaintAnswer(&layer, 0, query, answer));
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
