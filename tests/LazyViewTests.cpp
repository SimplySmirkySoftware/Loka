#include "core/util/ScopedPtr.hpp"
#ifdef LOKA_UPSTREAM_GAUGE_PIN
#include "support/UpstreamGaugePin.hpp"
#endif
#include "testing/scene/SceneTestFlow.hpp"
#include "LazyViewTests.hpp"
#include "support/TestVerify.hpp"
#include "support/SeatBuildRequestAccess.hpp"
#include "app/nodes/nestable/LazyView.hpp"
#include "app/nodes/nestable/ScrollView.hpp"
#include "app/nodes/nestable/Show.hpp"
#include "app/layout/CanvasLayout.hpp"
#include "support/LokaAllocFailure.hpp"
#include "app/nodes/Text.hpp"
#include "app/nodes/controls/Button.hpp"
#include "app/nodes/controls/EditText.hpp"
#include "app/scene/Scene.hpp"
#include "platform/null/NullScenePlatformController.hpp"
#include "platform/null/context/NullTextContext.hpp"
#include "testing/scene/OwnershipDump.hpp"
#include <cstdio>
#include <cstring>
#include "core/Operation.hpp"
#if defined(__linux__)
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#endif

namespace
{
  using namespace loka::app;
  using namespace loka::app::scene;
  using namespace loka::core;
  class CardNode;
  struct CardProps : NodePropsBase<CardProps>
  {
    typedef CardProps TypeTag;
    typedef CardNode NodeType;
    CardProps(const String &text = String(), short n = 0)
        : label(text),
          number(n)
    {
    }
    bool operator<(const PropsBase &rhs) const
    {
      if (rhs.propsTypeId() != this->propsTypeId())
        return false;
      const CardProps &other = static_cast<const CardProps &>(rhs);
      return this->number != other.number ? this->number < other.number : this->label.compare(other.label) < 0;
    }
    bool operator!=(const CardProps &other) const
    {
      return !this->label.equals(other.label) || this->number != other.number;
    }
    String label;
    short number;
  };
  struct Record
  {
    Record()
        : list(0),
          second(0),
          viewport(0),
          otherViewport(0),
          editing(false),
          scrolling(false),
          parking(false),
          updates(0), maximum(32), refuseAttach(-1), events(0), withoutPoll(false), refuseClass(false), onAttach(0)
    {
      for (int i = 0; i < 64; ++i)
      {
        constructions[i] = 0;
        bindings[i] = 0;
        cards[i] = 0;
      }
    }
    ObservableList<CardProps> *list, *second;
    State<Frame> *viewport, *otherViewport;
    bool editing, scrolling, parking;
    int updates;
    unsigned maximum;
    int refuseAttach;
    std::vector<int> *events;
    bool withoutPoll;
    int refuseClass;
    void (*onAttach)(int);
    int constructions[64], bindings[64];
    CardNode *cards[64];
  };
  Record *record;
  struct MissingNode;
  struct MissingProps : NodePropsBase<MissingProps>
  { typedef MissingProps TypeTag; typedef MissingNode NodeType;
    bool operator<(const PropsBase &) const { return false; } };
  struct MissingNode : Node
  {
    typedef MissingProps TypeTag;
    MissingProps props;
    explicit MissingNode(const MissingProps &p) : props(p) {}
    char distinctLayout[4096];
  };
  class CardNode : public ComponentNodeWithProps<CardProps>
  {
  public:
    explicit CardNode(const CardProps &p)
        : ComponentNodeWithProps<CardProps>(p),
          label(),
          toggle()
    {
      ++record->constructions[p.number];
      record->cards[p.number] = this;
      this->declareStates(2).state(this->label, p.label).state(this->toggle, false);
    }
    virtual ~CardNode()
    {
      if (record->cards[this->props.number] == this)
        record->cards[this->props.number] = 0;
    }
    virtual void declareBindings(BindingToken &)
    {
      ++record->bindings[this->props.number];
      if (!this->label.get().equals(this->props.label))
        this->label.set(this->props.label);
    }
    virtual void attachNode(NodeComposition &)
    {
      if (record->events) record->events->push_back(this->props.number + 1);
      if (record->onAttach) record->onAttach(this->props.number);
      if (record->refuseAttach == this->props.number)
      {
        record->refuseAttach = -1;
        this->componentContext()->boundary()->noteComposeAllocationFailure();
      }
    }
    virtual void detachNode(NodeComposition &)
    { if (record->events) record->events->push_back(-this->props.number - 1); }
    virtual void composeChildren(NodeComposition &c)
    {
      if (record->refuseClass == 1 || (record->refuseClass == 3 && this->props.number == 9)) c.declare(NodeDefinition<MissingProps, MissingNode>(MissingProps()));
      else if (record->refuseClass == 2)
        c.declare(Row() << Text("quota") << Button("one") << Button("two")
                       << Button("three") << Button("four") << Button("five") << Button("six"));
      else if (record->editing)
        c.declare(Row() << Text(this->label.state()) << EditText(this->label));
      else
        c.declare(Row() << Text(this->label.state()) << Button("marker"));
    }
    NodeState<String> label;
    NodeState<bool> toggle;
  };
  typedef reservation::Nodes<CardNode, 1, reservation::Nodes<StackNode, 1,
      reservation::Nodes<TextNode, 1, reservation::Nodes<ButtonNode, 1,
      reservation::Nodes<EditTextNode, 1> > > > > CardNodes;
  class CountedFlex : public LazyViewNode<CardProps>
  {
  public:
    explicit CountedFlex(const LazyViewProps<CardProps> &p)
        : LazyViewNode<CardProps>(p)
    {
    }
    virtual void composeWithContext(ComponentContext &context, ComposeEvent event)
    {
      if (event == COMPOSE_EVENT_UPDATE)
        ++record->updates;
      if (record->withoutPoll)
        StdCompositionBoundaryNodeBase<LazyViewProps<CardProps> >::composeWithContext(context, event);
      else LazyViewNode<CardProps>::composeWithContext(context, event);
    }
  };
  class Root : public BoundaryNodeFor<Root>
  {
  public:
    explicit Root(const BoundaryPropsFor<Root> &p)
        : BoundaryNodeFor<Root>(p),
          offset(),
          shown()
    {
      this->declareStates(2).state(this->offset, loka::core::Frame(0, 0, 0, 0)).state(this->shown, true);
    }
    virtual void declareBindings(BindingToken &token)
    {
      if (record->scrolling)
        token.watch(*record->viewport, this, &Root::copyOffset, true);
    }
    void copyOffset()
    {
      this->offset.set(loka::core::Frame(0, record->viewport->get().y, 0, 0));
    }
    NodeState<loka::core::Frame> offset;
    NodeState<bool> shown;
    virtual void composeNode(NodeComposition &c)
    {
      LazyViewProps<CardProps> p(*record->list, reservation::SeatNodes<CardNodes>(), record->maximum);
      p.layout = layout::FixedGrid(200, 20);
      p.viewport = record->viewport;
      if (record->second)
        c.declare(Column() << BoundaryDefinition<LazyViewProps<CardProps>, CountedFlex>(p)
                           << LazyColumn(*record->second, reservation::SeatNodes<CardNodes>(), 32).cells(200, 20).viewport(*record->otherViewport));
      else if (record->scrolling)
        c.declare(ScrollView(this->offset) << BoundaryDefinition<LazyViewProps<CardProps>, CountedFlex>(p));
      else if (record->parking)
        c.declare(Show(*this->shown.state()) << BoundaryDefinition<LazyViewProps<CardProps>, CountedFlex>(p));
      else
        c.declare(BoundaryDefinition<LazyViewProps<CardProps>, CountedFlex>(p));
    }
  };
  class PlacementPlatform : public NullScenePlatformController
  {
  public:
    PlacementPlatform()
        : watchedNumber(-1),
          projectedY(0)
    {
    }
    virtual bool prepareProjectedLayout(Node *node, LayoutState &state)
    {
      CardNode *card = this->watchedNumber >= 0 ? record->cards[this->watchedNumber] : 0;
      Node *row = card ? card->childrenHead() : 0;
      if (row && node == row->asNestable()->childrenHead())
        this->projectedY = state.y;
      return NullScenePlatformController::prepareProjectedLayout(node, state);
    }
    short watchedNumber;
    int projectedY;
  };
  struct Fixture
  {
    Fixture(unsigned short capacity = 32,
            int count = 20,
            bool empty = false,
            bool edit = false,
            bool two = false,
            bool scroll = false,
            bool park = false, unsigned maximum = 32, int coldRefusal = 0)
        : r(),
          tracker(),
          list(),
          second(),
          view(empty ? Frame() : Frame(0, 0, 200, 160)),
          otherView(Frame(0, 0, 200, 40)),
          platform(),
          scene((Boundary<Root>()))
    {
      record = &this->r;
      this->r.maximum = maximum;
      this->r.refuseClass = coldRefusal;
      this->r.list = &this->list;
      this->r.viewport = &this->view;
      this->r.editing = edit;
      this->r.scrolling = scroll;
      this->r.parking = park;
      this->tracker.addState(&this->view);
      this->tracker.addState(&this->otherView);
      LOKA_VERIFY(this->list.attach(&this->tracker, capacity) == ATTACH_OK);
      for (int i = 0; i < count; ++i)
        LOKA_VERIFY(this->list.insert(static_cast<unsigned short>(i), CardProps(String("old"), static_cast<short>(i)))
                    == EDIT_OK);
      if (two)
      {
        LOKA_VERIFY(this->second.attach(&this->tracker, capacity) == ATTACH_OK);
        for (int i = 0; i < count; ++i)
          LOKA_VERIFY(this->second.insert(static_cast<unsigned short>(i),
                                          CardProps(String("other"), static_cast<short>(i + 32)))
                      == EDIT_OK);
        this->r.second = &this->second;
        this->r.otherViewport = &this->otherView;
      }
      this->scene.mount(&this->platform);
      LayoutState bounds;
      bounds.width = 200;
      bounds.height = 400;
      this->platform.projectLayoutForTesting(loka::dsl::testing::SceneTestAccess::rootBoundary(this->scene), bounds);
      loka::dsl::testing::SceneTestAccess::updateAttached(this->scene, true);
      this->drain();
    }
    ~Fixture()
    {
      loka::dsl::testing::SceneTestAccess::unmount(this->scene);
      this->drain();
    }
    void drain()
    {
      this->scene.flushInvalidation();
      this->scene.flushInvalidation(); // The next clock boundary reclaims logical retirees silently.
      this->platform.drainNativeRetirements();
    }
    void page(int y)
    {
      {
        StateTrackerGuard guard(&this->tracker);
        this->view.set(Frame(0, y, 200, 160));
      }
      this->drain();
    }
    std::string dump() const
    {
      return loka::dsl::testing::OwnershipDump::dump(this->scene);
    }
    LazyViewNode<CardProps> *flex()
    {
      Node *node = loka::dsl::testing::SceneTestAccess::rootBoundary(this->scene)->childrenHead();
      if (this->r.scrolling || this->r.parking)
        node = node->asNestable()->childrenHead();
      return static_cast<LazyViewNode<CardProps> *>(node);
    }
    Record r;
    PushStateTracker tracker;
    ObservableList<CardProps> list, second;
    MutableState<Frame> view, otherView;
    PlacementPlatform platform;
    Scene scene;
  };
  void rows(Fixture &f, unsigned count)
  {
    {
      const bool ledgerFact = f.platform.ledger().size() == count;
      LOKA_VERIFY(ledgerFact);
    }
    for (size_t i = 0; i < f.platform.ledger().size(); ++i)
    {
      const bool ledgerFact = f.platform.ledger()[i].visible;
      LOKA_VERIFY(ledgerFact);
    }
  }
  TextNode *text(CardNode *card)
  {
    LOKA_VERIFY(card != 0);
    return card->childrenHead()->asNestable()->childrenHead()->asTextNode();
  }
} // namespace

