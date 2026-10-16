#include "RibbonTests.hpp"
#include "app/layout/LayoutHeuristics.hpp"
#include "app/layout/PlatformBuiltinLayoutHandlers.hpp"
#include "app/nodes/controls/Ribbon.hpp"
#include "dsl/composition/CompositionList.hpp"
#include "platform/null/NullScenePlatformController.hpp"
#include "testing/scene/SceneTestFlow.hpp"
#include "support/TestVerify.hpp"
#include <cstdio>

namespace
{
  using namespace loka::app;
  using namespace loka::app::scene;
  using namespace loka::dsl;
  using namespace loka::dsl::testing;

  class RibbonGeometryContext : public NodeContext
  {
  public:
    LayoutState rect;
    virtual short layout(IPlatformController *, LayoutState &state)
    {
      this->rect = state;
      return state.width;
    }
  };

  class RibbonGeometryHandler : public IPlatformNodeHandler
  {
  public:
    virtual const void *nodeTypeKey() const
    {
      return NodeTypeToken<ButtonNode>();
    }
    virtual NodeContext *ensureContext(Node *node, IPlatformController *, const LayoutState &)
    {
      if (!node->getContext())
        node->setContext(new RibbonGeometryContext());
      return node->getContext();
    }
  };

  class RibbonEdgeGeometryHandler : public IPlatformNodeHandler
  {
  public:
    virtual const void *nodeTypeKey() const
    {
      return NodeTypeToken<RibbonGroupEdgeNode>();
    }
    virtual NodeContext *ensureContext(Node *node, IPlatformController *, const LayoutState &)
    {
      if (!node->getContext())
        node->setContext(new RibbonGeometryContext());
      return node->getContext();
    }
  };

  void countRibbonClick(void *data)
  {
    ++*static_cast<int *>(data);
  }

  ButtonNode *ribbonButton(Scene &scene, const char *anchor, int index)
  {
    ButtonNode *node = 0;
    FlowError error;
    LOKA_VERIFY(ResolveSelector(&scene, WithinAnchor(anchor).descendant<ButtonNode>(index + 1), node, error)
                == FLOW_STEP_SUCCEEDED);
    LOKA_VERIFY(node != 0);
    return node;
  }
} // namespace

