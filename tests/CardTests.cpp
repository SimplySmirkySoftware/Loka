#include "CardTests.hpp"
#include "app/nodes/controls/Card.hpp"
#include "app/nodes/nestable/For.hpp"
#include "app/nodes/nestable/Match.hpp"
#include "core/Vector.hpp"
#include "app/layout/PlatformBuiltinLayoutHandlers.hpp"
#include "app/style/SurfaceGround.hpp"
#include "platform/null/NullScenePlatformController.hpp"
#include "testing/scene/SceneTestFlow.hpp"
#include "support/PropsReconciliation.hpp"
#include "support/TestVerify.hpp"
#include <climits>
#include <cstdio>

#ifdef LOKA_CARD_CLONE_FAULT_PINS
namespace
{
  unsigned cardAllocationCountdown = 0;
}
#if __SIZEOF_POINTER__ == 8
#define CARD_REAL_NEW __real__Znwm
#define CARD_WRAP_NEW __wrap__Znwm
#else
#define CARD_REAL_NEW __real__Znwj
#define CARD_WRAP_NEW __wrap__Znwj
#endif
extern "C" void *CARD_REAL_NEW(size_t);
extern "C" void *CARD_WRAP_NEW(size_t size)
{
  if (cardAllocationCountdown && --cardAllocationCountdown == 0)
    return 0;
  return CARD_REAL_NEW(size);
}
#endif

namespace
{
  using namespace loka::app;
  using namespace loka::app::scene;
  using namespace loka::app::layout;
  using namespace loka::dsl;
  using namespace loka::dsl::testing;

