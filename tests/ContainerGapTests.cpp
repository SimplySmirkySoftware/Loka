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
#include "app/nodes/boundary/StdComposition.hpp"
#include "app/scene/Scene.hpp"
#include "testing/scene/SceneTestFlow.hpp"
#include "platform/null/NullScenePlatformController.hpp"
#include "platform/null/context/NullTextContext.hpp"
#include "platform/null/context/NullRectSurfaceContext.hpp"
#include "../apple/toolbox/src/ToolboxPlatformLayoutHandlers.hpp"

#include "app/nodes/controls/Card.hpp"
#include "app/layout/LayoutWidthSum.hpp"
#include <cstdio>
#include "app/nodes/nestable/Show.hpp"

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
    Recorder toolboxColumn;
    LOKA_VERIFY(ComputeToolboxColumnLayout(&column, seat(), &toolboxColumn) == 100);
    LOKA_VERIFY(toolboxColumn.calls == 3 && toolboxColumn.states[0].y == 20);
    LOKA_VERIFY(toolboxColumn.states[2].y == 29 && toolboxColumn.bottom == 34);
    BoxNode box((BoxProps()));
    box.addChild(new TextNode(TextProps("a")));
    box.addChild(noSeat(kind, result));
    box.addChild(new TextNode(TextProps("b")));
    Recorder b;
    LOKA_VERIFY(computeBoxLayoutResultY(&box,
                                        loka::app::layout::RailSpacing(12, 4, 9),
                                        seat(),
                                        &b,
                                        &Recorder::child,
                                        12,
                                        &loka::testing::acceptShortLayoutY)
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

      metrics.fallbackHeight = 90;
      metrics.textHeight = 5;
      Recorder r;
      computeRowLayoutResultY(
          &row, seat(), metrics, loka::app::layout::RailSpacing(12, 4, 9), 0, &r, &Recorder::child, 4, 0);
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
  Recorder toolbox;
  LOKA_VERIFY(ComputeToolboxColumnLayout(&column, s, &toolbox) == 100);
  LOKA_VERIFY(toolbox.calls == 3 && toolbox.states[0].y == 20);
  LOKA_VERIFY(toolbox.states[1].y == 29 && toolbox.states[2].y == 38 && toolbox.bottom == 43);
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
  LOKA_VERIFY(platform.projectLayoutForTesting(&row, seat()) == 48);
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
    Recorder toolbox;
    ComputeToolboxColumnLayout(column->asStackNode(), seat(), &toolbox);
    LOKA_VERIFY(toolbox.calls == 1 && toolbox.bottom == seat().y);
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
  RowWidthConsultation widths(row.childrenHead(),
                              row.childrenCount(),
                              100,
                              4,
                              loka::app::layout::RailSpacing(12, 4, 9),
                              ROW_UNDECLARED_WIDTH_NATURAL,
                              &platform);
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

namespace
{
  FragmentNode *gapPair()
  {
    FragmentNode *pair = new FragmentNode(FragmentProps());
    pair->addChild(new TextNode(TextProps("a")));
    pair->addChild(new TextNode(TextProps("b")));
    return pair;
  }

  int gapTextY(NullScenePlatformController &platform, Node *node)
  {
    TextNode *text = node->asTextNode();
    LOKA_VERIFY(text != 0);
    NullTextContext *context = static_cast<NullTextContext *>(text->getContext());
    LOKA_VERIFY(context != 0);
    LOKA_VERIFY(context->commitPresented(text->props.text_->get(), platform.paintScope()));
    const PaintQuery query = {platform.paintScope(), PLACEMENT_ELIGIBLE};
    return context->queryPaintDamage(query).damage.y;
  }

  void verifyPairGap(NullScenePlatformController &platform, FragmentNode *pair, int gap)
  {
    LOKA_VERIFY(gapTextY(platform, pair->childrenHead()->nextInComposition) - gapTextY(platform, pair->childrenHead())
                == 12 + gap);
  }

  class GapDelivery : public BoundaryNodeFor<GapDelivery>
  {
  public:
    typedef BoundaryPropsFor<GapDelivery> Props;
    explicit GapDelivery(const Props &props)
        : BoundaryNodeFor<GapDelivery>(props)
    {
      this->state(this->visible_, true);
    }
    virtual void composeNode(NodeComposition &composition)
    {
      composition.declare(Column().gap(16)
                          << Text("one")
                          << (Show(*this->visible_.state()) << (Fragment() << Text("two") << Text("three")))
                          << Button("end"));
      composition.declare(Box().gap(16) << Text(
                              "one") << (Show(*this->visible_.state()) << (Fragment() << Text("two") << Text("three")))
                                        << Button("end"));
    }

  private:
    NodeState<bool> visible_;
  };

  void collectGapTexts(Node *node, Node **texts, unsigned &count)
  {
    if (node->asTextNode())
      texts[count++] = node;
    INestable *content = node->asNestable();
    for (Node *child = content ? content->childrenHead() : 0; child; child = child->nextInComposition)
      collectGapTexts(child, texts, count);
  }

  class GapNaturalSource : public INaturalWidthSource
  {
  public:
    virtual bool queryNaturalWidth(Node *, short &width) const
    {
      width = 10;
      return true;
    }
  };
} // namespace

void testGapDoorValuesAndIdentity()
{
  LOKA_VERIFY(sizeof(LayoutGap) == sizeof(short));
  LOKA_VERIFY(LayoutGap::unspecified().resolve(7, 9) == 7);
  LOKA_VERIFY(LayoutGap::of(0).resolve(7, 9) == 0);
  LOKA_VERIFY(LayoutGap::of(SHRT_MAX).resolve(7, 9) == SHRT_MAX);
  LOKA_VERIFY(LayoutGap::unspecified() != LayoutGap::of(0));
#ifdef NDEBUG
  LOKA_VERIFY(LayoutGap::of(-1).resolve(7, 9) == 0);
  LOKA_VERIFY(LayoutGap::of(SHRT_MAX + 1).resolve(7, 9) == SHRT_MAX);
#else
  std::printf("[skip] LayoutGap release clamps require NDEBUG\n");
#endif
  StackProps a, b;
  b.gap(0);
  LOKA_VERIFY(a < b || b < a);
  a.gap(20);
  LOKA_VERIFY(a < b || b < a);
  BoxProps c, d;
  d.setGap(0);
  LOKA_VERIFY(c < d || d < c);
  LOKA_VERIFY(c.hash() != d.hash());
  c.setGap(20);
  LOKA_VERIFY(c < d || d < c);
  LOKA_VERIFY(c.hash() != d.hash());
  LOKA_VERIFY(VStack().gap(2).props.gap_ == LayoutGap::of(2));
  LOKA_VERIFY(HStack().gap(3).props.gap_ == LayoutGap::of(3));
  loka::core::MutableState<StackAxis> axis(STACK_AXIS_COLUMN);
  LOKA_VERIFY(Stack(&axis).gap(4).props.gap_ == LayoutGap::of(4));

  NullScenePlatformController platform;
  StackNode column((StackProps(STACK_AXIS_COLUMN)));
  column.setPropsTypeId(StackProps::staticTypeId());
  column.addChild(new TextNode(TextProps("a")));
  column.addChild(new TextNode(TextProps("b")));
  platform.projectLayoutForTesting(&column, seat());
  LOKA_VERIFY(gapTextY(platform, column.childrenHead()->nextInComposition) == seat().y + 24);
  LOKA_VERIFY(Column().gap(20).applyPropsToNode(&column));
  platform.projectLayoutForTesting(&column, seat());
  LOKA_VERIFY(gapTextY(platform, column.childrenHead()->nextInComposition) == seat().y + 32);
}

void testGapDoorDelivery()
{
  NullScenePlatformController platform;
  Scene scene((Boundary<GapDelivery>()));
  scene.mount(&platform);
  loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);
  BoundaryNode *owner = loka::dsl::testing::SceneTestAccess::rootBoundary(scene);
  for (Node *container = owner->childrenHead(); container; container = container->nextInComposition)
  {
    platform.projectLayoutForTesting(container, seat());
    Node *texts[3];
    unsigned count = 0;
    collectGapTexts(container, texts, count);
    LOKA_VERIFY(count == 3);
    for (unsigned i = 0; i < 3; ++i)
      LOKA_VERIFY(gapTextY(platform, texts[i]) == seat().y + static_cast<int>(i) * 28);
    // The final Button starts after the same gap (its height is 32).
    LOKA_VERIFY(platform.projectLayoutForTesting(container, seat()) == seat().y + 3 * 28 + 32);
  }
  loka::dsl::testing::SceneTestAccess::unmount(scene);

  for (unsigned wrapper = 0; wrapper < 3; ++wrapper)
  {
    Node *root = wrapper == 0   ? static_cast<Node *>((Column().gap(20)).create())
                 : wrapper == 1 ? static_cast<Node *>(ScrollView().create())
                                : static_cast<Node *>(Fragment().create());
    BoxNode *box = new BoxNode(BoxProps());
    FragmentNode *inside = gapPair();
    box->addChild(inside);
    FragmentNode *after = gapPair();
    root->asNestable()->addChild(box);
    root->asNestable()->addChild(after);
    LayoutState state = seat();
    state.spacing = 0;
    state.height = 0;
    platform.projectLayoutForTesting(root, state);
    verifyPairGap(platform, inside, 12);
    verifyPairGap(platform, after, wrapper == 0 ? 20 : 12);
    LOKA_VERIFY(gapTextY(platform, after->childrenHead()) == state.y + 36 + (wrapper == 0 ? 20 : 12));
    LOKA_VERIFY(state.spacing == 0);
    DestroyHeapNode(root);
  }
  // The generic walker reuses its child offer; a callee must not write into it.
  {
    StackNode outer(Column().gap(20).props);
    FragmentNode *siblings = new FragmentNode(FragmentProps());
    siblings->addChild(Box().gap(3).create());
    FragmentNode *pair = gapPair();
    siblings->addChild(pair);
    outer.addChild(siblings);
    platform.projectLayoutForTesting(&outer, seat());
    verifyPairGap(platform, pair, 20);
  }
  for (unsigned specified = 0; specified < 2; ++specified)
  {
    StackProps props(STACK_AXIS_ROW);
    if (specified)
      props.gap(20);
    StackNode row(props);
    FragmentNode *pair = gapPair();
    row.addChild(pair);
    platform.projectLayoutForTesting(&row, seat());
    verifyPairGap(platform, pair, specified ? 20 : 4);
  }
}