void testRibbonFixedSeatsAndClicks()
{
  RibbonGeometryHandler handler;
  NullScenePlatformController platform;
  LOKA_VERIFY(platform.registerNodeHandler(&handler));
  loka::core::EmitterState events[4];
  int clicks[4] = {0, 0, 0, 0};
  for (int i = 0; i < 4; ++i)
    events[i].bind(&countRibbonClick, &clicks[i], false);
  NodeDefinitionBase *definition =
      (Column() << (RibbonControl().testId("RibbonUnderTest")
                    << RibbonItem("New").onClick(&events[0]) << RibbonItem("Open...").onClick(&events[1])
                    << RibbonItem("Save").onClick(&events[2])
                    << RibbonItem("Save As...").onClick(&events[3]).width(120))
                << (Row().testId("GapMetric")
                    << (Box().size(10, 0) << Button("A")) << (Box().size(10, 0) << Button("B")))
                << (Row().testId("OrdinaryRow") << Button("A") << Button("B")))
          .clone();
  LOKA_VERIFY(definition != 0);
  Scene scene(definition);
  LOKA_VERIFY(scene.mount(&platform));
  SceneTestAccess::updateAttached(scene, true);
  short unused = 0;
  LOKA_VERIFY(!platform.queryNaturalWidth(SceneTestAccess::rootNode(scene), unused));
  // As in the responsive layout fixtures, record the projected Button seats.
  RibbonGeometryContext *rects[8];
  for (int i = 0; i < 8; ++i)
  {
    ButtonNode *node = ribbonButton(scene, i < 4 ? "RibbonUnderTest" : i < 6 ? "GapMetric" : "OrdinaryRow",
                                    i < 4 ? i : i < 6 ? i - 4 : i - 6);
    rects[i] = static_cast<RibbonGeometryContext *>(node->getContext());
    LOKA_VERIFY(rects[i] != 0);
  }
  LayoutState viewport;
  viewport.width = 640;
  viewport.height = 160;
  viewport.lineHeight = 20;
  platform.projectLayoutForTesting(SceneTestAccess::rootNode(scene), viewport);
  // The Null rail keeps its Row metric private. A normal Row with known fixed
  // seats measures that same metric without duplicating its numeric value.
  const int gap = rects[5]->rect.x - (rects[4]->rect.x + rects[4]->rect.width);
  LOKA_VERIFY(gap > 0);
  LOKA_VERIFY(rects[6]->rect.width == (viewport.width - gap) / 2);
  LOKA_VERIFY(rects[7]->rect.width == rects[6]->rect.width);
  LOKA_VERIFY(rects[7]->rect.x == rects[6]->rect.x + rects[6]->rect.width + gap);
  const short widths[] = {40, 72, 48, 120};
  const char *titles[] = {"New", "Open...", "Save", "Save As..."};
  for (int i = 0; i < 4; ++i)
  {
    const LayoutState &rect = rects[i]->rect;
    std::printf("Ribbon item %d: x=%d width=%d expected=%d gap=%d\n", i, rect.x, rect.width, widths[i], gap);
    std::fflush(stdout);
    LOKA_VERIFY(rect.width == widths[i]);
    if (i < 3)
    {
      short first = 0, second = 0;
      ButtonNode *button = ribbonButton(scene, "RibbonUnderTest", i);
      LOKA_VERIFY(platform.queryNaturalWidth(button, first));
      LOKA_VERIFY(platform.queryNaturalWidth(button, second));
      LOKA_VERIFY(first == widths[i] && second == first);
    }
    if (i)
    {
      LOKA_VERIFY(rect.x > rects[i - 1]->rect.x);
      LOKA_VERIFY(rect.x - (rects[i - 1]->rect.x + rects[i - 1]->rect.width) == gap);
    }
    LOKA_VERIFY(
        ribbonButton(scene, "RibbonUnderTest", i)->props.text_->get().equals(loka::core::String::Literal(titles[i])));
  }
  // The Column offers its full width to the ribbon Row.
  LOKA_VERIFY(rects[3]->rect.x + rects[3]->rect.width < viewport.x + viewport.width);
  for (int i = 0; i < 4; ++i)
  {
    Scene *out = 0;
    FlowError error;
    LOKA_VERIFY(ClickButton(WithinAnchor("RibbonUnderTest").descendant<ButtonNode>(i + 1)).run(&scene, out, error)
                == FLOW_STEP_SUCCEEDED);
    for (int j = 0; j < 4; ++j)
      LOKA_VERIFY(clicks[j] == (j <= i ? 1 : 0));
  }
  SceneTestAccess::unmount(scene);
  for (int i = 0; i < 4; ++i)
    events[i].unbind(&countRibbonClick, &clicks[i]);
}

namespace
{
  class RibbonGroupWidthPlatform : public NullScenePlatformController
  {
  public:
    virtual bool queryNaturalWidth(Node *child, short &width) const
    {
      LOKA_VERIFY(child->asStackNode() == 0);
      LOKA_VERIFY(child->asRibbonGroupEdgeNode() == 0);
      if (child->asTextNode())
      {
        width = 20;
        return true;
      }
      return NullScenePlatformController::queryNaturalWidth(child, width);
    }
  };
} // namespace

