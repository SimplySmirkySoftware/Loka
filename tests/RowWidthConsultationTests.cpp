#include "app/nodes/nestable/Grid.hpp"
#include "app/nodes/controls/Card.hpp"
#include "../apple/toolbox/src/ToolboxScrollViewDecisions.hpp"
#include "app/nodes/nestable/ScrollView.hpp"
#include "app/nodes/controls/Ribbon.hpp"
#include <climits>
#include <cstdlib>
#include "platform/StringUTF8.hpp"
#include <cstdio>
#include "RowWidthConsultationTests.hpp"

#include "app/OpenFileDialog.hpp"
#include "app/nodes/controls/Button.hpp"
#include "app/RectSurface.hpp"
#include "app/layout/LayoutHeuristics.hpp"
#include "app/layout/RowLayout.hpp"
#include "app/nodes/ImageView.hpp"
#include "app/nodes/Text.hpp"
#include "app/nodes/AttributedText.hpp"
#include "app/nodes/boundary/StdComposition.hpp"
#include "app/nodes/nestable/Box.hpp"
#include "app/nodes/nestable/Fragment.hpp"
#include "app/nodes/nestable/RowColumn.hpp"
#include "app/nodes/nestable/Show.hpp"
#include "app/nodes/nestable/Keyed.hpp"
#include "app/scene/node/ComponentNode.hpp"
#include "platform/null/context/NullTextContext.hpp"
#include "app/scene/Scene.hpp"
#include "app/scene/state/NodeState.hpp"
#include "core/StateTracker.hpp"
#include "core/util/StateTrackerGuard.hpp"
#include "platform/null/NullScenePlatformController.hpp"
#include "support/TestVerify.hpp"
#include "testing/scene/SceneTestFlow.hpp"

namespace
{
  struct RowTextSeatRecord
  {
    RowTextSeatRecord()
        : calls(0),
          state()
    {
    }

    int calls;
    loka::app::scene::LayoutState state;
  };

  RowTextSeatRecord *g_rowTextSeatRecord = 0;

  class RowSeatTextNode : public loka::app::TextNode
  {
  public:
    typedef loka::app::TextTypeTag TypeTag;

    explicit RowSeatTextNode(const loka::app::TextProps &props)
        : loka::app::TextNode(props)
    {
    }

    virtual short layoutProjected(loka::app::scene::IPlatformController *controller,
                                  loka::app::scene::LayoutState &state)
    {
      if (g_rowTextSeatRecord)
      {
        ++g_rowTextSeatRecord->calls;
        g_rowTextSeatRecord->state = state;
      }
      return loka::app::TextNode::layoutProjected(controller, state);
    }
  };

  struct RowSeatTextDefinition : public loka::app::scene::NodeDefinition<loka::app::TextProps, RowSeatTextNode>
  {
    typedef loka::app::scene::NodeDefinition<loka::app::TextProps, RowSeatTextNode> BaseType;

    explicit RowSeatTextDefinition(const char *text)
        : BaseType(loka::app::TextProps(text))
    {
    }
  };

  loka::app::scene::LayoutState rowState(short width)
  {
    loka::app::scene::LayoutState state;
    state.width = width;
    state.height = 20;
    state.lineHeight = 20;
    return state;
  }

  template <int BoxHeight>
  class RowBranchWidthBoundaryNode
      : public loka::app::scene::BoundaryNodeFor<RowBranchWidthBoundaryNode<BoxHeight> >
  {
  public:
    typedef loka::app::scene::BoundaryPropsFor<RowBranchWidthBoundaryNode<BoxHeight> > Props;

    explicit RowBranchWidthBoundaryNode(const Props &props)
        : loka::app::scene::BoundaryNodeFor<RowBranchWidthBoundaryNode<BoxHeight> >(props),
          fixedArmShown_()
    {
      this->state(this->fixedArmShown_, false);
    }

    virtual void composeNode(loka::app::scene::NodeComposition &composition)
    {
      loka::app::BoxProps boxProps;
      boxProps.setSize(20, BoxHeight);
      loka::app::ShowDefinition fixedArm = loka::app::Show(*this->fixedArmShown_.state());
      fixedArm << loka::app::BoxDefinition(boxProps);

      loka::app::RowDefinition row;
      row << fixedArm << RowSeatTextDefinition("text");
      composition.declare(row);
    }

    void showFixedArm()
    {
      this->fixedArmShown_.set(true);
    }

  private:
    loka::app::scene::NodeState<bool> fixedArmShown_;
  };

  template <int BoxHeight>
  void verifyRowBranchArmSwitchRelayoutsWidthConsultation()
  {
    RowTextSeatRecord record;
    g_rowTextSeatRecord = &record;
    NullScenePlatformController platform;
    loka::app::scene::Scene scene(
        (loka::app::scene::Boundary<RowBranchWidthBoundaryNode<BoxHeight> >()));
    scene.mount(&platform);
    loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);

    RowBranchWidthBoundaryNode<BoxHeight> *boundary = static_cast<RowBranchWidthBoundaryNode<BoxHeight> *>(
        loka::dsl::testing::SceneTestAccess::rootBoundary(scene));
    LOKA_VERIFY(boundary != 0);
    const unsigned long layoutCalls = platform.onChangeCallCount();
    const int textCalls = record.calls;

    boundary->showFixedArm();
    if (scene.hasPendingInvalidation())
    {
      LOKA_VERIFY(scene.flushInvalidation());
    }

    LOKA_VERIFY(platform.onChangeCallCount() == layoutCalls + 1);
    LOKA_VERIFY((platform.lastOnChangeFlags() & loka::app::scene::NODE_DIRTY_CHILD) != 0);
    LOKA_VERIFY((platform.lastOnChangeFlags() & loka::app::scene::NODE_DIRTY_LAYOUT) != 0);
    LOKA_VERIFY(record.calls == textCalls + 1);
    LOKA_VERIFY(record.state.x == 24);
    LOKA_VERIFY(record.state.width == 76);

    loka::dsl::testing::SceneTestAccess::unmount(scene);
    g_rowTextSeatRecord = 0;
  }
} // namespace

void testRowWidthOnlyBoxLeavesOnlyRemainingSeatForText()
{
  loka::app::StackNode row((loka::app::StackProps(loka::app::STACK_AXIS_ROW)));
  loka::app::BoxProps boxProps;
  boxProps.setSize(200, 0);
  row.addChild(new loka::app::BoxNode(boxProps));
  row.addChild(new RowSeatTextNode(loka::app::TextProps("text")));

  RowTextSeatRecord record;
  g_rowTextSeatRecord = &record;
  NullScenePlatformController platform;
  platform.projectLayoutForTesting(&row, rowState(500));
  g_rowTextSeatRecord = 0;

  LOKA_VERIFY(record.calls == 1);
  LOKA_VERIFY(record.state.x == 204);
  LOKA_VERIFY(record.state.width == 296);
}

void testRowFixedBoxWidthLeavesOnlyRemainingSeatForText()
{
  loka::app::StackNode row((loka::app::StackProps(loka::app::STACK_AXIS_ROW)));
  loka::app::BoxProps boxProps;
  boxProps.setSize(200, 10);
  row.addChild(new loka::app::BoxNode(boxProps));
  row.addChild(new RowSeatTextNode(loka::app::TextProps("text")));

  RowTextSeatRecord record;
  g_rowTextSeatRecord = &record;
  NullScenePlatformController platform;
  platform.projectLayoutForTesting(&row, rowState(500));
  g_rowTextSeatRecord = 0;

  LOKA_VERIFY(record.calls == 1);
  LOKA_VERIFY(record.state.x == 204);
  LOKA_VERIFY(record.state.width == 296);
}

void testRowEmptyFragmentConsumesNeitherSeatNorGap()
{
  loka::app::StackNode row((loka::app::StackProps(loka::app::STACK_AXIS_ROW)));
  loka::app::FragmentNode *empty = new loka::app::FragmentNode((loka::app::FragmentProps()));
  empty->setPropsTypeId(loka::app::FragmentProps::staticTypeId());
  row.addChild(empty);
  row.addChild(new RowSeatTextNode(loka::app::TextProps("text")));

  RowTextSeatRecord record;
  g_rowTextSeatRecord = &record;
  NullScenePlatformController platform;
  platform.projectLayoutForTesting(&row, rowState(500));
  g_rowTextSeatRecord = 0;

  LOKA_VERIFY(record.calls == 1);
  LOKA_VERIFY(record.state.x == 0);
  LOKA_VERIFY(record.state.width == 500);
}

void testRowMaterializedDialogConsumesNeitherSeatNorGap()
{
  // A Show-gated OpenFileDialog materializes as a Fragment holding the
  // dialog node while the dialog is up. The Row's width consultation must
  // treat it like an empty branch: no seat, no gap, so the visual sibling
  // keeps the whole width whether the dialog is open or not (#588).
  loka::core::EmitterState onResult;
  loka::app::OpenFileDialogProps dialogProps;
  dialogProps.onResult(&onResult);
  loka::app::FragmentNode *branch = new loka::app::FragmentNode((loka::app::FragmentProps()));
  branch->setPropsTypeId(loka::app::FragmentProps::staticTypeId());
  branch->addChild(new loka::app::OpenFileDialogNode(dialogProps));

  loka::app::StackNode row((loka::app::StackProps(loka::app::STACK_AXIS_ROW)));
  row.addChild(new RowSeatTextNode(loka::app::TextProps("text")));
  row.addChild(branch);
  row.addChild(new loka::app::OpenFileDialogNode(dialogProps));

  RowTextSeatRecord record;
  g_rowTextSeatRecord = &record;
  NullScenePlatformController platform;
  platform.projectLayoutForTesting(&row, rowState(500));
  g_rowTextSeatRecord = 0;

  LOKA_VERIFY(record.calls == 1);
  LOKA_VERIFY(record.state.x == 0);
  LOKA_VERIFY(record.state.width == 500);
}

