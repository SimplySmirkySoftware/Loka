#include "ToolboxLayoutContractTests.hpp"

#include "support/TestVerify.hpp"

#include "../apple/toolbox/src/ToolboxPlatformLayoutHandlers.hpp"
#include "app/nodes/nestable/Box.hpp"
#include "app/nodes/controls/Button.hpp"
#include "app/nodes/Text.hpp"
#include "app/nodes/ImageView.hpp"
#include "app/RectSurface.hpp"
#include "app/nodes/nestable/Fragment.hpp"
#include "app/nodes/nestable/RowColumn.hpp"

namespace
{
  class ToolboxLayoutProbeNode : public loka::app::scene::Node
  {
  };

  class ToolboxLayoutContractTraversal : public loka::app::scene::IPlatformLayoutTraversal
  {
  public:
    ToolboxLayoutContractTraversal(short childWidth, short childHeight)
        : childWidth_(childWidth),
          childHeight_(childHeight),
          callCount_(0),
          layoutResultY_(0),
          lastState_()
    {
    }

    virtual int layoutChild(loka::app::scene::Node *, const loka::app::scene::LayoutState &state)
    {
      ++this->callCount_;
      this->lastState_ = state;
      this->layoutResultY_ = static_cast<short>(state.y + this->childHeight_);
      return this->childWidth_;
    }

    virtual void setLayoutResultY(short y)
    {
      this->layoutResultY_ = y;
    }

    virtual short layoutResultY() const
    {
      return this->layoutResultY_;
    }

    short childWidth_;
    short childHeight_;
    int callCount_;
    short layoutResultY_;
    loka::app::scene::LayoutState lastState_;
  };

  loka::app::scene::LayoutState FixedBoxInputState()
  {
    loka::app::scene::LayoutState state;
    state.x = 10;
    state.y = 20;
    state.width = 80;
    state.height = 40;
    state.lineHeight = 10;
    return state;
  }

} // namespace

void testToolboxFixedBoxLayoutCommitsDeclaredExtent()
{
  loka::app::BoxProps props;
  props.setPadding(5).setSize(300, 170);
  loka::app::BoxNode box(props);
  box.addChild(new ToolboxLayoutProbeNode());
  loka::app::scene::PlatformLayoutHandlerRegistry registry;
  RegisterToolboxPlatformLayoutHandlers(registry);
  ToolboxLayoutContractTraversal traversal(23, 7);
  loka::app::scene::LayoutState state = FixedBoxInputState();
  short width = 0;

  const bool usedHandler =
      ApplyToolboxPlatformLayoutHandler(registry, box, state, traversal, width);

  LOKA_VERIFY(usedHandler);
  LOKA_VERIFY(width == 300);
  LOKA_VERIFY(traversal.callCount_ == 1);
  LOKA_VERIFY(traversal.lastState_.x == 15);
  LOKA_VERIFY(traversal.lastState_.y == 25);
  LOKA_VERIFY(traversal.lastState_.width == 290);
  LOKA_VERIFY(traversal.lastState_.height == 160);
  LOKA_VERIFY(traversal.layoutResultY() == 190);
  LOKA_VERIFY(state.y == 190);
}

void testToolboxEmptyFixedBoxLayoutCommitsDeclaredExtent()
{
  loka::app::BoxProps props;
  props.setPadding(5).setSize(300, 170);
  loka::app::BoxNode box(props);
  loka::app::scene::PlatformLayoutHandlerRegistry registry;
  RegisterToolboxPlatformLayoutHandlers(registry);
  ToolboxLayoutContractTraversal traversal(23, 7);
  loka::app::scene::LayoutState state = FixedBoxInputState();
  short width = 0;

  const bool usedHandler =
      ApplyToolboxPlatformLayoutHandler(registry, box, state, traversal, width);

  LOKA_VERIFY(usedHandler);
  LOKA_VERIFY(width == 300);
  LOKA_VERIFY(traversal.callCount_ == 0);
  LOKA_VERIFY(traversal.layoutResultY() == 190);
  LOKA_VERIFY(state.y == 190);
}

namespace
{
  void ProbeToolboxWidthOnlyBox(bool empty, short contentWidth)
  {
    loka::app::BoxProps props;
    props.setPadding(5).setWidth(300);
    loka::app::BoxNode box(props);
    if (!empty)
      box.addChild(new ToolboxLayoutProbeNode());
    loka::app::scene::PlatformLayoutHandlerRegistry registry;
    RegisterToolboxPlatformLayoutHandlers(registry);
    ToolboxLayoutContractTraversal traversal(contentWidth, 7);
    loka::app::scene::LayoutState state = FixedBoxInputState();
    short width = 0;
    const bool usedHandler = ApplyToolboxPlatformLayoutHandler(registry, box, state, traversal, width);
    LOKA_VERIFY(usedHandler);
    LOKA_VERIFY(width == 300);
    LOKA_VERIFY(traversal.callCount_ == (empty ? 0 : 1));
    LOKA_VERIFY(state.y == (empty ? 30 : 37));
    LOKA_VERIFY(traversal.layoutResultY() == state.y);
    if (!empty)
    {
      LOKA_VERIFY(traversal.lastState_.x == 15);
      LOKA_VERIFY(traversal.lastState_.y == 25);
      LOKA_VERIFY(traversal.lastState_.width == 290);
      LOKA_VERIFY(traversal.lastState_.height == 30);
    }
  }

