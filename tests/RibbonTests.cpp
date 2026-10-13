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
  LOKA_VERIFY(frame != 0 && frame->childrenCount() == 1);
  LOKA_VERIFY(frame->kind() == NODE_KIND_RIBBON_GROUP);
  StackNode *group = frame->childrenHead()->asStackNode();
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
  layout::RowWidthConsultation measured(frame, 1, 640, gap, ROW_UNDECLARED_WIDTH_NATURAL, &platform);
  LOKA_VERIFY(measured.next(frame).width() == 40 + gap + 48 + 2 * p);
  const LayoutState after = static_cast<RibbonGeometryContext *>(ribbonButton(scene, "Groups", 3)->getContext())->rect;
  LOKA_VERIFY(after.x - first.x - gap == 40 + gap + 48 + RibbonGroupNode::kFrameInset);
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
    RibbonChildSeatTraversal()
        : child(0),
          seat()
    {
    }
    virtual int layoutChild(Node *value, const LayoutState &state)
    {
      LOKA_VERIFY(this->child == 0);
      this->child = value;
      this->seat = state;
      return state.y + 37;
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
  LOKA_VERIFY(group && group->childrenCount() == 1);
  layout::RowWidthConsultation declined(group, 1, 311, 7, ROW_UNDECLARED_WIDTH_NATURAL, &platform);
  LOKA_VERIFY(declined.next(group).width() == 311);

  LayoutState offer;
  offer.x = 19;
  offer.y = 23;
  offer.width = 211;
  offer.height = 137;
  offer.lineHeight = 20;
  const int p = RibbonGroupNode::kFrameInset;
  PlatformLayoutHandlerRegistry registry;
  layout::RegisterBuiltinPlatformLayoutHandlers(registry, 0, 0);
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
  const int groupBottom = platform.projectLayoutForTesting(group, offer);
  const int childBottom = platform.projectLayoutForTesting(group->childrenHead(), traversal.seat);
  LOKA_VERIFY(groupBottom == childBottom + p);
  SceneTestAccess::unmount(scene);
}