void testRowMaterializedDialogDoesNotRaiseAlignedRowHeight()
{
  // An aligned Row measures every child for the row height. The dialog
  // contributes no height, so a surface shorter than the fallback text
  // height keeps the row at its own height and lands at y=0 instead of
  // being centred inside a 20px row the dialog would have opened (#588).
  NullScenePlatformController platform;
  loka::core::MutableState<loka::core::Frame> extent;
  loka::core::PushStateTracker tracker;
  tracker.addState(&extent);
  loka::app::scene::NodeState<loka::core::Frame> extentState(&extent, &tracker);
  loka::app::RectSurfaceProps surfaceProps;
  surfaceProps.size(50, 8).laidOutExtent(extentState);
  loka::core::EmitterState onResult;
  loka::app::OpenFileDialogProps dialogProps;
  dialogProps.onResult(&onResult);

  loka::app::StackProps rowProps(loka::app::STACK_AXIS_ROW);
  rowProps.alignVertical(loka::app::VERTICAL_ALIGNMENT_CENTER);
  loka::app::StackNode row(rowProps);
  row.addChild(new loka::app::RectSurfaceNode(surfaceProps));
  row.addChild(new loka::app::OpenFileDialogNode(dialogProps));

  platform.projectLayoutForTesting(&row, rowState(500));

  LOKA_VERIFY(extent.get() == loka::core::Frame(0, 0, 50, 8));
}

void testRowBranchArmSwitchRelayoutsWidthConsultation()
{
  verifyRowBranchArmSwitchRelayoutsWidthConsultation<10>();
}

void testRowWidthOnlyBranchArmForwardsWidthClaim()
{
  verifyRowBranchArmSwitchRelayoutsWidthConsultation<0>();
}

void testRowHeightOnlyBoxKeepsFlexSeat()
{
  loka::app::StackNode row((loka::app::StackProps(loka::app::STACK_AXIS_ROW)));
  loka::app::BoxProps boxProps;
  boxProps.setSize(0, 10);
  row.addChild(new loka::app::BoxNode(boxProps));
  row.addChild(new RowSeatTextNode(loka::app::TextProps("text")));

  RowTextSeatRecord record;
  g_rowTextSeatRecord = &record;
  NullScenePlatformController platform;
  platform.projectLayoutForTesting(&row, rowState(500));
  g_rowTextSeatRecord = 0;

  LOKA_VERIFY(record.calls == 1);
  LOKA_VERIFY(record.state.x == 252);
  LOKA_VERIFY(record.state.width == 248);
}

void testRowWidthOnlyBoxUsesRowHeight()
{
  loka::app::StackNode row((loka::app::StackProps(loka::app::STACK_AXIS_ROW)));
  loka::app::BoxProps boxProps;
  boxProps.setSize(200, 0);
  loka::app::BoxNode *box = new loka::app::BoxNode(boxProps);
  RowTextSeatRecord record;
  box->addChild(new RowSeatTextNode(loka::app::TextProps("box")));
  row.addChild(box);

  g_rowTextSeatRecord = &record;
  NullScenePlatformController platform;
  platform.projectLayoutForTesting(&row, rowState(500));
  g_rowTextSeatRecord = 0;

  LOKA_VERIFY(record.calls == 1);
  LOKA_VERIFY(record.state.x == 0);
  LOKA_VERIFY(record.state.width == 200);
  LOKA_VERIFY(record.state.height == 20);
}

void testColumnWidthOnlyBoxUsesDeclaredWidth()
{
  loka::app::StackProps columnProps(loka::app::STACK_AXIS_COLUMN);
  columnProps.alignHorizontal(loka::app::HORIZONTAL_ALIGNMENT_LEADING);
  loka::app::StackNode column(columnProps);
  loka::app::BoxProps boxProps;
  boxProps.setSize(200, 0);
  loka::app::BoxNode *box = new loka::app::BoxNode(boxProps);
  RowTextSeatRecord record;
  box->addChild(new RowSeatTextNode(loka::app::TextProps("box")));
  column.addChild(box);

  g_rowTextSeatRecord = &record;
  NullScenePlatformController platform;
  platform.projectLayoutForTesting(&column, rowState(500));
  g_rowTextSeatRecord = 0;

  LOKA_VERIFY(record.calls == 1);
  LOKA_VERIFY(record.state.x == 0);
  LOKA_VERIFY(record.state.width == 200);
}

void testRowOversizedFixedBoxKeepsDeclaredSeatForSibling()
{
  loka::app::StackNode row((loka::app::StackProps(loka::app::STACK_AXIS_ROW)));
  loka::app::BoxProps boxProps;
  boxProps.setSize(600, 10);
  row.addChild(new loka::app::BoxNode(boxProps));
  row.addChild(new RowSeatTextNode(loka::app::TextProps("text")));

  RowTextSeatRecord record;
  g_rowTextSeatRecord = &record;
  NullScenePlatformController platform;
  platform.projectLayoutForTesting(&row, rowState(500));
  g_rowTextSeatRecord = 0;

  LOKA_VERIFY(record.calls == 1);
  LOKA_VERIFY(record.state.x == 604);
  LOKA_VERIFY(record.state.width == 0);
}

void testRowZeroWidthRowKeepsFixedSeatLiveAndGapped()
{
  loka::app::StackNode row((loka::app::StackProps(loka::app::STACK_AXIS_ROW)));
  loka::app::BoxProps boxProps;
  boxProps.setSize(200, 10);
  row.addChild(new loka::app::BoxNode(boxProps));
  row.addChild(new RowSeatTextNode(loka::app::TextProps("text")));

  RowTextSeatRecord record;
  g_rowTextSeatRecord = &record;
  NullScenePlatformController platform;
  platform.projectLayoutForTesting(&row, rowState(0));
  g_rowTextSeatRecord = 0;

  LOKA_VERIFY(record.calls == 1);
  LOKA_VERIFY(record.state.x == 204);
  LOKA_VERIFY(record.state.width == 0);
}

void testRowWidthHeuristicForwardsOnlySingleFragmentClaims()
{
  loka::app::ImageViewProps imageProps;
  imageProps.size(75, 20);
  loka::app::ImageViewNode *image = new loka::app::ImageViewNode(imageProps);

  loka::app::FragmentNode empty((loka::app::FragmentProps()));
  empty.setPropsTypeId(loka::app::FragmentProps::staticTypeId());
  LOKA_VERIFY(loka::app::layout::preferredChildWidthForRow(&empty) == 0);

  loka::app::FragmentNode single((loka::app::FragmentProps()));
  single.setPropsTypeId(loka::app::FragmentProps::staticTypeId());
  single.addChild(image);
  LOKA_VERIFY(loka::app::layout::preferredChildWidthForRow(&single) == 75);

  single.addChild(new loka::app::TextNode(loka::app::TextProps("text")));
  LOKA_VERIFY(loka::app::layout::preferredChildWidthForRow(&single) < 0);
}

namespace
{
  class NaturalWidthProbe : public loka::app::layout::INaturalSizeSource
  {
  public:
    explicit NaturalWidthProbe(loka::app::scene::Node *answered)
        : answered_(answered), calls_(0) {}
    virtual bool queryNaturalWidth(loka::app::scene::Node *child, short &width) const
    {
      ++this->calls_;
      if (child != this->answered_)
        return false;
      width = 37;
      return true;
    }
    loka::app::scene::Node *answered_;
    mutable int calls_;
  };
}

namespace
{
  /** Records Row offers while Null supplies natural sizes and lays out each child. */
  struct SpacerRowOffers
  {
    NullScenePlatformController platform;
    loka::core::Frame frames[3];
    unsigned count;

    SpacerRowOffers()
        : count(0)
    {
    }

    static int layout(void *context, loka::app::scene::Node *child, const loka::app::scene::LayoutState &state)
    {
      SpacerRowOffers &offers = *static_cast<SpacerRowOffers *>(context);
      LOKA_VERIFY(offers.count < 3);
      offers.frames[offers.count++] = loka::core::Frame(state.x, state.y, state.width, state.height);
      return offers.platform.projectLayoutForTesting(child, state);
    }
  };

  void layoutSpacerRow(loka::app::StackNode &row, short width, SpacerRowOffers &offers)
  {
    loka::app::layout::RowLayoutMetrics metrics;
    // Same metrics as the registered Null Row handler and the 1c height pins.
    metrics.fallbackHeight = metrics.buttonHeight = metrics.editTextHeight = 10;
    metrics.popupMenuHeight = metrics.textHeight = metrics.imageFallbackHeight = 10;
    loka::app::layout::computeRowLayoutResultY(&row,
                                               rowState(width),
                                               metrics,
                                               loka::app::layout::RailSpacing(12, 4, 9),
                                               &offers.platform,
                                               &offers,
                                               &SpacerRowOffers::layout,
                                               4,
                                               0);
    LOKA_VERIFY(offers.count == 3);
  }
} // namespace

void testSpacerNaturalRowRemainderAndOverflow()
{
  using namespace loka::app;
  SpacerRowOffers wide;
  SpacerRowOffers narrow;
  SpacerRowOffers split;
  StackNode row(Row().naturalWidth().gap(4).props);
  row.addChild(new ButtonNode(Button("A").props)); // Null natural width 24.
  row.addChild(new BoxNode(Spacer().props));
  row.addChild(new ButtonNode(Button("BB").props)); // Null natural width 32.
  layoutSpacerRow(row, 101, wide);
  LOKA_VERIFY(wide.frames[0] == loka::core::Frame(0, 0, 24, 20));
  LOKA_VERIFY(wide.frames[1] == loka::core::Frame(28, 0, 37, 20));
  LOKA_VERIFY(wide.frames[2] == loka::core::Frame(69, 0, 32, 20));
  LOKA_VERIFY(wide.frames[2].x + wide.frames[2].width == 101);
  layoutSpacerRow(row, 50, narrow);
  LOKA_VERIFY(narrow.frames[0] == loka::core::Frame(0, 0, 24, 20));
  LOKA_VERIFY(narrow.frames[1] == loka::core::Frame(28, 0, 0, 20));
  LOKA_VERIFY(narrow.frames[2] == loka::core::Frame(32, 0, 32, 20));
  LOKA_VERIFY(narrow.frames[2].x + narrow.frames[2].width == 64);

  StackNode centered(Row().naturalWidth().gap(4).props);
  centered.addChild(new BoxNode(Spacer().props));
  centered.addChild(new ButtonNode(Button("A").props));
  centered.addChild(new BoxNode(Spacer().props));
  layoutSpacerRow(centered, 101, split);
  // 101 - 24 - 8 = 69: the front gets the odd unit, 35 then 34.
  LOKA_VERIFY(split.frames[0] == loka::core::Frame(0, 0, 35, 20));
  LOKA_VERIFY(split.frames[1] == loka::core::Frame(39, 0, 24, 20));
  LOKA_VERIFY(split.frames[2] == loka::core::Frame(67, 0, 34, 20));
}