void testRibbonGroupExpansionAndGeometry()
{
  RibbonGeometryHandler buttons;
  RibbonGroupWidthPlatform platform;
  RibbonEdgeGeometryHandler edges;
  LOKA_VERIFY(platform.registerNodeHandler(&edges));
  LOKA_VERIFY(platform.registerNodeHandler(&buttons));
  NodeDefinitionBase *definition =
      (RibbonControl().testId("Groups") << RibbonItem("Before")
                                        << (RibbonGroup("File") << RibbonItem("New") << RibbonItem("Save"))
                                        << RibbonItem("After"))
          .clone();
  LOKA_VERIFY(definition != 0);
  Scene scene(definition);
  LOKA_VERIFY(scene.mount(&platform));
  SceneTestAccess::updateAttached(scene, true);
  Node *anchor = 0;
  FlowError error;
  LOKA_VERIFY(LookupNodeById<Node>(&scene, "Groups", anchor, error) == FLOW_STEP_SUCCEEDED);
  LOKA_VERIFY(anchor != 0);
  StackNode *ribbon = anchor->asStackNode();
  LOKA_VERIFY(ribbon != 0);
  LOKA_VERIFY(ribbon->props.effectiveAxis() == STACK_AXIS_ROW);
  LOKA_VERIFY(ribbon->props.rowUndeclaredWidth_ == ROW_UNDECLARED_WIDTH_NATURAL);
  LOKA_VERIFY(ribbon->childrenCount() == 3);
  loka::dsl::CompositionCursor<Node> top(ribbon->childrenHead(), ribbon->childrenCount());
  LOKA_VERIFY(top.next()->asButtonNode() == ribbonButton(scene, "Groups", 0));
  RibbonGroupNode *frame = top.next()->asRibbonGroupNode();
  LOKA_VERIFY(frame != 0 && frame->childrenCount() == 4);
  LOKA_VERIFY(frame->kind() == NODE_KIND_RIBBON_GROUP);
  loka::dsl::CompositionCursor<Node> frameChildren(frame->childrenHead(), frame->childrenCount());
  StackNode *group = frameChildren.next()->asStackNode();
  RibbonGroupEdgeNode *edgeNodes[3];
  const RibbonGroupEdge roles[] = {RIBBON_GROUP_EDGE_LEFT, RIBBON_GROUP_EDGE_RIGHT, RIBBON_GROUP_EDGE_BOTTOM};
  for (int i = 0; i < 3; ++i)
  {
    edgeNodes[i] = frameChildren.next()->asRibbonGroupEdgeNode();
    LOKA_VERIFY(edgeNodes[i] && edgeNodes[i]->props.edge == roles[i]);
    LOKA_VERIFY(edgeNodes[i]->asNestable() == 0);
    LOKA_VERIFY(edgeNodes[i]->kind() == NODE_KIND_RIBBON_GROUP_EDGE);
  }
  LOKA_VERIFY(frameChildren.next() == 0);
  LOKA_VERIFY(group != 0 && group->props.effectiveAxis() == STACK_AXIS_COLUMN);
  LOKA_VERIFY(top.next()->asButtonNode() == ribbonButton(scene, "Groups", 3));
  LOKA_VERIFY(group->childrenCount() == 2);
  loka::dsl::CompositionCursor<Node> parts(group->childrenHead(), group->childrenCount());
  StackNode *items = parts.next()->asStackNode();
  LOKA_VERIFY(items != 0 && items->props.effectiveAxis() == STACK_AXIS_ROW);
  LOKA_VERIFY(items->props.rowUndeclaredWidth_ == ROW_UNDECLARED_WIDTH_NATURAL);
  LOKA_VERIFY(items->childrenCount() == 2);
  TextNode *caption = parts.next()->asTextNode();
  LOKA_VERIFY(caption != 0);
  LOKA_VERIFY(caption->props.text_->get().equals(loka::core::String::Literal("File")));
  LOKA_VERIFY(caption->props.textStyle_ == SmallLabel + Secondary);
  LOKA_VERIFY(caption->props.blockStyle_ == block::Centered);
  const char *titles[] = {"Before", "New", "Save", "After"};
  for (int i = 0; i < 4; ++i)
    LOKA_VERIFY(ribbonButton(scene, "Groups", i)->props.text_->get().equals(loka::core::String::Literal(titles[i])));
  LayoutState viewport;
  viewport.width = 640;
  viewport.height = 160;
  viewport.lineHeight = 20;
  platform.projectLayoutForTesting(SceneTestAccess::rootNode(scene), viewport);
  const LayoutState first = static_cast<RibbonGeometryContext *>(ribbonButton(scene, "Groups", 1)->getContext())->rect;
  const LayoutState second = static_cast<RibbonGeometryContext *>(ribbonButton(scene, "Groups", 2)->getContext())->rect;
  const int gap = second.x - first.x - first.width;
  LOKA_VERIFY(gap > 0);
  LOKA_VERIFY(first.width == 40 && second.width == 48);
  const int p = RibbonGroupNode::kFrameInset;
  const LayoutState before = static_cast<RibbonGeometryContext *>(ribbonButton(scene, "Groups", 0)->getContext())->rect;
  LOKA_VERIFY(first.x == before.x + before.width + gap + p);
  LOKA_VERIFY(first.y == viewport.y + p);
  LOKA_VERIFY(layout::preferredChildWidthForRow(frame) == -1);
  LOKA_VERIFY(layout::declaredChildWidthForColumn(frame) == -1);
  layout::RowWidthConsultation measured(frame, 1, 640, gap, gap, ROW_UNDECLARED_WIDTH_NATURAL, &platform);
  LOKA_VERIFY(measured.next(frame).width() == 40 + gap + 48 + 2 * p);
  const LayoutState after = static_cast<RibbonGeometryContext *>(ribbonButton(scene, "Groups", 3)->getContext())->rect;
  LOKA_VERIFY(after.x - first.x - gap == 40 + gap + 48 + RibbonGroupNode::kFrameInset);
  LayoutState strip[3];
  for (int i = 0; i < 3; ++i)
  {
    LOKA_VERIFY(edgeNodes[i]->getContext() != 0);
    strip[i] = static_cast<RibbonGeometryContext *>(edgeNodes[i]->getContext())->rect;
  }
  LayoutState contentSeat = viewport;
  contentSeat.x = first.x;
  contentSeat.y = first.y;
  contentSeat.width = static_cast<short>(40 + gap + 48);
  contentSeat.height = static_cast<short>(viewport.height - 2 * p);
  const int contentBottom = platform.projectLayoutForTesting(group, contentSeat);
  const int gx = first.x - p;
  const int gy = viewport.y;
  const int gw = contentSeat.width + 2 * p;
  const int gh = contentBottom + p - gy;
  LOKA_VERIFY(strip[0].x == gx && strip[0].y == gy);
  LOKA_VERIFY(strip[0].width == 1 && strip[0].height == gh - 1);
  LOKA_VERIFY(strip[1].x == gx + gw - 1 && strip[1].y == gy);
  LOKA_VERIFY(strip[1].width == 1 && strip[1].height == gh - 1);
  LOKA_VERIFY(strip[2].x == gx && strip[2].y == gy + gh - 1);
  LOKA_VERIFY(strip[2].width == gw && strip[2].height == 1);
  SceneTestAccess::unmount(scene);
}

