#include "ContainerGapTests.hpp"
#include "app/nodes/nestable/ZStack.hpp"
#include "app/nodes/nestable/Grid.hpp"
#include "app/nodes/nestable/Canvas.hpp"
#include "app/nodes/controls/Cell.hpp"
#include "app/nodes/controls/EditText.hpp"
#include "app/nodes/controls/PopupMenu.hpp"
#include "app/nodes/controls/ScrollBar.hpp"
#include "app/nodes/controls/TextEditor.hpp"
#include "app/nodes/controls/Ribbon.hpp"
#include "app/nodes/AttributedText.hpp"
#include "app/nodes/ImageView.hpp"
#include "support/TestVerify.hpp"
#include "support/LayoutY.hpp"
#include "app/layout/ColumnLayout.hpp"
#include "app/layout/BoxLayout.hpp"
#include "app/layout/RowLayout.hpp"
#include "app/OpenFileDialog.hpp"
#include "app/ConfirmDialog.hpp"
#include "app/nodes/nestable/Fragment.hpp"
#include "app/nodes/nestable/For.hpp"
#include "app/nodes/nestable/ScrollView.hpp"
#include "app/nodes/controls/Button.hpp"
#include "app/scene/node/ComponentNode.hpp"
#include "app/nodes/nestable/Show.hpp"
#include "app/nodes/boundary/StdComposition.hpp"
#include "app/scene/Scene.hpp"
#include "testing/scene/SceneTestFlow.hpp"
#include "platform/null/NullScenePlatformController.hpp"
#include "platform/null/context/NullTextContext.hpp"
#include "platform/null/context/NullRectSurfaceContext.hpp"
#include "../apple/toolbox/src/ToolboxPlatformLayoutHandlers.hpp"

namespace
{
  using namespace loka::app;
  using namespace loka::app::scene;
  using namespace loka::app::layout;

  // Verbatim shape-based reference from origin/main (PR #1342).
  namespace reference
  {
    // clang-format off
      bool isLayoutPassThrough(scene::Node *node)
      {
        return node && node->asNestable() && !node->asProjectedLayoutNode() && !node->asStackNode()
               && !node->asBoxNode() && !node->asGridNode() && !node->asZStackNode() && !node->asScrollViewNode()
               && !node->asRibbonGroupNode() && !node->asCanvasNode() && !node->asRectSurfaceNode();
      }

      bool childTakesLayoutSeat(scene::Node *child)
      {
        if (!child || child->asOpenFileDialogNode() || child->asConfirmDialogNode())
          return false;
        if (!isLayoutPassThrough(child))
          return true;
        scene::INestable *content = child->asNestable();
        loka::dsl::CompositionCursor<scene::Node> children(content->childrenHead(), content->childrenCount());
        for (scene::Node *descendant = children.next(); descendant; descendant = children.next())
          if (childTakesLayoutSeat(descendant))
            return true;
        return false;
      }

    // clang-format on
  } // namespace reference

  void verifySeatVerdicts(Node *node)
  {
    LOKA_VERIFY(isLayoutPassThrough(node) == reference::isLayoutPassThrough(node));
    LOKA_VERIFY(childTakesLayoutSeat(node) == reference::childTakesLayoutSeat(node));
  }

  class ProjectedLeaf : public Node, public IProjectedLayoutNode
  {
  public:
    virtual IProjectedLayoutNode *asProjectedLayoutNode()
    {
      return this;
    }
    virtual short layoutProjected(IPlatformController *, LayoutState &s)
    {
      return s.y;
    }
  };

  class EmptyComponent : public ComponentNode
  {
    virtual void composeChildren(NodeComposition &) {}
  };

  struct Item
  {
  };
  struct TextFactory
  {
    TextDefinition operator()(const Item &, std::size_t) const
    {
      return Text("unused");
    }
  };