void testSpacerSharedRowEqualShares()
{
  using namespace loka::app;
  SpacerRowOffers offers;
  StackNode row(Row().gap(4).props);
  row.addChild(new TextNode(Text("A").props));
  row.addChild(new BoxNode(Spacer().props));
  row.addChild(new TextNode(Text("BB").props));
  layoutSpacerRow(row, 100, offers);
  // 100 - 8 = 92: 31, 31, 30, with remainder units at the front.
  LOKA_VERIFY(offers.frames[0] == loka::core::Frame(0, 0, 31, 20));
  LOKA_VERIFY(offers.frames[1] == loka::core::Frame(35, 0, 31, 20));
  LOKA_VERIFY(offers.frames[2] == loka::core::Frame(70, 0, 30, 20));
}

void testSpacerRowMatchesEmptyBoxFrames()
{
  using namespace loka::app;
  for (int policy = 0; policy < 2; ++policy)
    for (int width = 50; width <= 101; width += 51)
    {
      StackProps props = Row().gap(4).props;
      props.rowUndeclaredWidth_ = policy ? ROW_UNDECLARED_WIDTH_NATURAL : ROW_UNDECLARED_WIDTH_SHARED;
      SpacerRowOffers spacerOffers;
      SpacerRowOffers boxOffers;
      StackNode spacerRow(props);
      StackNode boxRow(props);
      spacerRow.addChild(new ButtonNode(Button("A").props));
      spacerRow.addChild(new BoxNode(Spacer().props));
      spacerRow.addChild(new ButtonNode(Button("BB").props));
      boxRow.addChild(new ButtonNode(Button("A").props));
      boxRow.addChild(new BoxNode(Box().props));
      boxRow.addChild(new ButtonNode(Button("BB").props));
      layoutSpacerRow(spacerRow, static_cast<short>(width), spacerOffers);
      layoutSpacerRow(boxRow, static_cast<short>(width), boxOffers);
      for (unsigned child = 0; child < 3; ++child)
        LOKA_VERIFY(spacerOffers.frames[child] == boxOffers.frames[child]);
    }
}

void testRowNaturalWidthConsultation()
{
  using namespace loka::app;
  using namespace loka::app::layout;
  StackNode row((StackProps()));
  TextNode *answered = new TextNode(TextProps("answered"));
  TextNode *declined = new TextNode(TextProps("declined"));
  BoxProps fixedProps;
  fixedProps.setSize(120, 0);
  BoxNode *fixed = new BoxNode(fixedProps);
  row.addChild(answered);
  row.addChild(declined);
  row.addChild(fixed);
  NaturalWidthProbe source(answered);
  RowWidthConsultation widths(row.childrenHead(),
                              row.childrenCount(),
                              300,
                              4,
                              loka::app::layout::RailSpacing(12, 4, 9),
                              ROW_UNDECLARED_WIDTH_NATURAL,
                              &source);
  LOKA_VERIFY(source.calls_ == 2);
  const RowChildWidth a = widths.next(answered);
  const RowChildWidth b = widths.next(declined);
  const RowChildWidth c = widths.next(fixed);
  LOKA_VERIFY(a.width() == 37 && a.isLiveSeat() && !a.hasGapBefore());
  LOKA_VERIFY(b.width() == 135 && b.hasGapBefore());
  LOKA_VERIFY(c.width() == 120 && c.hasGapBefore());
  LOKA_VERIFY(source.calls_ == 4);
  RowWidthConsultation narrow(row.childrenHead(),
                              row.childrenCount(),
                              10,
                              4,
                              loka::app::layout::RailSpacing(12, 4, 9),
                              ROW_UNDECLARED_WIDTH_NATURAL,
                              &source);
  LOKA_VERIFY(narrow.next(answered).width() == 37);
  LOKA_VERIFY(narrow.next(declined).width() == 0);
  LOKA_VERIFY(narrow.next(fixed).width() == 120);

}

void testRowSharedWidthNeverQueriesSource()
{
  using namespace loka::app;
  using namespace loka::app::layout;
  StackNode row((StackProps()));
  TextNode *a = new TextNode(TextProps("A"));
  TextNode *b = new TextNode(TextProps("B"));
  row.addChild(a);
  row.addChild(b);
  NaturalWidthProbe source(a);
  RowWidthConsultation shared(row.childrenHead(),
                              row.childrenCount(),
                              300,
                              4,
                              loka::app::layout::RailSpacing(12, 4, 9),
                              ROW_UNDECLARED_WIDTH_SHARED,
                              &source);
  LOKA_VERIFY(source.calls_ == 0);
  LOKA_VERIFY(shared.next(a).width() == 148);
  LOKA_VERIFY(shared.next(b).width() == 148);
  LOKA_VERIFY(source.calls_ == 0);
  RowWidthConsultation absent(row.childrenHead(),
                              row.childrenCount(),
                              300,
                              4,
                              loka::app::layout::RailSpacing(12, 4, 9),
                              ROW_UNDECLARED_WIDTH_NATURAL,
                              0); // Null-source fallback under test.
  LOKA_VERIFY(absent.next(a).width() == 148);
  LOKA_VERIFY(absent.next(b).width() == 148);
  StackProps defaults;
  StackProps natural;
  natural.rowUndeclaredWidth_ = ROW_UNDECLARED_WIDTH_NATURAL;
  LOKA_VERIFY(defaults.rowUndeclaredWidth_ == ROW_UNDECLARED_WIDTH_SHARED);
  LOKA_VERIFY(defaults < natural);
  LOKA_VERIFY(!(natural < defaults));
}

void testNullNaturalWidthDeclinesUnrepresentableWidth()
{
  NullScenePlatformController platform;
  loka::app::ButtonNode button(loka::app::ButtonProps().text(loka::core::String(std::string(8192, 'A'))));
  short width = 13;
  LOKA_VERIFY(!platform.queryNaturalWidth(&button, width));
  LOKA_VERIFY(width == 13);
  LOKA_VERIFY(!platform.queryNaturalWidth(&button, width));
  LOKA_VERIFY(width == 13);
}

namespace
{
  class StackWidthProbe : public loka::app::layout::INaturalSizeSource
  {
  public:
    StackWidthProbe(short a, short b, short caption)
        : calls(0),
          a_(a),
          b_(b),
          caption_(caption)
    {
    }
    virtual bool queryNaturalWidth(loka::app::scene::Node *child, short &width) const
    {
      LOKA_VERIFY(child->asStackNode() == 0);
      ++this->calls;
      loka::app::TextNode *text = child->asTextNode();
      if (!text)
        return false;
      const loka::core::String label = text->props.text_->get();
      width = label.equals(loka::core::String::Literal("a"))   ? this->a_
              : label.equals(loka::core::String::Literal("b")) ? this->b_
                                                               : this->caption_;
      return width > 0;
    }
    mutable int calls;

  private:
    short a_, b_, caption_;
  };

  loka::app::StackNode *widthRow(loka::app::RowUndeclaredWidth policy)
  {
    using namespace loka::app;
    StackProps props(STACK_AXIS_ROW);
    props.rowUndeclaredWidth_ = policy;
    StackNode *row = new StackNode(props);
    row->addChild(new TextNode(TextProps("a")));
    row->addChild(new TextNode(TextProps("b")));
    return row;
  }

  int consultedStackWidth(loka::app::StackNode &outer, const StackWidthProbe &probe)
  {
    using namespace loka::app;
    layout::RowWidthConsultation widths(outer.childrenHead(),
                                        outer.childrenCount(),
                                        300,
                                        4,
                                        loka::app::layout::RailSpacing(12, 4, 9),
                                        ROW_UNDECLARED_WIDTH_NATURAL,
                                        &probe);
    return widths.next(outer.childrenHead()).width();
  }
} // namespace

void testStackNaturalWidthComposesGroup()
{
  using namespace loka::app;
  StackNode outer((StackProps()));
  StackNode *group = new StackNode(StackProps(STACK_AXIS_COLUMN));
  outer.addChild(group);
  group->addChild(widthRow(ROW_UNDECLARED_WIDTH_NATURAL));
  group->addChild(new TextNode(TextProps("caption")));
  StackWidthProbe itemsWider(30, 40, 20);
  LOKA_VERIFY(consultedStackWidth(outer, itemsWider) == 74);
  LOKA_VERIFY(itemsWider.calls == 6);
  StackWidthProbe captionWider(10, 10, 50);
  LOKA_VERIFY(consultedStackWidth(outer, captionWider) == 50);
}

void testStackNaturalWidthSharedAndDeclaredSeats()
{
  using namespace loka::app;
  StackNode outer((StackProps()));
  StackNode *row = widthRow(ROW_UNDECLARED_WIDTH_SHARED);
  outer.addChild(row);
  StackWidthProbe probe(30, 40, 20);
  LOKA_VERIFY(consultedStackWidth(outer, probe) == 84);
  BoxProps fixed;
  fixed.setSize(25, 0);
  row->addChild(new BoxNode(fixed));
  LOKA_VERIFY(consultedStackWidth(outer, probe) == 113);
  row->props.rowUndeclaredWidth_ = ROW_UNDECLARED_WIDTH_NATURAL;
  FragmentNode *empty = new FragmentNode((FragmentProps()));
  empty->setPropsTypeId(FragmentProps::staticTypeId());
  row->addChild(empty);
  LOKA_VERIFY(consultedStackWidth(outer, probe) == 103);
}

