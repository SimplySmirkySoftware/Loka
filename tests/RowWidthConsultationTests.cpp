#include "app/nodes/nestable/Grid.hpp"
#include "app/nodes/controls/Card.hpp"
#include "../apple/toolbox/src/ToolboxScrollViewDecisions.hpp"
#include "app/nodes/nestable/ScrollView.hpp"
#include "app/nodes/controls/Ribbon.hpp"
#include <climits>
#include "RowWidthConsultationTests.hpp"

#include "app/OpenFileDialog.hpp"
#include "app/nodes/controls/Button.hpp"
#include "app/RectSurface.hpp"
#include "app/layout/LayoutHeuristics.hpp"
#include "app/nodes/ImageView.hpp"
#include "app/nodes/Text.hpp"
#include "app/nodes/boundary/StdComposition.hpp"
#include "app/nodes/nestable/Box.hpp"
#include "app/nodes/nestable/Fragment.hpp"
#include "app/nodes/nestable/RowColumn.hpp"
#include "app/nodes/nestable/Show.hpp"
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
  class NaturalWidthProbe : public loka::app::layout::INaturalWidthSource
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
  class StackWidthProbe : public loka::app::layout::INaturalWidthSource
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
      root = new CardNode(CardProps());
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
    // Padding consumes 10; Card adds its 3/6 insets before offering the Box.
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
      content = new CardNode(CardProps());
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
    // Card adds left=3 and right=6 to its content's 800-unit claim.
    LOKA_VERIFY(known);
    LOKA_VERIFY(width == (parent == 3 ? 809 : 800));
    LOKA_VERIFY(ToolboxScrollViewMaximum(width, 200) == (parent == 3 ? 609 : 600));
  }
}