void testLazyViewViewportCrossesTrackersInOneUpdate()
{
  Fixture f;
  rows(f, f.view.get().y == 0 ? 9 : 10);
  for (int i = 0; i < 20; ++i)
    LOKA_VERIFY(f.r.constructions[i] == (i < 9 ? 1 : 0));
  const int before = f.r.updates;
  f.page(200);
  rows(f, f.view.get().y == 0 ? 9 : 10);
  LOKA_VERIFY(f.r.updates == before + 1);
  for (int i = 0; i < 20; ++i)
  {
    LOKA_VERIFY(f.r.constructions[i] == (i < 9 || (i >= 9 && i < 19) ? 1 : 0));
    if (i < 9)
      LOKA_VERIFY(f.r.cards[i] == 0);
  }
}
void testLazyViewEmptyViewportWaitsForFirstWrite()
{
  Fixture f(32, 20, true);
  rows(f, 0);
  for (int i = 0; i < 20; ++i)
    LOKA_VERIFY(f.r.constructions[i] == 0);
  f.page(0);
  rows(f, f.view.get().y == 0 ? 9 : 10);
}
void testLazyViewTenPageFlipsKeepOwnershipBounded()
{
  Fixture f;
  // Both windows reclaim through the same generation clock.
  f.page(200);
  f.page(0);
  const std::string before = f.dump();
  LOKA_VERIFY(loka::dsl::testing::OwnershipDump::dumpSeatRuntime(*f.flex())
              == "seat parent=attached active=attached\n");
  for (int i = 0; i < 10; ++i)
  {
    f.page(200);
    rows(f, f.view.get().y == 0 ? 9 : 10);
    f.page(0);
    rows(f, f.view.get().y == 0 ? 9 : 10);
    LOKA_VERIFY(f.dump() == before);
    LOKA_VERIFY(loka::dsl::testing::OwnershipDump::dumpSeatRuntime(*f.flex())
                == "seat parent=attached active=attached\n");
  }
  printf("LazyView after ten round trips:\n%s", before.c_str());
}
void testLazyViewVisibleContentAppliesWithoutReconstruction()
{
  Fixture f;
  CardNode *changed = f.r.cards[3];
  LazyItem<CardProps> item(f.list, 3);
  LOKA_VERIFY(item.itemId() == f.list.at(3).id);
  LOKA_VERIFY(item.propsBase() == &f.list.at(3).value);
  CardNode *untouched = f.r.cards[4];
  untouched->toggle.set(true);
  f.drain();
  const int bindings = f.r.bindings[4];
  const unsigned long revision = f.list.revision().get().content;
  LOKA_VERIFY(f.list.update(f.list.at(3).id, CardProps(String("new"), 3)) == EDIT_OK);
  f.drain();
  LOKA_VERIFY(f.r.cards[3] == changed && f.r.constructions[3] == 1);
  LOKA_VERIFY(text(changed)->props.text_->get().equals(String("new")));
  LOKA_VERIFY(f.r.cards[4] == untouched && untouched->toggle.get());
  LOKA_VERIFY(f.r.bindings[4] == bindings);
  LOKA_VERIFY(f.list.revision().get().content == revision + 1);
}
void testLazyViewHiddenContentIsReadOnFirstMaterialization()
{
  Fixture f;
  LOKA_VERIFY(f.list.update(f.list.at(12).id, CardProps(String("hidden new"), 12)) == EDIT_OK);
  f.drain();
  LOKA_VERIFY(f.r.constructions[12] == 0);
  rows(f, f.view.get().y == 0 ? 9 : 10);
  f.page(200);
  LOKA_VERIFY(f.r.constructions[12] == 1);
  LOKA_VERIFY(text(f.r.cards[12])->props.text_->get().equals(String("hidden new")));
}
void testLazyViewStructureReplacesGeneration()
{
  Fixture f;
  Node *old = f.flex()->childrenHead();
  LOKA_VERIFY(f.list.insert(0, CardProps(String("inserted"), 20)) == EDIT_OK);
  f.drain();
  rows(f, f.view.get().y == 0 ? 9 : 10);
  LOKA_VERIFY(f.flex()->childrenHead() != old);
  for (int i = 0; i < 7; ++i)
    LOKA_VERIFY(f.r.constructions[i] == 2);
  LOKA_VERIFY(f.r.constructions[20] == 1);
  LOKA_VERIFY(f.list.remove(f.list.at(0).id) == EDIT_OK);
  f.drain();
  rows(f, f.view.get().y == 0 ? 9 : 10);
  LOKA_VERIFY(f.r.cards[20] == 0);
  LOKA_VERIFY(f.list.move(f.list.at(0).id, 19) == EDIT_OK);
  f.drain();
  rows(f, f.view.get().y == 0 ? 9 : 10);
  LOKA_VERIFY(f.r.cards[0] == 0 && f.r.cards[8] != 0);
  const std::string dump = f.dump();
  const size_t owner = dump.find("lazy-scope");
  LOKA_VERIFY(owner != std::string::npos && dump.find("lazy-scope", owner + 1) == std::string::npos);
}
void testLazyViewCapacityRefusal()
{
  Fixture f(300);
  rows(f, 0);
  LOKA_VERIFY(f.flex()->childrenHead() == 0);
}
void testLazyViewEditItemsRetireNativeIdentity()
{
  Fixture f(32, 20, false, true);
  rows(f, f.view.get().y == 0 ? 9 : 10);
  NullScenePlatformController::FakeControlHandle *old = f.platform.ledger()[3].handle;
  f.page(200);
  rows(f, f.view.get().y == 0 ? 9 : 10);
  LOKA_VERIFY(old->owner == 0 && old->hitOwner == 0);
  f.page(0);
  rows(f, f.view.get().y == 0 ? 9 : 10);
  LOKA_VERIFY(f.r.constructions[3] == 2);
}
void testLazyViewTwoListsStayIndependent()
{
  Fixture f(32, 20, false, false, true);
  rows(f, (f.view.get().y == 0 ? 9 : 10) + (f.otherView.get().y == 0 ? 3 : 4));
  CardNode *other = f.r.cards[32];
  f.page(200);
  rows(f, (f.view.get().y == 0 ? 9 : 10) + (f.otherView.get().y == 0 ? 3 : 4));
  LOKA_VERIFY(f.r.cards[32] == other && f.r.constructions[32] == 1);
  {
    StateTrackerGuard guard(&f.tracker);
    f.otherView.set(Frame(0, 200, 200, 40));
  }
  f.drain();
  rows(f, (f.view.get().y == 0 ? 9 : 10) + (f.otherView.get().y == 0 ? 3 : 4));
  LOKA_VERIFY(f.r.cards[32] == 0 && f.r.cards[42] != 0 && f.r.constructions[10] == 1);
}

