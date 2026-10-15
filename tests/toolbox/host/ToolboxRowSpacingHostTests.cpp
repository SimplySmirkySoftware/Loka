#include "ToolboxLayoutMetrics.hpp"
#include "context/ToolboxRibbonGroupEdgeContext.hpp"
#include "ToolboxPlatformLayoutHandlers.hpp"
#include "app/nodes/controls/Ribbon.hpp"
#include "context/ToolboxTextContext.hpp"
#include "app/nodes/boundary/StdComposition.hpp"
#include "testing/scene/SceneTestFlow.hpp"
#include "support/TestVerify.hpp"
#include "core/util/StateTrackerGuard.hpp"
#include "platform/String.hpp"
#include <cstdio>
#include <cstring>

namespace
{
  using namespace loka::app;
  using namespace loka::app::scene;

  class SpacingNode;
  typedef BoundaryPropsFor<SpacingNode> SpacingProps;
  class SpacingNode : public StdCompositionBoundaryNodeBase<SpacingProps>
  {
  public:
    typedef SpacingProps::TypeTag TypeTag;
    explicit SpacingNode(const SpacingProps &props) : StdCompositionBoundaryNodeBase<SpacingProps>(props) {}
    virtual void composeNode(NodeComposition &c)
    {
      c.declare(
          Column()
          << (Column().TEST_ID("unaligned") << (Row() << Button("A").TEST_ID("row-a") << Button("B").TEST_ID("row-b"))
                                            << Text("below").TEST_ID("row-below"))
          << (Column().TEST_ID("aligned") << (Row().alignVertical(VERTICAL_ALIGNMENT_CENTER)
                                              << Button("A").TEST_ID("aligned-a") << Button("B").TEST_ID("aligned-b"))
                                          << Text("below").TEST_ID("aligned-below"))
          << (Column().TEST_ID("plain") << Button("A").TEST_ID("plain-a") << Button("B").TEST_ID("plain-b"))
          << (RibbonControl().testId("ribbon") << RibbonItem("New") << RibbonItem("Save As...").width(120))
          << (RibbonControl().testId("group") << RibbonItem(
                  "Open") << (RibbonGroup("Alignment") << RibbonItem("L") << RibbonItem("C") << RibbonItem("R"))
                                              << RibbonItem("Close"))
          << (RibbonControl().testId("caption-wins") << RibbonItem("Open")
                                                     << (RibbonGroup("Alignment options for selected objects")
                                                         << RibbonItem("L") << RibbonItem("C") << RibbonItem("R"))
                                                     << RibbonItem("Close")));
    }
  };

  Node *SpacingLookup(Scene &scene, const char *id)
  {
    Node *node = 0;
    loka::dsl::FlowError error;
    LOKA_VERIFY(loka::dsl::testing::LookupNodeById<Node>(&scene, id, node, error)
                == loka::dsl::FLOW_STEP_SUCCEEDED);
    LOKA_VERIFY(node != 0);
    return node;
  }