  class HiddenArm : public BoundaryNodeFor<HiddenArm>
  {
  public:
    typedef BoundaryPropsFor<HiddenArm> Props;
    explicit HiddenArm(const Props &props)
        : BoundaryNodeFor<HiddenArm>(props)
    {
      this->state(this->shown_, false);
    }
    virtual void composeNode(NodeComposition &composition)
    {
      composition.declare(Column() << (Show(*this->shown_.state()) << Text("hidden")));
    }

  private:
    NodeState<bool> shown_;
  };

  Node *noSeat(unsigned kind, const NodeState<ConfirmResult> &result)
  {
    switch (kind)
    {
    case 0:
      return new FragmentNode(FragmentProps());
    case 1:
      return new EmptyComponent();
    // Hidden Show arms materialize as empty Fragments, not as Show nodes.
    case 2:
      return new FragmentNode(FragmentProps());
    case 3:
    {
      loka::Vector<Item> items;
      Fragment generated;
      generated << For(1, items, TextFactory());
      return generated.create();
    }
    case 4:
      return new ConfirmDialogNode(ConfirmDialogProps().result(result));
    case 5:
    {
      FragmentNode *fragment = new FragmentNode(FragmentProps());
      fragment->addChild(new ConfirmDialogNode(ConfirmDialogProps().result(result)));
      fragment->addChild(new ConfirmDialogNode(ConfirmDialogProps().result(result)));
      return fragment;
    }
    }
    return 0;
  }

  LayoutState seat()
  {
    LayoutState s;
    s.x = 10;
    s.y = 20;
    s.width = 100;
    s.height = 100;
    s.lineHeight = 10;
    s.spacing = 4;
    return s;
  }

  class ObservedButton : public ButtonNode
  {
  public:
    LayoutState placed;
    ObservedButton()
        : ButtonNode(Button("button").props)
    {
    }
    virtual short layoutProjected(IPlatformController *platform, LayoutState &state)
    {
      const short result = ButtonNode::layoutProjected(platform, state);
      this->placed = state;
      return result;
    }
  };

  struct Recorder : IPlatformLayoutTraversal
  {
    LayoutState states[8];
    unsigned calls;
    short bottom;
    Recorder()
        : calls(0),
          bottom(0)
    {
    }
    virtual int layoutChild(Node *node, const LayoutState &s)
    {
      states[calls++] = s;
      bottom = static_cast<short>(s.y + (childTakesLayoutSeat(node) ? 5 : 0));
      return s.width;
    }
    virtual short layoutResultY() const
    {
      return bottom;
    }
    virtual void setLayoutResultY(short y)
    {
      bottom = y;
    }
    static int child(void *p, Node *node, const LayoutState &s)
    {
      Recorder &r = *static_cast<Recorder *>(p);
      r.layoutChild(node, s);
      return r.bottom;
    }
  };

  class ProjectedNestable : public FragmentNode, public IProjectedLayoutNode
  {
  public:
    ProjectedNestable()
        : FragmentNode(FragmentProps())
    {
    }
    virtual IProjectedLayoutNode *asProjectedLayoutNode()
    {
      return this;
    }
    virtual short layoutProjected(IPlatformController *, LayoutState &s)
    {
      return s.y;
    }
  };

  class CustomNestable : public FragmentNode
  {
  public:
    CustomNestable()
        : FragmentNode(FragmentProps())
    {
    }
    virtual const void *nodeTypeKey() const
    {
      return NodeTypeToken<CustomNestable>();
    }
  };
  class CustomHandler : public IPlatformLayoutHandler
  {
  public:
    virtual const void *nodeTypeKey() const
    {
      return NodeTypeToken<CustomNestable>();
    }
    virtual int
    layoutNode(Node *, const LayoutState &s, IPlatformLayoutTraversal *, const LazyWindow * = 0, StackSpans * = 0)
    {
      return s.y + 5;
    }
  };

  int textY(TextNode *text)
  {
    NullTextContext *context = static_cast<NullTextContext *>(text->getContext());
    LOKA_VERIFY(context != 0);
    return context->measurement().height();
  }
} // namespace

