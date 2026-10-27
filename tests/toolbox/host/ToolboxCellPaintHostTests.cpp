#include "ToolboxPaintDrawerType.hpp"
#include "app/nodes/controls/Ribbon.hpp"
#include "context/ToolboxCellContext.hpp"
#include "context/ToolboxPaintSupport.hpp"
#include "ToolboxGround.hpp"
#include "app/nodes/AttributedText.hpp"
#include "app/nodes/Text.hpp"
#include "app/nodes/controls/TextEditor.hpp"
#include "app/nodes/boundary/StdComposition.hpp"
#include "app/nodes/nestable/RowColumn.hpp"
#include "app/scene/projection/CollectPaintAnswers.hpp"
#include "testing/scene/SceneTestFlow.hpp"
#include "debug/ToolboxSceneDebugStats.hpp"
#include "support/TestVerify.hpp"
#include "support/LifecycleFactTestAccess.hpp"
#include "core/util/StateTrackerGuard.hpp"
#include <cstdio>

namespace
{
#include "ToolboxPaintAnswers.hpp"
  using namespace loka::app;
  using namespace loka::app::scene;
  using namespace loka::core;
  class CellPaintNode;
  typedef BoundaryPropsFor<CellPaintNode> CellPaintProps;
  class CellPaintNode : public StdCompositionBoundaryNodeBase<CellPaintProps>
  {
  public:
    typedef CellPaintProps::TypeTag TypeTag;
    explicit CellPaintNode(const CellPaintProps &p) : StdCompositionBoundaryNodeBase<CellPaintProps>(p)
    { this->state(this->text, String::Literal("A")); }
    virtual void composeNode(NodeComposition &c)
    {
      c.declare(Row() << Cell(this->text.state()).TEST_ID("changed") << Cell("sibling").TEST_ID("sibling"));
    }
    NodeState<String> text;
  };
  LayoutState Seat(short width, short x = 10)
  {
    LayoutState s;
    s.x = x; s.y = 20; s.width = width; s.height = 24; s.spacing = 0;
    return s;
  }

}
int main()
{
  using loka::dsl::testing::SceneTestAccess;
  ToolboxWindow window;
  ToolboxScenePlatformController controller(&window);
  Scene scene((Boundary<CellPaintNode>(CellPaintProps())));
  scene.mount(&controller);
  SceneTestAccess::updateAttached(scene, true);
  Node *first = 0, *second = 0;
  loka::dsl::FlowError error;
  LOKA_VERIFY(loka::dsl::testing::LookupNodeById<Node>(&scene, "changed", first, error));
  LOKA_VERIFY(loka::dsl::testing::LookupNodeById<Node>(&scene, "sibling", second, error));
  CellNode &node = *first->asCellNode();
  ToolboxCellContext &cell = *new ToolboxCellContext(&node, &controller);
  ToolboxCellContext &sibling = *new ToolboxCellContext(second->asCellNode(), &controller);
  node.setContext(&cell);
  second->setContext(&sibling);
  {
    LayoutState plain = Seat(40), spaced = plain;
    plain.height = spaced.height = 0;
    spaced.spacing = 9;
    const short width = cell.layout(&controller, plain);
    LOKA_VERIFY(cell.layout(&controller, spaced) == width);
    LOKA_VERIFY(spaced.y == plain.y && spaced.y > 20);
  }
  toolbox_host::reset();
  LayoutState seat = Seat(40), other = Seat(40, 70);
  cell.layout(&controller, seat); sibling.layout(&controller, other);
  const PaintQuery query = {ToolboxPaintScope(), PLACEMENT_ELIGIBLE};
  LOKA_VERIFY(cell.queryPaintDamage(query).kind == PAINT_ANSWER_REFUSED);
  SetPort(&window.port);
  window.port.fgColor = redColor;
  cell.draw(&controller); sibling.draw(&controller);
  LOKA_VERIFY(toolbox_host::draws.size() == 2);
  LOKA_VERIFY(toolbox_host::draws[0].ink == blackColor);
  LOKA_VERIFY(toolbox_host::draws[1].ink == blackColor);
  LOKA_VERIFY(window.port.fgColor == redColor);
  PaintQuery unsettled = query;
  unsettled.placement = PLACEMENT_PENDING;
  LOKA_VERIFY(cell.queryPaintDamage(unsettled).kind == PAINT_ANSWER_REFUSED);
  unsettled = query; ++unsettled.scope.ownerKey;
  LOKA_VERIFY(cell.queryPaintDamage(unsettled).kind == PAINT_ANSWER_REFUSED);
  MutableState<String> replacement(String::Literal("replacement"));
  cell.updateData(&replacement);
  LOKA_VERIFY(cell.queryPaintDamage(query).kind == PAINT_ANSWER_REFUSED);
  cell.onPropsApplied();
  {
    StateTrackerGuard guard(SceneTestAccess::rootBoundary(scene)->tracker());
    static_cast<CellPaintNode *>(SceneTestAccess::rootBoundary(scene))->text.set(String::Literal("longer"));
  }
  ToolboxSceneDebugStats stats;
  ToolboxPaintAnswerSource source(stats);
  PaintAnswerBuffer<> answers;
  BoundaryLocalApplyInfo info; info.paintKind = LOCAL_APPLY_PAINT_GENERIC;
  const PaintApplyVerdict verdict = CollectPaintAnswers(*SceneTestAccess::rootBoundary(scene), query, answers, source);
  std::printf("cell collector: exact=%u refused=%u rects=%u\n", verdict.exactCount(), verdict.refusedCount(), answers.count());
  std::fflush(stdout);
  LOKA_VERIFY(verdict.canSkipBroadPaint(info));
  LOKA_VERIFY(answers.count() == 1);
  const PaintDamage &damage = answers.entry(0).damage;
  LOKA_VERIFY(damage.x == 10 && damage.y == 20 && damage.width == 40 && damage.height == 24);
  const std::size_t drawsBefore = toolbox_host::draws.size();
  const int erasesBefore = toolbox_host::erases;
  toolbox_host::backgroundColors.clear();
  cell.draw(&controller);
  LOKA_VERIFY(toolbox_host::draws.size() == drawsBefore + 1);
  LOKA_VERIFY(toolbox_host::draws.back().bytes == "longer");
  LOKA_VERIFY(toolbox_host::erases == erasesBefore + 1);
  // The Cell paints its control ground, then restores the window ground
  // (#1199 B2). Recording the calls discriminates even an all-white table.
  loka::toolbox::ToolboxPlanarColor controlGround, windowGround;
  LOKA_VERIFY(loka::toolbox::QueryToolboxGroundColor(SURFACE_GROUND_CONTROL, controlGround));
  LOKA_VERIFY(loka::toolbox::QueryToolboxGroundColor(SURFACE_GROUND_WINDOW, windowGround));
  LOKA_VERIFY(toolbox_host::backgroundColors.size() == 2);
  LOKA_VERIFY(toolbox_host::backgroundColors[0] == static_cast<long>(controlGround));
  LOKA_VERIFY(toolbox_host::backgroundColors[1] == static_cast<long>(windowGround));
  LOKA_VERIFY(cell.queryPaintDamage(query).damage.width == 0);
  // Same geometry in a clipped full render must not revoke disjoint history (#763).
  Rect disjoint = {100, 100, 120, 120};
  {
    ToolboxPaintClip clip(disjoint);
    seat = Seat(40); cell.layout(&controller, seat); cell.render(&controller);
  }
  LOKA_VERIFY(cell.queryPaintDamage(query).kind == PAINT_ANSWER_EXACT);
  Rect partial = {20, 10, 30, 20};
  { ToolboxPaintClip clip(partial); cell.draw(&controller); }
  const PaintAnswer unchanged = cell.queryPaintDamage(query);
  std::printf("unchanged partial Cell: kind=%d reason=%d\n", unchanged.kind, unchanged.reason);
  std::fflush(stdout);
  LOKA_VERIFY(unchanged.kind == PAINT_ANSWER_EXACT);
  LOKA_VERIFY(unchanged.damage.width == 0 && unchanged.damage.height == 0);
  {
    StateTrackerGuard guard(SceneTestAccess::rootBoundary(scene)->tracker());
    static_cast<CellPaintNode *>(SceneTestAccess::rootBoundary(scene))->text.set(String::Literal("changed-value"));
  }
  { ToolboxPaintClip clip(partial); cell.draw(&controller); }
  const PaintAnswer changed = cell.queryPaintDamage(query);
  LOKA_VERIFY(changed.kind == PAINT_ANSWER_REFUSED && changed.reason == PAINT_REFUSED_HISTORY_UNKNOWN);
  cell.draw(&controller);
  LOKA_VERIFY(cell.queryPaintDamage(query).kind == PAINT_ANSWER_EXACT);
  LOKA_VERIFY(cell.queryPaintDamage(query).damage.width == 0);
  toolbox_host::failRegions = 2;
  cell.draw(&controller);
  LOKA_VERIFY(cell.queryPaintDamage(query).kind == PAINT_ANSWER_REFUSED);
  toolbox_host::failRegions = 0;
  cell.draw(&controller);
  seat = Seat(0); cell.layout(&controller, seat); cell.draw(&controller);
  {
    StateTrackerGuard guard(SceneTestAccess::rootBoundary(scene)->tracker());
    static_cast<CellPaintNode *>(SceneTestAccess::rootBoundary(scene))->text.set(String::Literal("X"));
  }
  LOKA_VERIFY(cell.queryPaintDamage(query).kind == PAINT_ANSWER_REFUSED);
  seat = Seat(40, 12); cell.layout(&controller, seat);
  LOKA_VERIFY(cell.queryPaintDamage(query).kind == PAINT_ANSWER_REFUSED);
  cell.draw(&controller);
  // A changed projection clip invalidates even when layout geometry is stable.
  controller.projectionClip.right = 25;
  seat = Seat(40, 12); cell.layout(&controller, seat);
  LOKA_VERIFY(cell.queryPaintDamage(query).kind == PAINT_ANSWER_REFUSED);
  cell.draw(&controller);
  LOKA_VERIFY(cell.queryPaintDamage(query).kind == PAINT_ANSWER_EXACT);
  {
    StateTrackerGuard guard(SceneTestAccess::rootBoundary(scene)->tracker());
    static_cast<CellPaintNode *>(SceneTestAccess::rootBoundary(scene))->text.set(String());
  }
  LOKA_VERIFY(cell.queryPaintDamage(query).damage.width == 13);
  cell.draw(&controller);
  LOKA_VERIFY(cell.queryPaintDamage(query).kind == PAINT_ANSWER_EXACT);
  LOKA_VERIFY(cell.queryPaintDamage(query).damage.width == 0);
  // A fully clipped fixed Cell needs no presented fact.
  controller.projectionClip.right = 5;
  seat = Seat(40, 12); cell.layout(&controller, seat);
  LOKA_VERIFY(cell.queryPaintDamage(query).kind == PAINT_ANSWER_EXACT);
  LOKA_VERIFY(cell.queryPaintDamage(query).damage.width == 0);
  controller.projectionClip.right = 30000;
  seat = Seat(40, 12); cell.layout(&controller, seat); cell.draw(&controller);
  NotifySubtreeNodeDetached(&node);
  LifecycleFactTestAccess::DeliverFacts(&node);
  LOKA_VERIFY(cell.queryPaintDamage(query).kind == PAINT_ANSWER_REFUSED);
  SceneTestAccess::unmount(scene);
  std::puts("Cell paint pins passed");
}