  class SpacingTraversal : public IPlatformLayoutTraversal
  {
  public:
    explicit SpacingTraversal(ToolboxScenePlatformController &controller)
        : controller_(controller),
          resultY_(0),
          rectCount_(0)
    {
      RegisterToolboxPlatformLayoutHandlers(this->registry_);
    }
    virtual bool queryNaturalWidth(Node *child, short &width) const
    {
      return this->controller_.queryNaturalWidth(child, width);
    }
    virtual int layoutChild(Node *node, const LayoutState &offer)
    {
      if (node->asRibbonGroupEdgeNode())
      {
        const loka::app::RibbonGroupEdge edge = node->asRibbonGroupEdgeNode()->props.edge;
        node->setContext(new ToolboxRibbonGroupEdgeContext(node->asRibbonGroupEdgeNode(), &this->controller_));
        LayoutState state = offer;
        const int width = node->context->layout(&this->controller_, state);
        toolbox_host::fills.clear();
        node->context->render(&this->controller_);
        LOKA_VERIFY(!toolbox_host::fills.empty());
        Rect painted = toolbox_host::fills.front().rect;
        for (unsigned i = 1; i < toolbox_host::fills.size(); ++i)
          painted.top = toolbox_host::fills[i].rect.top;
        this->edges_[edge] = painted;
        this->resultY_ = state.y;
        return width;
      }
      LayoutState state = offer;
      short width = 0;
      if (!ApplyToolboxPlatformLayoutHandler(this->registry_, *node, state, *this, width))
      {
        if (node->asButtonNode())
          node->setContext(new ToolboxButtonContext(node->asButtonNode(), &this->controller_));
        else
        {
          LOKA_VERIFY(node->asTextNode() != 0);
          node->setContext(new ToolboxTextContext(node->asTextNode(), &this->controller_));
        }
        width = node->context->layout(&this->controller_, state);
        node->context->render(&this->controller_);
        LOKA_VERIFY(this->rectCount_ < 6);
        if (node->asButtonNode())
          this->rects_[this->rectCount_] = toolbox_host::controlRect;
        else
        {
          SetRect(&this->rects_[this->rectCount_], offer.x, offer.y, offer.x + width, state.y);
        }
        ++this->rectCount_;
      }
      this->resultY_ = state.y;
      return width;
    }
    virtual void setLayoutResultY(short y) { this->resultY_ = y; }
    virtual short layoutResultY() const { return this->resultY_; }
    const Rect &edge(RibbonGroupEdge kind) const
    {
      return this->edges_[kind];
    }
    const Rect &rect(unsigned i) const
    {
      LOKA_VERIFY(i < this->rectCount_);
      return this->rects_[i];
    }

  private:
    ToolboxScenePlatformController &controller_;
    PlatformLayoutHandlerRegistry registry_;
    short resultY_;
    Rect rects_[6];
    Rect edges_[3];
    unsigned rectCount_;
  };
}

namespace
{
  class RefusingNaturalString : public loka::platform::String
  {
  public:
    virtual bool appendUtf8(std::string &out) const
    {
      out += "partial";
      return false;
    }
  };

  class RefusingFirstNaturalCollection : public loka::platform::String
  {
  public:
    RefusingFirstNaturalCollection()
        : refused_(false)
    {
    }
    virtual bool appendUtf8(std::string &out) const
    {
      out += "partial";
      if (this->refused_)
        return true;
      this->refused_ = true;
      return false;
    }

  private:
    mutable bool refused_;
  };