void testLazyViewBatchRefreshesAllVisibleItems()
{
  Fixture f;
  ListOp<CardProps> ops[2] = {ListOp<CardProps>(UPDATE, f.list.at(2).id, 0, CardProps(String("two"), 2)),
                              ListOp<CardProps>(UPDATE, f.list.at(6).id, 0, CardProps(String("six"), 6))};
  ArrayListOpCursor<CardProps> cursor(ops, 2);
  LOKA_VERIFY(f.list.apply(cursor) == EDIT_OK);
  f.drain();
  LOKA_VERIFY(text(f.r.cards[2])->props.text_->get().equals(String("two")));
  LOKA_VERIFY(text(f.r.cards[6])->props.text_->get().equals(String("six")));
  LOKA_VERIFY(f.r.constructions[2] == 1 && f.r.constructions[6] == 1);
  CardNode *outgoing = f.r.cards[5];
  ops[0] = ListOp<CardProps>(REMOVE, f.list.at(0).id);
  ops[1] = ListOp<CardProps>(UPDATE, f.list.at(6).id, 0, CardProps(String("moved six"), 6));
  LOKA_VERIFY(f.list.apply(cursor) == EDIT_OK);
  LOKA_VERIFY(outgoing->props.number == 5);
  f.drain();
  rows(f, f.view.get().y == 0 ? 9 : 10);
  LOKA_VERIFY(text(f.r.cards[6])->props.text_->get().equals(String("moved six")));
  LOKA_VERIFY(f.r.constructions[6] == 2);
}