void testContainerGapSeatsAndTraversal()
{
  loka::core::MutableState<ConfirmResult> resultStorage;
  NodeState<ConfirmResult> result(&resultStorage, 0);
  for (unsigned kind = 0; kind != 6; ++kind)
  {
    StackNode column((StackProps(STACK_AXIS_COLUMN)));
    column.addChild(new TextNode(TextProps("a")));
    Node *empty = noSeat(kind, result);
    LOKA_VERIFY(empty != 0 && !childTakesLayoutSeat(empty));
    column.addChild(empty);
    column.addChild(new TextNode(TextProps("b")));
    Recorder c;
    const int bottom =
        computeColumnLayoutResultY(&column, seat(), &c, &Recorder::child, 12, &loka::testing::acceptShortLayoutY);
    LOKA_VERIFY(c.calls == 3 && c.states[0].y == 20 && c.states[2].y == 37 && bottom == 42);
    BoxNode box((BoxProps()));
    box.addChild(new TextNode(TextProps("a")));
    box.addChild(noSeat(kind, result));
    box.addChild(new TextNode(TextProps("b")));
    Recorder b;
    LOKA_VERIFY(computeBoxLayoutResultY(&box, seat(), &b, &Recorder::child, 12, &loka::testing::acceptShortLayoutY)
                == 42);
    LOKA_VERIFY(b.calls == 3 && b.states[2].y == 37);
    {
      NullScenePlatformController platform;
      StackNode projected((StackProps(STACK_AXIS_COLUMN)));
      projected.addChild(new TextNode(TextProps("a")));
      projected.addChild(noSeat(kind, result));
      projected.addChild(new TextNode(TextProps("b")));
      LOKA_VERIFY(platform.projectLayoutForTesting(&projected, seat()) == 56);
      const unsigned expectedLeaves = kind == 4 ? 3 : (kind == 5 ? 4 : 2);
      LOKA_VERIFY(platform.leafLayoutVisitsForTesting() == expectedLeaves);
    }
    for (unsigned aligned = 0; aligned != 2; ++aligned)
    {
      StackProps props(STACK_AXIS_ROW);
      props.hasVerticalAlignment_ = aligned != 0;
      props.verticalAlignment_ = VERTICAL_ALIGNMENT_CENTER;
      NullScenePlatformController platform;
      StackNode row(props);
      row.addChild(new TextNode(TextProps("a")));
      row.addChild(noSeat(kind, result));
      row.addChild(new TextNode(TextProps("b")));
      RowLayoutMetrics metrics;
      metrics.gap = 4;
      metrics.fallbackHeight = 90;
      metrics.textHeight = 5;
      Recorder r;
      computeRowLayoutResultY(&row, seat(), metrics, 0, &r, &Recorder::child);
      LOKA_VERIFY(r.calls == 3 && r.states[0].width == 48 && r.states[1].width == 0);
      LOKA_VERIFY(r.states[2].x == 62 && r.states[2].width == 48);
      LOKA_VERIFY(r.states[0].height == (aligned ? 5 : 100));
      LOKA_VERIFY(platform.projectLayoutForTesting(&row, seat()) == 32);
      Recorder toolbox;
      ComputeToolboxRowLayout(&row, seat(), &toolbox);
      LOKA_VERIFY(toolbox.calls == 3 && toolbox.states[0].width == 48);
      LOKA_VERIFY(toolbox.states[1].width == 0 && toolbox.states[2].x == 62);
      // Compare against the same twin with only seats: no empty child can raise height.
      StackNode baseline(props);
      baseline.addChild(new TextNode(TextProps("a")));
      baseline.addChild(new TextNode(TextProps("b")));
      Recorder plain;
      ComputeToolboxRowLayout(&baseline, seat(), &plain);
      LOKA_VERIFY(toolbox.states[0].height == plain.states[0].height);
      LOKA_VERIFY(toolbox.states[0].y == plain.states[0].y);
      if (aligned)
      {
        StackNode shortRow(props);
        RectSurfaceProps surface;
        surface.height_ = 3;
        shortRow.addChild(new RectSurfaceNode(surface));
        shortRow.addChild(noSeat(kind, result));
        shortRow.addChild(new RectSurfaceNode(surface));
        Recorder shortSeats;
        ComputeToolboxRowLayout(&shortRow, seat(), &shortSeats);
        LOKA_VERIFY(shortSeats.calls == 3 && shortSeats.states[0].height == 3);
        LOKA_VERIFY(shortSeats.states[2].height == 3 && shortSeats.states[2].y == 20);
      }
    }
  }
}