  void TextNaturalPins(ToolboxScenePlatformController &controller)
  {
    using namespace loka::core;
    PushStateTracker tracker;
    MutableState<String> value(String::Literal("Alignment"));
    MutableState<TextStyle> style((TextStyle()));
    TextProps props(&value);
    props.textStyleState_ = &style;
    TextNode node(props);
    ToolboxTextContext context(&node, &controller);
    const TextStyle styles[] = {TextStyle(), SmallLabel, SmallLabel + Bold};
    short widths[3] = {0, 0, 0};
    for (unsigned i = 0; i < 3; ++i)
    {
      {
        StateTrackerGuard guard(&tracker);
        style.set(styles[i]);
      }
      const int measurements = toolbox_host::widths;
      LOKA_VERIFY(controller.queryNaturalWidth(&node, widths[i]));
      LOKA_VERIFY(toolbox_host::widths == measurements + 1);
      LayoutState offer;
      offer.width = 0;
      offer.lineHeight = 16;
      offer.inputs = NODE_DIRTY_LAYOUT;
      LOKA_VERIFY(widths[i] == context.layout(&controller, offer));
    }
    LOKA_VERIFY(widths[1] < widths[0]);
    LOKA_VERIFY(widths[2] > widths[1]);
    short width = 123;
    ToolboxScenePlatformController portless(0);
    const int metrics = toolbox_host::metrics;
    LOKA_VERIFY(!portless.queryNaturalWidth(&node, width));
    LOKA_VERIFY(toolbox_host::metrics == metrics);
    LOKA_VERIFY(width == 123);
    {
      StateTrackerGuard guard(&tracker);
      style.set(TextStyle());
    }
    // Exercise nonpositive native measurements.
    ToolboxWindow *window = controller.window_;
    const short oldSize = window->port.txSize;
    {
      StateTrackerGuard guard(&tracker);
      value.set(String(std::string(255, 'a')));
    }
    window->port.txSize = 1;
    LOKA_VERIFY(!controller.queryNaturalWidth(&node, width));
    window->port.txSize = -3;
    LOKA_VERIFY(!controller.queryNaturalWidth(&node, width));
    window->port.txSize = oldSize;
    const String refused(Managed<loka::platform::String>::Wrap(new RefusingNaturalString()));
    const String refusesOnce(Managed<loka::platform::String>::Wrap(new RefusingFirstNaturalCollection()));
    const String declines[] = {String::Literal(""),
                               String::Literal("a\nb"),
                               String::Literal("a\rb"),
                               refused,
                               refusesOnce,
                               String(std::string(255, 'a') + "\n")};
    for (unsigned i = 0; i < sizeof(declines) / sizeof(declines[0]); ++i)
    {
      {
        StateTrackerGuard guard(&tracker);
        value.set(declines[i]);
      }
      LOKA_VERIFY(!controller.queryNaturalWidth(&node, width));
      LOKA_VERIFY(width == 123);
    }
    {
      StateTrackerGuard guard(&tracker);
      value.set(String::Literal("Changed"));
    }
    LOKA_VERIFY(controller.queryNaturalWidth(&node, width));
    LayoutState offer;
    offer.width = 0;
    offer.lineHeight = 16;
    offer.inputs = NODE_DIRTY_LAYOUT;
    LOKA_VERIFY(width == context.layout(&controller, offer));
    LOKA_VERIFY(width != widths[0]);
    std::string accented;
    for (unsigned i = 0; i < 255; ++i)
      accented += "\xC3\xA9";
    {
      StateTrackerGuard guard(&tracker);
      value.set(String(accented));
    }
    node.props.textStyleState_ = 0;
    node.props.textStyle_ = SmallLabel + Bold;
    LOKA_VERIFY(controller.queryNaturalWidth(&node, width));
    offer.inputs = NODE_DIRTY_LAYOUT;
    LOKA_VERIFY(width == context.layout(&controller, offer));
    node.props.text_ = 0;
    LOKA_VERIFY(!controller.queryNaturalWidth(&node, width));
    LOKA_VERIFY(!controller.queryNaturalWidth(0, width));
  }

  class GroupChildWidthTraversal : public IPlatformLayoutTraversal
  {
  public:
    LayoutState seat;
    virtual int layoutChild(Node *node, const LayoutState &state)
    {
      if (node->asRibbonGroupEdgeNode())
        return 999; // Child widths cannot change the group seat width.
      this->seat = state;
      return 73;
    }
    virtual void setLayoutResultY(short y)
    {
      this->seat.y = y;
    }
    virtual short layoutResultY() const
    {
      return this->seat.y;
    }
  };