  class CardTraversal : public IPlatformLayoutTraversal
  {
  public:
    LayoutState content;
    LayoutState bands[CARD_BAND_COUNT];
    int visits;
    const int advance;
    explicit CardTraversal(int height = 20)
        : visits(0),
          advance(height)
    {
    }
    virtual int layoutChild(Node *node, const LayoutState &state)
    {
      if (CardBandNode *band = node->asCardBandNode())
      {
        this->bands[band->props.role] = state;
        ++this->visits;
        return 12345; // Decorations do not determine the frame's end Y.
      }
      this->content = state;
      return state.y + this->advance;
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
  int recordCard(void *data, Node *node, const LayoutState &state)
  {
    return static_cast<CardTraversal *>(data)->layoutChild(node, state);
  }
  bool refuseCardY(void *, int)
  {
    return false;
  }
  void populate(CardNode &card)
  {
    card.addChild(new TextNode(TextProps("content")));
    for (int i = 0; i < CARD_BAND_COUNT; ++i)
      card.addChild(new CardBandNode(CardBandProps(static_cast<CardBandRole>(i), card.props.style)));
  }
  LayoutState offer(int width = 40)
  {
    LayoutState s;
    s.x = 10;
    s.y = 20;
    s.width = static_cast<short>(width);
    s.height = 80;
    return s;
  }
  void emptyBands(const CardTraversal &t, const LayoutState &s)
  {
    LOKA_VERIFY(t.visits == 4);
    for (int i = 0; i < CARD_BAND_COUNT; ++i)
    {
      LOKA_VERIFY(t.bands[i].x == s.x && t.bands[i].y == s.y);
      LOKA_VERIFY(t.bands[i].width == 0 && t.bands[i].height == 0);
    }
  }
  void verifyRing(const CardTraversal &t, const LayoutState &s, int bottom)
  {
    LOKA_VERIFY(t.visits == 4);
    const int right = s.x + s.width;
    for (int y = s.y; y < bottom; ++y)
      for (int x = s.x; x < right; ++x)
      {
        int hits = 0;
        for (int i = 0; i < CARD_BAND_COUNT; ++i)
        {
          const LayoutState &b = t.bands[i];
          if (x >= b.x && x < b.x + b.width && y >= b.y && y < b.y + b.height)
            ++hits;
        }
        const bool ring = x < s.x + 3 || x >= right - 6 || y < s.y + 3 || y >= bottom - 6;
        LOKA_VERIFY(hits == (ring ? 1 : 0));
      }
    // Recover exactly the three adjacent body edges; the opposite edge is unknown.
    const LayoutState &top = t.bands[CARD_BAND_TOP];
    LOKA_VERIFY(top.x == s.x && top.y == s.y && top.x + top.width + 3 == right - 3);
    const LayoutState &left = t.bands[CARD_BAND_LEFT];
    LOKA_VERIFY(left.x == s.x && left.y - 3 == s.y && left.y + left.height - 3 == bottom - 3);
    const LayoutState &r = t.bands[CARD_BAND_RIGHT];
    LOKA_VERIFY(r.y == s.y && r.x + r.width - 3 == right - 3 && r.y + r.height + 3 == bottom - 3);
    const LayoutState &b = t.bands[CARD_BAND_BOTTOM];
    LOKA_VERIFY(b.x - 3 == s.x && b.x + b.width - 3 == right - 3 && b.y + b.height - 3 == bottom - 3);
  }
  class CardGridTraversal : public IPlatformLayoutTraversal
  {
  public:
    int cardBottom;
    LayoutState cell;
    CardGridTraversal()
        : cardBottom(0)
    {
    }
    virtual int layoutChild(Node *node, const LayoutState &state)
    {
      this->cell = state;
      CardTraversal child(0);
      this->cardBottom = computeFramedLayoutResultY(node, state, &child, &recordCard, 0);
      return this->cardBottom;
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
  class OfferedContentContext : public NodeContext
  {
  public:
    LayoutState offered;
    OfferedContentContext()
    {
      this->offered.x = -1234;
    }
    virtual short layout(IPlatformController *, LayoutState &state)
    {
      this->offered = state;
      return state.width;
    }
  };
  class OfferedContentHandler : public IPlatformNodeHandler
  {
  public:
    virtual const void *nodeTypeKey() const
    {
      return NodeTypeToken<ButtonNode>();
    }
    virtual NodeContext *ensureContext(Node *node, IPlatformController *, const LayoutState &)
    {
      if (!node->getContext())
        node->setContext(new OfferedContentContext());
      return node->getContext();
    }
  };
  class CardWidth : public INaturalWidthSource
  {
  public:
    const short value;
    explicit CardWidth(short n)
        : value(n)
    {
    }
    virtual bool queryNaturalWidth(Node *, short &width) const
    {
      width = this->value;
      return this->value >= 0;
    }
  };
  int measured(CardNode &card, const INaturalWidthSource &source)
  {
    RowWidthConsultation width(
        &card, 1, 301, 0, loka::app::layout::RailSpacing(12, 0, 9), ROW_UNDECLARED_WIDTH_NATURAL, &source);
    return width.next(&card).width();
  }
} // namespace

void testCardGeometryAndLimits()
{
  CardNode card((CardProps(CardStyle(CARD_SHADOW_DROP))));
  populate(card);
  PlatformLayoutHandlerRegistry registry;
  RegisterBuiltinPlatformLayoutHandlers(registry, 0, 0, loka::app::layout::RailSpacing(0, 0, 9));
  IPlatformLayoutHandler *handler = registry.find(&card);
  LOKA_VERIFY(handler != 0);
  const FrameGeometry g = card.frameGeometry();
  LOKA_VERIFY(g.insets.left == 3 && g.insets.top == 3 && g.insets.right == 6 && g.insets.bottom == 6);
  LOKA_VERIFY(g.minimumWidth == 17 && g.minimumHeight == 17);
  LOKA_VERIFY(CardStyle::minimumBodyExtent(5, 1) == 16);
  LOKA_VERIFY(CardStyle::minimumBodyExtent(1, 5) == 16);
  for (int width = 17; width <= 45; ++width)
    for (int height = 0; height <= 24; height += 3)
    {
      LayoutState s = offer(width);
      CardTraversal t(height);
      const int bottom = handler->layoutNode(&card, s, &t);
      LOKA_VERIFY(bottom == s.y + (height + 9 < 17 ? 17 : height + 9));
      LOKA_VERIFY(t.content.x == 13 && t.content.y == 23);
      LOKA_VERIFY(t.content.width == width - 9 && t.content.height == 71);
      verifyRing(t, s, bottom);
    }
  const int widths[] = {17, 16, 17, 0};
  for (int i = 0; i < 4; ++i)
  {
    LayoutState s = offer(widths[i]);
    CardTraversal t;
    const int bottom = handler->layoutNode(&card, s, &t);
    if (widths[i] < 17)
      emptyBands(t, s);
    else
      verifyRing(t, s, bottom);
    LOKA_VERIFY(t.content.width == (widths[i] < 9 ? 0 : widths[i] - 9));
  }
  for (int overflow = 0; overflow < 3; ++overflow)
  {
    LayoutState s = offer(20);
    if (overflow == 0)
      s.x = 32750; // Every band origin and extent fits; the right endpoints do not.
    if (overflow == 1)
      s.y = 32755; // Empty content fits; height minimum overflows.
    if (overflow == 2)
      s.x = 32767; // Content narrowing remains the existing convention.
    CardTraversal t(0);
    const int bottom = handler->layoutNode(&card, s, &t);
    LOKA_VERIFY(bottom == s.y + 17);
    emptyBands(t, s);
    LOKA_VERIFY(t.content.x == static_cast<short>(s.x + 3));
    LOKA_VERIFY(t.content.y == static_cast<short>(s.y + 3));
  }
  LayoutState tall = offer(20);
  tall.y = -30000;
  CardTraversal tallTraversal(40000);
  LOKA_VERIFY(handler->layoutNode(&card, tall, &tallTraversal) == 10009);
  emptyBands(tallTraversal, tall); // Endpoints fit; the bands' heights do not.
  CardTraversal refused;
  LOKA_VERIFY(computeFramedLayoutResultY(&card, offer(), &refused, &recordCard, &refuseCardY) == 37);
  LOKA_VERIFY(refused.visits == 4);
  CardNode absent((CardProps(CardStyle(CARD_SHADOW_DROP))));
  CardTraversal none;
  LOKA_VERIFY(handler->layoutNode(&absent, offer(), &none) == 37);
  LOKA_VERIFY(none.visits == 0);

  // Actual layout facts: Card does not contain content that ignores its offer.
  OfferedContentHandler contentHandler;
  NullScenePlatformController platform;
  LOKA_VERIFY(platform.registerNodeHandler(&contentHandler));
  Scene scene(
      (Card(CardStyle(CARD_SHADOW_DROP)).testId("card-limit") << (Box().size(100, 2) << Button("wide"))).clone());
  LOKA_VERIFY(scene.mount(&platform));
  SceneTestAccess::updateAttached(scene, true);
  Node *node = 0;
  FlowError error;
  LOKA_VERIFY(LookupNodeById<Node>(&scene, "card-limit", node, error) == FLOW_STEP_SUCCEEDED);
  LayoutState small = offer(17);
  LOKA_VERIFY(platform.projectLayoutForTesting(node, small) == 37);
  Node *box = node->asNestable()->childrenHead()->asNestable()->childrenHead();
  OfferedContentContext *context =
      static_cast<OfferedContentContext *>(box->asNestable()->childrenHead()->getContext());
  LOKA_VERIFY(context && context->offered.width == 100);
  LOKA_VERIFY(context->offered.x + context->offered.width > small.x + small.width);
  SceneTestAccess::unmount(scene);

  loka::core::MutableState<loka::core::Frame> viewport(loka::core::Frame(0, 0, 100, 100));
  CanvasNode grid(CanvasProps(20, 3, &viewport));
  CardNode *gridCard = new CardNode(CardProps(CardStyle(CARD_SHADOW_DROP)));
  populate(*gridCard);
  grid.addChild(gridCard);
  CanvasPlatformLayoutHandler gridHandler;
  CardGridTraversal gridTraversal;
  const int gridBottom = gridHandler.layoutNode(&grid, offer(), &gridTraversal);
  LOKA_VERIFY(gridTraversal.cell.width == 20 && gridTraversal.cell.height == 3);
  LOKA_VERIFY(gridBottom == 23 && gridTraversal.cardBottom == 37);
}

void testCardNaturalWidthAndSeats()
{
  CardWidth natural(20), tiny(1), excessive(SHRT_MAX), declining(-1), zero(0);
  CardNode absent((CardProps(CardStyle(CARD_SHADOW_DROP))));
  LOKA_VERIFY(childTakesLayoutSeat(&absent));
  LOKA_VERIFY(!isLayoutPassThrough(&absent));
  LOKA_VERIFY(measured(absent, natural) == 301);
  CardNode empty((CardProps(CardStyle(CARD_SHADOW_DROP))));
  empty.addChild(new StackNode(StackProps(STACK_AXIS_COLUMN)));
  LOKA_VERIFY(childTakesLayoutSeat(&empty));
  LOKA_VERIFY(measured(empty, natural) == 301);
  CardNode card((CardProps(CardStyle(CARD_SHADOW_DROP))));
  populate(card);
  LOKA_VERIFY(measured(card, natural) == 29);
  LOKA_VERIFY(measured(card, tiny) == 17);
  LOKA_VERIFY(measured(card, excessive) == 301);
  LOKA_VERIFY(measured(card, declining) == 301);
  LOKA_VERIFY(measured(card, zero) == 301);
  LOKA_VERIFY(GroundForKind(NODE_KIND_CARD) == SURFACE_GROUND_TRANSPARENT);
  LOKA_VERIFY(GroundForKind(NODE_KIND_CARD_BAND) == SURFACE_GROUND_TRANSPARENT);
  LOKA_VERIFY(GroundForKind(NODE_KIND_RIBBON_GROUP_EDGE) == SURFACE_GROUND_WINDOW);
}

void testCardSingleStyleRetainedAndRefusal()
{
  using namespace PropsReconciliationSupport;
  const CardStyle style;
  const CardStyle copy(style);
  LOKA_VERIFY(copy == style && !(copy < style) && copy.hash() == style.hash());
  Card declaration;
  LOKA_VERIFY(declaration.props.style == style);
  declaration << Text("projected");
  Card copied(declaration);
  loka::core::OwnedDef<NodeDefinitionBase> cloned(copied.clone());
  LOKA_VERIFY(cloned.isSet());
  LOKA_VERIFY(static_cast<const CardProps *>(cloned->propsBase())->style == style);
  LOKA_VERIFY(static_cast<const CardProps *>(cloned->propsBase())->hash() == declaration.props.hash());
  NullScenePlatformController platform;
  Scene scene((Boundary<Tree<Card> >(Props<Card>(&declaration))));
  LOKA_VERIFY(scene.mount(&platform));
  SceneTestAccess::updateAttached(scene, true);
  settle(scene);
  Node *card = root(scene)->childrenHead();
  LOKA_VERIFY(card && card->kind() == NODE_KIND_CARD);
  LOKA_VERIFY(card->asNestable()->childrenCount() == 5);
  const PaintQuery paintQuery = {platform.paintScope(), PLACEMENT_ELIGIBLE};
  PaintAnswer paintAnswer;
  LOKA_VERIFY(!platform.queryPaintAnswer(card, 0, paintQuery, paintAnswer));
  Node *column = card->asNestable()->childrenHead();
  LOKA_VERIFY(column->asNestable()->childrenHead()->getContext() != 0);
  Node *bands[4];
  CardBandProps before[4];
  Node *band = column->nextInComposition;
  for (int i = 0; i < 4; ++i, band = band->nextInComposition)
  {
    LOKA_VERIFY(band && band->asCardBandNode());
    bands[i] = band;
    before[i] = band->asCardBandNode()->props;
    LOKA_VERIFY(before[i].role == i && before[i].style == style);
    LOKA_VERIFY(band->getContext() == 0);
    LOKA_VERIFY(!platform.queryPaintAnswer(band, 0, paintQuery, paintAnswer));
    LayoutState s = offer();
    LOKA_VERIFY(!platform.prepareProjectedLayout(band, s));
  }
  LOKA_VERIFY(band == 0);
  applyProps(scene, copied);
  LOKA_VERIFY(root(scene)->childrenHead() == card);
  LOKA_VERIFY(static_cast<CardNode *>(card)->props.style == style);
  NodeDefinitionBase *bandDefinition = cloned->asNestableDefinition()->childrenHead()->nextInComposition;
  band = card->asNestable()->childrenHead()->nextInComposition;
  for (int i = 0; i < 4; ++i, band = band->nextInComposition)
  {
    LOKA_VERIFY(band == bands[i]);
    LOKA_VERIFY(bandDefinition->applyPropsToNode(band));
    bandDefinition = bandDefinition->nextInComposition;
    const CardBandProps &after = band->asCardBandNode()->props;
    LOKA_VERIFY(!(before[i] < after) && !(after < before[i]) && before[i].hash() == after.hash());
    LOKA_VERIFY(after.style == style && band->getContext() == 0);
  }
  SceneTestAccess::unmount(scene);
}

void testCardDefinitionCloneRefusal()
{
#ifdef LOKA_CARD_CLONE_FAULT_PINS
  // Each shell clone (column, then four bands) can refuse independently.
  for (unsigned failed = 1; failed <= 5; ++failed)
  {
    cardAllocationCountdown = failed;
    Card card;
    cardAllocationCountdown = 0;
    LOKA_VERIFY(card.childrenCount() == 0);
    card << Text("refused shell");
    LOKA_VERIFY(card.childrenCount() == 0);
    loka::core::OwnedDef<NodeDefinitionBase> refused(card.clone());
    LOKA_VERIFY(!refused.isSet());
  }
  Card complete;
  complete << Text("complete");
  for (unsigned failed = 1; failed <= 7; ++failed)
  {
    cardAllocationCountdown = failed;
    loka::core::OwnedDef<NodeDefinitionBase> refused(complete.clone());
    cardAllocationCountdown = 0;
    LOKA_VERIFY(!refused.isSet());
    LOKA_VERIFY(complete.childrenCount() == 5);
  }
  loka::core::OwnedDef<NodeDefinitionBase> recovered(complete.clone());
  LOKA_VERIFY(recovered.isSet() && recovered->asNestableDefinition()->childrenCount() == 5);
#else
  std::puts(
      "[skip] Card allocation refusal requires the Linux wrapped-new fixture; normal clone/apply runs on every rail.");
#endif
}

namespace
{
  struct CardInsertItem
  {
  };
  struct CardInsertFactory
  {
    TextDefinition operator()(const CardInsertItem &, std::size_t) const
    {
      return Text("item");
    }
  };
  size_t cardContentCount(const CardDefinition &card)
  {
    NodeDefinitionBase *column = card.childrenHead();
    INestableDefinition *nestable = column ? column->asNestableDefinition() : 0;
    return nestable ? nestable->childrenCount() : 0;
  }
} // namespace

/** Card hides the inherited insertion forms; each of its own forms reaches the content column. */
void testCardInsertionFormsReachContent()
{
  Card card;
  TextDefinition lvalue = Text("lvalue");
  card << lvalue;
  LOKA_VERIFY(cardContentCount(card) == 1);
  card << Text("temporary");
  LOKA_VERIFY(cardContentCount(card) == 2);
  card << new TextDefinition(Text("owned"));
  LOKA_VERIFY(cardContentCount(card) == 3);
  TextDefinition listed = Text("listed");
  std::vector<NodeDefinitionBase *> list;
  list.push_back(&listed);
  card << list;
  LOKA_VERIFY(cardContentCount(card) == 4);
  loka::Vector<CardInsertItem> items;
  items.push_back(CardInsertItem());
  items.push_back(CardInsertItem());
  card << For(1, items, CardInsertFactory());
  LOKA_VERIFY(cardContentCount(card) == 6);
  // The card itself still holds exactly its column and four bands.
  LOKA_VERIFY(card.childrenCount() == 1 + CARD_BAND_COUNT);
}

void testCardBodyReconstruction()
{
  using namespace loka::app;
  using loka::core::Frame;
  // Asymmetric v1 allowances: zero top/left, three right/bottom.
  const Frame bands[] = {Frame(10, 20, 34, 3), Frame(10, 23, 3, 37), Frame(44, 20, 6, 34), Frame(13, 54, 37, 6)};
  const Frame bodies[] = {Frame(10, 20, 37, 12), Frame(10, 20, 12, 37), Frame(35, 20, 12, 37), Frame(10, 45, 37, 12)};
  for (int role = 0; role < CARD_BAND_COUNT; ++role)
  {
    const CardBandProps props(static_cast<CardBandRole>(role), CardStyle(CARD_SHADOW_DROP));
    LOKA_VERIFY(CardBodyInBand(bands[role], props) == bodies[role]);
    const Frame shifted(bands[role].x + 71, bands[role].y + 93, bands[role].width, bands[role].height);
    const Frame expected(bodies[role].x + 71, bodies[role].y + 93, bodies[role].width, bodies[role].height);
    LOKA_VERIFY(CardBodyInBand(shifted, props) == expected);
  }
}

/** Values-only harness: explicitly apply the same per-child definitions that
    the kernel's parked-branch reentry plan applies. This makes no layout claim. */
void testCardShadowHarnessRetainedValues()
{
  using namespace PropsReconciliationSupport;
  Card declaration;
  declaration << Button("content");
  NullScenePlatformController platform;
  Scene scene((Boundary<Tree<Card> >(Props<Card>(&declaration))));
  LOKA_VERIFY(scene.mount(&platform));
  SceneTestAccess::updateAttached(scene, true);
  settle(scene);
  Node *card = root(scene)->childrenHead();
  Node *bands[4];
  Node *band = card->asNestable()->childrenHead()->nextInComposition;
  for (int i = 0; i < 4; ++i, band = band->nextInComposition)
    bands[i] = band;
  const CardShadowExtent extents[] = {CARD_SHADOW_NONE,
                                      CARD_SHADOW_DROP,
                                      CARD_SHADOW_WIDE,
                                      CARD_SHADOW_WIDE,
                                      CARD_SHADOW_WIDE,
                                      CARD_SHADOW_DROP,
                                      CARD_SHADOW_NONE,
                                      CARD_SHADOW_DROP};
  for (int step = 0; step < 8; ++step)
  {
    const CardStyle style(extents[step], step % 2 ? CARD_SHADOW_STENCIL : CARD_SHADOW_ALPHA);
    Card next(style);
    next << Button("content");
    applyProps(scene, next);
    NodeDefinitionBase *definition = next.childrenHead()->nextInComposition;
    band = card->asNestable()->childrenHead()->nextInComposition;
    LOKA_VERIFY(root(scene)->childrenHead() == card);
    LOKA_VERIFY(static_cast<CardNode *>(card)->props.style == style);
    for (int i = 0; i < 4; ++i, band = band->nextInComposition, definition = definition->nextInComposition)
    {
      LOKA_VERIFY(band == bands[i]);
      LOKA_VERIFY(definition->applyPropsToNode(band));
      LOKA_VERIFY(band->asCardBandNode()->props.role == i);
      LOKA_VERIFY(band->asCardBandNode()->props.style == style);
      LOKA_VERIFY(band->asCardBandNode()->props.style.ink() == style.ink());
    }
  }
  SceneTestAccess::unmount(scene);
}

namespace
{
  /** Observes actual Null projection entries without changing refusal or ink. */
  class CardProjectionPlatform : public NullScenePlatformController
  {
  public:
    LayoutState bands[4];
    CardBandNode *nodes[4];
    CardProjectionPlatform()
    {
      for (int i = 0; i < 4; ++i)
        this->nodes[i] = 0;
      this->resetOffers();
    }
    void resetOffers()
    {
      for (int i = 0; i < 4; ++i)
        this->bands[i].width = -1;
    }
    virtual bool prepareProjectedLayout(Node *node, LayoutState &state)
    {
      if (CardBandNode *band = node ? node->asCardBandNode() : 0)
      {
        this->nodes[band->props.role] = band;
        this->bands[band->props.role] = state;
      }
      return NullScenePlatformController::prepareProjectedLayout(node, state);
    }
  };

  class CardSeatRoot : public BoundaryNodeFor<CardSeatRoot>
  {
  public:
    NodeState<int> selection;
    NodeState<int> width;
    explicit CardSeatRoot(const BoundaryPropsFor<CardSeatRoot> &props)
        : BoundaryNodeFor<CardSeatRoot>(props)
    {
      this->state(this->selection, 0);
      this->state(this->width, 70);
    }
    virtual bool flushViewDirtyImmediately(NodeDirtyFlags) const
    {
      return false;
    }
    virtual void composeNode(NodeComposition &composition)
    {
      composition.declare(
          Row() << (Box().width(this->width.state())
                    << Match(*this->selection.state())
                           .arm(0, Card(CardStyle(CARD_SHADOW_NONE)).testId("none-card") << Button("content"))
                           .arm(1, Card(CardStyle(CARD_SHADOW_DROP)).testId("drop-card") << Button("content"))
                           .arm(2,
                                Card(CardStyle(CARD_SHADOW_WIDE, CARD_SHADOW_ALPHA)).testId("wide-card")
                                    << Button("content"))));
    }
  };
  CardNode *seatCard(Scene &scene, const char *id)
  {
    Node *node = 0;
    FlowError error;
    LOKA_VERIFY(LookupNodeById<Node>(&scene, id, node, error) == FLOW_STEP_SUCCEEDED);
    LOKA_VERIFY(node && node->kind() == NODE_KIND_CARD);
    return static_cast<CardNode *>(node);
  }
  OfferedContentContext *cardContent(CardNode *card)
  {
    Node *column = card->childrenHead();
    OfferedContentContext *context =
        static_cast<OfferedContentContext *>(column->asNestable()->childrenHead()->getContext());
    LOKA_VERIFY(context != 0);
    return context;
  }
  void verifySeat(CardNode *card, CardProjectionPlatform &platform, const CardStyle &style, int width)
  {
    LOKA_VERIFY(card->props.style == style);
    const loka::app::detail::CardShadowTable &g = style.geometry();
    const LayoutState &content = cardContent(card)->offered;
    const int height = 20 - g.allowance[1] - g.allowance[3] - 6;
    LOKA_VERIFY(content.x == g.allowance[0] + 3 && content.y == g.allowance[1] + 3);
    LOKA_VERIFY(content.width == width - g.allowance[0] - g.allowance[2] - 6);
    LOKA_VERIFY(content.height == (height > 0 ? height : 0));
    Node *band = card->childrenHead()->nextInComposition;
    for (int i = 0; i < 4; ++i, band = band->nextInComposition)
    {
      LOKA_VERIFY(band == platform.nodes[i]);
      LOKA_VERIFY(platform.nodes[i]->props.style == style);
      LOKA_VERIFY(platform.nodes[i]->props.role == i);
      const bool enabled = width >= g.minimumWidth + g.allowance[0] + g.allowance[2];
      if (enabled)
        LOKA_VERIFY(platform.bands[i].width > 0 && platform.bands[i].height > 0);
      else
        LOKA_VERIFY(platform.bands[i].width == 0 && platform.bands[i].height == 0);
    }
  }
} // namespace

void testCardShadowSeatLookAndReentry()
{
  OfferedContentHandler contentHandler;
  CardProjectionPlatform platform;
  LOKA_VERIFY(platform.registerNodeHandler(&contentHandler));
  Scene scene((Boundary<CardSeatRoot>()));
  LOKA_VERIFY(scene.mount(&platform));
  SceneTestAccess::updateAttached(scene, true);
  PropsReconciliationSupport::settle(scene);
  CardSeatRoot *root = static_cast<CardSeatRoot *>(SceneTestAccess::rootBoundary(scene));
  verifySeat(seatCard(scene, "none-card"), platform, CardStyle(CARD_SHADOW_NONE), 70);
  CardNode *parkedDrop = 0;
  CardBandNode *parkedBands[4] = {0, 0, 0, 0};
  const int arms[] = {1, 2, 1};
  for (int step = 0; step < 3; ++step)
  {
    if (parkedDrop)
      cardContent(parkedDrop)->offered.x = -1234;
    platform.resetOffers();
    {
      loka::core::StateTrackerGuard guard(root->tracker());
      root->selection.set(arms[step]);
    }
    PropsReconciliationSupport::settle(scene);
    const bool wide = arms[step] == 2;
    CardNode *card = seatCard(scene, wide ? "wide-card" : "drop-card");
    verifySeat(card,
               platform,
               CardStyle(wide ? CARD_SHADOW_WIDE : CARD_SHADOW_DROP, wide ? CARD_SHADOW_ALPHA : CARD_SHADOW_STENCIL),
               70);
    if (step == 0)
    {
      parkedDrop = card;
      for (int i = 0; i < 4; ++i)
        parkedBands[i] = platform.nodes[i];
    }
    if (step == 2)
    {
      LOKA_VERIFY(card == parkedDrop);
      for (int i = 0; i < 4; ++i)
        LOKA_VERIFY(platform.nodes[i] == parkedBands[i]);
    }
  }
  SceneTestAccess::unmount(scene);
}

void testCardShadowStateWidthRetainsBands()
{
  OfferedContentHandler contentHandler;
  CardProjectionPlatform platform;
  LOKA_VERIFY(platform.registerNodeHandler(&contentHandler));
  Scene scene((Boundary<CardSeatRoot>()));
  LOKA_VERIFY(scene.mount(&platform));
  SceneTestAccess::updateAttached(scene, true);
  CardSeatRoot *root = static_cast<CardSeatRoot *>(SceneTestAccess::rootBoundary(scene));
  {
    loka::core::StateTrackerGuard guard(root->tracker());
    root->selection.set(2);
  }
  PropsReconciliationSupport::settle(scene);
  CardNode *card = seatCard(scene, "wide-card");
  const CardStyle style(CARD_SHADOW_WIDE, CARD_SHADOW_ALPHA);
  verifySeat(card, platform, style, 70);
  CardBandNode *bands[4];
  for (int i = 0; i < 4; ++i)
    bands[i] = platform.nodes[i];
  const int widths[] = {53, 70};
  for (int step = 0; step < 2; ++step)
  {
    cardContent(card)->offered.x = -1234;
    platform.resetOffers();
    {
      loka::core::StateTrackerGuard guard(root->tracker());
      root->width.set(widths[step]);
    }
    PropsReconciliationSupport::settle(scene);
    LOKA_VERIFY(seatCard(scene, "wide-card") == card);
    verifySeat(card, platform, style, widths[step]);
    for (int i = 0; i < 4; ++i)
      LOKA_VERIFY(platform.nodes[i] == bands[i]);
  }
  SceneTestAccess::unmount(scene);
}

void testCardShadowValueIdentity()
{
  for (int a = 0; a < 6; ++a)
    for (int b = 0; b < 6; ++b)
    {
      const CardStyle first(static_cast<CardShadowExtent>(a / 2), static_cast<CardShadowInk>(a % 2));
      const CardStyle second(static_cast<CardShadowExtent>(b / 2), static_cast<CardShadowInk>(b % 2));
      LOKA_VERIFY((first == second) == (a == b));
      LOKA_VERIFY((first < second) == (a < b));
      LOKA_VERIFY((first.hash() == second.hash()) == (a == b));
      const CardBandProps firstBand(CARD_BAND_TOP, first), secondBand(CARD_BAND_TOP, second);
      LOKA_VERIFY((firstBand < secondBand) == (a < b));
    }
}

void testCardShadowSixGeometries()
{
  const int allowances[3][4] = {{0, 0, 0, 0}, {0, 0, 3, 3}, {12, 8, 12, 16}};
  const int minimums[3][2] = {{12, 12}, {17, 17}, {60, 68}};
  for (int extent = 0; extent < 3; ++extent)
    for (int ink = 0; ink < 2; ++ink)
    {
      const CardStyle style(static_cast<CardShadowExtent>(extent), static_cast<CardShadowInk>(ink));
      CardNode card((CardProps(style)));
      populate(card);
      const FrameGeometry g = card.frameGeometry();
      const int *a = allowances[extent];
      LOKA_VERIFY(g.insets.left == a[0] + 3 && g.insets.top == a[1] + 3);
      LOKA_VERIFY(g.insets.right == a[2] + 3 && g.insets.bottom == a[3] + 3);
      LOKA_VERIFY(g.minimumWidth == minimums[extent][0] && g.minimumHeight == minimums[extent][1]);
      for (int sample = 0; sample < 10; ++sample)
      {
        LayoutState s = offer(sample < 3 ? minimums[extent][0] + sample - 1 : 120);
        if (sample == 3)
          s.height = 1;
        if (sample == 4)
        {
          s.x = -50;
          s.y = -80;
        }
        if (sample == 5)
          s.x = SHRT_MAX - s.width;
        if (sample == 6)
          s.x = SHRT_MAX - s.width + 1;
        if (sample == 7)
        {
          s.x = SHRT_MIN;
          s.y = SHRT_MIN;
        }
        if (sample == 8)
          s.y = static_cast<short>(SHRT_MAX - minimums[extent][1] + 1);
        CardTraversal t(sample == 9 ? 80 : 0); // Short content rises to the minimum height.
        const int bottom = computeFramedLayoutResultY(&card, s, &t, &recordCard, 0);
        const int expectedHeight = sample == 9 ? 80 + a[1] + a[3] + 6 : minimums[extent][1];
        LOKA_VERIFY(bottom == s.y + expectedHeight);
        LOKA_VERIFY(t.content.x == static_cast<short>(s.x + a[0] + 3));
        LOKA_VERIFY(t.content.y == static_cast<short>(s.y + a[1] + 3));
        LOKA_VERIFY(t.content.width == s.width - a[0] - a[2] - 6);
        const int contentHeight = s.height - a[1] - a[3] - 6;
        LOKA_VERIFY(t.content.height == (contentHeight > 0 ? contentHeight : 0));
        if (sample == 0 || sample == 6 || sample == 8)
        {
          emptyBands(t, s);
          continue;
        }
        const int l = s.x, r = s.x + s.width, top = s.y;
        const loka::core::Frame expected[] = {
            loka::core::Frame(l, top, s.width - a[2] - 3, a[1] + 3),
            loka::core::Frame(l, top + a[1] + 3, a[0] + 3, bottom - top - a[1] - 3),
            loka::core::Frame(r - a[2] - 3, top, a[2] + 3, bottom - top - a[3] - 3),
            loka::core::Frame(l + a[0] + 3, bottom - a[3] - 3, s.width - a[0] - 3, a[3] + 3)};
        for (int role = 0; role < 4; ++role)
        {
          const LayoutState &band = t.bands[role];
          LOKA_VERIFY(loka::core::Frame(band.x, band.y, band.width, band.height) == expected[role]);
        }
      }
    }
}

void testCardStandardStyle()
{
  using namespace loka::platform;
  const CardStyle drop = StandardCardStyleFor(CPU_CLASS_68K);
  LOKA_VERIFY(drop.extent() == CARD_SHADOW_DROP && drop.ink() == CARD_SHADOW_STENCIL);
  const CardStyle ppc = StandardCardStyleFor(CPU_CLASS_PPC_60X);
  LOKA_VERIFY(ppc.extent() == CARD_SHADOW_WIDE && ppc.ink() == CARD_SHADOW_STENCIL);
  const CardStyle fast = StandardCardStyleFor(CPU_CLASS_G3_OR_FASTER);
  LOKA_VERIFY(fast.extent() == CARD_SHADOW_WIDE && fast.ink() == CARD_SHADOW_ALPHA);
  CpuClass answer;
  const bool answered = queryCpuClass(answer);
#if defined(__linux__)
  LOKA_VERIFY(!answered);
#else
  std::printf("[skip] Linux-only queryCpuClass decline pin on non-Linux target\n");
#endif
#if defined(_WIN32)
  LOKA_VERIFY(answered && answer == CPU_CLASS_G3_OR_FASTER);
#endif
  if (!answered)
    answer = CPU_CLASS_68K;
  LOKA_VERIFY(StandardCardStyle() == StandardCardStyleFor(answer));
  LOKA_VERIFY(CardStyle() == StandardCardStyle());
  const CardStyle explicitStyle(CARD_SHADOW_WIDE, CARD_SHADOW_ALPHA);
  LOKA_VERIFY(explicitStyle.extent() == CARD_SHADOW_WIDE && explicitStyle.ink() == CARD_SHADOW_ALPHA);
}