void testContainerGapNullGeometry()
{
  NullScenePlatformController platform;
  StackNode column((StackProps(STACK_AXIS_COLUMN)));
  for (int i = 0; i != 3; ++i)
    column.addChild(new TextNode(TextProps("x")));
  LayoutState s = seat();
  LOKA_VERIFY(platform.projectLayoutForTesting(&column, s) == 20 + 3 * 12 + 2 * 12);
  unsigned index = 0;
  for (Node *child = column.childrenHead(); child; child = child->nextInComposition, ++index)
  {
    TextNode *text = static_cast<TextNode *>(child);
    NullTextContext *context = static_cast<NullTextContext *>(text->getContext());
    LOKA_VERIFY(context->commitPresented(text->props.text_->get(), platform.paintScope()));
    const PaintQuery query = {platform.paintScope(), PLACEMENT_ELIGIBLE};
    LOKA_VERIFY(context->queryPaintDamage(query).damage.y == 20 + static_cast<int>(index) * 24);
  }
  BoxProps props;
  props.setPadding(8);
  BoxNode box(props);
  StackNode *inner = new StackNode(StackProps(STACK_AXIS_COLUMN));
  ObservedButton *button = new ObservedButton();
  inner->addChild(button);
  box.addChild(inner);
  LOKA_VERIFY(platform.projectLayoutForTesting(&box, s) == 20 + 8 + 32 + 8);
  LOKA_VERIFY(button->placed.y == 28 && button->placed.height == 32);
  ScrollViewNode scroll((ScrollViewProps()));
  scroll.addChild(new TextNode(TextProps("a")));
  scroll.addChild(new FragmentNode(FragmentProps()));
  scroll.addChild(new TextNode(TextProps("b")));
  s.height = 0;
  LOKA_VERIFY(platform.projectLayoutForTesting(&scroll, s) == 56);
}

void testContainerGapPassThroughLimits()
{
  NullScenePlatformController platform;
  StackNode row((StackProps(STACK_AXIS_ROW)));
  FragmentNode *fragment = new FragmentNode(FragmentProps());
  fragment->addChild(new TextNode(TextProps("a")));
  fragment->addChild(new TextNode(TextProps("b")));
  row.addChild(fragment);
  LOKA_VERIFY(platform.projectLayoutForTesting(&row, seat()) == 56);
  StackNode column((StackProps(STACK_AXIS_COLUMN)));
  column.addChild(new TextNode(TextProps("head")));
  FragmentNode *middle = new FragmentNode(FragmentProps());
  middle->addChild(new TextNode(TextProps("a")));
  middle->addChild(new TextNode(TextProps("b")));
  column.addChild(middle);
  column.addChild(new TextNode(TextProps("tail")));
  LOKA_VERIFY(platform.projectLayoutForTesting(&column, seat()) == 104);
  StackNode fillColumn((StackProps(STACK_AXIS_COLUMN)));
  FragmentNode *fill = new FragmentNode(FragmentProps());
  fill->addChild(new TextNode(TextProps("a")));
  fill->addChild(new RectSurfaceNode(RectSurfaceProps()));
  fillColumn.addChild(fill);
  // The Fragment's seat keeps all 100 units, including for its later fill child.
  LOKA_VERIFY(platform.projectLayoutForTesting(&fillColumn, seat()) == 144);
  StackProps centered(STACK_AXIS_COLUMN);
  centered.hasHorizontalAlignment_ = true;
  centered.horizontalAlignment_ = HORIZONTAL_ALIGNMENT_CENTER;
  StackNode aligned(centered);
  FragmentNode *wrapper = new FragmentNode(FragmentProps());
  BoxProps narrow;
  narrow.setSize(20, 5);
  BoxNode *box = new BoxNode(narrow);
  TextNode *text = new TextNode(TextProps("a"));
  box->addChild(text);
  wrapper->addChild(box);
  aligned.addChild(wrapper);
  platform.projectLayoutForTesting(&aligned, seat());
  NullTextContext *context = static_cast<NullTextContext *>(text->getContext());
  LOKA_VERIFY(context != 0);
  LOKA_VERIFY(context->commitPresented(text->props.text_->get(), platform.paintScope()));
  const PaintQuery query = {platform.paintScope(), PLACEMENT_ELIGIBLE};
  LOKA_VERIFY(context->queryPaintDamage(query).damage.x == 10);
}