void testStackNaturalWidthDeclinesIncompleteAndOverflow()
{
  using namespace loka::app;
  StackNode outer((StackProps()));
  StackNode *group = new StackNode(StackProps(STACK_AXIS_COLUMN));
  outer.addChild(group);
  group->addChild(widthRow(ROW_UNDECLARED_WIDTH_NATURAL));
  group->addChild(new TextNode(TextProps("caption")));
  outer.addChild(new TextNode(TextProps("caption")));
  StackWidthProbe refused(30, 40, 0);
  LOKA_VERIFY(consultedStackWidth(outer, refused) == 148);
  StackWidthProbe refusedItem(0, 40, 20);
  LOKA_VERIFY(consultedStackWidth(outer, refusedItem) == 276);
  StackWidthProbe overflow(32767, 32767, 20);
  LOKA_VERIFY(consultedStackWidth(outer, overflow) == 276);

  StackNode emptyOuter((StackProps()));
  StackNode *empty = new StackNode(StackProps(STACK_AXIS_ROW));
  emptyOuter.addChild(empty);
  LOKA_VERIFY(consultedStackWidth(emptyOuter, refused) == 300);
  FragmentNode *fragment = new FragmentNode((FragmentProps()));
  fragment->setPropsTypeId(FragmentProps::staticTypeId());
  empty->addChild(fragment);
  LOKA_VERIFY(consultedStackWidth(emptyOuter, refused) == 300);
  empty->props.axis_ = STACK_AXIS_COLUMN;
  LOKA_VERIFY(consultedStackWidth(emptyOuter, refused) == 300);
}

void testStackNaturalWidthColumnDeclaredWidths()
{
  using namespace loka::app;
  StackNode outer((StackProps()));
  StackNode *column = new StackNode(StackProps(STACK_AXIS_COLUMN));
  outer.addChild(column);
  BoxProps box;
  box.setSize(60, 10);
  BoxNode *fixed = new BoxNode(box);
  column->addChild(fixed);
  column->addChild(new TextNode(TextProps("a")));
  StackWidthProbe probe(30, 40, 20);
  LOKA_VERIFY(consultedStackWidth(outer, probe) == 60);
  ImageViewProps imageProps;
  imageProps.size(75, 10);
  column->addChild(new ImageViewNode(imageProps));
  LOKA_VERIFY(consultedStackWidth(outer, probe) == 75);
  RectSurfaceProps surfaceProps;
  surfaceProps.size(90, 10);
  column->addChild(new RectSurfaceNode(surfaceProps));
  LOKA_VERIFY(consultedStackWidth(outer, probe) == 90);
  fixed->props.setSize(60, 0); // Width-only Boxes also declare a Column width.
  LOKA_VERIFY(consultedStackWidth(outer, probe) == 90);
}

void testStackNaturalWidthReadsLiveAxis()
{
  using namespace loka::app;
  loka::core::MutableState<StackAxis> axis(STACK_AXIS_ROW);
  loka::core::PushStateTracker tracker;
  tracker.addState(&axis);
  StackNode outer((StackProps()));
  StackNode *child = widthRow(ROW_UNDECLARED_WIDTH_NATURAL);
  child->props.axisState_ = &axis;
  outer.addChild(child);
  StackWidthProbe probe(30, 40, 20);
  LOKA_VERIFY(consultedStackWidth(outer, probe) == 74);
  {
    loka::core::StateTrackerGuard guard(&tracker);
    axis.set(STACK_AXIS_COLUMN);
  }
  LOKA_VERIFY(consultedStackWidth(outer, probe) == 40);
}

void testContentOverflowIgnoresOfferBoundText()
{
  using namespace loka::app;
  StackNode column((StackProps(STACK_AXIS_COLUMN)));
  column.addChild(new TextNode(TextProps("a")));
  BoxProps fixed;
  fixed.setSize(300, 10);
  column.addChild(new BoxNode(fixed));
  StackWidthProbe probe(500, 40, 20);
  short width = 19;
  LOKA_VERIFY(layout::queryContentOverflowWidth(&column, loka::app::layout::RailSpacing(12, 4, 9), &probe, width));
  LOKA_VERIFY(width == 300);
  LOKA_VERIFY(probe.calls == 0);
}

void testContentOverflowOfferBoundAnswersZero()
{
  using namespace loka::app;
  StackNode column((StackProps(STACK_AXIS_COLUMN)));
  column.addChild(new TextNode(TextProps("a")));
  StackWidthProbe probe(500, 40, 20);
  short width = 19;
  LOKA_VERIFY(layout::queryContentOverflowWidth(&column, loka::app::layout::RailSpacing(12, 4, 9), &probe, width));
  LOKA_VERIFY(width == 0);
  column.addChild(widthRow(ROW_UNDECLARED_WIDTH_SHARED));
  LOKA_VERIFY(layout::queryContentOverflowWidth(&column, loka::app::layout::RailSpacing(12, 4, 9), &probe, width));
  LOKA_VERIFY(width == 0);
  LOKA_VERIFY(probe.calls == 0);
  LOKA_VERIFY(layout::queryContentOverflowWidth(0, loka::app::layout::RailSpacing(12, 4, 9), 0, width));
  LOKA_VERIFY(width == 0);
}

void testContentOverflowNaturalDeclinePreservesOutput()
{
  using namespace loka::app;
  StackNode column((StackProps(STACK_AXIS_COLUMN)));
  column.addChild(widthRow(ROW_UNDECLARED_WIDTH_NATURAL));
  StackWidthProbe probe(30, 0, 20);
  short width = 19;
  LOKA_VERIFY(!layout::queryContentOverflowWidth(&column, loka::app::layout::RailSpacing(12, 4, 9), &probe, width));
  LOKA_VERIFY(width == 19);
}

void testContentOverflowNestedMaximumAndOverflow()
{
  using namespace loka::app;
  StackNode column((StackProps(STACK_AXIS_COLUMN)));
  StackNode *nested = new StackNode(StackProps(STACK_AXIS_COLUMN));
  column.addChild(nested);
  nested->addChild(widthRow(ROW_UNDECLARED_WIDTH_NATURAL));
  BoxProps fixed;
  fixed.setSize(60, 10);
  column.addChild(new BoxNode(fixed));
  StackWidthProbe probe(30, 40, 20);
  short width = 19;
  LOKA_VERIFY(layout::queryContentOverflowWidth(&column, loka::app::layout::RailSpacing(12, 4, 9), &probe, width));
  LOKA_VERIFY(width == 74);
  StackWidthProbe overflow(32767, 40, 20);
  LOKA_VERIFY(!layout::queryContentOverflowWidth(&column, loka::app::layout::RailSpacing(12, 4, 9), &overflow, width));
  LOKA_VERIFY(width == 74);
  ImageViewProps imageProps;
  imageProps.size(SHRT_MAX, 10);
  StackNode images((StackProps(STACK_AXIS_COLUMN)));
  ImageViewNode *image = new ImageViewNode(imageProps);
  images.addChild(image);
  LOKA_VERIFY(layout::queryContentOverflowWidth(&images, loka::app::layout::RailSpacing(12, 4, 9), 0, width));
  LOKA_VERIFY(width == SHRT_MAX);
  image->props.width_ = static_cast<int>(SHRT_MAX) + 1;
  LOKA_VERIFY(!layout::queryContentOverflowWidth(&images, loka::app::layout::RailSpacing(12, 4, 9), 0, width));
  LOKA_VERIFY(width == SHRT_MAX);
}

void testContentOverflowBoxPaddingIsOfferBound()
{
  using namespace loka::app;
  ScrollViewNode scroll((ScrollViewProps()));
  BoxProps props;
  props.padding = loka::app::layout::LayoutGap::of(8);
  BoxNode *box = new BoxNode(props);
  scroll.addChild(box);
  box->addChild(new TextNode(TextProps("a")));
  StackWidthProbe probe(500, 40, 20);
  short width = 19;
  LOKA_VERIFY(layout::queryContentOverflowWidth(&scroll, loka::app::layout::RailSpacing(12, 4, 9), &probe, width));
  LOKA_VERIFY(width == 0);
  LOKA_VERIFY(probe.calls == 0);
}

void testContentOverflowPassThroughIsTransparent()
{
  using namespace loka::app;
  // A Fragment (the shape a Show arm materializes as) lays its children out
  // in place under the same offer, so its declared-width child overflows.
  ScrollViewNode scroll((ScrollViewProps()));
  FragmentNode *fragment = new FragmentNode(FragmentProps());
  scroll.addChild(fragment);
  fragment->addChild(new TextNode(TextProps("offer-bound")));
  BoxProps fixed;
  fixed.setSize(300, 10);
  fragment->addChild(new BoxNode(fixed));
  StackWidthProbe probe(500, 40, 20);
  short width = 19;
  LOKA_VERIFY(layout::queryContentOverflowWidth(&scroll, loka::app::layout::RailSpacing(12, 4, 9), &probe, width));
  LOKA_VERIFY(width == 300);
  LOKA_VERIFY(probe.calls == 0);
  // Nested pass-through wrappers stay transparent; a decline inside propagates.
  FragmentNode *inner = new FragmentNode(FragmentProps());
  fragment->addChild(inner);
  inner->addChild(widthRow(ROW_UNDECLARED_WIDTH_NATURAL));
  StackWidthProbe refused(30, 0, 20);
  width = 19;
  LOKA_VERIFY(!layout::queryContentOverflowWidth(&scroll, loka::app::layout::RailSpacing(12, 4, 9), &refused, width));
  LOKA_VERIFY(width == 19);
}

void testNaturalRowSeesThroughPassThroughWrapper()
{
  using namespace loka::app;
  // A natural-width Row whose seat is a pass-through wrapper (a component such
  // as a JS Button, a Fragment) measures the wrapper as its widest child; a
  // declared width inside wins before the rail is asked, as in the stack paths.
  ScrollViewNode scroll((ScrollViewProps()));
  StackNode *row = widthRow(ROW_UNDECLARED_WIDTH_NATURAL);
  scroll.addChild(row);
  FragmentNode *wrapper = new FragmentNode(FragmentProps());
  row->addChild(wrapper);
  wrapper->addChild(new TextNode(TextProps("a")));
  ImageViewProps sized;
  sized.size(75, 10);
  wrapper->addChild(new ImageViewNode(sized));
  StackWidthProbe probe(30, 40, 20);
  short width = 19;
  LOKA_VERIFY(layout::queryContentOverflowWidth(&scroll, layout::RailSpacing(12, 4, 9), &probe, width));
  // widthRow seats "a" and "b" (30 + 40) and the wrapper (75) with two gaps.
  LOKA_VERIFY(width == 30 + 40 + 75 + 2 * 4);
  LOKA_VERIFY(probe.calls == 3);
  // A wrapper holding only a sized image never asks the rail.
  FragmentNode *imageOnly = new FragmentNode(FragmentProps());
  row->addChild(imageOnly);
  imageOnly->addChild(new ImageViewNode(sized));
  StackWidthProbe refusing(30, 40, 0);
  width = 19;
  LOKA_VERIFY(layout::queryContentOverflowWidth(&scroll, layout::RailSpacing(12, 4, 9), &refusing, width));
  LOKA_VERIFY(width == 30 + 40 + 75 + 75 + 3 * 4);
  // A wrapper whose only child the rail declines makes the Row decline.
  FragmentNode *declined = new FragmentNode(FragmentProps());
  row->addChild(declined);
  declined->addChild(new TextNode(TextProps("caption")));
  width = 19;
  LOKA_VERIFY(!layout::queryContentOverflowWidth(&scroll, layout::RailSpacing(12, 4, 9), &refusing, width));
  LOKA_VERIFY(width == 19);
}

