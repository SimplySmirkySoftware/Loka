#include "app/nodes/controls/Card.hpp"
#include "app/style/CardShadow.hpp"
#include "context/ToolboxCardBandContext.hpp"
#include "support/LifecycleFactTestAccess.hpp"
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
#include "app/nodes/nestable/Layer.hpp"
#include "app/scene/projection/CollectPaintAnswers.hpp"
#include "testing/scene/SceneTestFlow.hpp"
#include "debug/ToolboxSceneDebugStats.hpp"
#include "support/TestVerify.hpp"
#include "core/util/StateTrackerGuard.hpp"
#include <cstdio>
#include <cstddef>

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
  HostPortScope port(window.window());
  ToolboxScenePlatformController controller(&window);
  Scene scene((Boundary<FoldNode>(FoldProps())));
  scene.mount(&controller);
  SceneTestAccess::updateAttached(scene, true);
  FoldNode *root = static_cast<FoldNode *>(SceneTestAccess::rootBoundary(scene));
  RibbonGroupEdgeNode *edge = root->asNestable()->childrenHead()->asNestable()->childrenHead()->asRibbonGroupEdgeNode();
  LOKA_VERIFY(edge != 0);
  LOKA_VERIFY(RegisterToolboxBuiltInSupport(controller));
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
                    == loka::toolbox::kToolboxGradientPatterns[8 - 2 * band].pattern.pat[y]);
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
namespace
{
  template <typename T, size_t N> size_t ElementAlignment(const T (&)[N])
  {
    struct Probe
    {
      char c;
      T element;
    };
    return offsetof(Probe, element);
  }
} // namespace
void testToolboxGradientPatternsAreWordAligned()
{
  LOKA_VERIFY(ElementAlignment(loka::toolbox::kToolboxGradientPatterns) >= 2);
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
        row += (kToolboxGradientPatterns[k].pattern.pat[a] >> b) & 1;
        column += (kToolboxGradientPatterns[k].pattern.pat[b] >> a) & 1;
      }
      LOKA_VERIFY(row == k && column == k);
      LOKA_VERIFY(kToolboxGradientPatterns[0].pattern.pat[a] == 0);
      LOKA_VERIFY(kToolboxGradientPatterns[8].pattern.pat[a] == 255);
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
          LOKA_VERIFY(toolbox_host::fills[i].pattern.pat[y] == kToolboxGradientPatterns[levels[i]].pattern.pat[y]);
      }
    }
  }
}
/** Observe transfer payloads, not a fake QuickDraw rasterizer. */
void CardStencilPins()
{
  ToolboxWindow window;
  HostPortScope port(window.window());
  ToolboxScenePlatformController controller(&window);
  for (int extent = CARD_SHADOW_NONE; extent <= CARD_SHADOW_WIDE; ++extent)
    for (int residue = 0; residue < 8; ++residue)
      for (int ink = CARD_SHADOW_STENCIL; ink <= CARD_SHADOW_ALPHA; ++ink)
        for (int role = 0; role < CARD_BAND_COUNT; ++role)
        {
          CardBandNode node(
              CardBandProps(static_cast<CardBandRole>(role),
                            CardStyle(static_cast<CardShadowExtent>(extent), static_cast<CardShadowInk>(ink))));
          ToolboxCardBandContext context(&node, &controller);
          LayoutState seat = Seat(17 + residue, 21 + (residue * 3) % 8, 77 + residue);
          seat.height = 79 + (residue * 5) % 8;
          const CardStyle style = node.props.style;
          const loka::core::Frame band(seat.x, seat.y, seat.width, seat.height);
          const loka::core::Frame body = CardBodyInBand(band, node.props);
          // Odd viewport edges exercise phase independently of the full band.
          SetRect(
              &controller.projectionClip, band.x + 3, band.y + 5, band.x + band.width - 1, band.y + band.height - 2);
          context.layout(&controller, seat);
          toolbox_host::cardOperations.clear();
          toolbox_host::stencilTransfers.clear();
          toolbox_host::stencilRuns.clear();
          toolbox_host::roundFrames.clear();
          context.render(&controller);
          LOKA_VERIFY(node.props.style == style);
          LOKA_VERIFY(!toolbox_host::cardOperations.empty() && toolbox_host::cardOperations.back() == 'B');
          LOKA_VERIFY(toolbox_host::roundFrames.size() == 1);
          if (extent == CARD_SHADOW_NONE)
            LOKA_VERIFY(toolbox_host::stencilTransfers.empty() && toolbox_host::stencilRuns.empty());
          // Empty tiles need no native transfer. Required ink is pinned by the
          // complete expected-black coverage check below.
          for (std::size_t t = 0; t < toolbox_host::stencilTransfers.size(); ++t)
          {
            const toolbox_host::StencilTransfer &transfer = toolbox_host::stencilTransfers[t];
            LOKA_VERIFY(transfer.mode == srcOr && transfer.rowBytes == 4);
            bool hasInk = false;
            for (std::size_t byte = 0; byte < transfer.bytes.size(); ++byte)
              hasInk = hasInk || transfer.bytes[byte] != 0;
            LOKA_VERIFY(hasInk);
            const int width = transfer.rect.right - transfer.rect.left;
            const int height = transfer.rect.bottom - transfer.rect.top;
            LOKA_VERIFY(width <= 30 && height <= 30 && transfer.bytes.size() <= 120);
            for (int y = 0; y < height; ++y)
              for (int x = 0; x < transfer.rowBytes * 8; ++x)
              {
                const bool bit = (transfer.bytes[y * transfer.rowBytes + x / 8] & (0x80 >> (x & 7))) != 0;
                const int wx = transfer.rect.left + x, wy = transfer.rect.top + y;
                const bool expected = x < width && CardShadowStencilInk(CardShadowDensity(style, body, wx, wy), wx, wy);
                LOKA_VERIFY(bit == expected);
              }
          }
          for (std::size_t t = 0; t < toolbox_host::stencilRuns.size(); ++t)
          {
            const toolbox_host::Fill &run = toolbox_host::stencilRuns[t];
            LOKA_VERIFY(run.ink == patOr);
            for (int y = run.rect.top; y < run.rect.bottom; ++y)
              for (int x = run.rect.left; x < run.rect.right; ++x)
              {
                const bool bit = (run.pattern.pat[y & 7] & (0x80 >> (x & 7))) != 0;
                LOKA_VERIFY(bit == CardShadowStencilInk(CardShadowDensity(style, body, x, y), x, y));
              }
          }
          // Every expected black sample must be covered exactly once by the
          // transfer rectangles. This checks partition coverage, not QuickDraw.
          for (int y = controller.projectionClip.top; y < controller.projectionClip.bottom; ++y)
            for (int x = controller.projectionClip.left; x < controller.projectionClip.right; ++x)
            {
              if (!CardShadowStencilInk(CardShadowDensity(style, body, x, y), x, y))
                continue;
              unsigned covers = 0;
              for (std::size_t t = 0; t < toolbox_host::stencilTransfers.size(); ++t)
              {
                const Rect &r = toolbox_host::stencilTransfers[t].rect;
                covers += x >= r.left && x < r.right && y >= r.top && y < r.bottom;
              }
              for (std::size_t t = 0; t < toolbox_host::stencilRuns.size(); ++t)
              {
                const Rect &r = toolbox_host::stencilRuns[t].rect;
                covers += x >= r.left && x < r.right && y >= r.top && y < r.bottom;
              }
              LOKA_VERIFY(covers == 1);
            }
        }
}