namespace
{
  CanvasNode *canvas(LazyViewNode<CardProps> *view)
  {
    Node *generation = view->childrenHead();
    return generation->asNestable()->childrenHead()->asNestable()->childrenHead()->asCanvasNode();
  }
  Frame extent(LazyViewNode<CardProps> *view)
  {
    Frame result;
    LOKA_VERIFY(layout::CanvasPlatformLayoutHandler::contentExtent(*canvas(view), result) == CANVAS_LAYOUT_READY);
    return result;
  }
} // namespace
void testLazyViewRefusedGenerationKeepsOldPresentation()
{
  loka::core::testing::failLokaAllocRaw("LazyView", "ItemIndex", 0);
  {
    Fixture f(32, 20, false, false, false, true);
    Node *old = f.flex()->childrenHead();
    const Frame full = extent(f.flex());
    f.platform.watchedNumber = 3;
    loka::core::testing::failLokaAllocRaw("LazyView", "ItemIndex", 1);
    {
      StateTrackerGuard guard(&f.tracker);
      f.view.set(Frame(0, 200, 200, 160));
    }
    f.scene.flushInvalidation();
    LOKA_VERIFY(f.flex()->childrenHead() == old);
    LOKA_VERIFY(loka::dsl::testing::SeatBuildRequestAccess::firstReservation(*f.flex())->request().retiring());
    LOKA_VERIFY(extent(f.flex()) == full && full.height == 400);
    LOKA_VERIFY(f.platform.projectedY == 60 - 200);
    rows(f, 9); // Stale old window at the moved offset, until a new flush retries.
    const Frame moved = f.view.get();
    f.platform.watchedNumber = 12;
    f.drain();
    LOKA_VERIFY(f.view.get() == moved && f.flex()->childrenHead() != old);
    LOKA_VERIFY(extent(f.flex()) == full);
    LOKA_VERIFY(f.platform.projectedY == 240 - 200);
    rows(f, 10);

    old = f.flex()->childrenHead();
    loka::core::testing::failLokaAllocRaw("LazyView", "ItemIndex", 1);
    for (int i = 0; i < 8; ++i)
      LOKA_VERIFY(f.list.remove(f.list.at(f.list.size() - 1).id) == EDIT_OK);
    f.scene.flushInvalidation();
    LOKA_VERIFY(f.flex()->childrenHead() == old);
    LOKA_VERIFY(extent(f.flex()).height == 240);
    f.drain();
    LOKA_VERIFY(f.view.get() == moved && f.flex()->childrenHead() != old);
    LOKA_VERIFY(extent(f.flex()).height == 240);
    rows(f, 3);
  }
  LOKA_VERIFY(loka::core::testing::lokaAllocRawLive() == 0);
  loka::core::testing::allowLokaAllocRaw();
}
void testLazyViewUnmountCancelsForeignWatches()
{
  Fixture f;
  loka::dsl::testing::SceneTestAccess::unmount(f.scene);
  f.drain();
  const int before = f.r.updates;
  f.page(200);
  LOKA_VERIFY(f.list.update(f.list.at(12).id, CardProps(String("after unmount"), 12)) == EDIT_OK);
  f.drain();
  LOKA_VERIFY(f.r.updates == before);
  rows(f, 0);
}

void testLazyViewRowWrapUsesHalfOpenCells()
{
  Fixture f(32, 20, true);
  loka::dsl::testing::SceneTestAccess::unmount(f.scene);
  f.drain();
  {
    StateTrackerGuard guard(&f.tracker);
    f.view.set(Frame(0, 0, 80, 80));
  }
  Scene scene(LazyRow(f.list, reservation::SeatNodes<CardNodes>(), 32).cells(40, 40).wrap(2).viewport(f.view));
  scene.mount(&f.platform);
  loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);
  rows(f, 6);
  for (int i = 0; i < 20; ++i)
    LOKA_VERIFY(f.r.constructions[i] == (i < 6 ? 1 : 0));
  {
    StateTrackerGuard guard(&f.tracker);
    f.view.set(Frame(120, 0, 80, 80));
  }
  scene.flushInvalidation();
  scene.flushInvalidation();
  f.platform.drainNativeRetirements();
  rows(f, 8);
  for (int i = 0; i < 20; ++i)
  {
    LOKA_VERIFY((f.r.cards[i] != 0) == (i >= 4 && i < 12));
    LOKA_VERIFY(f.r.constructions[i] == ((i < 6 ? 1 : 0) + (i >= 4 && i < 12 ? 1 : 0)));
  }
  // Moving viewport.x selects only; horizontal scrolling awaits a ScrollView X offset.
  class AbsoluteRowPlacement : public IPlatformLayoutTraversal
  {
  public:
    virtual int layoutChild(Node *child, const LayoutState &state)
    {
      const int index = static_cast<CardNode *>(child)->props.number;
      LOKA_VERIFY(index >= 4 && index < 12);
      LOKA_VERIFY(state.x == (index / 2) * 40);
      LOKA_VERIFY(state.y == (index % 2) * 40);
      return state.y + state.height;
    }
    virtual void setLayoutResultY(short) {}
    virtual short layoutResultY() const { return 0; }
  } placement;
  LazyViewNode<CardProps> *view = static_cast<LazyViewNode<CardProps> *>(
      loka::dsl::testing::SceneTestAccess::rootBoundary(scene));
  LOKA_VERIFY(canvas(view)->childrenCount() == 8);
  LayoutState bounds;
  bounds.width = 80;
  bounds.height = 80;
  layout::CanvasPlatformLayoutHandler handler;
  handler.layoutNode(canvas(view), bounds, &placement);
  {
    StateTrackerGuard guard(&f.tracker);
    f.view.set(Frame(INT_MIN, 0, INT_MAX, 80));
  }
  scene.flushInvalidation();
  scene.flushInvalidation();
  f.platform.drainNativeRetirements();
  rows(f, 0);
}