void testContainerGapShortRefusal()
{
  NullScenePlatformController platform;
  ScrollViewNode scroll((ScrollViewProps()));
  StackNode *column = new StackNode(StackProps(STACK_AXIS_COLUMN));
  BoxProps large;
  large.setSize(10, 32760);
  BoxProps tiny;
  tiny.setSize(10, 1);
  column->addChild(new BoxNode(large));
  BoxNode *last = new BoxNode(tiny);
  TextNode *unreached = new TextNode(TextProps("unreached"));
  last->addChild(unreached);
  column->addChild(last);
  scroll.addChild(column);
  LayoutState s = seat();
  s.y = 0;
  const unsigned before = platform.scrollViewShortRangeRefusalCount();
  platform.projectLayoutForTesting(&scroll, s);
  LOKA_VERIFY(platform.scrollViewShortRangeRefusalCount() == before + 1);
  LOKA_VERIFY(unreached->getContext() == 0);
  // Both content endpoints fit short, but their total including the gap does not.
  ScrollViewNode accumulated((ScrollViewProps()));
  large.setSize(10, 20000);
  tiny.setSize(10, 12760);
  accumulated.addChild(new BoxNode(large));
  accumulated.addChild(new BoxNode(tiny));
  s.y = -10000;
  platform.projectLayoutForTesting(&accumulated, s);
  LOKA_VERIFY(platform.scrollViewShortRangeRefusalCount() == before + 2);

  // The common Box and Null generic path must also refuse before narrowing.
  for (unsigned kind = 0; kind != 2; ++kind)
  {
    ScrollViewNode wrapper((ScrollViewProps()));
    Node *container =
        kind ? static_cast<Node *>(new BoxNode(BoxProps())) : static_cast<Node *>(new FragmentNode(FragmentProps()));
    large.setSize(10, 32760);
    tiny.setSize(10, 1);
    container->asNestable()->addChild(new BoxNode(large));
    BoxNode *end = new BoxNode(tiny);
    TextNode *never = new TextNode(TextProps("never"));
    end->addChild(never);
    container->asNestable()->addChild(end);
    wrapper.addChild(container);
    s.y = 0;
    platform.projectLayoutForTesting(&wrapper, s);
    LOKA_VERIFY(platform.scrollViewShortRangeRefusalCount() == before + 3 + kind);
    LOKA_VERIFY(never->getContext() == 0);
  }
}

void testContainerGapShapeVerdicts()
{
  {
    NullScenePlatformController platform;
    Scene scene((Boundary<HiddenArm>()));
    scene.mount(&platform);
    loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);
    BoundaryNode *owner = loka::dsl::testing::SceneTestAccess::rootBoundary(scene);
    Node *column = owner->childrenHead();
    LOKA_VERIFY(column && column->asStackNode());
    Node *arm = column->asNestable()->childrenHead();
    LOKA_VERIFY(arm && isLayoutPassThrough(arm) && !childTakesLayoutSeat(arm));
    verifySeatVerdicts(arm);
    Recorder recorder;
    computeColumnLayoutResultY(
        column->asStackNode(), seat(), &recorder, &Recorder::child, 12, &loka::testing::acceptShortLayoutY);
    LOKA_VERIFY(recorder.calls == 1);
    loka::dsl::testing::SceneTestAccess::unmount(scene);
  }

  ProjectedNestable projected;
  LOKA_VERIFY(!isLayoutPassThrough(&projected) && childTakesLayoutSeat(&projected));
  CustomNestable custom;
  PlatformLayoutHandlerRegistry registry;
  LOKA_VERIFY(registry.registerHandler(new CustomHandler()));
  LOKA_VERIFY(registry.find(&custom) != 0);
  LOKA_VERIFY(isLayoutPassThrough(&custom) && !childTakesLayoutSeat(&custom));
}