void testContentOverflowRibbonUsesNaturalMeasurement()
{
  using namespace loka::app;
  StackNode column((StackProps(STACK_AXIS_COLUMN)));
  RibbonGroupNode *group = new RibbonGroupNode(RibbonGroupProps());
  column.addChild(group);
  group->addChild(widthRow(ROW_UNDECLARED_WIDTH_NATURAL));
  StackWidthProbe probe(30, 40, 20);
  short width = 19;
  LOKA_VERIFY(layout::queryContentOverflowWidth(&column, loka::app::layout::RailSpacing(12, 4, 9), &probe, width));
  LOKA_VERIFY(width == 74 + 2 * RibbonGroupNode::kFrameInset);
  StackWidthProbe refused(30, 0, 20);
  width = 19;
  LOKA_VERIFY(!layout::queryContentOverflowWidth(&column, loka::app::layout::RailSpacing(12, 4, 9), &refused, width));
  LOKA_VERIFY(width == 19);
}

void testBoxWidthDoorOrderAndSaturation()
{
  using namespace loka::app;
  loka::core::MutableState<int> live(125);
  LOKA_VERIFY(Box().size(200, 40).width(&live).width(static_cast<loka::core::State<int> *>(0)).props.effectiveWidth()
              == 200);
  LOKA_VERIFY(Box().size(200, 40).width(0).props.effectiveWidth() == 0);
  LOKA_VERIFY(Box().width(&live).width(300).props.effectiveWidth() == 300);
  LOKA_VERIFY(Box().width(300).width(&live).props.effectiveWidth() == 125);
  LOKA_VERIFY(Box().size(200, 40).width(static_cast<short>(0)).props.effectiveWidth() == 0);
  LOKA_VERIFY(Box().size(200, 40).width(false).props.effectiveWidth() == 0);
  LOKA_VERIFY(Box().width(40000).props.effectiveWidth() == SHRT_MAX);
  LOKA_VERIFY(Box().width(INT_MIN).props.effectiveWidth() == 0);
  live.set(40000);
  LOKA_VERIFY(Box().width(&live).props.effectiveWidth() == SHRT_MAX);
  live.set(INT_MIN);
  LOKA_VERIFY(Box().width(&live).props.effectiveWidth() == 0);
  live.set(-65535); // Narrowing first would turn this into a positive width.
  LOKA_VERIFY(Box().width(&live).props.effectiveWidth() == 0);
  LOKA_VERIFY(!Box().width(200).props.hasFixedSize());
  LOKA_VERIFY(Box().size(200, 40).props.hasFixedSize());
}

void testBoxWidthColumnAlignment()
{
  using namespace loka::app;
  const HorizontalAlignment alignments[] = {
      HORIZONTAL_ALIGNMENT_LEADING, HORIZONTAL_ALIGNMENT_CENTER, HORIZONTAL_ALIGNMENT_TRAILING};
  for (int mode = 0; mode < 5; ++mode)
  {
    for (int oversized = 0; oversized < 2; ++oversized)
    {
      StackProps props(STACK_AXIS_COLUMN);
      if (mode)
        props.alignHorizontal(alignments[mode == 4 ? 2 : mode - 1]);
      StackNode column(props);
      BoxNode *box = new BoxNode(Box().width(oversized ? 600 : 300).props);
      box->addChild(new RowSeatTextNode(TextProps("box")));
      if (mode == 4)
      {
        FragmentNode *fragment = new FragmentNode(FragmentProps());
        fragment->setPropsTypeId(FragmentProps::staticTypeId());
        fragment->addChild(box);
        column.addChild(fragment);
      }
      else
        column.addChild(box);
      RowTextSeatRecord record;
      g_rowTextSeatRecord = &record;
      NullScenePlatformController platform;
      platform.projectLayoutForTesting(&column, rowState(500));
      g_rowTextSeatRecord = 0;
      // The 500-wide Column has 200 spare units; oversized seats retain offset zero.
      LOKA_VERIFY(record.calls == 1);
      LOKA_VERIFY(record.state.x == (oversized || mode < 2 || mode == 4 ? 0 : (mode - 1) * 100));
      LOKA_VERIFY(record.state.width == (oversized ? 600 : 300));
      LOKA_VERIFY(record.state.height == 20);
    }
  }
}

void testBoxWidthParentOffersAndHeight()
{
  using namespace loka::app;
  for (int parent = 0; parent < 4; ++parent)
  {
    scene::NestableNode *root = 0;
    if (parent == 0)
      root = new FragmentNode(FragmentProps());
    else if (parent == 1)
      root = new ScrollViewNode(ScrollViewProps());
    else if (parent == 2)
      root = new CardNode(CardProps(CardStyle(CARD_SHADOW_DROP)));
    else
      root = new GridNode(GridProps(1, 2));
    BoxNode *box = new BoxNode(Box().width(300).padding(5).props);
    box->addChild(new RowSeatTextNode(TextProps("box")));
    root->addChild(box);
    RowTextSeatRecord record;
    g_rowTextSeatRecord = &record;
    NullScenePlatformController platform;
    const int resultY = platform.projectLayoutForTesting(root, rowState(200));
    g_rowTextSeatRecord = 0;
    // Padding consumes 10; the DROP Card (same insets on every rail) adds 3/6 before offering the Box.
    LOKA_VERIFY(record.calls == 1);
    LOKA_VERIFY(record.state.width == 290);
    LOKA_VERIFY(record.state.x == (parent == 2 ? 8 : 5));
    LOKA_VERIFY(record.state.height == (parent == 2 ? 1 : 10));
    LOKA_VERIFY(resultY == (parent == 1 ? 20 : parent == 2 ? 31 : 22));
    delete root;
  }
  for (int fixed = 0; fixed < 2; ++fixed)
  {
    BoxNode box(fixed ? Box().size(200, 40).props : Box().width(200).props);
    box.addChild(new RowSeatTextNode(TextProps("box")));
    RowTextSeatRecord record;
    g_rowTextSeatRecord = &record;
    NullScenePlatformController platform;
    const int resultY = platform.projectLayoutForTesting(&box, rowState(500));
    g_rowTextSeatRecord = 0;
    LOKA_VERIFY(record.calls == 1);
    LOKA_VERIFY(record.state.width == 200);
    LOKA_VERIFY(record.state.height == (fixed ? 40 : 20));
    LOKA_VERIFY(resultY == (fixed ? 40 : 12));
  }
}

void testBoxWidthOverflowEstimates()
{
  using namespace loka::app;
  for (int parent = 0; parent < 4; ++parent)
  {
    ScrollViewNode scroll((ScrollViewProps()));
    scene::NestableNode *content = &scroll;
    if (parent == 1)
      content = new StackNode(Column().props);
    else if (parent == 2)
      content = new StackNode(Row().naturalWidth().props);
    else if (parent == 3)
    {
      content = new CardNode(CardProps(CardStyle(CARD_SHADOW_DROP)));
      content->addChild(new StackNode(Column().props));
    }
    if (content != &scroll)
      scroll.addChild(content);
    if (parent == 3)
      content = static_cast<scene::NestableNode *>(content->childrenHead());
    content->addChild(new BoxNode(Box().width(800).props));
    short width = 0;
    NullScenePlatformController platform;
    const bool known = layout::queryContentOverflowWidth(&scroll, layout::RailSpacing(12, 4, 9), &platform, width);
    // The DROP Card (same insets on every rail) adds left=3 and right=6 to its content's 800-unit claim.
    LOKA_VERIFY(known);
    LOKA_VERIFY(width == (parent == 3 ? 809 : 800));
    LOKA_VERIFY(ToolboxScrollViewMaximum(width, 200) == (parent == 3 ? 609 : 600));
  }
}

namespace
{
  class ShrinkWidthProbe : public loka::app::layout::INaturalSizeSource
  {
  public:
    ShrinkWidthProbe()
        : calls(0)
    {
    }
    virtual bool queryNaturalWidth(loka::app::scene::Node *node, short &width) const
    {
      ++calls;
      loka::app::TextNode *text = node->asTextNode();
      if (!text)
      {
        if (node->asBoxNode() || node->asAttributedTextNode())
        {
          width = 200;
          return true;
        }
        return false;
      }
      std::string bytes;
      if (!loka::platform::CollectUtf8(text->props.text_->get(), bytes))
        return false;
      width = static_cast<short>(atoi(bytes.c_str()));
      return width > 0;
    }
    mutable int calls;
  };

  loka::app::TextNode *shrinkText(int width)
  {
    char buffer[16];
    std::snprintf(buffer, sizeof buffer, "%d", width);
    loka::app::TextProps props(buffer);
    props.blockStyle_ = loka::app::BlockStyle().truncation(loka::app::TEXT_TRUNCATION_ELLIPSIS);
    return new loka::app::TextNode(props);
  }