void testGapDoorResets()
{
  NullScenePlatformController platform;
  for (unsigned kind = 0; kind < 6; ++kind)
  {
    StackNode outer(Column().gap(20).props);
    Node *reset = 0;
    switch (kind)
    {
    case 0:
      reset = ZStack().create();
      break;
    case 1:
      reset = Grid().create();
      break;
    case 2:
      reset = new CardNode(CardProps());
      break;
    case 3:
      reset = new RibbonGroupNode(RibbonGroupProps());
      break;
    case 4:
      reset = ScrollView().create();
      break;
    case 5:
      reset = Column().create();
      break;
    }
    FragmentNode *pair = gapPair();
    reset->asNestable()->addChild(pair);
    outer.addChild(reset);
    LayoutState state = seat();
    state.height = 0;
    platform.projectLayoutForTesting(&outer, state);
    verifyPairGap(platform, pair, 12);
  }
  FragmentNode root((FragmentProps()));
  root.addChild(new TextNode(TextProps("a")));
  root.addChild(new TextNode(TextProps("b")));
  LayoutState state = seat();
  state.spacing = 0;
  platform.projectLayoutForTesting(&root, state);
  verifyPairGap(platform, &root, 12);
}

void testGapDoorOverflow()
{
  // Pin both calls to the shared acceptance door, including the origin which
  // a positive-width right-edge check would otherwise subsume on Null.
  struct Acceptance
  {
    int coordinates[4];
    unsigned count;
    unsigned placed;
    Acceptance()
        : count(0),
          placed(0)
    {
    }
    static bool accept(void *context, int coordinate)
    {
      Acceptance &self = *static_cast<Acceptance *>(context);
      self.coordinates[self.count++] = coordinate;
      return coordinate <= SHRT_MAX;
    }
    static int child(void *context, Node *, const LayoutState &state)
    {
      ++static_cast<Acceptance *>(context)->placed;
      return state.y + 1;
    }
  };
  for (unsigned edge = 0; edge < 2; ++edge)
  {
    StackNode row(Row().props);
    row.addChild(Box().size(1, 1).create());
    row.addChild(Box().size(1, 1).create());
    LayoutState state = seat();
    state.x = 0;
    Acceptance acceptance;
    RowLayoutMetrics metrics;
    const int result = computeRowLayoutResultY(&row,
                                               state,
                                               metrics,
                                               loka::app::layout::RailSpacing(12, 4, 9),
                                               0,
                                               &acceptance,
                                               &Acceptance::child,
                                               edge ? SHRT_MAX - 1 : SHRT_MAX,
                                               &Acceptance::accept);
    LOKA_VERIFY(result == state.y + 1);
    LOKA_VERIFY(acceptance.placed == 1);
    LOKA_VERIFY(acceptance.count == (edge ? 4u : 3u));
    LOKA_VERIFY(acceptance.coordinates[0] == 0 && acceptance.coordinates[1] == 1);
    LOKA_VERIFY(acceptance.coordinates[2] == (edge ? SHRT_MAX : SHRT_MAX + 1));
    if (edge)
      LOKA_VERIFY(acceptance.coordinates[3] == SHRT_MAX + 1);
  }

  for (unsigned rightEdge = 0; rightEdge < 2; ++rightEdge)
  {
    NullScenePlatformController platform;
    ScrollViewNode scroll((ScrollViewProps()));
    StackNode *row = new StackNode(Row().gap(rightEdge ? SHRT_MAX - 1 : SHRT_MAX).props);
    row->addChild(Box().size(1, 1).create());
    BoxNode *last = new BoxNode(Box().size(1, 1).props);
    TextNode *unreached = new TextNode(TextProps("never"));
    last->addChild(unreached);
    row->addChild(last);
    scroll.addChild(row);
    LayoutState state = seat();
    state.x = state.y = 0;
    platform.projectLayoutForTesting(&scroll, state);
    LOKA_VERIFY(platform.scrollViewShortRangeRefusalCount() == 1);
    LOKA_VERIFY(unreached->getContext() == 0);
  }
  LayoutWidthSum sum;
  for (long i = 0; i < 65540; ++i)
    sum.add(SHRT_MAX);
  LOKA_VERIFY(sum.value() == SHRT_MAX + 1L);
  sum.add(0);
  LOKA_VERIFY(sum.value() == SHRT_MAX + 1L);
  LayoutWidthSum product;
  product.addProduct(65540, SHRT_MAX);
  LOKA_VERIFY(product.value() == SHRT_MAX + 1L);
  GapNaturalSource source;
  StackNode row(Row().gap(SHRT_MAX).props);
  row.props.rowUndeclaredWidth_ = ROW_UNDECLARED_WIDTH_NATURAL;
  for (int i = 0; i < 3; ++i)
    row.addChild(Button("b").create());
  RowWidthConsultation measure(
      &row, 1, 100, 4, loka::app::layout::RailSpacing(12, 4, 9), ROW_UNDECLARED_WIDTH_NATURAL, &source);
  LOKA_VERIFY(measure.next(&row).width() == 100); // Declined natural width becomes a flex seat.
  RowWidthConsultation allocate(row.childrenHead(),
                                row.childrenCount(),
                                100,
                                SHRT_MAX,
                                loka::app::layout::RailSpacing(12, 4, 9),
                                ROW_UNDECLARED_WIDTH_SHARED,
                                &source);
  LOKA_VERIFY(allocate.next(row.childrenHead()).width() == 0);
  // Fixed widths overflow the tally independently of gap multiplication.
  StackNode fixed(Row().props);
  fixed.addChild(Box().size(20000, 1).create());
  fixed.addChild(Box().size(20000, 1).create());
  fixed.addChild(Button("flex").create());
  RowWidthConsultation fixedAllocation(fixed.childrenHead(),
                                       fixed.childrenCount(),
                                       100,
                                       0,
                                       loka::app::layout::RailSpacing(12, 4, 9),
                                       ROW_UNDECLARED_WIDTH_SHARED,
                                       &source);
  fixedAllocation.next(fixed.childrenHead());
  fixedAllocation.next(fixed.childrenHead()->nextInComposition);
  LOKA_VERIFY(fixedAllocation.next(fixed.childrenHead()->nextInComposition->nextInComposition).width() == 0);
  class WideSource : public INaturalWidthSource
  {
  public:
    virtual bool queryNaturalWidth(Node *, short &width) const
    {
      width = 20000;
      return true;
    }
  } wide;
  StackNode sharedWide(Row().gap(0).props);
  sharedWide.addChild(Button("one").create());
  sharedWide.addChild(Button("two").create());
  RowWidthConsultation flexProduct(
      &sharedWide, 1, 100, 0, loka::app::layout::RailSpacing(12, 4, 9), ROW_UNDECLARED_WIDTH_NATURAL, &wide);
  LOKA_VERIFY(flexProduct.next(&sharedWide).width() == 100);
  row.props.rowUndeclaredWidth_ = ROW_UNDECLARED_WIDTH_SHARED;
  RowWidthConsultation shared(
      &row, 1, 100, 4, loka::app::layout::RailSpacing(12, 4, 9), ROW_UNDECLARED_WIDTH_NATURAL, &source);
  LOKA_VERIFY(shared.next(&row).width() == 100);
}