void CardPins()
{
  ToolboxWindow window;
  HostPortScope port(window.window());
  ToolboxScenePlatformController controller(&window);
  LOKA_VERIFY(RegisterToolboxBuiltInSupport(controller));
  CardBandNode node((CardBandProps(CARD_BAND_TOP, CardStyle(CARD_SHADOW_DROP))));
  IPlatformNodeHandler *handler = controller.nodeHandlerRegistry_.find(&node);
  LOKA_VERIFY(handler != 0);
  LayoutState seat = Seat(10, 20, 34);
  seat.height = 3;
  ToolboxCardBandContext *context =
      static_cast<ToolboxCardBandContext *>(handler->ensureContext(&node, &controller, seat));
  LOKA_VERIFY(context != 0 && node.getContext() == context);
  LOKA_VERIFY(controller.compositionReplay.required());
  LOKA_VERIFY(handler->ensureContext(&node, &controller, seat) == context);
  context->onFactChanged(NODE_FACT_DETACHED_RETAINED, NODE_FACT_ATTACHED);
  LOKA_VERIFY(context->layout(&controller, seat) == 34 && seat.y == 23);
  const PaintQuery query = {ToolboxPaintScope(), PLACEMENT_ELIGIBLE};
  PaintAnswer answer = context->queryPaintDamage(query);
  LOKA_VERIFY(answer.kind == PAINT_ANSWER_EXACT && answer.damage.x == 10 && answer.damage.y == 20
              && answer.damage.width == 34 && answer.damage.height == 3);
  const PaintQuery pending = {ToolboxPaintScope(), PLACEMENT_PENDING};
  const PaintQuery foreign = {PaintScope(), PLACEMENT_ELIGIBLE};
  LOKA_VERIFY(context->queryPaintDamage(pending).kind == PAINT_ANSWER_REFUSED);
  LOKA_VERIFY(context->queryPaintDamage(foreign).kind == PAINT_ANSWER_REFUSED);
  ToolboxSceneDebugStats cardStats;
  ToolboxPaintAnswerSource cardSource(cardStats);
  PaintAnswer collected;
  LOKA_VERIFY(cardSource.queryPaintAnswer(&node, context, query, collected));
  LOKA_VERIFY(collected.kind == PAINT_ANSWER_EXACT && collected.damage.width == 34);

  // Real frame (10,20)-(50,60), in enum order TOP, LEFT, RIGHT, BOTTOM.
  const Rect bands[] = {{20, 10, 23, 44}, {23, 10, 60, 13}, {20, 44, 54, 50}, {54, 13, 60, 50}};
  const Rect bodies[] = {{20, 10, 32, 47}, {20, 10, 57, 22}, {20, 35, 57, 47}, {45, 10, 57, 47}};
  for (int role = 0; role < CARD_BAND_COUNT; ++role)
  {
    node.props.role = static_cast<CardBandRole>(role);
    seat = Seat(bands[role].left, bands[role].top, bands[role].right - bands[role].left);
    seat.height = bands[role].bottom - bands[role].top;
    context->layout(&controller, seat);
    toolbox_host::roundFrames.clear();
    const int paints = toolbox_host::shadowPaints;

    const int erases = toolbox_host::erases;
    const int regions = toolbox_host::regions;
    toolbox_host::backgroundColors.clear();
    window.port.fgColor = greenColor;
    PenMode(42);
    toolbox_host::pen.width = 2;
    toolbox_host::pen.height = 3;
    PenPat(&loka::toolbox::kToolboxGradientPatterns[2].pattern);
    const Rect savedClip = toolbox_host::currentClip();
    PenState saved;
    GetPenState(&saved);
    context->render(&controller);
    LOKA_VERIFY(toolbox_host::regions - regions == 2);
    LOKA_VERIFY(toolbox_host::shadowPaints == paints);
    LOKA_VERIFY(toolbox_host::roundFrames.size() == 1 && EqualRect(&toolbox_host::roundFrames[0], &bodies[role]));
    LOKA_VERIFY(toolbox_host::erases == erases && toolbox_host::backgroundColors.empty());
    LOKA_VERIFY(window.port.fgColor == greenColor && toolbox_host::pen.mode == saved.mode);
    LOKA_VERIFY(toolbox_host::pen.width == saved.width && toolbox_host::pen.height == saved.height);
    for (int row = 0; row < 8; ++row)
      LOKA_VERIFY(toolbox_host::pen.pattern.pat[row] == saved.pattern.pat[row]);
    const Rect restoredClip = toolbox_host::currentClip();
    LOKA_VERIFY(EqualRect(&savedClip, &restoredClip));
    // Clip allocation refusal must not fall back to drawing under an unknown clip.
    toolbox_host::roundFrames.clear();
    toolbox_host::stencilTransfers.clear();
    toolbox_host::stencilRuns.clear();
    toolbox_host::failRegions = 2;
    context->render(&controller);
    LOKA_VERIFY(toolbox_host::roundFrames.empty() && toolbox_host::shadowPaints == paints);
    toolbox_host::failRegions = 0;
    for (int allocation = 1; allocation <= 2; ++allocation)
    {
      toolbox_host::refuseRegionAttempt = toolbox_host::regions + allocation;
      context->render(&controller);
      LOKA_VERIFY(toolbox_host::roundFrames.empty() && toolbox_host::shadowPaints == paints);
      LOKA_VERIFY(toolbox_host::stencilTransfers.empty() && toolbox_host::stencilRuns.empty());
      const Rect refusedClip = toolbox_host::currentClip();
      LOKA_VERIFY(EqualRect(&savedClip, &refusedClip));
      LOKA_VERIFY(window.port.fgColor == greenColor && toolbox_host::pen.mode == saved.mode);
      LOKA_VERIFY(toolbox_host::pen.width == saved.width && toolbox_host::pen.height == saved.height);
      for (int row = 0; row < 8; ++row)
        LOKA_VERIFY(toolbox_host::pen.pattern.pat[row] == saved.pattern.pat[row]);
      toolbox_host::refuseRegionAttempt = 0;
    }
  }
  // A projected viewport narrows the conservative paint answer.
  SetRect(&controller.projectionClip, 15, 55, 30, 58);
  seat = Seat(13, 54, 37);
  seat.height = 6;
  context->layout(&controller, seat);
  answer = context->queryPaintDamage(query);
  LOKA_VERIFY(answer.damage.x == 15 && answer.damage.y == 55 && answer.damage.width == 15 && answer.damage.height == 3);
  SetRect(&controller.projectionClip, -30000, -30000, 30000, 30000);
  seat = Seat(10, 20, 0);
  seat.height = 0;
  context->layout(&controller, seat);
  const int allocations = toolbox_host::regions;
  context->render(&controller);
  LOKA_VERIFY(toolbox_host::regions == allocations);
  for (int terminal = 0; terminal < 2; ++terminal)
  {
    seat = Seat(13, 54, 37);
    seat.height = 6;
    context->layout(&controller, seat);
    if (terminal)
      LifecycleFactTestAccess::MarkSubtreeRetired(&node);
    else
      NotifySubtreeNodeDetached(&node);
    LifecycleFactTestAccess::DeliverFacts(&node);
    LOKA_VERIFY(!controller.compositionReplay.required());
    LOKA_VERIFY(context->queryPaintDamage(query).kind == PAINT_ANSWER_REFUSED);
    toolbox_host::roundFrames.clear();
    context->render(&controller);
    LOKA_VERIFY(toolbox_host::roundFrames.empty());
    if (!terminal)
    {
      NotifySubtreeNodeAttached(&node);
      LifecycleFactTestAccess::DeliverFacts(&node);
      LOKA_VERIFY(controller.compositionReplay.required());
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
  testToolboxGradientPatternsAreWordAligned();
  {
    // A Layer anywhere below the root selects composition-order replay, like a
    // ZStack; a plain column does not. Pins the stacking-container walk that
    // the replay selection and the boundary apply both read.
    loka::app::StackNode plain((loka::app::StackProps(loka::app::STACK_AXIS_COLUMN)));
    plain.addChild(new loka::app::TextNode((loka::app::TextProps("a"))));
    LOKA_VERIFY(!ToolboxTreeHasStackingContainer(&plain));
    loka::app::StackNode outer((loka::app::StackProps(loka::app::STACK_AXIS_COLUMN)));
    loka::app::StackNode *inner = new loka::app::StackNode((loka::app::StackProps(loka::app::STACK_AXIS_COLUMN)));
    inner->addChild(new loka::app::LayerNode((loka::app::LayerProps())));
    outer.addChild(inner);
    LOKA_VERIFY(ToolboxTreeHasStackingContainer(&outer));
  }
  CardPins();
  CardStencilPins();
  GradientPins();
  Run(false);
  Run(true);
  Run(false, true);
  Run(true, true);
  std::puts("Paint fold pins passed");
}