  class ToolboxNestedBoxTraversal : public ToolboxLayoutContractTraversal
  {
  public:
    ToolboxNestedBoxTraversal()
        : ToolboxLayoutContractTraversal(23, 7)
    {
    }

    virtual int layoutChild(loka::app::scene::Node *child, const loka::app::scene::LayoutState &state)
    {
      if (child->asBoxNode())
        return ComputeToolboxBoxLayout(child->asBoxNode(), state, this);
      return ToolboxLayoutContractTraversal::layoutChild(child, state);
    }
  };
} // namespace

void testToolboxEmptyWidthOnlyBoxReportsWidth()
{
  ProbeToolboxWidthOnlyBox(true, 23);
}

void testToolboxNarrowWidthOnlyBoxReportsWidth()
{
  ProbeToolboxWidthOnlyBox(false, 23);
}

void testToolboxOverflowingWidthOnlyBoxReportsWidth()
{
  ProbeToolboxWidthOnlyBox(false, 450);
}

void testToolboxWrappedWidthOnlyBoxLiveWidth()
{
  // An untracked State notifies synchronously; no tracker transaction is needed.
  loka::core::MutableState<int> liveWidth(300);
  loka::app::BoxProps childProps;
  childProps.setPadding(5);
  childProps.widthState_ = &liveWidth;
  loka::app::BoxNode *child = new loka::app::BoxNode(childProps);
  child->addChild(new ToolboxLayoutProbeNode());
  loka::app::BoxProps wrapperProps;
  wrapperProps.setPadding(3);
  loka::app::BoxNode wrapper(wrapperProps);
  wrapper.addChild(child);
  loka::app::scene::PlatformLayoutHandlerRegistry registry;
  RegisterToolboxPlatformLayoutHandlers(registry);
  ToolboxNestedBoxTraversal traversal;
  for (int i = 0; i < 2; ++i)
  {
    if (i == 1)
      liveWidth.set(180);
    loka::app::scene::LayoutState state = FixedBoxInputState();
    short width = 0;
    const bool usedHandler = ApplyToolboxPlatformLayoutHandler(registry, wrapper, state, traversal, width);
    LOKA_VERIFY(usedHandler);
    LOKA_VERIFY(width == (i == 0 ? 306 : 186));
    LOKA_VERIFY(state.y == 43);
    LOKA_VERIFY(traversal.lastState_.x == 18);
    LOKA_VERIFY(traversal.lastState_.y == 28);
    LOKA_VERIFY(traversal.lastState_.width == (i == 0 ? 290 : 170));
    LOKA_VERIFY(traversal.lastState_.height == 24);
  }
}

void testToolboxRowConsultsFixedChildWidth()
{
  loka::app::StackNode row((loka::app::StackProps(loka::app::STACK_AXIS_ROW)));
  loka::app::BoxProps boxProps;
  boxProps.setSize(200, 10);
  row.addChild(new loka::app::BoxNode(boxProps));
  row.addChild(new ToolboxLayoutProbeNode());
  loka::app::scene::PlatformLayoutHandlerRegistry registry;
  RegisterToolboxPlatformLayoutHandlers(registry);
  ToolboxLayoutContractTraversal traversal(23, 7);
  loka::app::scene::LayoutState state = FixedBoxInputState();
  state.width = 500;
  state.spacing = 4;
  short width = 0;

  const bool usedHandler = ApplyToolboxPlatformLayoutHandler(registry, row, state, traversal, width);

  LOKA_VERIFY(usedHandler);
  LOKA_VERIFY(width == 500);
  LOKA_VERIFY(traversal.callCount_ == 2);
  LOKA_VERIFY(traversal.lastState_.x == 214);
  LOKA_VERIFY(traversal.lastState_.width == 296);
}