void testGapDoorNaturalWidths()
{
  GapNaturalSource source;
  for (unsigned policy = 0; policy < 2; ++policy)
  {
    for (unsigned shape = 0; shape < 3; ++shape)
    {
      StackNode outer(Row().gap(20).props);
      outer.props.rowUndeclaredWidth_ = policy ? ROW_UNDECLARED_WIDTH_NATURAL : ROW_UNDECLARED_WIDTH_SHARED;
      outer.addChild(Button("a").create());
      StackNode *inner = new StackNode(shape == 1 ? Row().gap(3).props : Row().props);
      inner->props.rowUndeclaredWidth_ = outer.props.rowUndeclaredWidth_;
      inner->addChild(Button("b").create());
      inner->addChild(Button("c").create());
      Node *nested = inner;
      if (shape == 2)
      {
        StackNode *column = new StackNode(Column().gap(30).props);
        column->addChild(inner);
        nested = column;
      }
      outer.addChild(nested);
      const int innerWidth = shape == 1 ? 23 : 24;
      RowWidthConsultation local(outer.childrenHead(),
                                 outer.childrenCount(),
                                 200,
                                 20,
                                 loka::app::layout::RailSpacing(12, 4, 9),
                                 ROW_UNDECLARED_WIDTH_NATURAL,
                                 &source);
      LOKA_VERIFY(local.next(outer.childrenHead()).width() == 10);
      LOKA_VERIFY(local.next(nested).width() == innerWidth);
      RowWidthConsultation natural(
          &outer, 1, 200, 4, loka::app::layout::RailSpacing(12, 4, 9), ROW_UNDECLARED_WIDTH_NATURAL, &source);
      const int expected = policy ? 10 + 20 + innerWidth : 2 * innerWidth + 20;
      LOKA_VERIFY(natural.next(&outer).width() == expected);
      // A framed parent makes both Row policies eligible for overflow measurement.
      CardNode frame((CardProps()));
      StackNode *copy = new StackNode(outer.props);
      copy->addChild(Button("a").create());
      StackNode *copyInner = new StackNode(inner->props);
      copyInner->addChild(Button("b").create());
      copyInner->addChild(Button("c").create());
      if (shape == 2)
      {
        StackNode *column = new StackNode(Column().gap(30).props);
        column->addChild(copyInner);
        copy->addChild(column);
      }
      else
        copy->addChild(copyInner);
      frame.addChild(copy);
      FragmentNode parent((FragmentProps()));
      // The frame's lifetime belongs to this stack frame, so detach before teardown.
      parent.addChild(&frame);
      short width = 0;
      const bool known = queryContentOverflowWidth(&parent, loka::app::layout::RailSpacing(12, 4, 9), &source, width);
      parent.detachChildren();
      LOKA_VERIFY(known);
      LOKA_VERIFY(width == expected + frame.frameGeometry().insets.left + frame.frameGeometry().insets.right);
    }
  }
}

