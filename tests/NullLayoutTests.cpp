#include <climits>
#include <cstdio>
#include "app/nodes/nestable/Match.hpp"
#include "app/nodes/nestable/PolicyScope.hpp"
#include "app/nodes/nestable/Show.hpp"
#include "support/PropsReconciliation.hpp"
#include "app/nodes/controls/Card.hpp"
#include "core/util/OwnedDef.hpp"
#include "app/nodes/nestable/ScrollView.hpp"
#include "app/nodes/nestable/Canvas.hpp"
#include "NullLayoutTests.hpp"
#include "support/TestVerify.hpp"

#include <cassert>

#include "app/layout/FallbackControlMetrics.hpp"
#include "app/layout/ZStackLayout.hpp"
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
  for (int mark = 0; mark != 3; ++mark)
  {
    NullScenePlatformController platform;
    loka::app::ZStack definition;
    if (mark == 1)
      definition.expBlurUnit();
    if (mark == 2)
      definition.expStaticBlurUnit();
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
    // Rail frame/ledger placement uses this same band fact after child layout.
    const loka::app::ZStackUnitGround ground = definition.props.unitGround;
    LOKA_VERIFY(loka::app::layout::zStackUnitBottom(ground, 1, 20, 27, 90) == (mark == 2 ? 90 : 27));
    LOKA_VERIFY(loka::app::layout::zStackUnitBottom(ground, 1, 20, 100, 90) == 100);
    LOKA_VERIFY(loka::app::layout::zStackUnitBottom(ground, 2, 20, 27, 90) == 27);
    LOKA_VERIFY(loka::app::layout::zStackUnitBottom(ground, 1, 20, 20, 90) == 20);
    NullScenePlatformController tallPlatform;
    loka::app::ZStackNode tallStack(definition.props);
    FixedLayoutProbeNode *tall = new FixedLayoutProbeNode(70);
    FixedLayoutProbeNode *shortChild = new FixedLayoutProbeNode(7);
    tallStack.addChild(tall);
    tallStack.addChild(shortChild);
    LOKA_VERIFY(tallPlatform.projectLayoutForTesting(&tallStack, makeState(10, 20, 80, 100)) == 90);
    assertGeometry(shortChild, 10, 20, 80, 100);
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

void testProjectionFrameKindsAndClipAdmission()
{
  using namespace loka::app::scene;
  using loka::core::Frame;
  ProjectionParentScopeStack scopes;
  ProjectionParentScope viewport(0, -7, -11, Frame(1, 2, 80, 90));
  int nativeParent = 0;
  ProjectionParentScope clip(&nativeParent, 300, 400, Frame(3, 4, 50, 60));
  ProjectionParentScope unit;
  LOKA_VERIFY(!scopes.insideViewport() && !scopes.insideUnit());
  LOKA_VERIFY(!scopes.current().channel);
  ProjectionMeasurementChannel *viewportChannel = 0;
  {
    ProjectionViewportScopeGuard guard(scopes, viewport);
    LOKA_VERIFY(guard.isActive());
    viewportChannel = scopes.current().channel;
    LOKA_VERIFY(viewportChannel);
    LOKA_VERIFY(scopes.insideViewport() && !scopes.insideUnit());
    {
      ProjectionUnitScopeGuard unitGuard(scopes, unit);
      LOKA_VERIFY(unitGuard.isActive());
      LOKA_VERIFY(scopes.insideViewport() && scopes.insideUnit());
      LOKA_VERIFY(scopes.current().channel == viewportChannel);
    }
    {
      ProjectionClipScopeGuard clipGuard(scopes, clip);
      LOKA_VERIFY(clipGuard.isActive());
      LOKA_VERIFY(scopes.insideViewport() && !scopes.insideUnit());
      LOKA_VERIFY(scopes.current().channel == viewportChannel);
      LOKA_VERIFY(scopes.current().translationX == -7 && scopes.current().translationY == -11);
      LOKA_VERIFY(scopes.current().clipRect == clip.clipRect);
      LOKA_VERIFY(scopes.current().nativeParent == &nativeParent);
      ProjectionViewportScopeGuard refused(scopes, viewport);
      LOKA_VERIFY(!refused.isActive());
      ProjectionUnitScopeGuard unitGuard(scopes, unit);
      LOKA_VERIFY(unitGuard.isActive());
      LOKA_VERIFY(scopes.current().channel == viewportChannel);
      LOKA_VERIFY(scopes.insideViewport() && scopes.insideUnit());
    }
    LOKA_VERIFY(scopes.activeDepth() == 1);
  }
  ProjectionMeasurementChannel *rootUnitChannel = 0;
  {
    ProjectionUnitScopeGuard guard(scopes, unit);
    LOKA_VERIFY(guard.isActive());
    rootUnitChannel = scopes.current().channel;
    LOKA_VERIFY(rootUnitChannel && rootUnitChannel != viewportChannel);
    LOKA_VERIFY(scopes.insideUnit() && !scopes.insideViewport());
    LOKA_VERIFY(!scopes.pushViewport(viewport));
    ProjectionClipScopeGuard clipGuard(scopes, clip);
    LOKA_VERIFY(clipGuard.isActive());
    LOKA_VERIFY(scopes.insideUnit() && !scopes.insideViewport());
    LOKA_VERIFY(scopes.current().channel == rootUnitChannel);
    LOKA_VERIFY(!scopes.pushViewport(viewport));
    LOKA_VERIFY(!scopes.pushUnit(unit));
  }
  {
    ProjectionClipScopeGuard clipGuard(scopes, clip);
    LOKA_VERIFY(clipGuard.isActive());
    LOKA_VERIFY(!scopes.insideViewport() && !scopes.insideUnit());
    LOKA_VERIFY(!scopes.current().channel);
    LOKA_VERIFY(scopes.current().translationX == 0 && scopes.current().translationY == 0);
    {
      ProjectionUnitScopeGuard guard(scopes, unit);
      LOKA_VERIFY(guard.isActive());
      LOKA_VERIFY(scopes.current().channel == rootUnitChannel);
      LOKA_VERIFY(scopes.insideUnit() && !scopes.insideViewport());
    }
    {
      ProjectionViewportScopeGuard guard(scopes, viewport);
      LOKA_VERIFY(guard.isActive());
      LOKA_VERIFY(scopes.current().channel == viewportChannel);
      LOKA_VERIFY(scopes.insideViewport() && !scopes.insideUnit());
    }
  }
  LOKA_VERIFY(scopes.activeDepth() == 0);
  // Six active frames, not six CLIPs: exercise both possible reservations.
  for (unsigned withViewport = 0; withViewport != 2; ++withViewport)
  {
    if (withViewport)
      LOKA_VERIFY(scopes.pushViewport(viewport));
    for (unsigned i = withViewport; i != 6; ++i)
      LOKA_VERIFY(scopes.pushClip(clip));
    LOKA_VERIFY(scopes.activeDepth() == 6);
    {
      ProjectionClipScopeGuard refused(scopes, clip);
      LOKA_VERIFY(!refused.isActive());
    }
    LOKA_VERIFY(scopes.activeDepth() == 6);
    if (!withViewport)
      LOKA_VERIFY(scopes.pushViewport(viewport));
    LOKA_VERIFY(scopes.pushUnit(unit));
    LOKA_VERIFY(scopes.activeDepth() == (withViewport ? 7u : 8u));
    LOKA_VERIFY(!scopes.pushClip(clip));
    LOKA_VERIFY(!scopes.pushViewport(viewport));
    LOKA_VERIFY(!scopes.pushUnit(unit));
    while (scopes.activeDepth())
      LOKA_VERIFY(scopes.pop());
  }
  LOKA_VERIFY(ProjectionParentScopeStack::MAX_ACTIVE_SCOPES == 8);
  LOKA_VERIFY(!scopes.insideViewport() && !scopes.insideUnit());
  LOKA_VERIFY(!scopes.current().channel);
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

namespace loka
{
  namespace testing
  {
    /** Private fixture access; Null has no production UNIT traversal. */
    class NullClipAccess
    {
    public:
      static app::scene::ProjectionParentScopeStack &scopes(NullScenePlatformController &controller)
      {
        return controller.projectionParentScopes_;
      }
      static bool refusesY(NullScenePlatformController &controller, int y)
      {
        return controller.refuseNarrowingInScrollScope(y);
      }
      static void install(NullScenePlatformController &controller, app::scene::IPlatformLayoutHandler *handler)
      {
        LOKA_VERIFY(controller.layoutHandlers_.registerHandler(handler));
      }
    };
  } // namespace testing
} // namespace loka

namespace
{
  class ClipFrameProbe : public loka::app::scene::Node, public loka::app::scene::IProjectedLayoutNode
  {
  public:
    const unsigned depth;
    const bool viewport;
    const bool unit;
    const loka::core::Frame clip;
    const int accumulated;
    bool visited;
    ClipFrameProbe(unsigned d, bool v, bool u, const loka::core::Frame &r, int height = 0)
        : depth(d),
          viewport(v),
          unit(u),
          clip(r),
          accumulated(height),
          visited(false)
    {
    }
    virtual loka::app::scene::IProjectedLayoutNode *asProjectedLayoutNode()
    {
      return this;
    }
    virtual short layoutProjected(loka::app::scene::IPlatformController *controller,
                                  loka::app::scene::LayoutState &state)
    {
      this->visited = true;
      loka::app::scene::ProjectionParentScopeStack &scopes =
          loka::testing::NullClipAccess::scopes(*static_cast<NullScenePlatformController *>(controller));
      LOKA_VERIFY(scopes.activeDepth() == this->depth);
      LOKA_VERIFY(scopes.insideViewport() == this->viewport);
      LOKA_VERIFY(scopes.insideUnit() == this->unit);
      LOKA_VERIFY((scopes.current().channel != 0) == (this->viewport || this->unit));
      LOKA_VERIFY(scopes.current().clipRect == this->clip);
      LOKA_VERIFY(scopes.current().contentHeight() == this->accumulated);
      if (!this->viewport && !this->unit)
        LOKA_VERIFY(!loka::testing::NullClipAccess::refusesY(*static_cast<NullScenePlatformController *>(controller),
                                                             SHRT_MAX + 1));
      return static_cast<short>(state.y + 7);
    }
  };

  /** Exercises C1 UNIT admission under a real Null CLIP; not a native ZStack implementation. */
  class ClipUnitHandler : public loka::app::scene::IPlatformLayoutHandler
  {
  public:
    explicit ClipUnitHandler(NullScenePlatformController &controller)
        : controller_(controller)
    {
    }
    virtual const void *nodeTypeKey() const
    {
      return loka::app::scene::NodeTypeToken<loka::app::ZStackNode>();
    }
    virtual int layoutNode(loka::app::scene::Node *node,
                           const loka::app::scene::LayoutState &state,
                           loka::app::scene::IPlatformLayoutTraversal *traversal,
                           const loka::app::layout::LazyWindow *,
                           loka::app::layout::StackSpans *)
    {
      using namespace loka::app::scene;
      ProjectionParentScopeStack &scopes = loka::testing::NullClipAccess::scopes(this->controller_);
      LOKA_VERIFY(!scopes.current().channel);
      const ProjectionParentScope parent = scopes.current();
      ProjectionUnitScopeGuard guard(scopes, parent);
      LOKA_VERIFY(guard.isActive());
      LOKA_VERIFY(scopes.current().channel);
      return traversal->layoutChild(node->asNestable()->childrenHead(), state);
    }

  private:
    NullScenePlatformController &controller_;
  };

  template <class Definition, class Props> void verifyClipProps()
  {
    Definition plain;
    Definition clipped;
    LOKA_VERIFY(plain.props.childClip_ == loka::app::layout::CHILD_CLIP_NONE);
    LOKA_VERIFY(&clipped.clipChildren() == &clipped);
    LOKA_VERIFY(clipped.props.childClip_ == loka::app::layout::CHILD_CLIP_OWN_RECT);
    LOKA_VERIFY(plain.props.hash() != clipped.props.hash());
    LOKA_VERIFY(plain.props < clipped.props);
    LOKA_VERIFY(!(clipped.props < plain.props));
    Definition copied(clipped);
    LOKA_VERIFY(copied.props.childClip_ == loka::app::layout::CHILD_CLIP_OWN_RECT);
    LOKA_VERIFY(copied.props.hash() == clipped.props.hash());
    LOKA_VERIFY(!(copied.props < clipped.props) && !(clipped.props < copied.props));
    loka::core::OwnedDef<loka::app::scene::NodeDefinitionBase> clone(clipped.clone());
    LOKA_VERIFY(clone.get());
    const Props &clonedProps = *static_cast<const Props *>(clone->propsBase());
    LOKA_VERIFY(clonedProps.childClip_ == loka::app::layout::CHILD_CLIP_OWN_RECT);
    LOKA_VERIFY(clonedProps.hash() == clipped.props.hash());
    LOKA_VERIFY(!(clipped.props < *clone->propsBase()) && !(*clone->propsBase() < clipped.props));
  }
} // namespace

void testClipChildrenBoxPropsIdentityAndCopy()
{
  verifyClipProps<loka::app::BoxDefinition, loka::app::BoxProps>();
}

void testClipChildrenCardPropsIdentityAndCopy()
{
  verifyClipProps<loka::app::CardDefinition, loka::app::CardProps>();
}

void testNullClipBoxFrameAndUnflaggedGeometry()
{
  using namespace loka::app;
  for (int clipped = 0; clipped != 2; ++clipped)
  {
    NullScenePlatformController platform;
    Box definition = Box().size(120, 40).padding(5);
    if (clipped)
      definition.clipChildren();
    BoxNode box(definition.props);
    ClipFrameProbe *probe =
        new ClipFrameProbe(clipped, false, false, loka::core::Frame(10, 20, clipped ? 120 : 80, 40));
    box.addChild(probe);
    LOKA_VERIFY(platform.projectLayoutForTesting(&box, makeState(10, 20, 80, 40)) == 60);
    LOKA_VERIFY(probe->visited);
    LOKA_VERIFY(platform.projectionParentScopeDepthForTesting() == 0);
    // Flagging does not change the content offer; Null has no width result.
    FixedLayoutProbeNode *geometry = new FixedLayoutProbeNode(7);
    BoxNode geometryBox(definition.props);
    geometryBox.addChild(geometry);
    LOKA_VERIFY(platform.projectLayoutForTesting(&geometryBox, makeState(10, 20, 80, 40)) == 60);
    assertGeometry(geometry, 15, 25, 110, 30);
  }
}

void testNullClipCardFrame()
{
  NullScenePlatformController platform;
  loka::app::CardNode card(loka::app::Card().clipChildren().props);
  ClipFrameProbe *probe = new ClipFrameProbe(1, false, false, loka::core::Frame(10, 20, 80, SHRT_MAX - 20));
  card.addChild(probe);
  platform.projectLayoutForTesting(&card, makeState(10, 20, 80, 40));
  LOKA_VERIFY(probe->visited);
  LOKA_VERIFY(platform.projectionParentScopeDepthForTesting() == 0);
}

void testNullClipScrollAdmissionAndMeasurement()
{
  using namespace loka::app;
  NullScenePlatformController platform;
  BoxNode box(Box().size(60, 50).clipChildren().props);
  ScrollViewNode *scroll = new ScrollViewNode(ScrollViewProps());
  ClipFrameProbe *probe = new ClipFrameProbe(2, true, false, loka::core::Frame(10, 20, 60, 50));
  scroll->addChild(probe);
  BoxNode *wide = new BoxNode(Box().size(100, 50).props);
  wide->addChild(scroll);
  box.addChild(wide);
  LOKA_VERIFY(platform.projectLayoutForTesting(&box, makeState(10, 20, 80, 40)) == 70);
  LOKA_VERIFY(probe->visited);
  LOKA_VERIFY(platform.nestedScrollViewRefusalCount() == 0);
  LOKA_VERIFY(platform.scrollViewShortRangeRefusalCount() == 0);
  LOKA_VERIFY(platform.projectionParentScopeDepthForTesting() == 0);
  NullScenePlatformController measuredPlatform;
  BoxNode measuredBox(Box().clipChildren().props);
  ScrollViewNode *measuredScroll = new ScrollViewNode(ScrollViewProps());
  FixedLayoutProbeNode *measured = new FixedLayoutProbeNode(7);
  measuredScroll->addChild(measured);
  measuredBox.addChild(measuredScroll);
  LOKA_VERIFY(measuredPlatform.projectLayoutForTesting(&measuredBox, makeState(10, 20, 80, 0)) == 27);
  LOKA_VERIFY(measured->wasLaidOut());
  LOKA_VERIFY(measuredPlatform.nestedScrollViewRefusalCount() == 0);
}

void testNullClipInsideViewportKeepsChannel()
{
  using namespace loka::app;
  for (int inset = 0; inset <= 12; inset += 12)
  {
    NullScenePlatformController platform;
    ScrollViewNode scroll(ScrollViewProps().contentInsets(ScrollViewContentInsets::of(inset, 0)));
    BoxNode *box = new BoxNode(Box().height(40).clipChildren().props);
    ClipFrameProbe *inner = new ClipFrameProbe(2, true, false, loka::core::Frame(10, 20 + inset, 80, 40));
    box->addChild(inner);
    scroll.addChild(box);
    ClipFrameProbe *after = new ClipFrameProbe(1, true, false, loka::core::Frame(10, 20, 80, 100), 40);
    scroll.addChild(after);
    LOKA_VERIFY(platform.projectLayoutForTesting(&scroll, makeState(10, 20, 80, 100)) == 120);
    LOKA_VERIFY(inner->visited && after->visited);
    LOKA_VERIFY(platform.projectionParentScopeDepthForTesting() == 0);
  }
}

void testNullClipRootUnitAdmissionFixture()
{
  using namespace loka::app;
  NullScenePlatformController platform;
  loka::testing::NullClipAccess::install(platform, new ClipUnitHandler(platform));
  BoxNode box(Box().size(80, 40).clipChildren().props);
  ZStackNode *unit = new ZStackNode(ZStack().expBlurUnit().props);
  ClipFrameProbe *probe = new ClipFrameProbe(2, false, true, loka::core::Frame(10, 20, 80, 40));
  unit->addChild(probe);
  box.addChild(unit);
  LOKA_VERIFY(platform.projectLayoutForTesting(&box, makeState(10, 20, 80, 40)) == 60);
  LOKA_VERIFY(probe->visited);
  LOKA_VERIFY(platform.projectionParentScopeDepthForTesting() == 0);
}

void testNullClipIntersectionAndSeventhRefusal()
{
  using namespace loka::app;
  NullScenePlatformController platform;
  BoxNode root(Box().size(60, 30).padding(5).clipChildren().props);
  BoxNode *tail = &root;
  for (int i = 0; i != 6; ++i)
  {
    BoxNode *child = new BoxNode(Box().size(200, 200).clipChildren().props);
    tail->addChild(child);
    tail = child;
  }
  ClipFrameProbe *probe = new ClipFrameProbe(6, false, false, loka::core::Frame(15, 25, 55, 25));
  tail->addChild(probe);
  LOKA_VERIFY(platform.projectLayoutForTesting(&root, makeState(10, 20, 80, 40)) == 50);
  LOKA_VERIFY(probe->visited);
  LOKA_VERIFY(platform.projectionParentScopeDepthForTesting() == 0);
}

void testNullClipUnconstrainedEdges()
{
  for (int width = -1; width <= 0; ++width)
  {
    NullScenePlatformController platform;
    loka::app::BoxNode box(loka::app::Box().clipChildren().props);
    ClipFrameProbe *probe =
        new ClipFrameProbe(1, false, false, loka::core::Frame(10, 20, SHRT_MAX - 10, SHRT_MAX - 20));
    box.addChild(probe);
    LOKA_VERIFY(platform.projectLayoutForTesting(&box, makeState(10, 20, static_cast<short>(width), 0)) == 27);
    LOKA_VERIFY(probe->visited);
  }
}

namespace
{
  // Red with the generic applier: a differing definition overwrites the birth flag.
  // Equal flags characterize the existing copy behavior, including all other fields.
  template <class Definition, class NodeT> void verifyRetainedClip(Definition original, Definition next, bool different)
  {
    using namespace loka::app;
    for (int flagged = 0; flagged != 2; ++flagged)
    {
      original.props.childClip_ = flagged ? layout::CHILD_CLIP_OWN_RECT : layout::CHILD_CLIP_NONE;
      next.props.childClip_ = ((flagged != 0) != different) ? layout::CHILD_CLIP_OWN_RECT : layout::CHILD_CLIP_NONE;
      NodeT node(original.props);
      node.setPropsTypeId(original.props.propsTypeId());
      Definition expected(next);
      expected.props.childClip_ = original.props.childClip_;
      LOKA_VERIFY(next.isCompatibleWithNode(&node));
      LOKA_VERIFY(next.applyPropsToNode(&node));
      LOKA_VERIFY(node.props.childClip_ == original.props.childClip_);
      LOKA_VERIFY(!(node.props < expected.props) && !(expected.props < node.props));
      loka::core::OwnedDef<scene::NodeDefinitionBase> clone(next.clone());
      LOKA_VERIFY(clone.get());
      for (int repeat = 0; repeat != 2; ++repeat)
      {
        LOKA_VERIFY(clone->isCompatibleWithNode(&node));
        LOKA_VERIFY(clone->applyPropsToNode(&node));
        LOKA_VERIFY(node.props.childClip_ == original.props.childClip_);
        LOKA_VERIFY(!(node.props < expected.props) && !(expected.props < node.props));
      }
    }
  }

  void verifyRetainedBoxClip(bool different)
  {
    using namespace loka::app;
    loka::core::MutableState<int> width(93);
    verifyRetainedClip<Box, BoxNode>(
        Box().width(20).height(30).padding(2).gap(3), Box().width(&width).height(60).padding(7).gap(8), different);
    verifyRetainedClip<Box, BoxNode>(Box().width(&width), Box().width(71), different);
  }

  void verifyRetainedCardClip(bool different)
  {
    using namespace loka::app;
    verifyRetainedClip<Card, CardNode>(
        Card(CardStyle(CARD_SHADOW_NONE)), Card(CardStyle(CARD_SHADOW_WIDE, CARD_SHADOW_STENCIL)), different);
  }

  /** Uses the existing Boundary reconciliation door, as the refusal harness does. */
  class ClipReentryHarness : public loka::app::scene::BoundaryNodeFor<ClipReentryHarness>
  {
  public:
    explicit ClipReentryHarness(const loka::app::scene::BoundaryPropsFor<ClipReentryHarness> &p)
        : loka::app::scene::BoundaryNodeFor<ClipReentryHarness>(p)
    {
    }
    virtual void composeNode(loka::app::scene::NodeComposition &) {}
    loka::app::scene::Node *materialize(loka::app::scene::ComponentContext &context,
                                        loka::app::scene::NodeDefinitionBase &definition)
    {
      return this->materializeLocalRebuildNode(context, &definition);
    }
    bool reconcile(loka::app::scene::ComponentContext &context,
                   loka::app::scene::Node *node,
                   loka::app::scene::NodeDefinitionBase &definition)
    {
      return this->reconcileParkedBranch(context, node, &definition);
    }
  };
} // namespace

void testClipChildrenBoxRetainedEqual()
{
  verifyRetainedBoxClip(false);
}

void testClipChildrenCardRetainedEqual()
{
  verifyRetainedCardClip(false);
}

void testClipChildrenBoxRetainedDifferent()
{
#ifndef LOKA_LIFECYCLE_AUDIT
  verifyRetainedBoxClip(true);
#else
  std::printf("[skip] Box differing clip flag (lifecycle audit asserts here)\n");
#endif
}

void testClipChildrenCardRetainedDifferent()
{
#ifndef LOKA_LIFECYCLE_AUDIT
  verifyRetainedCardClip(true);
#else
  std::printf("[skip] Card differing clip flag (lifecycle audit asserts here)\n");
#endif
}

void testClipChildrenBoundaryRetained()
{
  using namespace loka::app;
  using namespace loka::app::scene;
  // Root goes through reconcileParkedBranch; its child goes through ACTION_RETAIN
  // and recursive reconciliation. Red: the generic applier changes both flags.
  ClipReentryHarness owner((BoundaryPropsFor<ClipReentryHarness>()));
  ComponentContext context;
  context.setBoundary(&owner);
  context.setStateOwner(&owner);
  Box original = Box().width(20).clipChildren();
  original << Box().width(30).clipChildren();
  Node *node = owner.materialize(context, original);
  LOKA_VERIFY(node && node->asNestable()->childrenHead());
  Node *child = node->asNestable()->childrenHead();
  Box next = Box().width(40);
  Box nextChild = Box().width(50);
#ifdef LOKA_LIFECYCLE_AUDIT
  next.clipChildren();
  nextChild.clipChildren();
  std::printf("[skip] Boundary differing clip flags (lifecycle audit asserts here)\n");
#endif
  next << nextChild;
  for (int repeat = 0; repeat != 2; ++repeat)
  {
    LOKA_VERIFY(owner.reconcile(context, node, next));
    LOKA_VERIFY(node->asNestable()->childrenHead() == child);
    LOKA_VERIFY(node->asBoxNode()->props.width == 40 && child->asBoxNode()->props.width == 50);
    LOKA_VERIFY(node->asBoxNode()->props.childClip_ == layout::CHILD_CLIP_OWN_RECT);
    LOKA_VERIFY(child->asBoxNode()->props.childClip_ == layout::CHILD_CLIP_OWN_RECT);
  }
  DestroyHeapNode(node);
}

void testClipChildrenMatchBirthAndParkedReentry()
{
  using namespace loka::app;
  using namespace loka::app::scene;
  using namespace loka::dsl::testing;
  using namespace PropsReconciliationSupport;
  // Characterization: separate arms are born with their own flags. Reentry
  // retains the original node and reapplies the declared width with an equal flag.
  loka::core::MutableState<int> selection(0);
  MatchDefinition<int> declaration = Match(selection);
  declaration.arm(0, Box().width(20).clipChildren()).otherwise(Box().width(30));
  NullScenePlatformController platform;
  Scene scene((Boundary<Tree<MatchDefinition<int> > >(Props<MatchDefinition<int> >(&declaration))));
  LOKA_VERIFY(scene.mount(&platform));
  SceneTestAccess::updateAttached(scene, true);
  settle(scene);
  Node *firstBranch = root(scene)->childrenHead();
  BoxNode *first = firstBranch->asNestable()->childrenHead()->asBoxNode();
  LOKA_VERIFY(first && first->props.childClip_ == layout::CHILD_CLIP_OWN_RECT);
  selection.set(1);
  settle(scene);
  BoxNode *second = root(scene)->childrenHead()->asNestable()->childrenHead()->asBoxNode();
  LOKA_VERIFY(second && second != first && second->props.childClip_ == layout::CHILD_CLIP_NONE);
  first->props.width = 99; // Test-only sentinel proving parked reentry applies the definition.
  selection.set(0);
  settle(scene);
  LOKA_VERIFY(root(scene)->childrenHead() == firstBranch);
  LOKA_VERIFY(first->props.width == 20 && first->props.childClip_ == layout::CHILD_CLIP_OWN_RECT);
  SceneTestAccess::unmount(scene);
}

void testClipChildrenDetachedDelivery()
{
  using namespace loka::app;
  using namespace loka::app::scene;
  using namespace loka::dsl::testing;
  using namespace PropsReconciliationSupport;
  // Characterization of the detached delivery route with an equal birth flag.
  // Differing flags are pinned at the same reconciliation door by BoundaryRetained.
  loka::core::MutableState<bool> shown(true);
  PolicyScopeDefinition scope;
  scope.deliverWhileDetached() << Box().width(20).clipChildren();
  ShowDefinition declaration = Show(shown);
  declaration << scope;
  NullScenePlatformController platform;
  Scene scene((Boundary<Tree<ShowDefinition> >(Props<ShowDefinition>(&declaration))));
  LOKA_VERIFY(scene.mount(&platform));
  SceneTestAccess::updateAttached(scene, true);
  settle(scene);
  Node *branch = root(scene)->childrenHead();
  BoxNode *box = branch->asNestable()->childrenHead()->asBoxNode();
  LOKA_VERIFY(box && box->props.childClip_ == layout::CHILD_CLIP_OWN_RECT);
  shown.set(false);
  settle(scene);
  LOKA_VERIFY(box->lifecycleFact() != NODE_FACT_ATTACHED);
  box->props.width = 99; // Test-only sentinel: the parked definition must be delivered again.
  root(scene)->markViewDirty(NODE_DIRTY_PROPS);
  settle(scene);
  LOKA_VERIFY(box->props.width == 20 && box->props.childClip_ == layout::CHILD_CLIP_OWN_RECT);
  LOKA_VERIFY(box->lifecycleFact() != NODE_FACT_ATTACHED);
  shown.set(true);
  settle(scene);
  LOKA_VERIFY(root(scene)->childrenHead() == branch);
  SceneTestAccess::unmount(scene);
}