void testToolboxRowConsultsWidthOnlyBoxWidth()
{
  loka::app::StackNode row((loka::app::StackProps(loka::app::STACK_AXIS_ROW)));
  loka::app::BoxProps boxProps;
  boxProps.setSize(200, 0);
  row.addChild(new loka::app::BoxNode(boxProps));
  row.addChild(new ToolboxLayoutProbeNode());
  loka::app::scene::PlatformLayoutHandlerRegistry registry;
  RegisterToolboxPlatformLayoutHandlers(registry);
  ToolboxLayoutContractTraversal traversal(23, 7);
  loka::app::scene::LayoutState state = FixedBoxInputState();
  state.width = 500;
  state.spacing = 4;
  short width = 0;

  const bool usedHandler = ApplyToolboxPlatformLayoutHandler(registry, row, state, traversal, width);

  LOKA_VERIFY(usedHandler);
  LOKA_VERIFY(width == 500);
  LOKA_VERIFY(traversal.callCount_ == 2);
  LOKA_VERIFY(traversal.lastState_.x == 214);
  LOKA_VERIFY(traversal.lastState_.width == 296);
}

void testToolboxRowEmptyFragmentConsumesNoSeatOrGap()
{
  loka::app::StackNode row((loka::app::StackProps(loka::app::STACK_AXIS_ROW)));
  loka::app::FragmentNode *empty = new loka::app::FragmentNode((loka::app::FragmentProps()));
  empty->setPropsTypeId(loka::app::FragmentProps::staticTypeId());
  row.addChild(empty);
  row.addChild(new ToolboxLayoutProbeNode());
  loka::app::scene::PlatformLayoutHandlerRegistry registry;
  RegisterToolboxPlatformLayoutHandlers(registry);
  ToolboxLayoutContractTraversal traversal(23, 7);
  loka::app::scene::LayoutState state = FixedBoxInputState();
  state.width = 500;
  state.spacing = 4;
  short width = 0;

  const bool usedHandler = ApplyToolboxPlatformLayoutHandler(registry, row, state, traversal, width);

  LOKA_VERIFY(usedHandler);
  LOKA_VERIFY(width == 500);
  LOKA_VERIFY(traversal.callCount_ == 2);
  LOKA_VERIFY(traversal.lastState_.x == 10);
  LOKA_VERIFY(traversal.lastState_.width == 500);
}

namespace
{
  class ToolboxRowSeatTraversal : public ToolboxLayoutContractTraversal
  {
  public:
    ToolboxRowSeatTraversal() : ToolboxLayoutContractTraversal(20, 0) {}

    virtual int layoutChild(loka::app::scene::Node *child, const loka::app::scene::LayoutState &state)
    {
      LOKA_VERIFY(this->callCount_ < 7);
      this->seats_[this->callCount_] = state;
      const int width = ToolboxLayoutContractTraversal::layoutChild(child, state);
      // Mirror the real child handlers: a text control advances from the
      // box top it was handed by lineHeight (ToolboxButtonContext,
      // ToolboxTextContext); other kinds consume their seat.
      const bool baselineControl = child->asButtonNode() || child->asTextNode() || child->asEditTextNode()
                                   || child->asPopupMenuNode();
      const short result =
          baselineControl ? static_cast<short>(state.y + state.lineHeight) : static_cast<short>(state.y + state.height);
      this->setLayoutResultY(result);
      return width;
    }

    loka::app::scene::LayoutState seats_[7];
  };

  void VerifyToolboxRowChildHeightSeats(bool centered)
  {
    loka::app::StackProps props(loka::app::STACK_AXIS_ROW);
    if (centered)
    {
      props.alignVertical(loka::app::VERTICAL_ALIGNMENT_CENTER);
    }
    loka::app::StackNode row(props);
    row.addChild(new loka::app::RectSurfaceNode(loka::app::RectSurfaceProps().size(20, 60)));
    row.addChild(new loka::app::ButtonNode(loka::app::ButtonProps()));
    row.addChild(new loka::app::TextNode(loka::app::TextProps()));
    loka::app::ImageViewProps imageProps;
    imageProps.height_ = 24;
    row.addChild(new loka::app::ImageViewNode(imageProps));
    row.addChild(new loka::app::ImageViewNode(loka::app::ImageViewProps()));
    row.addChild(new loka::app::RectSurfaceNode(loka::app::RectSurfaceProps().size(20, 30)));
    row.addChild(new loka::app::RectSurfaceNode(loka::app::RectSurfaceProps()));
    loka::app::scene::PlatformLayoutHandlerRegistry registry;
    RegisterToolboxPlatformLayoutHandlers(registry);
    ToolboxRowSeatTraversal traversal;
    loka::app::scene::LayoutState state = FixedBoxInputState();
    state.width = 500;
    state.spacing = 4;
    const short startY = state.y;
    short width = 0;
    const bool usedHandler = ApplyToolboxPlatformLayoutHandler(registry, row, state, traversal, width);
    LOKA_VERIFY(usedHandler);
    LOKA_VERIFY(traversal.callCount_ == 7);

    const char *names[] = {"anchor", "button", "text", "image-24", "image-auto", "surface-30", "surface-auto"};
    std::fprintf(stderr, "Toolbox Row %s: extent=%d (spacing=4)\n",
                 centered ? "CENTER" : "unaligned", state.y - startY);
    for (int i = 0; i < 7; ++i)
    {
      std::fprintf(stderr, "  %s: y-offset=%d seat-height=%d\n", names[i],
                   traversal.seats_[i].y - startY, traversal.seats_[i].height);
    }
    // Top-origin controls center at 50 with top 43 and bottom 57, without an ascent lead.
    const short centeredOffsets[] = {0, 23, 23, 18, 0, 15, 0};
    const short centeredHeights[] = {60, 14, 14, 24, 60, 30, 60};
    LOKA_VERIFY(state.y - startY == (centered ? 60 : 40));
    for (int i = 0; i < 7; ++i)
    {
      LOKA_VERIFY(traversal.seats_[i].y - startY == (centered ? centeredOffsets[i] : 0));
      LOKA_VERIFY(traversal.seats_[i].height == (centered ? centeredHeights[i] : 40));
    }
  }
} // namespace