void testGapDoorSpans()
{
  NullScenePlatformController platform;
  StackNode column((Column().props));
  column.setPropsTypeId(StackProps::staticTypeId());
  for (int i = 0; i < 3; ++i)
    column.addChild(Text("x").create());
  const LazyWindow bands[] = {{1, 1}, {0, 1}, {0, 0}};
  for (unsigned i = 0; i < 3; ++i)
  {
    LOKA_VERIFY(Column().applyPropsToNode(&column));
    StackSpans spans;
    const int old = platform.projectLayoutForTesting(&column, seat(), 0, &spans);
    LOKA_VERIFY(old == seat().y + 60);
    LOKA_VERIFY(Column().gap(20).applyPropsToNode(&column));
    const unsigned long before = platform.leafLayoutVisitsForTesting();
    const int band = platform.projectLayoutForTesting(&column, seat(), &bands[i], &spans);
    LOKA_VERIFY(platform.leafLayoutVisitsForTesting() - before == 3);
    LOKA_VERIFY(band == seat().y + 76);
    StackSpans fresh;
    LOKA_VERIFY(band == platform.projectLayoutForTesting(&column, seat(), 0, &fresh));
    LOKA_VERIFY(spans.total() == fresh.total());
  }
}

void testWindowMarginSpacingValues()
{
  const LayoutGap values[] = {LayoutGap::unspecified(), StandardWindowMargin(), LayoutGap::of(9)};
  LOKA_VERIFY(values[0].resolve(7, 9) == 7);
  LOKA_VERIFY(values[1].resolve(7, 19) == 19);
  LOKA_VERIFY(values[2].resolve(7, 19) == 9);
  for (unsigned i = 0; i < 3; ++i)
    for (unsigned j = i + 1; j < 3; ++j)
    {
      LOKA_VERIFY(values[i] != values[j]);
      LOKA_VERIFY(values[i] < values[j] || values[j] < values[i]);
      LOKA_VERIFY(values[i].hash() != values[j].hash());
    }
  BoxProps unspecified, zero;
  zero.setPadding(0);
  LOKA_VERIFY(unspecified < zero || zero < unspecified);
  LOKA_VERIFY(unspecified.hash() != zero.hash());
  LOKA_VERIFY(unspecified.padding.resolve(0, 9) == zero.padding.resolve(0, 9));
#ifdef NDEBUG
  LOKA_VERIFY(LayoutGap::of(-2) == LayoutGap::of(0));
  LOKA_VERIFY(Box().padding(-2).props.padding == LayoutGap::of(0));
  LOKA_VERIFY(Box().padding(SHRT_MAX + 1).props.padding == LayoutGap::of(SHRT_MAX));
#else
  std::printf("[skip] named margin numeric rejection and padding clamps require NDEBUG\n");
#endif
  LOKA_VERIFY(Box().gap(StandardWindowMargin()).props.gap == values[1]);
  LOKA_VERIFY(Stack(STACK_AXIS_COLUMN).gap(StandardWindowMargin()).props.gap_ == values[1]);
  LOKA_VERIFY(Row().gap(StandardWindowMargin()).props.gap_ == values[1]);
  LOKA_VERIFY(Column().gap(StandardWindowMargin()).props.gap_ == values[1]);
}