#ifdef LOKA_LAZYFLEX_ALLOC_CENSUS
#include "support/AllocCensus.hpp"

void allocpin::RunLazyViewPageFlipAllocPin()
{
  Fixture f;
  f.page(200);
  f.page(0);
  for (int capture = 0; capture < 2; ++capture)
  {
    BeginCapture(capture);
#ifdef LOKA_UPSTREAM_GAUGE_PIN
    const loka::core::UpstreamGauge upstreamBefore = upstreamPinSnapshot();
#endif
    f.page(capture == 0 ? 200 : 0);
    EndCapture();
#ifdef LOKA_UPSTREAM_GAUGE_PIN
    // Both attached windows were warmed above. This stronger whole-fixture
    // zero also covers the outer generation's node-domain upstream acquisitions.
    upstreamPinCheck("LazyView", upstreamBefore, upstreamPinSnapshot(), 0, 0);
#endif
    rows(f, f.view.get().y == 0 ? 9 : 10);
#ifdef LOKA_UPSTREAM_GAUGE_PIN
    const bool settled = !f.scene.hasPendingInvalidation();
    LOKA_VERIFY(settled);
    for (int i = 0; i < 20; ++i)
      LOKA_VERIFY((f.r.cards[i] != 0) == (capture == 0 ? (i >= 9 && i < 19) : i < 9));
#endif
  }
  std::fprintf(stderr,
               "LazyView warmed margin-window page flip: %lu / %lu allocations\n",
               CaptureAllocCount(0),
               CaptureAllocCount(1));
  // 43/39 node allocations disappear into the seat partition. Preserve-first
  // ATTACH also reuses the still-live viewport observation row: one fewer
  // BoundaryObservedState allocation than the 289/266 arithmetic forecast.
  LOKA_VERIFY(CaptureAllocCount(0) <= 288);
  LOKA_VERIFY(CaptureAllocCount(1) <= 265);
}
#endif

void testLazyViewContentMapsWindowLocally()
{
  Fixture f;
  f.page(200);
  Node *generation = f.flex()->childrenHead();
  CardNode *changed = f.r.cards[12];
  const int before = f.r.bindings[10];
  LOKA_VERIFY(f.list.update(f.list.at(12).id, CardProps(String("window twelve"), 12)) == EDIT_OK);
  f.drain();
  LOKA_VERIFY(f.flex()->childrenHead() == generation && f.r.cards[12] == changed);
  LOKA_VERIFY(text(changed)->props.text_->get().equals(String("window twelve")));
  LOKA_VERIFY(f.r.bindings[10] == before);
  LOKA_VERIFY(f.list.update(f.list.at(2).id, CardProps(String("outside"), 2)) == EDIT_OK);
  f.drain();
  LOKA_VERIFY(f.r.cards[2] == 0 && f.r.bindings[10] == before);
  LOKA_VERIFY(f.list.update(f.list.at(19).id, CardProps(String("after window"), 19)) == EDIT_OK);
  f.drain();
  LOKA_VERIFY(f.r.cards[19] == 0 && f.r.bindings[10] == before);
}

void testLazyViewPropsReselectAndReplaceBorrow()
{
  Fixture f;
  Node *generation = f.flex()->childrenHead();
  LazyViewProps<CardProps> props = f.flex()->props;
  props.layout.cellWidth = 180;
  NodeDefinition<LazyViewProps<CardProps>, LazyViewNode<CardProps> > definition(props);
  LOKA_VERIFY(definition.applyPropsToNode(f.flex()));
  f.drain();
  LOKA_VERIFY(f.flex()->childrenHead() == generation);
  LOKA_VERIFY(extent(f.flex()).width == 180);
  LOKA_VERIFY(canvas(f.flex())->placement().policy.place(3).width == 180);
  definition.props.layout.margin = 0;
  LOKA_VERIFY(definition.applyPropsToNode(f.flex()));
  f.drain();
  LOKA_VERIFY(f.flex()->childrenHead() != generation);
  rows(f, 8);
  definition.props.viewport = &f.otherView;
  LOKA_VERIFY(definition.applyPropsToNode(f.flex()));
  f.drain();
  rows(f, 2);
  generation = f.flex()->childrenHead();
  f.page(200); // Old borrowed viewport must have been unbound.
  LOKA_VERIFY(f.flex()->childrenHead() == generation);
  rows(f, 2);
  {
    StateTrackerGuard guard(&f.tracker);
    f.otherView.set(Frame(0, 200, 200, 40));
  }
  f.drain();
  LOKA_VERIFY(f.flex()->childrenHead() != generation && f.r.cards[10] != 0);
  rows(f, 2);
}

void testLazyViewParkedReattachReselects()
{
  Fixture f(32, 20, false, false, false, false, true);
  Root *root = static_cast<Root *>(loka::dsl::testing::SceneTestAccess::rootBoundary(f.scene));
  Node *old = f.flex()->childrenHead();
  root->shown.set(false);
  f.drain();
  {
    StateTrackerGuard guard(&f.tracker);
    f.view.set(Frame(0, 200, 200, 160));
  }
  LOKA_VERIFY(f.list.move(f.list.at(0).id, 19) == EDIT_OK);
  f.drain();
  root->shown.set(true);
  f.drain();
  LOKA_VERIFY(f.flex()->childrenHead() != old);
  LOKA_VERIFY(f.r.cards[10] != 0 && f.r.cards[1] == 0);
  rows(f, 10);
}