void testToolboxCenteredRowChildHeightSeats()
{
  VerifyToolboxRowChildHeightSeats(true);
}

void testToolboxUnalignedRowChildHeightSeats()
{
  VerifyToolboxRowChildHeightSeats(false);
}

void testToolboxCenteredControlOnlyRowPaintedBounds()
{
  loka::app::StackNode row(loka::app::StackProps(loka::app::STACK_AXIS_ROW)
                               .alignVertical(loka::app::VERTICAL_ALIGNMENT_CENTER));
  row.addChild(new loka::app::ButtonNode(loka::app::ButtonProps()));
  loka::app::scene::PlatformLayoutHandlerRegistry registry;
  RegisterToolboxPlatformLayoutHandlers(registry);
  ToolboxRowSeatTraversal traversal;
  loka::app::scene::LayoutState state = FixedBoxInputState();
  state.spacing = 4;
  short width = 0;
  const bool usedHandler = ApplyToolboxPlatformLayoutHandler(registry, row, state, traversal, width);
  LOKA_VERIFY(usedHandler);
  LOKA_VERIFY(traversal.callCount_ == 1);
  std::fprintf(stderr, "Toolbox control-only CENTER: top=%d height=%d extent=%d\n",
               traversal.seats_[0].y, traversal.seats_[0].height, state.y - 20);
  // A top-origin control starts at the row top; its painted height stays 14.
  LOKA_VERIFY(traversal.seats_[0].y == 20);
  LOKA_VERIFY(traversal.seats_[0].height == 14);
  LOKA_VERIFY(state.y == 34);
}

void testToolboxAlignedRowCharacterizationExcludesBoxHeight()
{
  const loka::app::VerticalAlignment alignments[] = {
      loka::app::VERTICAL_ALIGNMENT_TOP, loka::app::VERTICAL_ALIGNMENT_CENTER, loka::app::VERTICAL_ALIGNMENT_BOTTOM};
  for (int alignment = 0; alignment < 3; ++alignment)
  {
    for (int fixedWidth = 0; fixedWidth < 2; ++fixedWidth)
    {
      loka::app::StackNode row(loka::app::StackProps(loka::app::STACK_AXIS_ROW).alignVertical(alignments[alignment]));
      loka::app::BoxProps props;
      props.setPadding(5).setHeight(200);
      if (fixedWidth)
        props.setWidth(300);
      loka::app::BoxNode *box = new loka::app::BoxNode(props);
      box->addChild(new ToolboxLayoutProbeNode());
      row.addChild(box);
      loka::app::scene::PlatformLayoutHandlerRegistry registry;
      RegisterToolboxPlatformLayoutHandlers(registry);
      ToolboxNestedBoxTraversal traversal;
      loka::app::scene::LayoutState state = FixedBoxInputState();
      short width = 0;
      const bool usedHandler = ApplyToolboxPlatformLayoutHandler(registry, row, state, traversal, width);
      LOKA_VERIFY(usedHandler);
      // Preserve Toolbox's existing aligned-row measurement: unlike shared Row,
      // it replaces the child's result Y (220) with the measured row end (30).
      LOKA_VERIFY(width == (fixedWidth ? 300 : 80));
      LOKA_VERIFY(state.y == 30);
      LOKA_VERIFY(traversal.callCount_ == 1);
      LOKA_VERIFY(traversal.lastState_.x == 15);
      LOKA_VERIFY(traversal.lastState_.y == 25);
      LOKA_VERIFY(traversal.lastState_.width == (fixedWidth ? 290 : 70));
      LOKA_VERIFY(traversal.lastState_.height == 190);
    }
  }
}