namespace
{
  class RibbonTextDecliningPlatform : public NullScenePlatformController
  {
  public:
    virtual bool queryNaturalWidth(Node *child, short &width) const
    {
      if (child->asTextNode())
        return false;
      return NullScenePlatformController::queryNaturalWidth(child, width);
    }
  };

  class RibbonChildSeatTraversal : public IPlatformLayoutTraversal
  {
  public:
    Node *child;
    LayoutState seat;
    LayoutState edgeSeats[3];
    const int contentAdvance;
    explicit RibbonChildSeatTraversal(int advance = 37)
        : child(0),
          seat(),
          contentAdvance(advance)
    {
    }
    virtual int layoutChild(Node *value, const LayoutState &state)
    {
      if (RibbonGroupEdgeNode *edge = value->asRibbonGroupEdgeNode())
      {
        this->edgeSeats[edge->props.edge] = state;
        return state.y + 999; // Decorations cannot advance the group.
      }
      LOKA_VERIFY(this->child == 0);
      this->child = value;
      this->seat = state;
      return state.y + this->contentAdvance;
    }
    virtual void setLayoutResultY(short)
    {
      LOKA_VERIFY(false);
    }
    virtual short layoutResultY() const
    {
      return 0;
    }
  };
} // namespace

namespace
{
  int recordRibbonChild(void *context, Node *node, const LayoutState &state)
  {
    return static_cast<RibbonChildSeatTraversal *>(context)->layoutChild(node, state);
  }
  class RibbonConstantWidth : public layout::INaturalWidthSource
  {
  public:
    explicit RibbonConstantWidth(short width)
        : width_(width)
    {
    }
    virtual bool queryNaturalWidth(Node *, short &width) const
    {
      width = this->width_;
      return true;
    }

  private:
    const short width_;
  };
  bool rejectRibbonY(void *, int)
  {
    return false;
  }
} // namespace