void testContainerGapRefusedTextRecovery()
{
  NullScenePlatformController platform;
  StackNode column((StackProps(STACK_AXIS_COLUMN)));
  TextNode *first = new TextNode(TextProps("a"));
  column.addChild(first);
  column.addChild(new TextNode(TextProps("b")));
  loka::app::testing::NullTextMeasurementAccess::failMeasure(1);
  const int refused = platform.projectLayoutForTesting(&column, seat());
  const int fallback = textY(first);
  LOKA_VERIFY(refused == 20 + fallback + 12 + 12);
  loka::app::testing::NullTextMeasurementAccess::failMeasure(0);
  LOKA_VERIFY(platform.projectLayoutForTesting(&column, seat()) == 56);
}

void testContainerGapNaturalColumn()
{
  loka::core::MutableState<ConfirmResult> resultStorage;
  NodeState<ConfirmResult> result(&resultStorage, 0);
  NullScenePlatformController platform;
  StackNode row((StackProps(STACK_AXIS_ROW)));
  StackNode *column = new StackNode(StackProps(STACK_AXIS_COLUMN));
  column->addChild(new ConfirmDialogNode(ConfirmDialogProps().result(result)));
  column->addChild(new ButtonNode(Button("a").props));
  row.addChild(column);
  StackNode *nestedRow = new StackNode(StackProps(STACK_AXIS_ROW));
  nestedRow->addChild(new ConfirmDialogNode(ConfirmDialogProps().result(result)));
  nestedRow->addChild(new ButtonNode(Button("bb").props));
  row.addChild(nestedRow);
  RowWidthConsultation widths(row.childrenHead(), row.childrenCount(), 100, 4, ROW_UNDECLARED_WIDTH_NATURAL, &platform);
  LOKA_VERIFY(widths.next(column).width() == 24);
  LOKA_VERIFY(widths.next(column->nextInComposition).width() == 32);
}