  void GroupNaturalPins(Scene &scene,
                        ToolboxScenePlatformController &controller,
                        SpacingTraversal &traversal,
                        const LayoutState &state,
                        bool captionWins)
  {
    const char *id = captionWins ? "caption-wins" : "group";
    traversal.layoutChild(SpacingLookup(scene, id), state);
    short buttons[5] = {0, 0, 0, 0, 0};
    loka::dsl::FlowError error;
    for (unsigned i = 0; i < 5; ++i)
    {
      ButtonNode *button = 0;
      LOKA_VERIFY(loka::dsl::testing::ResolveSelector(
                      &scene, loka::dsl::testing::WithinAnchor(id).descendant<ButtonNode>(i + 1), button, error)
                  == loka::dsl::FLOW_STEP_SUCCEEDED);
      LOKA_VERIFY(controller.queryNaturalWidth(button, buttons[i]));
    }
    TextNode *caption = 0;
    LOKA_VERIFY(loka::dsl::testing::ResolveSelector(
                    &scene, loka::dsl::testing::WithinAnchor(id).descendant<TextNode>(1), caption, error)
                == loka::dsl::FLOW_STEP_SUCCEEDED);
    short captionWidth = 0;
    LOKA_VERIFY(controller.queryNaturalWidth(caption, captionWidth));
    const int items = buttons[1] + state.spacing + buttons[2] + state.spacing + buttons[3];
    LOKA_VERIFY(captionWins ? captionWidth > items : captionWidth < items);
    const int groupWidth = items > captionWidth ? items : captionWidth;
    Node *ribbon = SpacingLookup(scene, id);
    loka::dsl::CompositionCursor<Node> groups(ribbon->asNestable()->childrenHead(),
                                              ribbon->asNestable()->childrenCount());
    groups.next();
    RibbonGroupNode *group = groups.next()->asRibbonGroupNode();
    LOKA_VERIFY(group != 0);
    GroupChildWidthTraversal widthTraversal;
    const int inset = RibbonGroupNode::kFrameInset;
    LOKA_VERIFY(ComputeToolboxFramedLayout(group, state, &widthTraversal) == state.width);
    LOKA_VERIFY(widthTraversal.seat.x == state.x + inset);
    LOKA_VERIFY(widthTraversal.seat.width == state.width - 2 * inset);
    LOKA_VERIFY(widthTraversal.layoutResultY() == state.y + 2 * inset);
    const Rect open = traversal.rect(0), left = traversal.rect(1);
    const Rect center = traversal.rect(2), right = traversal.rect(3);
    const Rect label = traversal.rect(4), close = traversal.rect(5);
    LOKA_VERIFY(open.right - open.left == buttons[0]);
    LOKA_VERIFY(left.left == open.right + state.spacing + RibbonGroupNode::kFrameInset);
    LOKA_VERIFY(left.right - left.left == buttons[1]);
    LOKA_VERIFY(center.left == left.right + state.spacing);
    LOKA_VERIFY(center.right - center.left == buttons[2]);
    LOKA_VERIFY(right.left == center.right + state.spacing);
    LOKA_VERIFY(right.right - right.left == buttons[3]);
    LOKA_VERIFY(label.left == left.left && label.right - label.left == groupWidth);
    LOKA_VERIFY(close.left == label.right + state.spacing + RibbonGroupNode::kFrameInset);
    LOKA_VERIFY(close.right - close.left == buttons[4]);
    const int x = left.left - inset;
    const int y = left.top - inset;
    const int w = groupWidth + 2 * inset;
    // The content Column ends at its final child bottom.
    const int h = label.bottom + inset - y;
    const Rect expected[] = {
        {static_cast<short>(y), static_cast<short>(x), static_cast<short>(y + h - 1), static_cast<short>(x + 1)},
        {static_cast<short>(y),
         static_cast<short>(x + w - 1),
         static_cast<short>(y + h - 1),
         static_cast<short>(x + w)},
        {static_cast<short>(y + h - 1), static_cast<short>(x), static_cast<short>(y + h), static_cast<short>(x + w)}};
    for (unsigned i = 0; i < 3; ++i)
    {
      const Rect &actual = traversal.edge(static_cast<RibbonGroupEdge>(i));
      std::printf("edge %u actual=(%d,%d,%d,%d) expected=(%d,%d,%d,%d)\n",
                  i,
                  actual.left,
                  actual.top,
                  actual.right,
                  actual.bottom,
                  expected[i].left,
                  expected[i].top,
                  expected[i].right,
                  expected[i].bottom);
    }
    std::fflush(stdout);
    LOKA_VERIFY(EqualRect(&traversal.edge(RIBBON_GROUP_EDGE_LEFT), &expected[0]));
    LOKA_VERIFY(EqualRect(&traversal.edge(RIBBON_GROUP_EDGE_RIGHT), &expected[1]));
    LOKA_VERIFY(EqualRect(&traversal.edge(RIBBON_GROUP_EDGE_BOTTOM), &expected[2]));
    std::printf("%s: items=%d caption=%d group=%d gap=%d\n",
                id,
                items,
                captionWidth,
                label.right - label.left,
                close.left - label.right);
  }
} // namespace