void testRibbonGroupInsetAndDecliningNaturalWidth()
{
  RibbonTextDecliningPlatform platform;
  NodeDefinitionBase *definition =
      (RibbonControl().testId("InsetGroup") << (RibbonGroup("Caption") << RibbonItem("New"))).clone();
  LOKA_VERIFY(definition != 0);
  Scene scene(definition);
  LOKA_VERIFY(scene.mount(&platform));
  SceneTestAccess::updateAttached(scene, true);
  Node *anchor = 0;
  FlowError error;
  LOKA_VERIFY(LookupNodeById<Node>(&scene, "InsetGroup", anchor, error) == FLOW_STEP_SUCCEEDED);
  LOKA_VERIFY(anchor && anchor->asStackNode());
  RibbonGroupNode *group = anchor->asStackNode()->childrenHead()->asRibbonGroupNode();
  LOKA_VERIFY(group && group->childrenCount() == 4);
  layout::RowWidthConsultation declined(group, 1, 311, 7, 7, ROW_UNDECLARED_WIDTH_NATURAL, &platform);
  LOKA_VERIFY(declined.next(group).width() == 311);

  RibbonConstantWidth natural(100);
  layout::RowWidthConsultation measured(group, 1, 311, 0, 0, ROW_UNDECLARED_WIDTH_NATURAL, &natural);
  LOKA_VERIFY(measured.next(group).width() == 108);
  short overflow = -1;
  LOKA_VERIFY(layout::queryContentOverflowWidth(anchor, 0, &natural, overflow));
  LOKA_VERIFY(overflow == 108);
  RibbonConstantWidth excessive(32767);
  layout::RowWidthConsultation tooWide(group, 1, 311, 0, 0, ROW_UNDECLARED_WIDTH_NATURAL, &excessive);
  LOKA_VERIFY(tooWide.next(group).width() == 311);
  overflow = -1;
  LOKA_VERIFY(!layout::queryContentOverflowWidth(anchor, 0, &excessive, overflow));
  LOKA_VERIFY(overflow == -1);

  LayoutState offer;
  offer.x = 19;
  offer.y = 23;
  offer.width = 211;
  offer.height = 137;
  offer.lineHeight = 20;
  const int p = RibbonGroupNode::kFrameInset;
  PlatformLayoutHandlerRegistry registry;
  layout::RegisterBuiltinPlatformLayoutHandlers(registry, 0, 0, 0);
  IPlatformLayoutHandler *handler = registry.find(group);
  LOKA_VERIFY(handler != 0);
  RibbonChildSeatTraversal traversal;
  const int bottom = handler->layoutNode(group, offer, &traversal);
  LOKA_VERIFY(traversal.child == group->childrenHead());
  LOKA_VERIFY(traversal.seat.x == offer.x + p);
  LOKA_VERIFY(traversal.seat.y == offer.y + p);
  LOKA_VERIFY(traversal.seat.width == offer.width - 2 * p);
  LOKA_VERIFY(traversal.seat.height == offer.height - 2 * p);
  LOKA_VERIFY(bottom == traversal.seat.y + 37 + p);
  PlatformLayoutHandlerRegistry noHandlers;
  LOKA_VERIFY(noHandlers.find(group) == 0);
  RibbonChildSeatTraversal fallback;
  LOKA_VERIFY(layout::computeFramedLayoutResultY(group, offer, &fallback, &recordRibbonChild, 0) == bottom);
  for (int edge = 0; edge < 3; ++edge)
  {
    LOKA_VERIFY(fallback.edgeSeats[edge].x == traversal.edgeSeats[edge].x);
    LOKA_VERIFY(fallback.edgeSeats[edge].y == traversal.edgeSeats[edge].y);
    LOKA_VERIFY(fallback.edgeSeats[edge].width == traversal.edgeSeats[edge].width);
    LOKA_VERIFY(fallback.edgeSeats[edge].height == traversal.edgeSeats[edge].height);
  }
  const int groupBottom = platform.projectLayoutForTesting(group, offer);
  const int childBottom = platform.projectLayoutForTesting(group->childrenHead(), traversal.seat);
  LOKA_VERIFY(groupBottom == childBottom + p);
  loka::dsl::CompositionCursor<Node> noDraw(group->childrenHead(), group->childrenCount());
  noDraw.next();
  for (Node *edge = noDraw.next(); edge; edge = noDraw.next())
    LOKA_VERIFY(edge->getContext() == 0);
  // Strip width follows the offered seat, not the much smaller natural content width.
  LOKA_VERIFY(traversal.edgeSeats[RIBBON_GROUP_EDGE_BOTTOM].width == offer.width);
  for (int sample = 0; sample < 3; ++sample)
  {
    LayoutState small = offer;
    if (sample == 0)
      small.width = 1;
    RibbonChildSeatTraversal tiny(sample == 0 ? 37 : sample == 1 ? 1 - 2 * p : -20);
    const int tinyBottom = handler->layoutNode(group, small, &tiny);
    LOKA_VERIFY(tinyBottom == small.y + 2 * p + tiny.contentAdvance);
    for (int i = 0; i < 3; ++i)
    {
      LOKA_VERIFY(tiny.edgeSeats[i].x == small.x && tiny.edgeSeats[i].y == small.y);
      LOKA_VERIFY(tiny.edgeSeats[i].width == 0 && tiny.edgeSeats[i].height == 0);
    }
  }
  RibbonChildSeatTraversal rejected;
  const int rejectedY = layout::computeFramedLayoutResultY(group, offer, &rejected, &recordRibbonChild, &rejectRibbonY);
  LOKA_VERIFY(rejectedY == offer.y + p);
  LOKA_VERIFY(rejected.child == 0);
  LOKA_VERIFY(rejected.edgeSeats[0].x == offer.x);
  LOKA_VERIFY(rejected.edgeSeats[0].height == p - 1);
  LOKA_VERIFY(rejected.edgeSeats[1].x == offer.x + offer.width - 1);
  LOKA_VERIFY(rejected.edgeSeats[2].y == rejectedY - 1);
  LOKA_VERIFY(rejected.edgeSeats[2].width == offer.width);
  RibbonGroupProps emptyProps;
  RibbonGroupNode empty(emptyProps);
  RibbonChildSeatTraversal emptyTraversal;
  LOKA_VERIFY(layout::computeFramedLayoutResultY(&empty, offer, &emptyTraversal, &recordRibbonChild, 0)
              == offer.y + 2 * p);
  LOKA_VERIFY(emptyTraversal.child == 0);
  SceneTestAccess::unmount(scene);
}