void testContainerGapKindCharacterization()
{
  loka::core::MutableState<FileChooserResult> fileResultStorage;
  NodeState<FileChooserResult> fileResult(&fileResultStorage, 0);
  loka::core::MutableState<ConfirmResult> confirmResultStorage;
  NodeState<ConfirmResult> confirmResult(&confirmResultStorage, 0);
  verifySeatVerdicts(0);
  BoxNode box((BoxProps()));
  LOKA_VERIFY(box.kind() == NODE_KIND_BOX);
  verifySeatVerdicts(&box);
  ZStackNode zstack((ZStackProps()));
  LOKA_VERIFY(zstack.kind() == NODE_KIND_ZSTACK);
  verifySeatVerdicts(&zstack);
  GridNode grid((GridProps()));
  LOKA_VERIFY(grid.kind() == NODE_KIND_GRID);
  verifySeatVerdicts(&grid);
  CellNode cell((CellProps()));
  LOKA_VERIFY(cell.kind() == NODE_KIND_CELL);
  verifySeatVerdicts(&cell);
  StackNode row((StackProps(STACK_AXIS_ROW)));
  LOKA_VERIFY(row.kind() == NODE_KIND_STACK);
  verifySeatVerdicts(&row);
  StackNode column((StackProps(STACK_AXIS_COLUMN)));
  LOKA_VERIFY(column.kind() == NODE_KIND_STACK);
  verifySeatVerdicts(&column);
  TextNode textLeaf((TextProps()));
  LOKA_VERIFY(textLeaf.kind() == NODE_KIND_TEXT);
  verifySeatVerdicts(&textLeaf);
  ButtonNode button((ButtonProps()));
  LOKA_VERIFY(button.kind() == NODE_KIND_BUTTON);
  verifySeatVerdicts(&button);
  EditTextNode editText((EditTextProps()));
  LOKA_VERIFY(editText.kind() == NODE_KIND_EDIT_TEXT);
  verifySeatVerdicts(&editText);
  PopupMenuNode popupMenu((PopupMenuProps()));
  LOKA_VERIFY(popupMenu.kind() == NODE_KIND_POPUP_MENU);
  verifySeatVerdicts(&popupMenu);
  OpenFileDialogNode openDialog(OpenFileDialogProps().result(fileResult));
  LOKA_VERIFY(openDialog.kind() == NODE_KIND_OPEN_FILE_DIALOG);
  verifySeatVerdicts(&openDialog);
  ImageViewNode image((ImageViewProps()));
  LOKA_VERIFY(image.kind() == NODE_KIND_IMAGE_VIEW);
  verifySeatVerdicts(&image);
  RectSurfaceNode surface((RectSurfaceProps()));
  LOKA_VERIFY(surface.kind() == NODE_KIND_RECT_SURFACE);
  verifySeatVerdicts(&surface);
  ScrollBarNode scrollBar((ScrollBarProps()));
  LOKA_VERIFY(scrollBar.kind() == NODE_KIND_SCROLL_BAR);
  verifySeatVerdicts(&scrollBar);
  ScrollViewNode scrollView((ScrollViewProps()));
  LOKA_VERIFY(scrollView.kind() == NODE_KIND_SCROLL_VIEW);
  verifySeatVerdicts(&scrollView);
  CanvasNode canvas((CanvasProps()));
  LOKA_VERIFY(canvas.kind() == NODE_KIND_CANVAS);
  verifySeatVerdicts(&canvas);
  AttributedTextNode attributedText((AttributedTextProps()));
  LOKA_VERIFY(attributedText.kind() == NODE_KIND_ATTRIBUTED_TEXT);
  verifySeatVerdicts(&attributedText);
  TextEditorNode editor((TextEditorProps()));
  LOKA_VERIFY(editor.kind() == NODE_KIND_TEXT_EDITOR);
  verifySeatVerdicts(&editor);
  ConfirmDialogNode confirmDialog(ConfirmDialogProps().result(confirmResult));
  LOKA_VERIFY(confirmDialog.kind() == NODE_KIND_CONFIRM_DIALOG);
  verifySeatVerdicts(&confirmDialog);
  RibbonGroupNode ribbon((RibbonGroupProps()));
  LOKA_VERIFY(ribbon.kind() == NODE_KIND_RIBBON_GROUP);
  verifySeatVerdicts(&ribbon);
  RibbonGroupEdgeNode ribbonEdge((RibbonGroupEdgeProps()));
  LOKA_VERIFY(ribbonEdge.kind() == NODE_KIND_RIBBON_GROUP_EDGE);
  verifySeatVerdicts(&ribbonEdge);
  OpenFileDialogNode saveDialog(SaveFileDialog(loka::core::String()).result(fileResult).props);
  LOKA_VERIFY(saveDialog.kind() == NODE_KIND_OPEN_FILE_DIALOG);
  verifySeatVerdicts(&saveDialog);
  FragmentNode empty((FragmentProps()));
  verifySeatVerdicts(&empty);
  FragmentNode text((FragmentProps()));
  text.addChild(new TextNode(TextProps("seat")));
  verifySeatVerdicts(&text);
  FragmentNode dialog((FragmentProps()));
  dialog.addChild(new ConfirmDialogNode(ConfirmDialogProps().result(confirmResult)));
  verifySeatVerdicts(&dialog);
  EmptyComponent component;
  verifySeatVerdicts(&component);
  CustomNestable custom;
  verifySeatVerdicts(&custom);
  ProjectedLeaf projected;
  LOKA_VERIFY(projected.kind() == NODE_KIND_UNKNOWN && !projected.asNestable());
  verifySeatVerdicts(&projected);
  ProjectedNestable projectedNestable;
  verifySeatVerdicts(&projectedNestable);
}