  void checkShrinkWidths(const int *natural, int count, int deficit, int gap)
  {
    using namespace loka::app;
    StackNode row(Row().naturalWidth().props);
    TextNode *seats[3];
    int sum = 0;
    int maximum = 0;
    for (int i = 0; i < count; ++i)
    {
      seats[i] = shrinkText(natural[i]);
      row.addChild(seats[i]);
      sum += natural[i];
      if (natural[i] > maximum)
        maximum = natural[i];
    }
    const int budget = sum - deficit < count ? count : sum - deficit;
    int cap = maximum;
    if (budget != sum)
    {
      cap = 1;
      for (int candidate = 1; candidate <= maximum; ++candidate)
      {
        int total = 0;
        for (int i = 0; i < count; ++i)
          total += natural[i] < candidate ? natural[i] : candidate;
        if (total <= budget)
          cap = candidate;
      }
    }
    int capped = 0;
    for (int i = 0; i < count; ++i)
      capped += natural[i] < cap ? natural[i] : cap;
    int remainder = budget - capped;
    ShrinkWidthProbe source;
    layout::RowWidthConsultation widths(row.childrenHead(),
                                        row.childrenCount(),
                                        sum + (count - 1) * gap - deficit,
                                        gap,
                                        layout::RailSpacing(12, gap, 9),
                                        ROW_UNDECLARED_WIDTH_NATURAL,
                                        &source);
    if (count == 1 || deficit == 0 || budget == sum)
      LOKA_VERIFY(source.calls == count);
    else
      LOKA_VERIFY(source.calls <= count * (count + 1));
    int actualSum = 0;
    for (int i = 0; i < count; ++i)
    {
      int expected = natural[i] < cap ? natural[i] : cap;
      if (natural[i] > cap && remainder > 0)
      {
        ++expected;
        --remainder;
      }
      const layout::RowChildWidth answer = widths.next(seats[i]);
      LOKA_VERIFY(answer.width() == expected);
      LOKA_VERIFY(answer.width() >= 1 && answer.width() <= natural[i]);
      LOKA_VERIFY(answer.hasGapBefore() == (i > 0));
      actualSum += answer.width();
    }
    LOKA_VERIFY(actualSum == budget);
    if (count == 1 || deficit == 0 || budget == sum)
      LOKA_VERIFY(source.calls == 2 * count);
  }
} // namespace

void testRowEllipsisShrinkIntegerRule()
{
  const int single[] = {260};
  checkShrinkWidths(single, 1, 86, 8);
  checkShrinkWidths(single, 1, 400, 8);
  const int unequal[] = {120, 40};
  checkShrinkWidths(unequal, 2, 60, 8);
  const int equal[] = {120, 120};
  checkShrinkWidths(equal, 2, 1, 8);
  const int ones[] = {1, 1, 1};
  checkShrinkWidths(ones, 3, 30, 8);
  for (int count = 1; count <= 3; ++count)
    for (int a = 1; a <= 8; ++a)
      for (int b = 1; b <= (count >= 2 ? 8 : 1); ++b)
        for (int c = 1; c <= (count >= 3 ? 8 : 1); ++c)
          for (int deficit = 0; deficit <= 30; ++deficit)
          {
            const int widths[] = {a, b, c};
            checkShrinkWidths(widths, count, deficit, 8);
          }
}

void testRowEllipsisShrinkClassificationAndSaturation()
{
  using namespace loka::app;
  using namespace loka::app::layout;
  for (int shape = 0; shape < 10; ++shape)
  {
    StackNode row(Row().naturalWidth().props);
    TextNode *text = shrinkText(200);
    scene::Node *seat = text;
    if (shape == 1)
      text->props.blockStyle_ = BlockStyle();
    if (shape == 2)
      text->props.blockStyle_.wrap(TEXT_WRAP_WORD);
    if (shape >= 3 && shape <= 6)
    {
      FragmentNode *wrapper = new FragmentNode(FragmentProps());
      wrapper->addChild(text);
      if (shape == 4)
        wrapper->addChild(shrinkText(100));
      if (shape == 5)
        wrapper->addChild(new BoxNode(Box().size(0, 0).props));
      if (shape == 6)
        wrapper->addChild(new FragmentNode(FragmentProps()));
      seat = wrapper;
    }
    if (shape == 7)
    {
      BoxNode *box = new BoxNode(Box().width(200).props);
      box->addChild(text);
      seat = box;
    }
    if (shape == 8)
    {
      CardNode *card = new CardNode(CardProps(CardStyle(CARD_SHADOW_DROP)));
      StackNode *column = new StackNode(Column().props);
      card->addChild(column);
      column->addChild(text);
      seat = card;
    }
    if (shape == 9)
    {
      BoxNode *box = new BoxNode(Box().props);
      box->addChild(text);
      seat = box;
    }
    row.addChild(seat);
    ShrinkWidthProbe source;
    RowWidthConsultation widths(
        row.childrenHead(), row.childrenCount(), 50, 8, RailSpacing(12, 8, 9), ROW_UNDECLARED_WIDTH_NATURAL, &source);
    const int actual = widths.next(seat).width();
    LOKA_VERIFY(actual == ((shape == 0 || shape == 3 || shape == 6) ? 50 : shape == 8 ? 209 : 200));
    // Even a zero-size Box is a second live leaf; the injected answer avoids a flex false positive.
    if (shape == 5)
      LOKA_VERIFY(source.calls == 4);
  }
  StackNode content(Column().props);
  StackNode *row = new StackNode(Row().naturalWidth().props);
  content.addChild(row);
  row->addChild(shrinkText(20000));
  row->addChild(shrinkText(20000));
  ShrinkWidthProbe source;
  RowWidthConsultation widths(
      row->childrenHead(), row->childrenCount(), 50, 8, RailSpacing(12, 8, 9), ROW_UNDECLARED_WIDTH_NATURAL, &source);
  LOKA_VERIFY(widths.next(row->childrenHead()).width() == 20000);
  short overflow = 123;
  LOKA_VERIFY(!queryContentOverflowWidth(&content, RailSpacing(12, 8, 9), &source, overflow));
  LOKA_VERIFY(overflow == 123);
}

void testContentOverflowShrunkFramesAndNestedSeats()
{
  using namespace loka::app;
  using namespace loka::app::layout;
  for (int shape = 0; shape < 5; ++shape)
  {
    StackNode content(Column().props);
    StackNode *row = new StackNode(Row().naturalWidth().props);
    if (shape == 1)
    {
      StackNode *outer = new StackNode(Row().naturalWidth().props);
      content.addChild(outer);
      outer->addChild(row);
      outer->addChild(shrinkText(100));
    }
    else
    {
      CardNode *card = new CardNode(CardProps(CardStyle(CARD_SHADOW_DROP)));
      StackNode *column = new StackNode(Column().props);
      card->addChild(column);
      column->addChild(row);
      if (shape == 2)
      {
        StackNode *outer = new StackNode(Row().naturalWidth().props);
        content.addChild(outer);
        outer->addChild(card);
      }
      else
        content.addChild(card);
    }
    if (shape == 3)
      row->addChild(new BoxNode(Box().size(100, 20).props));
    row->addChild(shrinkText(200));
    if (shape == 4)
      row->addChild(shrinkText(200));
    ShrinkWidthProbe source;
    short overflow = 0;
    LOKA_VERIFY(queryContentOverflowWidth(&content, RailSpacing(12, 8, 9), &source, overflow));
    const int expected[] = {17, 209, 209, 118, 19};
    LOKA_VERIFY(overflow == expected[shape]);
    for (int window = 16; window <= 18; ++window)
      LOKA_VERIFY(ToolboxScrollViewMaximum(overflow, window) == expected[shape] - window
                  || (expected[shape] <= window && ToolboxScrollViewMaximum(overflow, window) == 0));
  }
}

namespace
{
  loka::app::TextNode *toolbarText(loka::app::scene::Node *node)
  {
    if (node->asTextNode())
      return node->asTextNode();
    loka::app::scene::INestable *group = node->asNestable();
    for (loka::app::scene::Node *child = group ? group->childrenHead() : 0; child; child = child->nextInComposition)
      if (loka::app::TextNode *text = toolbarText(child))
        return text;
    return 0;
  }

  class ToolbarPresenter : public NullScenePlatformController
  {
  public:
    virtual void onChange(loka::app::scene::Node *root, loka::app::scene::NodeDirtyFlags, bool)
    {
      this->projectLayoutForTesting(root, rowState(180));
    }
  };
} // namespace

void testMountedNullToolbarShrinksInsideFixedBoxes()
{
  using namespace loka::app;
  using namespace loka::app::scene;
  for (int scrolling = 0; scrolling < 2; ++scrolling)
    for (int container = 0; container < 3; ++container)
    {
      ToolbarPresenter platform;
      RowTextSeatRecord record;
      g_rowTextSeatRecord = &record;
      RowSeatTextDefinition title("A very long toolbar title that cannot fit beside both buttons");
      title.props.blockStyle_ = BlockStyle().truncation(TEXT_TRUNCATION_ELLIPSIS);
      RowDefinition row = Row().naturalWidth();
      row << Button("< Back") << title << Button("Main");
      NodeDefinitionBase *definition = 0;
      if (container == 0)
        definition = scrolling ? (ScrollView() << row).clone() : row.clone();
      else
      {
        BoxDefinition box = container == 1 ? Box().width(160) : Box().size(160, 20);
        box << row;
        RowDefinition outer;
        outer << box;
        definition = scrolling ? (ScrollView() << outer).clone() : outer.clone();
      }
      LOKA_VERIFY(definition);
      Scene scene(definition);
      LOKA_VERIFY(scene.mount(&platform));
      loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);
      ButtonNode first(ButtonProps().text("< Back"));
      ButtonNode last(ButtonProps().text("Main"));
      short firstWidth = 0, lastWidth = 0;
      LOKA_VERIFY(platform.queryNaturalWidth(&first, firstWidth));
      LOKA_VERIFY(platform.queryNaturalWidth(&last, lastWidth));
      const int offer = container == 0 ? 180 : 160;
      LOKA_VERIFY(record.calls > 0);
      TextNode *text = toolbarText(loka::dsl::testing::SceneTestAccess::rootNode(scene));
      LOKA_VERIFY(text && text->getContext());
      const NullTextMeasurement &measurement = static_cast<NullTextContext *>(text->getContext())->measurement();
      LOKA_VERIFY(measurement.lineCount() == 1);
      LOKA_VERIFY(measurement.width() <= record.state.width);
      LOKA_VERIFY(loka::app::testing::NullTextMeasurementAccess::line(measurement, 0).width <= record.state.width);
      LOKA_VERIFY(record.state.width == offer - firstWidth - lastWidth - 8);
      LOKA_VERIFY(record.state.x == firstWidth + 4);
      LOKA_VERIFY(record.state.x + record.state.width + 4 + lastWidth <= offer);
      loka::dsl::testing::SceneTestAccess::unmount(scene);
      g_rowTextSeatRecord = 0;
    }
}