void testToolboxRowSpacing(const char *mode)
{
  const bool ribbon = std::strcmp(mode, "spacing-ribbon") == 0;
  const bool plain = std::strcmp(mode, "spacing-column") == 0;
  const bool aligned = std::strcmp(mode, "spacing-aligned") == 0;
  ToolboxWindow window;
  ToolboxScenePlatformController controller(&window);
  if (std::strcmp(mode, "spacing-text-natural") == 0)
  {
    TextNaturalPins(controller);
    return;
  }
  Scene scene((Boundary<SpacingNode>(SpacingProps())));
  scene.mount(&controller);
  typedef loka::dsl::testing::SceneTestAccess Access;
  Access::updateAttached(scene, true);
  SpacingTraversal traversal(controller);
  LayoutState state;
  state.x = 10;
  state.y = 20;
  state.width = 240;
  state.height = 100;
  state.lineHeight = 16;
  state.spacing = 6;
  if (std::strcmp(mode, "spacing-group") == 0 || std::strcmp(mode, "spacing-caption-wins") == 0)
  {
    GroupNaturalPins(scene, controller, traversal, state, std::strcmp(mode, "spacing-caption-wins") == 0);
    Access::unmount(scene);
    return;
  }
  traversal.layoutChild(SpacingLookup(scene, ribbon ? "ribbon" : plain ? "plain" : aligned ? "aligned" : "unaligned"), state);
  const Rect a = traversal.rect(0), b = traversal.rect(1);
  if (ribbon)
  {
    std::printf("Ribbon Toolbox widths: %d, %d; gap: %d\n", a.right - a.left, b.right - b.left, b.left - a.right);
    std::fflush(stdout);
    const short natural = controller.measurePushButtonNaturalWidth(loka::core::String::Literal("New"));
    LOKA_VERIFY(a.right - a.left == natural);
    // Standalone natural layout uses the same formula as the controller answer.
    loka::dsl::FlowError error;
    ButtonNode *button = 0;
    LOKA_VERIFY(loka::dsl::testing::ResolveSelector(&scene,
        loka::dsl::testing::WithinAnchor("ribbon").descendant<ButtonNode>(1), button, error)
        == loka::dsl::FLOW_STEP_SUCCEEDED);
    short first = 0, second = 0;
    LOKA_VERIFY(controller.queryNaturalWidth(button, first));
    LOKA_VERIFY(controller.queryNaturalWidth(button, second));
    LOKA_VERIFY(first == natural && second == natural);
    LayoutState standalone = state;
    standalone.width = 0;
    LOKA_VERIFY(button->context->layout(&controller, standalone) == natural);
    // Without a measurement port the answer declines, so the Row keeps a
    // shared seat instead of an inset-only fixed one.
    ToolboxScenePlatformController portless(0);
    short refused = 0;
    LOKA_VERIFY(!portless.queryNaturalWidth(button, refused));
    LOKA_VERIFY(b.right - b.left == 120);
    LOKA_VERIFY(b.left > a.left);
    LOKA_VERIFY(b.left - a.right == state.spacing);
    Access::unmount(scene);
    return;
  }
  const Rect next = traversal.rect(plain ? 1 : 2);
  const short bottom = plain ? a.bottom : (a.bottom > b.bottom ? a.bottom : b.bottom);
  const short gap = static_cast<short>(next.top - bottom);
  std::printf("%s: A=(%d,%d,%d,%d) B=(%d,%d,%d,%d) next-top=%d gap=%d\n", mode,
              a.left, a.top, a.right, a.bottom, b.left, b.top, b.right, b.bottom, next.top, gap);
  std::fflush(stdout);
  LOKA_VERIFY(a.top == 20 && a.bottom == 40);
  LOKA_VERIFY(b.bottom - b.top == 20);
  if (!plain)
  {
    LOKA_VERIFY(b.top == a.top && b.bottom == a.bottom);
    LOKA_VERIFY(b.left - a.right == 6);
  }
  LOKA_VERIFY(gap == 6);
  Access::unmount(scene);
}