namespace
{
  void canceledWindowRefresh(bool refuse)
  {
    loka::core::testing::failLokaAllocRaw("LazyView", "ItemIndex", 0);
    {
      Fixture f;
      Node *committed = f.flex()->childrenHead();
      if (refuse)
        loka::core::testing::failLokaAllocRaw("LazyView", "ItemIndex", 1);
      {
        StateTrackerGuard guard(&f.tracker);
        f.view.set(Frame(0, 200, 200, 160));
      }
      if (refuse)
        f.scene.flushInvalidation();
      LOKA_VERIFY(f.flex()->childrenHead() == committed);
      LOKA_VERIFY(f.list.update(f.list.at(3).id, CardProps(String("pending three"), 3)) == EDIT_OK);
      LOKA_VERIFY(f.list.update(f.list.at(6).id, CardProps(String("pending six"), 6)) == EDIT_OK);
      {
        StateTrackerGuard guard(&f.tracker);
        f.view.set(Frame(0, 0, 200, 160));
      }
      f.drain();
      LOKA_VERIFY(f.flex()->childrenHead() == committed);
      LOKA_VERIFY(text(f.r.cards[3])->props.text_->get().equals(String("pending three")));
      LOKA_VERIFY(text(f.r.cards[6])->props.text_->get().equals(String("pending six")));
      LOKA_VERIFY(f.r.constructions[3] == 1 && f.r.constructions[6] == 1);
    }
    LOKA_VERIFY(loka::core::testing::lokaAllocRawLive() == 0);
    loka::core::testing::allowLokaAllocRaw();
  }
} // namespace
void testLazyViewCanceledQueuedWindowRefreshesContent()
{
  canceledWindowRefresh(false);
}
void testLazyViewCanceledRefusedWindowRefreshesContent()
{
  canceledWindowRefresh(true);
}