namespace
{
  class ShrinkComponent : public loka::app::scene::ComponentNode
  {
    virtual void composeChildren(loka::app::scene::NodeComposition &) {}
  };

  class ShrinkArms : public loka::app::scene::BoundaryNodeFor<ShrinkArms>
  {
  public:
    loka::app::scene::NodeState<bool> shown;
    loka::app::scene::NodeState<int> key;
    explicit ShrinkArms(const loka::app::scene::BoundaryPropsFor<ShrinkArms> &props)
        : loka::app::scene::BoundaryNodeFor<ShrinkArms>(props)
    {
      this->declareStates(2).state(this->shown, true).state(this->key, 0);
    }
    virtual void composeNode(loka::app::scene::NodeComposition &c)
    {
      using namespace loka::app;
      c.declare(Row().naturalWidth()
                << (Show(*this->shown.state())
                    << Keyed(*this->key.state(),
                             this,
                             &ShrinkArms::arm,
                             reservation::SeatNodes<reservation::Nodes<RowSeatTextNode, 1, reservation::End> >())));
    }
    void arm(loka::app::scene::NodeComposition &c)
    {
      RowSeatTextDefinition title(this->key.get() ? "Changed title still too long to fit in this short row's offer"
                                                  : "Original title too long to fit in this short row's offer");
      title.props.blockStyle_ = loka::app::BlockStyle().truncation(loka::app::TEXT_TRUNCATION_ELLIPSIS);
      c.declare(title);
    }
  };
} // namespace

void testRowEllipsisCurrentArmsAndComponent()
{
  using namespace loka::app;
  using namespace loka::app::layout;
  ShrinkComponent component;
  component.addChild(shrinkText(200));
  ShrinkWidthProbe source;
  RowWidthConsultation widths(&component, 1, 50, 8, RailSpacing(12, 8, 9), ROW_UNDECLARED_WIDTH_NATURAL, &source);
  LOKA_VERIFY(widths.next(&component).width() == 50);
  LOKA_VERIFY(source.calls == 2);
  ToolbarPresenter platform;
  RowTextSeatRecord record;
  g_rowTextSeatRecord = &record;
  scene::Scene scene((scene::Boundary<ShrinkArms>()));
  LOKA_VERIFY(scene.mount(&platform));
  loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);
  ShrinkArms *root = static_cast<ShrinkArms *>(loka::dsl::testing::SceneTestAccess::rootBoundary(scene));
  for (int generation = 0; generation < 2; ++generation)
  {
    if (generation)
    {
      loka::core::StateTrackerGuard transaction(root->tracker());
      root->key.set(1);
    }
    for (int i = 0; scene.hasPendingInvalidation() && i < 12; ++i)
      LOKA_VERIFY(scene.flushInvalidation());
    LOKA_VERIFY(record.state.width == 180);
    short overflow = 42;
    LOKA_VERIFY(queryContentOverflowWidth(root, RailSpacing(12, 4, 9), &platform, overflow));
    LOKA_VERIFY(overflow == 1);
  }
  {
    loka::core::StateTrackerGuard transaction(root->tracker());
    root->shown.set(false);
  }
  for (int i = 0; scene.hasPendingInvalidation() && i < 12; ++i)
    LOKA_VERIFY(scene.flushInvalidation());
  short overflow = 42;
  // Empty NATURAL Rows keep the pre-existing refusal, rather than inventing a natural width.
  LOKA_VERIFY(!queryContentOverflowWidth(root, RailSpacing(12, 4, 9), &platform, overflow));
  LOKA_VERIFY(overflow == 42);
  loka::dsl::testing::SceneTestAccess::unmount(scene);
  g_rowTextSeatRecord = 0;
}

void testRowEllipsisRefusalsAndLiveGaps()
{
  using namespace loka::app;
  using namespace loka::app::layout;
  ShrinkWidthProbe source;
  StackNode content(Column().props);
  StackNode *row = new StackNode(Row().naturalWidth().props);
  content.addChild(row);
  TextNode *title = shrinkText(200);
  row->addChild(title);
  row->addChild(new FragmentNode(FragmentProps()));
  BoxNode *zero = new BoxNode(Box().size(0, 0).props);
  row->addChild(zero);
  RowWidthConsultation widths(
      row->childrenHead(), row->childrenCount(), 50, 8, RailSpacing(12, 8, 9), ROW_UNDECLARED_WIDTH_NATURAL, &source);
  LOKA_VERIFY(source.calls == 2);
  LOKA_VERIFY(widths.next(title).width() == 1);
  LOKA_VERIFY(!widths.next(title->nextInComposition).isLiveSeat());
  const RowChildWidth zeroSeat = widths.next(zero);
  LOKA_VERIFY(zeroSeat.width() == 200 && zeroSeat.hasGapBefore());
  short overflow = 0;
  LOKA_VERIFY(queryContentOverflowWidth(&content, RailSpacing(12, 8, 9), &source, overflow));
  LOKA_VERIFY(overflow == 209);
  TextNode declined(TextProps("0"));
  declined.props.blockStyle_ = BlockStyle().truncation(TEXT_TRUNCATION_ELLIPSIS);
  RowWidthConsultation declinedWidths(
      &declined, 1, 50, 8, RailSpacing(12, 8, 9), ROW_UNDECLARED_WIDTH_NATURAL, &source);
  LOKA_VERIFY(declinedWidths.next(&declined).width() == 50);
  AttributedTextProps attributedProps;
  attributedProps.blockStyle_ = BlockStyle().truncation(TEXT_TRUNCATION_ELLIPSIS);
  AttributedTextNode attributed(attributedProps);
  RowWidthConsultation attributedWidths(
      &attributed, 1, 50, 8, RailSpacing(12, 8, 9), ROW_UNDECLARED_WIDTH_NATURAL, &source);
  LOKA_VERIFY(attributedWidths.next(&attributed).width() == 200);
  row->addChild(shrinkText(0));
  overflow = 321;
  LOKA_VERIFY(!queryContentOverflowWidth(&content, RailSpacing(12, 8, 9), &source, overflow));
  LOKA_VERIFY(overflow == 321);
}

void testContentOverflowFixedCarouselAndSharedSeat()
{
  using namespace loka::app;
  using namespace loka::app::layout;
  StackNode content(Column().props);
  StackNode *row = new StackNode(Row().naturalWidth().props);
  content.addChild(row);
  for (int i = 0; i < 2; ++i)
  {
    BoxNode *box = new BoxNode(Box().size(160, 40).props);
    CardNode *card = new CardNode(CardProps(CardStyle(CARD_SHADOW_DROP)));
    card->addChild(shrinkText(200));
    box->addChild(card);
    row->addChild(box);
  }
  ShrinkWidthProbe source;
  short overflow = 0;
  LOKA_VERIFY(queryContentOverflowWidth(&content, RailSpacing(12, 8, 9), &source, overflow));
  LOKA_VERIFY(overflow == 328 && source.calls == 0);
  RowWidthConsultation widths(
      row->childrenHead(), row->childrenCount(), 50, 8, RailSpacing(12, 8, 9), ROW_UNDECLARED_WIDTH_NATURAL, &source);
  LOKA_VERIFY(widths.next(row->childrenHead()).width() == 160);
  LOKA_VERIFY(widths.next(row->childrenHead()->nextInComposition).width() == 160);
  LOKA_VERIFY(source.calls == 0);
  StackNode nestedContent(Column().props);
  StackNode *outer = new StackNode(Row().naturalWidth().props);
  StackNode *shared = new StackNode(Row().props);
  shared->addChild(shrinkText(20));
  shared->addChild(shrinkText(100));
  outer->addChild(shared);
  outer->addChild(shrinkText(100));
  nestedContent.addChild(outer);
  LOKA_VERIFY(queryContentOverflowWidth(&nestedContent, RailSpacing(12, 8, 9), &source, overflow));
  LOKA_VERIFY(overflow == 217);
}

namespace
{
  class HeightButton : public loka::app::ButtonNode
  {
  public:
    explicit HeightButton(const loka::app::ButtonProps &props)
        : loka::app::ButtonNode(props),
          offer()
    {
    }
    virtual short layoutProjected(loka::app::scene::IPlatformController *controller,
                                  loka::app::scene::LayoutState &state)
    {
      this->offer = state;
      return loka::app::ButtonNode::layoutProjected(controller, state);
    }
    loka::app::scene::LayoutState offer;
  };

  class HeightPropsComponent;

  struct HeightProps : public loka::app::scene::NodePropsBase<HeightProps>
  {
    typedef HeightProps TypeTag;
    typedef HeightPropsComponent NodeType;
    virtual bool operator<(const loka::app::scene::PropsBase &rhs) const
    {
      return this->propsTypeId() < rhs.propsTypeId();
    }
  };

  // Models JsClickNode's component base without depending on SmirkyCard.
  class HeightPropsComponent : public loka::app::scene::ComponentNodeWithProps<HeightProps>
  {
  public:
    explicit HeightPropsComponent(const HeightProps &props = HeightProps())
        : loka::app::scene::ComponentNodeWithProps<HeightProps>(props)
    {
    }
    virtual void composeChildren(loka::app::scene::NodeComposition &) {}
  };
} // namespace

