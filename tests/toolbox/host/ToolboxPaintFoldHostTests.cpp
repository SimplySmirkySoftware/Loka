#include "app/nodes/controls/Card.hpp"
#include "ToolboxBuiltInSupport.hpp"
#include "ToolboxPaintGradient.hpp"
#include "ToolboxDirtyReplay.hpp"
#include "context/ToolboxRibbonGroupEdgeContext.hpp"
#include "app/nodes/controls/Ribbon.hpp"
#include "context/ToolboxCellContext.hpp"
#include "context/ToolboxPaintSupport.hpp"
#include "app/nodes/AttributedText.hpp"
#include "app/nodes/Text.hpp"
#include "app/nodes/controls/TextEditor.hpp"
#include "app/nodes/boundary/StdComposition.hpp"
#include "app/nodes/nestable/RowColumn.hpp"
#include "app/scene/projection/CollectPaintAnswers.hpp"
#include "testing/scene/SceneTestFlow.hpp"
#include "debug/ToolboxSceneDebugStats.hpp"
#include "support/TestVerify.hpp"
#include "core/util/StateTrackerGuard.hpp"
#include <cstdio>

namespace
{
#include "ToolboxPaintAnswers.hpp"
  using namespace loka::app;
  using namespace loka::app::scene;
  using namespace loka::core;
  enum
  {
    kBoardCells = 9
  };
  const char *const kIds[kBoardCells] = {"c0", "c1", "c2", "c3", "c4", "c5", "c6", "c7", "c8"};
  // The fold must not depend on whether the status is enumerated before or after the board.
  bool gStatusLast = false;
  class FoldNode;
  typedef BoundaryPropsFor<FoldNode> FoldProps;
  /** MINES.JS in miniature: one Boundary owns a status and a board whose cells
      all change in one apply (a flood fill). */
  class FoldNode : public StdCompositionBoundaryNodeBase<FoldProps>
  {
  public:
    typedef FoldProps::TypeTag TypeTag;
    explicit FoldNode(const FoldProps &p) : StdCompositionBoundaryNodeBase<FoldProps>(p)
    {
      this->state(this->status, String::Literal("Mines left: 10"));
      for (int i = 0; i < kBoardCells; ++i)
        this->state(this->cells[i], String::Literal("."));
    }
    virtual void composeNode(NodeComposition &c)
    {
      Row board;
      for (int i = 0; i < kBoardCells; ++i)
        board << Cell(this->cells[i].state()).TEST_ID(kIds[i]);
      if (gStatusLast)
        c.declare(Column() << NodeDefinition<RibbonGroupEdgeProps, RibbonGroupEdgeNode>(
                      RibbonGroupEdgeProps(RIBBON_GROUP_EDGE_LEFT))
                           << (board << Cell("unchanged").TEST_ID("neighbour")).TEST_ID("board")
                           << Cell(this->status.state()).TEST_ID("status"));
      else
        c.declare(Column() << NodeDefinition<RibbonGroupEdgeProps, RibbonGroupEdgeNode>(
                      RibbonGroupEdgeProps(RIBBON_GROUP_EDGE_LEFT))
                           << Cell(this->status.state()).TEST_ID("status")
                           << (board << Cell("unchanged").TEST_ID("neighbour")).TEST_ID("board"));
    }
    NodeState<String> status;
    NodeState<String> cells[kBoardCells];
  };
  LayoutState Seat(short x, short y, short width)
  {
    LayoutState s;
    s.x = x; s.y = y; s.width = width; s.height = 20; s.spacing = 0;
    return s;
  }
  ToolboxCellContext *Attach(Scene &scene, ToolboxScenePlatformController &controller, const char *id,
                             const LayoutState &seat)
  {
    Node *node = 0;
    loka::dsl::FlowError error;
    LOKA_VERIFY(loka::dsl::testing::LookupNodeById<Node>(&scene, id, node, error));
    ToolboxCellContext *context = new ToolboxCellContext(node->asCellNode(), &controller);
    node->setContext(context);
    LayoutState s = seat;
    context->layout(&controller, s);
    context->draw(&controller);
    return context;
  }
}
void Run(bool statusLast, bool partialNeighbour = false)
{
  gStatusLast = statusLast;
  using loka::dsl::testing::SceneTestAccess;
  ToolboxWindow window;
  ToolboxScenePlatformController controller(&window);
  Scene scene((Boundary<FoldNode>(FoldProps())));
  scene.mount(&controller);
  SceneTestAccess::updateAttached(scene, true);
  FoldNode *root = static_cast<FoldNode *>(SceneTestAccess::rootBoundary(scene));
  RibbonGroupEdgeNode *edge = root->asNestable()->childrenHead()->asNestable()->childrenHead()->asRibbonGroupEdgeNode();
  LOKA_VERIFY(edge != 0);
  LOKA_VERIFY(RegisterToolboxBuiltInSupport(controller));
  CardBandNode refusedBand((CardBandProps()));
  IPlatformNodeHandler *refusal = controller.nodeHandlerRegistry_.find(&refusedBand);
  LOKA_VERIFY(refusal != 0);
  LOKA_VERIFY(refusal->ensureContext(&refusedBand, &controller, Seat(10, 20, 30)) == 0);
  LOKA_VERIFY(refusedBand.getContext() == 0);
  LayoutState edgeSeat = Seat(20, 65, 1);
  IPlatformNodeHandler *handler = controller.nodeHandlerRegistry_.find(edge);
  LOKA_VERIFY(handler != 0);
  ToolboxRibbonGroupEdgeContext *edgeContext =
      static_cast<ToolboxRibbonGroupEdgeContext *>(handler->ensureContext(edge, &controller, edgeSeat));
  LOKA_VERIFY(edgeContext != 0);
  LOKA_VERIFY(edgeContext->layout(&controller, edgeSeat) == 1);
  LOKA_VERIFY(edgeSeat.y == 85);
  LOKA_VERIFY(controller.compositionReplay.required());
  LOKA_VERIFY(handler->ensureContext(edge, &controller, edgeSeat) == edgeContext);
  const PaintQuery edgeQuery = {ToolboxPaintScope(), PLACEMENT_ELIGIBLE};
  LOKA_VERIFY(edgeContext->queryPaintDamage(edgeQuery).kind == PAINT_ANSWER_EXACT);
  LOKA_VERIFY(edgeContext->queryPaintDamage(edgeQuery).damage.width == 0);
  const PaintQuery unsettled = {ToolboxPaintScope(), PLACEMENT_PENDING};
  LOKA_VERIFY(edgeContext->queryPaintDamage(unsettled).kind == PAINT_ANSWER_REFUSED);
  // Projection clipping bounds every band, including an entirely hidden strip.
  for (unsigned hidden = 0; hidden < 2; ++hidden)
  {
    SetRect(&controller.projectionClip, 20, 70, hidden ? 20 : 21, 75);
    edgeSeat = Seat(20, 65, 1);
    edgeContext->layout(&controller, edgeSeat);
    toolbox_host::fills.clear();
    edgeContext->render(&controller);
    LOKA_VERIFY(hidden ? toolbox_host::fills.empty() : !toolbox_host::fills.empty());
    for (unsigned i = 0; i < toolbox_host::fills.size(); ++i)
      LOKA_VERIFY(toolbox_host::fills[i].rect.top >= 70 && toolbox_host::fills[i].rect.bottom <= 75);
  }
  SetRect(&controller.projectionClip, -30000, -30000, 30000, 30000);
  // Render both side kinds and the bottom through the actual leaf, then restore the fold seat.
  for (unsigned kind = 0; kind < 3; ++kind)
  {
    edge->props.edge = static_cast<RibbonGroupEdge>(kind);
    LayoutState paintSeat = Seat(20, 65, kind == RIBBON_GROUP_EDGE_BOTTOM ? 41 : 1);
    paintSeat.height = kind == RIBBON_GROUP_EDGE_BOTTOM ? 1 : 41;
    edgeContext->layout(&controller, paintSeat);
    toolbox_host::fills.clear();
    toolbox_host::backgroundColors.clear();
    window.port.fgColor = greenColor;
    edgeContext->render(&controller);
    LOKA_VERIFY(window.port.fgColor == greenColor);
    LOKA_VERIFY(!toolbox_host::backgroundColors.empty() && toolbox_host::backgroundColors.back() == whiteColor);
    const unsigned count = kind == RIBBON_GROUP_EDGE_BOTTOM ? 1 : 4;
    LOKA_VERIFY(toolbox_host::fills.size() == count);
    for (unsigned band = 0; band < count; ++band)
    {
      LOKA_VERIFY(toolbox_host::fills[band].ink == blackColor);
      for (unsigned y = 0; y < 8; ++y)
        LOKA_VERIFY(toolbox_host::fills[band].pattern.pat[y]
                    == loka::toolbox::kToolboxGradientPatterns[8 - 2 * band].pat[y]);
    }
  }
  edge->props.edge = RIBBON_GROUP_EDGE_LEFT;
  edgeSeat = Seat(20, 65, 1);
  edgeContext->layout(&controller, edgeSeat);
  ToolboxCellContext *residents[kBoardCells + 2];
  Rect rects[kBoardCells + 2];
  residents[0] = Attach(scene, controller, "status", Seat(10, 0, 120));
  SetRect(&rects[0], 10, 0, 130, 20);
  for (int i = 0; i < kBoardCells; ++i)
  {
    const short x = static_cast<short>(partialNeighbour && i >= 2 ? 100 + 100 * i : 10 + 20 * i);
    const short y = partialNeighbour && i == 1 ? 50 : 40;
    residents[i + 1] = Attach(scene, controller, kIds[i], Seat(x, y, 20));
    SetRect(&rects[i + 1], x, y, x + 20, y + 20);
  }
  // In the partial case c0 and c1 have the cheapest bounding union:
  // (10,40)-(50,70). The neighbour touches neither changed cell, but its
  // top five pixels intersect that union's otherwise empty lower-left corner.
  const short neighbourX = partialNeighbour ? 10 : 220;
  const short neighbourY = partialNeighbour ? 65 : 40;
  residents[kBoardCells + 1] = Attach(scene, controller, "neighbour", Seat(neighbourX, neighbourY, 20));
  SetRect(&rects[kBoardCells + 1], neighbourX, neighbourY, neighbourX + 20, neighbourY + 20);
  {
    StateTrackerGuard guard(root->tracker());
    root->status.set(String::Literal("Mines left: 9"));
    for (int i = 0; i < kBoardCells; ++i)
      root->cells[i].set(String::Literal("1"));
  }
  const PaintQuery query = {ToolboxPaintScope(), PLACEMENT_ELIGIBLE};
  ToolboxSceneDebugStats stats;
  ToolboxPaintAnswerSource source(stats);
  PaintAnswerBuffer<> answers;
  BoundaryLocalApplyInfo info;
  info.paintKind = LOCAL_APPLY_PAINT_GENERIC;
  const PaintApplyVerdict verdict = CollectPaintAnswers(*root, query, answers, source);
  std::printf("fold collector (status %s): exact=%u refused=%u widened=%d rects=%u\n", gStatusLast ? "last" : "first", verdict.exactCount(),
              verdict.refusedCount(), verdict.widened() ? 1 : 0, answers.count());
  std::fflush(stdout);
  // Ten changed Cells in one apply: the rail must not widen to the window.
  LOKA_VERIFY(verdict.canSkipBroadPaint(info));
  LOKA_VERIFY(verdict.exactCount() == kBoardCells + 3);
  LOKA_VERIFY(answers.count() <= kApplyPaintPlanCapacity);
  bool covered[kBoardCells + 1] = {false};
  for (unsigned r = 0; r < answers.count(); ++r)
  {
    const PaintDamage &d = answers.entry(r).damage;
    if (d.y < 20)
      LOKA_VERIFY(d.x == 10 && d.y == 0 && d.width == 120 && d.height == 20);
    for (int i = 0; i <= kBoardCells; ++i)
      if (d.x <= rects[i].left && d.y <= rects[i].top
          && d.x + d.width >= rects[i].right && d.y + d.height >= rects[i].bottom)
        covered[i] = true;
  }
  for (int i = 0; i <= kBoardCells; ++i)
    LOKA_VERIFY(covered[i]);
  const PaintAnswer neighbourBefore = residents[kBoardCells + 1]->queryPaintDamage(query);
  LOKA_VERIFY(neighbourBefore.kind == PAINT_ANSWER_EXACT);
  LOKA_VERIFY(neighbourBefore.damage.width == 0 || neighbourBefore.damage.height == 0);
  unsigned partialDraws = 0;
  unsigned edgeDraws = 0;
  for (unsigned r = 0; r < answers.count(); ++r)
  {
    const PaintDamage &d = answers.entry(r).damage;
    Rect delivery;
    SetRect(&delivery, d.x, d.y, d.x + d.width, d.y + d.height);
    toolbox_host::fills.clear();
    controller.rootNode_ = root;
    controller.renderContext = edgeContext;
    if (controller.compositionReplay.required())
      ToolboxRenderDirtyInCompositionOrder(controller, delivery);
    if (!toolbox_host::fills.empty())
    {
      ++edgeDraws;
      LOKA_VERIFY(partialNeighbour);
      // Neither changed Cell touches this strip; only their folded union does.
      LOKA_VERIFY(delivery.top <= 65 && delivery.bottom > 65);
      LOKA_VERIFY(toolbox_host::fills.front().rect.left == 20);
      LOKA_VERIFY(toolbox_host::fills.back().rect.top == 65);
    }
    ToolboxPaintClip clip(delivery);
    for (int i = 0; i < kBoardCells + 2; ++i)
    {
      if (delivery.left < rects[i].right && delivery.right > rects[i].left
          && delivery.top < rects[i].bottom && delivery.bottom > rects[i].top)
      {
        residents[i]->draw(&controller);
        if (partialNeighbour && i == kBoardCells + 1)
        {
          LOKA_VERIFY(delivery.top > rects[i].top || delivery.left > rects[i].left
                      || delivery.bottom < rects[i].bottom || delivery.right < rects[i].right);
          ++partialDraws;
          const PaintAnswer after = residents[i]->queryPaintDamage(query);
          std::printf("partial neighbour: clip=(%d,%d,%d,%d) resident=(%d,%d,%d,%d) kind=%d reason=%d\n",
                      delivery.left, delivery.top, delivery.right, delivery.bottom,
                      rects[i].left, rects[i].top, rects[i].right, rects[i].bottom,
                      after.kind, after.reason);
        }
      }
    }
  }
  if (partialNeighbour)
  {
    LOKA_VERIFY(partialDraws != 0);
    LOKA_VERIFY(edgeDraws != 0);
  }
  {
    StateTrackerGuard guard(root->tracker());
    root->cells[0].set(String::Literal("2"));
  }
  const PaintApplyVerdict next = CollectPaintAnswers(*root, query, answers, source);
  std::printf("next click (status %s, partial neighbour %d): exact=%u refused=%u widened=%d reason=%d\n",
              statusLast ? "last" : "first", partialNeighbour ? 1 : 0,
              next.exactCount(), next.refusedCount(), next.widened() ? 1 : 0, next.refusalReason());
  std::fflush(stdout);
  LOKA_VERIFY(next.canSkipBroadPaint(info));
  LOKA_VERIFY(next.refusedCount() == 0);
  edgeContext->onFactChanged(NODE_FACT_ATTACHED, NODE_FACT_DETACHED_RETAINED);
  LOKA_VERIFY(!controller.compositionReplay.required());
  toolbox_host::fills.clear();
  edgeContext->render(&controller);
  LOKA_VERIFY(toolbox_host::fills.empty());
  edgeContext->onFactChanged(NODE_FACT_DETACHED_RETAINED, NODE_FACT_ATTACHED);
  LOKA_VERIFY(controller.compositionReplay.required());
  controller.renderContext = 0;
  SceneTestAccess::unmount(scene);
  LOKA_VERIFY(!controller.compositionReplay.required());
}
void GradientPins()
{
  using namespace loka::toolbox;
  for (unsigned k = 0; k <= 8; ++k)
    for (unsigned a = 0; a < 8; ++a)
    {
      unsigned row = 0, column = 0;
      for (unsigned b = 0; b < 8; ++b)
      {
        row += (kToolboxGradientPatterns[k].pat[a] >> b) & 1;
        column += (kToolboxGradientPatterns[k].pat[b] >> a) & 1;
      }
      LOKA_VERIFY(row == k && column == k);
      LOKA_VERIFY(kToolboxGradientPatterns[0].pat[a] == 0);
      LOKA_VERIFY(kToolboxGradientPatterns[8].pat[a] == 255);
    }
  const unsigned char levels[] = {8, 6, 4, 2};
  const int heights[] = {1, 3, 4, 41, 43};
  for (unsigned h = 0; h < sizeof(heights) / sizeof(heights[0]); ++h)
  {
    const Rect rect = {7, 13, static_cast<short>(7 + heights[h]), 14};
    toolbox_host::fills.clear();
    ToolboxPaintGradient(rect, BOTTOM_TO_TOP, levels, 4);
    unsigned painted = 0;
    short previousTop = rect.bottom;
    for (unsigned i = 0; i < toolbox_host::fills.size(); ++i)
    {
      const toolbox_host::Fill &fill = toolbox_host::fills[i];
      LOKA_VERIFY(fill.rect.bottom == previousTop);
      LOKA_VERIFY(fill.rect.top >= rect.top && fill.rect.bottom <= rect.bottom);
      LOKA_VERIFY(fill.rect.left == rect.left && fill.rect.right == rect.right);
      painted += fill.rect.bottom - fill.rect.top;
      previousTop = fill.rect.top;
    }
    LOKA_VERIFY(painted == static_cast<unsigned>(heights[h]) && previousTop == rect.top);
    if (heights[h] == 41)
    {
      const short boundaries[] = {48, 38, 28, 18, 7};
      LOKA_VERIFY(toolbox_host::fills.size() == 4);
      for (unsigned i = 0; i < 4; ++i)
      {
        LOKA_VERIFY(toolbox_host::fills[i].rect.bottom == boundaries[i]);
        LOKA_VERIFY(toolbox_host::fills[i].rect.top == boundaries[i + 1]);
        for (unsigned y = 0; y < 8; ++y)
          LOKA_VERIFY(toolbox_host::fills[i].pattern.pat[y] == kToolboxGradientPatterns[levels[i]].pat[y]);
      }
    }
  }
}
int main()
{
  ToolboxSceneDebugStats stats;
  ToolboxPaintAnswerSource source(stats);
  CardNode card((CardProps()));
  CardBandNode band((CardBandProps()));
  PaintAnswer answer;
  const PaintQuery query = {PaintScope(), PLACEMENT_ELIGIBLE};
  LOKA_VERIFY(!source.queryPaintAnswer(&card, 0, query, answer));
  LOKA_VERIFY(source.queryPaintAnswer(&band, 0, query, answer));
  LOKA_VERIFY(answer.kind == PAINT_ANSWER_REFUSED);
  GradientPins();
  Run(false);
  Run(true);
  Run(false, true);
  Run(true, true);
  std::puts("Paint fold pins passed");
}
