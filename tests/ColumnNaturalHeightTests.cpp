#include "ColumnNaturalHeightTests.hpp"
#include "support/TestVerify.hpp"
#include "app/nodes/nestable/RowColumn.hpp"
#include "app/nodes/nestable/Box.hpp"
#include "app/nodes/nestable/ScrollView.hpp"
#include "app/RectSurface.hpp"
#include "core/StateTracker.hpp"
#include "core/util/OwnedDef.hpp"
#include "platform/null/NullScenePlatformController.hpp"

namespace
{
  using namespace loka::app;
  using namespace loka::app::scene;

  LayoutState columnHeightSeat()
  {
    LayoutState state;
    state.x = 0;
    state.y = 10;
    state.width = 200;
    state.height = 100;
    state.spacing = 4;
    state.lineHeight = 12;
    return state;
  }
} // namespace

void testColumnNaturalHeightOffersAndExtent()
{
  for (int scrolled = 0; scrolled < 2; ++scrolled)
  {
    for (int natural = 0; natural < 2; ++natural)
    {
      NullScenePlatformController platform;
      loka::core::MutableState<loka::core::Frame> firstExtent, lastExtent;
      loka::core::PushStateTracker tracker;
      tracker.addState(&firstExtent);
      tracker.addState(&lastExtent);
      NodeState<loka::core::Frame> firstState(&firstExtent, &tracker), lastState(&lastExtent, &tracker);
      RectSurfaceProps firstProps, lastProps;
      firstProps.laidOutExtent(firstState);
      lastProps.size(30, 15).laidOutExtent(lastState);
      ScrollViewNode scroll((ScrollViewProps()));
      VStack definition;
      definition.gap(4);
      if (natural)
        definition.naturalHeight();
      StackNode *column = new StackNode(definition.props);
      scroll.addChild(column);
      column->addChild(new RectSurfaceNode(firstProps));
      column->addChild(Box().height(20).create());
      column->addChild(new RectSurfaceNode(lastProps));
      // The undeclared surface consumes the offer; declared children keep 20/15.
      const int extent =
          platform.projectLayoutForTesting(scrolled ? static_cast<Node *>(&scroll) : column, columnHeightSeat());
      const int contentBottom = 10 + (natural ? 0 : 100) + 20 + 15 + 2 * 4;
      LOKA_VERIFY(firstExtent.get().height == (natural ? 0 : 100));
      LOKA_VERIFY(lastExtent.get().height == 15);
      LOKA_VERIFY(lastExtent.get().y + lastExtent.get().height == contentBottom);
      LOKA_VERIFY(extent == (scrolled ? 110 : contentBottom));
    }
  }
}

void testColumnNaturalHeightPropsAndRetention()
{
  StackProps defaults;
  StackProps columnProps(STACK_AXIS_COLUMN);
  loka::core::MutableState<StackAxis> axis(STACK_AXIS_COLUMN);
  StackProps live(&axis);
  LOKA_VERIFY(defaults.columnUndeclaredHeight_ == COLUMN_UNDECLARED_HEIGHT_REMAINING);
  LOKA_VERIFY(columnProps.columnUndeclaredHeight_ == COLUMN_UNDECLARED_HEIGHT_REMAINING);
  LOKA_VERIFY(live.columnUndeclaredHeight_ == COLUMN_UNDECLARED_HEIGHT_REMAINING);
  VStack natural;
  natural.naturalHeight();
  LOKA_VERIFY(columnProps < natural.props);
  LOKA_VERIFY(!(natural.props < columnProps));
  VStack copy(natural);
  LOKA_VERIFY(copy.props.columnUndeclaredHeight_ == COLUMN_UNDECLARED_HEIGHT_NATURAL);
  loka::core::OwnedDef<NodeDefinitionBase> clone(natural.clone());
  LOKA_VERIFY(clone.isSet());
  const StackProps *clonedProps = static_cast<const StackProps *>(clone->propsBase());
  LOKA_VERIFY(clonedProps->columnUndeclaredHeight_ == COLUMN_UNDECLARED_HEIGHT_NATURAL);

  NullScenePlatformController platform;
  StackNode column(columnProps);
  column.setPropsTypeId(StackProps::staticTypeId());
  column.addChild(RectSurface().create());
  LOKA_VERIFY(platform.projectLayoutForTesting(&column, columnHeightSeat()) == 110);
  column.takeLayoutInputs();
  LOKA_VERIFY(natural.applyPropsToNode(&column));
  LOKA_VERIFY((column.takeLayoutInputs() & NODE_DIRTY_LAYOUT) != 0);
  LOKA_VERIFY(platform.projectLayoutForTesting(&column, columnHeightSeat()) == 10);
}