namespace
{
  /** Exercises the capability independently of the legacy RibbonGroup query. */
  class CapabilityFrame : public RibbonGroupNode
  {
  public:
    CapabilityFrame()
        : RibbonGroupNode(RibbonGroupProps())
    {
    }
    virtual RibbonGroupNode *asRibbonGroupNode()
    {
      return 0;
    }
    virtual const void *nodeTypeKey() const
    {
      return NodeTypeToken<CapabilityFrame>();
    }
    virtual layout::FrameGeometry frameGeometry() const
    {
      return layout::FrameGeometry(layout::FRAME_PLACEMENT_RIBBON_GROUP_EDGES, 6);
    }
  };
} // namespace

void testFramedCapabilityLayoutAndMeasurement()
{
  CapabilityFrame frame;
  frame.addChild(new ButtonNode(ButtonProps().text("content")));
  frame.addChild(new RibbonGroupEdgeNode(RibbonGroupEdgeProps(RIBBON_GROUP_EDGE_BOTTOM)));
  LayoutState offer;
  offer.x = 10;
  offer.y = 20;
  offer.width = 100;
  offer.height = 80;
  PlatformLayoutHandlerRegistry registry;
  LOKA_VERIFY(registry.registerHandler(new layout::FramedPlatformLayoutHandler<CapabilityFrame>()));
  IPlatformLayoutHandler *handler = registry.find(&frame);
  LOKA_VERIFY(handler != 0);
  RibbonChildSeatTraversal traversal;
  LOKA_VERIFY(handler->layoutNode(&frame, offer, &traversal) == 69);
  LOKA_VERIFY(traversal.seat.x == 16 && traversal.seat.y == 26);
  LOKA_VERIFY(traversal.seat.width == 88 && traversal.seat.height == 68);
  LOKA_VERIFY(traversal.edgeSeats[RIBBON_GROUP_EDGE_BOTTOM].y == 68);
  LOKA_VERIFY(traversal.edgeSeats[RIBBON_GROUP_EDGE_BOTTOM].width == 100);
  RibbonConstantWidth natural(100);
  layout::RowWidthConsultation measured(&frame, 1, 311, 0, 0, ROW_UNDECLARED_WIDTH_NATURAL, &natural);
  LOKA_VERIFY(measured.next(&frame).width() == 112);
  // Stack owns the frame below, so use a distinct heap resident for overflow.
  StackNode parent((StackProps(STACK_AXIS_COLUMN)));
  CapabilityFrame *overflowFrame = new CapabilityFrame();
  overflowFrame->addChild(new ButtonNode(ButtonProps().text("content")));
  parent.addChild(overflowFrame);
  short overflow = 0;
  LOKA_VERIFY(layout::queryContentOverflowWidth(&parent, 0, &natural, overflow));
  LOKA_VERIFY(overflow == 112);
}
