#include "CardTests.hpp"
#include "app/nodes/controls/Card.hpp"
#include "app/nodes/nestable/For.hpp"
#include "core/Vector.hpp"
#include "app/layout/PlatformBuiltinLayoutHandlers.hpp"
#include "app/style/SurfaceGround.hpp"
#include "platform/null/NullScenePlatformController.hpp"
#include "testing/scene/SceneTestFlow.hpp"
#include "support/PropsReconciliation.hpp"
#include "support/TestVerify.hpp"
#include <climits>

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
  CardNode card((CardProps()));
  populate(card);
  PlatformLayoutHandlerRegistry registry;
  RegisterBuiltinPlatformLayoutHandlers(registry, 0, 0, loka::app::layout::RailSpacing(0, 0, 9));
  IPlatformLayoutHandler *handler = registry.find(&card);
  LOKA_VERIFY(handler != 0);
  const FrameGeometry g = card.frameGeometry();
  LOKA_VERIFY(g.insets.left == 3 && g.insets.top == 3 && g.insets.right == 6 && g.insets.bottom == 6);
  LOKA_VERIFY(g.minimumWidth == 15 && g.minimumHeight == 15);
  for (int width = 15; width <= 45; ++width)
    for (int height = 0; height <= 24; height += 3)
    {
      LayoutState s = offer(width);
      CardTraversal t(height);
      const int bottom = handler->layoutNode(&card, s, &t);
      LOKA_VERIFY(bottom == s.y + (height + 9 < 15 ? 15 : height + 9));
      LOKA_VERIFY(t.content.x == 13 && t.content.y == 23);
      LOKA_VERIFY(t.content.width == width - 9 && t.content.height == 71);
      verifyRing(t, s, bottom);
    }
  const int widths[] = {15, 14, 15, 0};
  for (int i = 0; i < 4; ++i)
  {
    LayoutState s = offer(widths[i]);
    CardTraversal t;
    const int bottom = handler->layoutNode(&card, s, &t);
    if (widths[i] < 15)
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
    LOKA_VERIFY(bottom == s.y + 15);
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
  LOKA_VERIFY(computeFramedLayoutResultY(&card, offer(), &refused, &recordCard, &refuseCardY) == 35);
  LOKA_VERIFY(refused.visits == 4);
  CardNode absent((CardProps()));
  CardTraversal none;
  LOKA_VERIFY(handler->layoutNode(&absent, offer(), &none) == 35);
  LOKA_VERIFY(none.visits == 0);

  // Actual layout facts: Card does not contain content that ignores its offer.
  OfferedContentHandler contentHandler;
  NullScenePlatformController platform;
  LOKA_VERIFY(platform.registerNodeHandler(&contentHandler));
  Scene scene((Card().testId("card-limit") << (Box().size(100, 2) << Button("wide"))).clone());
  LOKA_VERIFY(scene.mount(&platform));
  SceneTestAccess::updateAttached(scene, true);
  Node *node = 0;
  FlowError error;
  LOKA_VERIFY(LookupNodeById<Node>(&scene, "card-limit", node, error) == FLOW_STEP_SUCCEEDED);
  LayoutState small = offer(15);
  LOKA_VERIFY(platform.projectLayoutForTesting(node, small) == 35);
  Node *box = node->asNestable()->childrenHead()->asNestable()->childrenHead();
  OfferedContentContext *context =
      static_cast<OfferedContentContext *>(box->asNestable()->childrenHead()->getContext());
  LOKA_VERIFY(context && context->offered.width == 100);
  LOKA_VERIFY(context->offered.x + context->offered.width > small.x + small.width);
  SceneTestAccess::unmount(scene);

  loka::core::MutableState<loka::core::Frame> viewport(loka::core::Frame(0, 0, 100, 100));
  CanvasNode grid(CanvasProps(20, 3, &viewport));
  CardNode *gridCard = new CardNode(CardProps());
  populate(*gridCard);
  grid.addChild(gridCard);
  CanvasPlatformLayoutHandler gridHandler;
  CardGridTraversal gridTraversal;
  const int gridBottom = gridHandler.layoutNode(&grid, offer(), &gridTraversal);
  LOKA_VERIFY(gridTraversal.cell.width == 20 && gridTraversal.cell.height == 3);
  LOKA_VERIFY(gridBottom == 23 && gridTraversal.cardBottom == 35);
}

void testCardNaturalWidthAndSeats()
{
  CardWidth natural(20), tiny(1), excessive(SHRT_MAX), declining(-1), zero(0);
  CardNode absent((CardProps()));
  LOKA_VERIFY(childTakesLayoutSeat(&absent));
  LOKA_VERIFY(!isLayoutPassThrough(&absent));
  LOKA_VERIFY(measured(absent, natural) == 301);
  CardNode empty((CardProps()));
  empty.addChild(new StackNode(StackProps(STACK_AXIS_COLUMN)));
  LOKA_VERIFY(childTakesLayoutSeat(&empty));
  LOKA_VERIFY(measured(empty, natural) == 301);
  CardNode card((CardProps()));
  populate(card);
  LOKA_VERIFY(measured(card, natural) == 29);
  LOKA_VERIFY(measured(card, tiny) == 15);
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
  Card declaration(copy);
  declaration << Text("projected");
  Card copied(declaration);
  loka::core::OwnedDef<NodeDefinitionBase> cloned(copied.clone());
  LOKA_VERIFY(cloned.isSet());
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