void testSpacerAlignedRowFallbackHeight()
{
  using namespace loka::app;
  const VerticalAlignment alignments[] = {VERTICAL_ALIGNMENT_TOP, VERTICAL_ALIGNMENT_CENTER, VERTICAL_ALIGNMENT_BOTTOM};
  for (unsigned alignment = 0; alignment < 3; ++alignment)
  {
    SpacerRowOffers spacerOffers;
    SpacerRowOffers boxOffers;
    StackNode spacerRow(Row().alignVertical(alignments[alignment]).gap(4).props);
    StackNode boxRow(Row().alignVertical(alignments[alignment]).gap(4).props);
    BoxNode *spacer = new BoxNode(Spacer().props);
    BoxNode *box = new BoxNode(Box().props);
    spacerRow.addChild(new ButtonNode(Button("A").props));
    spacerRow.addChild(spacer);
    spacerRow.addChild(new ButtonNode(Button("BB").props));
    boxRow.addChild(new ButtonNode(Button("A").props));
    boxRow.addChild(box);
    boxRow.addChild(new ButtonNode(Button("BB").props));
    // Distinct F values discriminate fallback from a coincidental Button height.
    for (int fallback = 7; fallback <= 40; fallback += 33)
    {
      const int spacerHeight =
          layout::preferredChildHeightForRow(spacer, fallback, 10, 11, 12, 13, 14, &spacerOffers.platform);
      const int boxHeight = layout::preferredChildHeightForRow(box, fallback, 10, 11, 12, 13, 14, &boxOffers.platform);
      LOKA_VERIFY(spacerHeight == fallback);
      LOKA_VERIFY(spacerHeight == boxHeight);
    }
    layoutSpacerRow(spacerRow, 100, spacerOffers);
    layoutSpacerRow(boxRow, 100, boxOffers);
    LOKA_VERIFY(spacerOffers.frames[1].height == 10);
    for (unsigned child = 0; child < 3; ++child)
      LOKA_VERIFY(spacerOffers.frames[child] == boxOffers.frames[child]);
  }
}

void testRowWrappedButtonHeightOffers()
{
  // Observed before the fix: the TOP wrapper got y=0, height=40; the bare Button got height=10.
  using namespace loka::app;
  const VerticalAlignment alignments[] = {VERTICAL_ALIGNMENT_TOP, VERTICAL_ALIGNMENT_CENTER, VERTICAL_ALIGNMENT_BOTTOM};
  for (int alignment = 0; alignment < 3; ++alignment)
    for (int shape = 0; shape < 3; ++shape)
    {
      NullScenePlatformController platform;
      StackNode row(Row().alignVertical(alignments[alignment]).props);
      row.addChild(new BoxNode(Box().size(40, 40).props));
      HeightButton *bare = new HeightButton(ButtonProps());
      HeightButton *wrapped = new HeightButton(ButtonProps());
      row.addChild(bare);
      scene::NestableNode *wrapper = shape == 0 ? static_cast<scene::NestableNode *>(new FragmentNode(FragmentProps()))
                                     : shape == 1 ? static_cast<scene::NestableNode *>(new ShrinkComponent())
                                                  : static_cast<scene::NestableNode *>(new HeightPropsComponent());
      wrapper->addChild(wrapped);
      row.addChild(wrapper);
      platform.projectLayoutForTesting(&row, rowState(500));
      std::fprintf(stderr,
                   "wrapped height shape=%d alignment=%d: bare y=%d h=%d wrapped y=%d h=%d\n",
                   shape,
                   alignment,
                   bare->offer.y,
                   bare->offer.height,
                   wrapped->offer.y,
                   wrapped->offer.height);
      LOKA_VERIFY(wrapped->offer.height == bare->offer.height);
      LOKA_VERIFY(wrapped->offer.y == bare->offer.y);
      LOKA_VERIFY(bare->offer.height == 10);
      LOKA_VERIFY(bare->offer.y == (alignment == 0 ? 0 : alignment == 1 ? 15 : 30));
    }
}

void testRowShownButtonAndMultipleLeafHeightOffers()
{
  using namespace loka::app;
  using namespace loka::app::layout;
  const VerticalAlignment alignments[] = {VERTICAL_ALIGNMENT_TOP, VERTICAL_ALIGNMENT_CENTER, VERTICAL_ALIGNMENT_BOTTOM};
  for (int alignment = 0; alignment < 3; ++alignment)
    for (int shape = 0; shape < 4; ++shape)
    {
      NullScenePlatformController platform;
      loka::core::MutableState<bool> shown(shape != 3);
      scene::NodeDefinition<ButtonProps, HeightButton> button((ButtonProps()));
      RowDefinition row = Row().alignVertical(alignments[alignment]);
      row << Box().size(40, 40) << button;
      if (shape == 1)
        row << (Show(shown) << button << button);
      else if (shape == 2)
        row << (Show(shown) << ImageView() << Text("caption"));
      else
        row << (Show(shown) << button);
      scene::Scene scene(row.clone());
      LOKA_VERIFY(scene.mount(&platform));
      loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);
      scene::Node *root = loka::dsl::testing::SceneTestAccess::rootNode(scene);
      while (root && !root->asStackNode() && root->asNestable())
        root = root->asNestable()->childrenHead();
      LOKA_VERIFY(root && root->asStackNode());
      scene::Node *bare = root->asNestable()->childrenHead()->nextInComposition;
      scene::Node *wrapper = bare->nextInComposition;
      LOKA_VERIFY(wrapper && isLayoutPassThrough(wrapper));
      LOKA_VERIFY(preferredChildHeightForRow(wrapper, 7, 10, 11, 12, 13, 14, 0)
                  == (shape == 0   ? 10
                      : shape == 3 ? 0
                                   : 7));
      LOKA_VERIFY(preferredChildHeightForRow(wrapper, 40, 10, 11, 12, 13, 14, 0)
                  == (shape == 0   ? 10
                      : shape == 3 ? 0
                                   : 40));
      platform.projectLayoutForTesting(root, rowState(500));
      if (shape == 0)
      {
        HeightButton *leaf = static_cast<HeightButton *>(wrapper->asNestable()->childrenHead());
        HeightButton *bareButton = static_cast<HeightButton *>(bare);
        LOKA_VERIFY(leaf->offer.y == bareButton->offer.y);
        LOKA_VERIFY(leaf->offer.height == bareButton->offer.height);
      }
      if (shape == 1)
      {
        HeightButton *first = static_cast<HeightButton *>(wrapper->asNestable()->childrenHead());
        LOKA_VERIFY(first->offer.y == 0);
        LOKA_VERIFY(first->offer.height == 40);
      }
      loka::dsl::testing::SceneTestAccess::unmount(scene);
    }
}

namespace
{
  class LeafShapeWidthProbe : public ShrinkWidthProbe
  {
  public:
    virtual bool queryNaturalWidth(loka::app::scene::Node *node, short &width) const
    {
      if (node->asScrollViewNode())
      {
        width = 200;
        return true;
      }
      return ShrinkWidthProbe::queryNaturalWidth(node, width);
    }
  };
} // namespace

void testRowSingleLiveLeafWidthShapes()
{
  using namespace loka::app;
  using namespace loka::app::layout;
  for (int shape = 0; shape < 8; ++shape)
  {
    loka::core::EmitterState dialogResult;
    FragmentNode wrapper((FragmentProps()));
    scene::NestableNode *content = &wrapper;
    if (shape == 0)
      for (int depth = 0; depth < 8; ++depth)
      {
        FragmentNode *inner = new FragmentNode(FragmentProps());
        content->addChild(inner);
        content = inner;
      }
    if (shape == 1)
      content->addChild(new FragmentNode(FragmentProps()));
    if (shape == 2)
      content->addChild(new OpenFileDialogNode(OpenFileDialogProps().onResult(&dialogResult)));
    if (shape == 6)
    {
      FragmentNode *inner = new FragmentNode(FragmentProps());
      content->addChild(inner);
      content = inner;
    }
    TextNode *text = shape == 7 ? 0 : shrinkText(200);
    scene::Node *terminal = text;
    if (shape == 5)
    {
      ScrollViewNode *scroll = new ScrollViewNode(ScrollViewProps());
      content->addChild(scroll);
      scroll->addChild(text);
      terminal = scroll;
    }
    else if (text)
      content->addChild(text);
    if (shape == 1 || shape == 7)
      content->addChild(new FragmentNode(FragmentProps()));
    if (shape == 3)
      content->addChild(new BoxNode(Box().props));
    if (shape == 4)
      content->addChild(new StackNode(Row().props));
    if (shape == 6)
      content->addChild(shrinkText(100));
    const SeatLeaves leaves = singleLiveLeaf(&wrapper);
    const SeatLeaves::Kind expectedKind = shape == 7                               ? SeatLeaves::NO_SEAT
                                          : shape == 3 || shape == 4 || shape == 6 ? SeatLeaves::MANY_LEAVES
                                                                                   : SeatLeaves::ONE_LEAF;
    LOKA_VERIFY(leaves.kind() == expectedKind);
    LOKA_VERIFY(leaves.leaf() == (expectedKind == SeatLeaves::ONE_LEAF ? terminal : 0));
    LeafShapeWidthProbe source;
    RowWidthConsultation widths(&wrapper, 1, 50, 8, RailSpacing(12, 8, 9), ROW_UNDECLARED_WIDTH_NATURAL, &source);
    const int expected[] = {50, 50, 50, 200, 50, 200, 200, 0};
    LOKA_VERIFY(widths.next(&wrapper).width() == expected[shape]);
  }
}

namespace
{
  class SeatVisitProbe : public loka::app::scene::Node
  {
  public:
    explicit SeatVisitProbe(int &visits)
        : visits_(visits)
    {
    }
    virtual loka::app::scene::NodeKind kind() const
    {
      ++this->visits_;
      return loka::app::scene::NODE_KIND_UNKNOWN;
    }

  private:
    int &visits_;
  };
} // namespace

void testSingleLiveLeafStopsAtSecondSeat()
{
  using namespace loka::app;
  using namespace loka::app::layout;
  LOKA_VERIFY(singleLiveLeaf(0).kind() == SeatLeaves::NO_SEAT);
  loka::core::EmitterState result;
  OpenFileDialogNode dialog(OpenFileDialogProps().onResult(&result));
  LOKA_VERIFY(singleLiveLeaf(&dialog).kind() == SeatLeaves::NO_SEAT);
  int visits = 0;
  FragmentNode outer((FragmentProps()));
  FragmentNode *inner = new FragmentNode(FragmentProps());
  outer.addChild(inner);
  inner->addChild(new ButtonNode(ButtonProps()));
  inner->addChild(new ButtonNode(ButtonProps()));
  inner->addChild(new SeatVisitProbe(visits));
  outer.addChild(new SeatVisitProbe(visits));
  LOKA_VERIFY(singleLiveLeaf(&outer).kind() == SeatLeaves::MANY_LEAVES);
  LOKA_VERIFY(visits == 0);
}