namespace
{
  typedef loka::app::scene::detail::SeatReservation Reservation;
  typedef loka::dsl::testing::SeatBuildRequestAccess SeatAccess;
  const Reservation &seat(Fixture &f)
  { return *SeatAccess::firstReservation(*f.flex()); }
  void select(Fixture &f, int y, int height = 160)
  { StateTrackerGuard guard(&f.tracker); f.view.set(Frame(0, y, 200, height)); }
  void provenance(Node *node, scene::detail::NodePartition *bank)
  {
    LOKA_VERIFY(node->partitionOwner() == bank);
    INestable *children = node->asNestable();
    for (Node *child = children ? children->childrenHead() : 0; child; child = child->nextInComposition)
      provenance(child, bank);
  }
}
void testLazyViewPartitionReuseAndQuietDrain()
{
  Fixture f;
  const Reservation &reservation = seat(f);
  Node *a = f.flex()->childrenHead();
  provenance(a, &reservation.partition());
  std::vector<int> events;
  f.r.events = &events;
  SeatAccess::holdRetirement(*f.flex(), true);
  select(f, 200);
  f.scene.flushInvalidation();
  Node *b = f.flex()->childrenHead();
  LOKA_VERIFY(a != b && SeatAccess::returning(seat(f).request()) == a);
  provenance(a, &reservation.partition()); // Detached storage is still occupied.
  provenance(b, &reservation.partition());
  LOKA_VERIFY(a->lifecycleFact() == NODE_FACT_RETIRED);
  LOKA_VERIFY(events.size() == 19 && events[0] == 10 && events[9] == 19 && events[10] == -1);
  const int updates = f.r.updates;
  SeatAccess::holdRetirement(*f.flex(), false);
  f.drain();
  LOKA_VERIFY(!reservation.request().retiring() && !reservation.request().waiting());
  LOKA_VERIFY(f.r.updates == updates && !f.scene.hasPendingInvalidation());
  f.page(0);
  LOKA_VERIFY(f.flex()->childrenHead() == a && &seat(f) == &reservation);
  f.page(200);
  LOKA_VERIFY(f.flex()->childrenHead() == b && &seat(f) == &reservation);
  provenance(f.flex()->childrenHead(), &reservation.partition());
  f.r.events = 0;
}
void testLazyViewWaitingLatestKeyAndDrainWake()
{
  Fixture f;
  f.r.withoutPoll = true; // Isolate the drain-tail wake from LazyView's after-run poll.
  SeatAccess::holdRetirement(*f.flex(), true);
  select(f, 200, 20);
  f.scene.flushInvalidation();
  Node *installed = f.flex()->childrenHead();
  LOKA_VERIFY(seat(f).request().retiring());
  select(f, 300, 20);
  f.scene.flushInvalidation();
  select(f, 340, 20);
  f.scene.flushInvalidation();
  LOKA_VERIFY(f.flex()->childrenHead() == installed && f.r.constructions[17] == 0);
  LOKA_VERIFY(seat(f).request().waiting() && !f.scene.hasPendingInvalidation());
  SeatAccess::holdRetirement(*f.flex(), false);
  f.flex()->drainRetiredSubtreesAtNextTrackerRun();
  LOKA_VERIFY(!seat(f).request().retiring() && f.scene.hasPendingInvalidation());
  f.drain(); // No State event after return: latest key, not the first blocked key.
  LOKA_VERIFY(f.flex()->childrenHead() != installed && f.r.cards[17] && !f.r.cards[14]);
}
void testLazyViewLateRefusalKeepsBindingsAndReturnsSlots()
{
  Fixture f;
  Node *old = f.flex()->childrenHead();
  CardNode *card = f.r.cards[3];
  SeatAccess::holdRetirement(*f.flex(), true);
  f.r.refuseAttach = 12;
  select(f, 200);
  f.scene.flushInvalidation();
  LOKA_VERIFY(f.flex()->childrenHead() == old && seat(f).request().retiring());
  { StateTrackerGuard guard(card->label.dangerouslyTracker()); card->label.set(String("still live")); }
  LOKA_VERIFY(text(card)->props.text_->get().equals(String("still live")));
  f.scene.flushInvalidation();
  LOKA_VERIFY(f.flex()->childrenHead() == old);
  SeatAccess::holdRetirement(*f.flex(), false);
  f.drain();
  LOKA_VERIFY(f.flex()->childrenHead() != old && !seat(f).request().retiring());
  f.page(0);
  LOKA_VERIFY(f.flex()->childrenHead() == old); // Same bank slot, after complete return.
}
namespace
{
  void maximumRefusal()
  {
    Fixture f(32, 20, false, false, false, false, false, 9);
    Node *old = f.flex()->childrenHead(); // Exact M admitted on the first page.
    State<LazyViewKey> *key = static_cast<State<LazyViewKey> *>(SeatAccess::firstCondition(*f.flex()));
    select(f, 200); // Publish ten rows; declaration refuses before constructing.
    f.drain();
    LOKA_VERIFY(f.flex()->childrenHead() == old && f.r.constructions[9] == 0 && key->get().window.count == 10);
    LOKA_VERIFY(!seat(f).request().waiting() && !f.scene.hasPendingInvalidation());
    const int updates = f.r.updates;
    for (int turn = 0; turn != 5; ++turn) f.scene.flushInvalidation();
    LOKA_VERIFY(f.r.updates == updates && !f.scene.hasPendingInvalidation());
    select(f, 220, 120);
    f.drain();
    LOKA_VERIFY(f.flex()->childrenHead() != old);
  }
  void oversizedStructureKeepsIdentity()
  {
    Fixture f(32, 20, false, false, false, false, false, 9);
    Node *installed = f.flex()->childrenHead();
    CardNode *retained[9];
    for (int i = 0; i != 9; ++i) retained[i] = f.r.cards[i];
    select(f, 0, 400);
    f.drain();
    { StateTrackerGuard guard(&f.tracker);
      LOKA_VERIFY(f.list.remove(f.list.at(0).id) == EDIT_OK); }
    f.drain();
    LOKA_VERIFY(f.flex()->childrenHead() == installed);
    for (int i = 0; i != 9; ++i)
      LOKA_VERIFY(retained[i] && retained[i]->props.number == i);
    LOKA_VERIFY(!seat(f).request().waiting() && !f.scene.hasPendingInvalidation());
  }
  void classRefusal(int kind)
  {
    Fixture f(32, 20, false, false, false, false, false, 10);
    Node *old = f.flex()->childrenHead();
    f.r.refuseClass = kind;
    select(f, 200);
    f.scene.flushInvalidation();
    LOKA_VERIFY(f.flex()->childrenHead() == old && seat(f).request().retiring());
    Node *discarded = SeatAccess::returning(seat(f).request());
    LOKA_VERIFY(discarded != old && discarded->nodeTypeKey() == NodeTypeToken<LazyScopeNode>());
    const int constructions = f.r.constructions[10];
    f.drain();
    LOKA_VERIFY(!seat(f).request().waiting() && !seat(f).request().retiring());
    for (int i = 0; i != 5; ++i) f.scene.flushInvalidation();
    LOKA_VERIFY(!f.scene.hasPendingInvalidation() && f.r.constructions[10] == constructions);
    f.r.refuseClass = false;
    f.flex()->markViewDirty(NODE_DIRTY_LAYOUT);
    f.drain();
    LOKA_VERIFY(f.flex()->childrenHead() == old); // Warm refusal waits for a changed key.
    select(f, 220);
    f.drain();
    LOKA_VERIFY(f.flex()->childrenHead() != old);
  }
  Fixture *redirectedFixture;
  void redirectDuringAttach(int number)
  {
    if (number != 9) return;
    redirectedFixture->r.onAttach = 0;
    select(*redirectedFixture, 300, 20);
  }
  void newerKeyDuringRefusal()
  {
    Fixture f;
    redirectedFixture = &f;
    f.r.onAttach = &redirectDuringAttach;
    f.r.refuseClass = 3;
    SeatAccess::holdRetirement(*f.flex(), true);
    select(f, 200, 20);
    f.scene.flushInvalidation();
    LOKA_VERIFY(seat(f).request().waiting() && seat(f).request().retiring());
    SeatAccess::holdRetirement(*f.flex(), false);
    f.drain();
    LOKA_VERIFY(f.r.cards[14] && !seat(f).request().waiting());
    redirectedFixture = 0;
  }
  void coldClassRefusal()
  {
    Fixture f(32, 20, false, false, false, false, false, 32, 1);
    const int before = f.r.constructions[0];
    LOKA_VERIFY(before > 0 && !f.flex()->childrenHead());
    for (int turn = 0; turn != 5; ++turn) f.scene.flushInvalidation();
    LOKA_VERIFY(f.r.constructions[0] == before && !f.scene.hasPendingInvalidation());
    // Cold replay intentionally mirrors Keyed: an external update may retry.
    f.flex()->markViewDirty(NODE_DIRTY_LAYOUT);
    f.drain();
    LOKA_VERIFY(f.r.constructions[0] > before);
  }
  void missingClass() { classRefusal(1); }
  void exhaustedClass() { classRefusal(2); }
  void directMaximumRefusal()
  {
    loka::core::testing::failLokaAllocRaw("LazyView", "ItemIndex", 0);
    {
      Fixture f;
      const layout::LazyWindow oversized = {0, 33};
      MutableState<LazyViewKey> key(LazyViewKey(oversized, 0));
      typedef lazy_view_detail::WindowReservation<CardProps> Recipe;
      LazyScopeDefinition<LazyViewKey, LazyGenerationNode<CardProps>, Recipe> direct =
          LazyScope(key, LazyGenerationProps<CardProps>(&f.flex()->props, &key, &f.view),
                    Recipe(*f.flex(), f.flex()->props.recipe));
      ComponentContext context;
      context.setBoundary(f.flex());
      context.setOwner(f.flex());
      context.setStateOwner(f.flex());
      LOKA_VERIFY(direct.prepareSeatReservation());
      loka::core::testing::failLokaAllocRaw("LazyView", "ItemIndex", 1);
      OwnedDef<BranchSeatDeclaration> refused(direct.declareBranchCandidate(context));
      LOKA_VERIFY(!refused.isSet() && loka::core::testing::lokaAllocRawAttempts() == 0);
    }
    LOKA_VERIFY(loka::core::testing::lokaAllocRawLive() == 0);
    loka::core::testing::allowLokaAllocRaw();
  }
  void expectRefusal(void (*probe)(), const char *diagnostic)
  {
#ifdef NDEBUG
    (void)diagnostic;
    probe();
#elif defined(__linux__)
    int output[2];
    LOKA_VERIFY(pipe(output) == 0);
    const pid_t child = fork();
    LOKA_VERIFY(child >= 0);
    if (!child)
    {
      close(output[0]);
      LOKA_VERIFY(dup2(output[1], STDERR_FILENO) >= 0);
      close(output[1]); probe(); _exit(0);
    }
    close(output[1]);
    std::string message;
    char buffer[512];
    ssize_t count;
    while ((count = read(output[0], buffer, sizeof(buffer))) > 0) message.append(buffer, count);
    close(output[0]);
    int status = 0;
    LOKA_VERIFY(waitpid(child, &status, 0) == child);
    LOKA_VERIFY(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
    LOKA_VERIFY(message.find(diagnostic) != std::string::npos);
#else
    (void)probe; (void)diagnostic;
    std::fprintf(stderr, "[skip] Debug refusal subprocess needs fork\n");
#endif
  }
}
void testLazyViewPendingWindowRecoversFromMaximumRefusal()
{
  Fixture f(32, 20, false, false, false, false, false, 9);
  SeatAccess::holdRetirement(*f.flex(), true);
  select(f, 200, 20);
  f.scene.flushInvalidation();
  Node *installed = f.flex()->childrenHead();
  LOKA_VERIFY(seat(f).request().retiring());
  select(f, 300, 20); // C waits for the old root's return.
  f.scene.flushInvalidation();
  select(f, 0, 400); // Oversized fact must not erase C's next notification.
  f.scene.flushInvalidation();
  select(f, 300, 20);
  f.scene.flushInvalidation();
  LOKA_VERIFY(f.flex()->childrenHead() == installed);
  SeatAccess::holdRetirement(*f.flex(), false);
  f.drain();
  LOKA_VERIFY(f.flex()->childrenHead() != installed && f.r.cards[14]);
  LOKA_VERIFY(!seat(f).request().waiting() && !f.scene.hasPendingInvalidation());
}
void testLazyViewOversizedStructureKeepsIdentity()
{ expectRefusal(&oversizedStructureKeepsIdentity, "LazyView window exceeds"); }
void testLazyViewMaximumRefusalSettles() { expectRefusal(&maximumRefusal, "LazyView window exceeds"); expectRefusal(&directMaximumRefusal, "LazyView window exceeds"); }
void testLazyViewClassRefusalSettles() { expectRefusal(&missingClass, "seat node declaration incomplete"); expectRefusal(&exhaustedClass, "seat node declaration incomplete"); expectRefusal(&newerKeyDuringRefusal, "seat node declaration incomplete"); expectRefusal(&coldClassRefusal, "seat node declaration incomplete"); }
void testLazyViewRecipeCompatibilityAndClones()
{
  Fixture f;
  const Reservation *bank = &seat(f);
  lazy_view_detail::WindowReservation<CardProps> instruction(*f.flex(), f.flex()->props.recipe);
  LOKA_VERIFY(instruction.prepare());
  lazy_view_detail::WindowReservation<CardProps> copied(instruction);
  LOKA_VERIFY(!copied.reservation() && copied.prepare());
  LOKA_VERIFY(copied.reservation() != instruction.reservation());
  for (int variant = 0; variant != 2; ++variant)
  {
    LazyViewProps<CardProps> p(f.list, reservation::SeatNodes<CardNodes>(), 31);
    if (variant) p = LazyViewProps<CardProps>(f.list, reservation::SeatNodes<reservation::Nodes<CardNode, 1> >(), 32);
    NodeDefinition<LazyViewProps<CardProps>, LazyViewNode<CardProps> > definition(p);
    OwnedDef<NodeDefinitionBase> clone(definition.clone());
    LOKA_VERIFY(clone.isSet() && !definition.isCompatibleWithNode(f.flex()));
    LOKA_VERIFY(!clone->isCompatibleWithNode(f.flex()) && !clone->applyPropsToNode(f.flex()));
    LOKA_VERIFY(&seat(f) == bank && !(f.flex()->props.recipe == p.recipe));
  }
  LazyView<CardProps> valid(f.list, f.flex()->props.layout, reservation::SeatNodes<CardNodes>(), 32);
  OwnedDef<NodeDefinitionBase> clone(valid.clone());
  LOKA_VERIFY(clone.isSet() && clone->isCompatibleWithNode(f.flex()));
}
#include "testing/core/StateTrackerTestAccess.hpp"
void testLazyViewPartitionReuseWithinTurn()
{
  Fixture f;
  Operation turn;
  LOKA_VERIFY(Operation::hasActive());
  Node *a = f.flex()->childrenHead();
  scene::detail::NodePartition *bank = a->partitionOwner();
  LOKA_VERIFY(bank && bank == &seat(f).partition());
  f.page(200); // Reclaim A while the rail turn remains open.
  f.page(0);
  LOKA_VERIFY(f.flex()->childrenHead() == a && a->partitionOwner() == bank);
  // #1079 (seat writes join the active clock) landed first, so this pin carries
  // the enrolment half: a seat write in the generation rebuilt into the reused
  // slot enrolls its live ledger, which the clock settles and releases at close.
  CardNode *card = f.r.cards[0];
  LOKA_VERIFY(card != 0);
  card->toggle.set(true);
  StateTracker *ledger = card->toggle.state()->trackerOwner();
  LOKA_VERIFY(ledger != 0 && ledger->asPushTracker() != 0);
  LOKA_VERIFY(loka::core::testing::PushStateTrackerTestAccess::depth(*ledger->asPushTracker()) == 1);
  LOKA_VERIFY(Operation::openActive(ledger) == OPEN_ALREADY_OPEN);
  LOKA_VERIFY(turn.close().status == OPERATION_SETTLED);
  LOKA_VERIFY(card->toggle.get() && ledger->phase() == TRACKER_IDLE);
  LOKA_VERIFY(loka::core::testing::PushStateTrackerTestAccess::depth(*ledger->asPushTracker()) == 0);
}

void testLazyViewSeatGuardOrders()
{
  typedef loka::core::testing::PushStateTrackerTestAccess Access;
  for (unsigned clocked = 0; clocked != 2; ++clocked)
    for (unsigned seatFirst = 0; seatFirst != 2; ++seatFirst)
    {
      Access::InvalidationProbe commits;
      Fixture f;
      LazyViewNode<CardProps> *flex = f.flex();
      PushStateTracker &owner = *flex->asStateOwner()->tracker()->asPushTracker();
      NodeState<int> earlier;
      StateBatchBase::CreateImmediateState(flex->asStateOwner(), earlier, 0);
      loka::core::ScopedPtr<Operation> turn(clocked ? new Operation : 0);
      if (seatFirst) earlier.set(1);
      commits.install(owner);
      {
        StateTrackerGuard viewportGuard(&f.tracker);
        f.view.set(Frame(0, 200, 200, 160));
      }
      // C2: either write order joins; without a clock selection still commits here.
      LOKA_VERIFY(Access::depth(owner) == clocked);
      LOKA_VERIFY(commits.calls == (clocked ? 0 : 1));
      if (turn.get()) turn->close();
      LOKA_VERIFY(commits.calls == 1);
      f.drain();
      LOKA_VERIFY(f.r.cards[10] && !f.r.cards[0]);
    }
}