void testWindowMarginSpacingLayout()
{
  const RailSpacing spacing(12, 4, 9);
  BoxNode box(Box().padding(StandardWindowMargin()).props);
  box.addChild(new TextNode(TextProps("pad")));
  Recorder r;
  LOKA_VERIFY(
      computeBoxLayoutResultY(&box, spacing, seat(), &r, &Recorder::child, 12, &loka::testing::acceptShortLayoutY)
      == seat().y + 9 + 5 + 9);
  LOKA_VERIFY(r.calls == 1);
  LOKA_VERIFY(r.states[0].x == seat().x + 9 && r.states[0].y == seat().y + 9);
  LOKA_VERIFY(r.states[0].width == seat().width - 18 && r.states[0].height == seat().height - 18);
  NullScenePlatformController platform;
  platform.projectLayoutForTesting(&box, seat());
  LOKA_VERIFY(gapTextY(platform, box.childrenHead()) == seat().y + 9);
  StackNode column(VStack().gap(StandardWindowMargin()).props);
  column.addChild(new TextNode(TextProps("a")));
  column.addChild(new TextNode(TextProps("b")));
  platform.projectLayoutForTesting(&column, seat());
  LOKA_VERIFY(gapTextY(platform, column.childrenHead()->nextInComposition) == seat().y + 12 + 9);
  StackNode nested(HStack().gap(StandardWindowMargin()).props);
  nested.props.rowUndeclaredWidth_ = ROW_UNDECLARED_WIDTH_NATURAL;
  nested.addChild(Button("one").create());
  nested.addChild(Button("two").create());
  GapNaturalSource source;
  RowWidthConsultation widths(&nested, 1, 100, 4, spacing, ROW_UNDECLARED_WIDTH_NATURAL, &source);
  LOKA_VERIFY(widths.next(&nested).width() == 29);
  StackNode overflowParent(Column().props);
  StackNode *overflowRow = new StackNode(nested.props);
  overflowRow->addChild(Button("one").create());
  overflowRow->addChild(Button("two").create());
  overflowParent.addChild(overflowRow);
  short overflowWidth = 0;
  LOKA_VERIFY(queryContentOverflowWidth(&overflowParent, spacing, &source, overflowWidth));
  LOKA_VERIFY(overflowWidth == 29);

  BoxNode namedGap(Box().gap(StandardWindowMargin()).props);
  namedGap.addChild(new TextNode(TextProps("first")));
  namedGap.addChild(new TextNode(TextProps("second")));
  platform.projectLayoutForTesting(&namedGap, seat());
  LOKA_VERIFY(gapTextY(platform, namedGap.childrenHead()->nextInComposition) == seat().y + 12 + 9);
  for (unsigned explicitZero = 0; explicitZero < 2; ++explicitZero)
  {
    BoxProps props;
    if (explicitZero)
      props.setPadding(0);
    BoxNode zero(props);
    zero.addChild(new TextNode(TextProps("zero")));
    Recorder zeroOffer;
    computeBoxLayoutResultY(
        &zero, spacing, seat(), &zeroOffer, &Recorder::child, 12, &loka::testing::acceptShortLayoutY);
    LOKA_VERIFY(zeroOffer.calls == 1 && zeroOffer.states[0].x == seat().x && zeroOffer.states[0].y == seat().y
                && zeroOffer.states[0].width == seat().width && zeroOffer.states[0].height == seat().height);
  }
  Recorder toolbox;
  LOKA_VERIFY(ComputeToolboxBoxLayout(&box, seat(), &toolbox) == seat().width);
  LOKA_VERIFY(toolbox.states[0].x == seat().x + 12);
  LOKA_VERIFY(toolbox.states[0].width == seat().width - 24);
}

namespace
{
  // Modern rail offers use int coordinates; the Box still refuses above short range.
  struct WideOffer
  {
    typedef int Coordinate;
    int x, y, width, height;
    WideOffer()
        : x(0),
          y(0),
          width(10),
          height(10)
    {
    }
  };
  struct WideRecorder
  {
    unsigned calls;
    WideRecorder()
        : calls(0)
    {
    }
    static int child(void *context, Node *, const WideOffer &offer)
    {
      ++static_cast<WideRecorder *>(context)->calls;
      return offer.y;
    }
  };
} // namespace

void testWindowMarginBoxRefusal()
{
  const RailSpacing spacing(12, 4, 9);
  BoxNode box(Box().padding(20000).props);
  box.addChild(new TextNode(TextProps("zero offer")));
  LayoutState state = seat();
  state.x = state.y = 0;
  Recorder r;
  computeBoxLayoutResultY(&box, spacing, state, &r, &Recorder::child, 12, &loka::testing::acceptShortLayoutY);
  LOKA_VERIFY(r.calls == 1 && r.states[0].width == 0 && r.states[0].height == 0);
  for (unsigned axis = 0; axis < 2; ++axis)
  {
    NullScenePlatformController platform;
    ScrollViewNode scroll((ScrollViewProps()));
    BoxNode *inner = new BoxNode(Box().padding(20000).props);
    TextNode *unreached = new TextNode(TextProps("refused origin"));
    inner->addChild(unreached);
    scroll.addChild(inner);
    state.x = axis ? 0 : 20000;
    state.y = axis ? 20000 : 0;
    platform.projectLayoutForTesting(&scroll, state);
    LOKA_VERIFY(platform.scrollViewShortRangeRefusalCount() == 1);
    LOKA_VERIFY(unreached->getContext() == 0);
  }
  BoxNode unpadded(Box().props);
  unpadded.addChild(new TextNode(TextProps("upper offer")));
  for (unsigned axis = 0; axis < 2; ++axis)
  {
    WideOffer offer;
    if (axis)
      offer.height = SHRT_MAX + 1;
    else
      offer.width = SHRT_MAX + 1;
    WideRecorder rejected;
    computeBoxLayoutResultY(&unpadded, spacing, offer, &rejected, &WideRecorder::child, 0, 0);
    LOKA_VERIFY(rejected.calls == 0);
  }
  struct PartialTraversal
  {
    static int child(void *, Node *, const LayoutState &)
    {
      return SHRT_MAX;
    }
  };
  // An empty Box has no child acceptance call to mask a missing origin preflight.
  BoxNode emptyRefused(Box().padding(20000).props);
  state.x = 0;
  state.y = 20000;
  LOKA_VERIFY(computeBoxLayoutResultY(
                  &emptyRefused, spacing, state, 0, &PartialTraversal::child, 0, &loka::testing::acceptShortLayoutY)
              == state.y);
  for (unsigned fixed = 0; fixed < 2; ++fixed)
  {
    BoxProps props = Box().padding(1).props;
    if (fixed)
      props.setSize(100, 100);
    BoxNode partial(props);
    partial.addChild(new TextNode(TextProps("first")));
    partial.addChild(new TextNode(TextProps("refused second")));
    state.x = state.y = 0;
    const int bottom = computeBoxLayoutResultY(
        &partial, spacing, state, 0, &PartialTraversal::child, 1, &loka::testing::acceptShortLayoutY);
    LOKA_VERIFY(bottom == SHRT_MAX);
  }
  // An accepting caller owns the range policy; an empty result retains the int origin.
  BoxNode empty(Box().padding(1).props);
  state.y = SHRT_MAX;
  LOKA_VERIFY(computeBoxLayoutResultY(&empty, spacing, state, 0, &PartialTraversal::child, 0, 0) == SHRT_MAX + 2);
  struct RefusingRecorder : Recorder
  {
    virtual bool refuseLayoutResultY(int y)
    {
      return y < SHRT_MIN || y > SHRT_MAX;
    }
    virtual int layoutChild(Node *, const LayoutState &s)
    {
      this->bottom = static_cast<short>(s.y + 1);
      return SHRT_MAX;
    }
  } toolbox;
  BoxNode wide(Box().padding(1).props);
  wide.addChild(new TextNode(TextProps("wide")));
  state.x = state.y = 0;
  LOKA_VERIFY(ComputeToolboxBoxLayout(&wide, state, &toolbox) == 0);
}
