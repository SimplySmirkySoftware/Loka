#include "DocumentWindowSetTests.hpp"
#include "app/core/AppComposition.hpp"
#include "app/core/WindowSeat.hpp"
#include "app/nodes/boundary/StdComposition.hpp"
#include "app/nodes/nestable/Show.hpp"
#include "platform/null/NullApp.hpp"
#include "platform/null/NullPlatformContext.hpp"
#include "support/WindowAdmissionTestApp.hpp"
#include "support/TestVerify.hpp"

namespace
{
  using namespace loka::core;
  using namespace loka::app;
  using namespace loka::app::scene;

  struct Document
  {
    int value;
    explicit Document(int v = 0) : value(v) {}
  };
  class DocumentRoot;
  struct RootFacts
  {
    int compositions, detaches;
    bool detachedInsideTurn;
    DocumentRoster<Document, 4> *roster;
    DocumentRoot *root;
    bool opened;
    ItemId openedId;
    int openerCompositions;
    RootFacts()
        : compositions(0), detaches(0), detachedInsideTurn(false),
          roster(0), root(0), opened(false), openerCompositions(0) {}
  };
  struct DocumentProps : NodePropsBase<DocumentProps>
  {
    typedef DocumentRoot NodeType;
    struct TypeTag {};
    RootFacts *facts;
    explicit DocumentProps(RootFacts &f) : facts(&f) {}
    bool operator<(const PropsBase &rhs) const
    {
      if (rhs.propsTypeId() != this->propsTypeId())
        return this->propsTypeId() < rhs.propsTypeId();
      return std::less<RootFacts *>()(this->facts, static_cast<const DocumentProps &>(rhs).facts);
    }
  };
  class OpenDocumentRoot;
  struct OpenDocumentProps : NodePropsBase<OpenDocumentProps>
  {
    typedef OpenDocumentRoot NodeType;
    struct TypeTag {};
    RootFacts *facts;
    explicit OpenDocumentProps(RootFacts &f) : facts(&f) {}
    bool operator<(const PropsBase &rhs) const
    {
      if (rhs.propsTypeId() != this->propsTypeId())
        return this->propsTypeId() < rhs.propsTypeId();
      return std::less<RootFacts *>()(this->facts, static_cast<const OpenDocumentProps &>(rhs).facts);
    }
  };
  /** Mounted by a state-driven Show during Scene run; boundaries compose once. */
  class OpenDocumentRoot : public StdCompositionBoundaryNodeBase<OpenDocumentProps>
  {
  public:
    explicit OpenDocumentRoot(const OpenDocumentProps &props)
        : StdCompositionBoundaryNodeBase<OpenDocumentProps>(props) {}
    virtual void composeNode(NodeComposition &)
    {
      RootFacts &facts = *this->props.facts;
      ++facts.openerCompositions;
      if (!facts.opened)
      {
        facts.opened = true;
        facts.openedId = facts.roster->open(Document(1));
        LOKA_VERIFY(!facts.openedId.isNone());
      }
    }
  };
  class DocumentRoot : public StdCompositionBoundaryNodeBase<DocumentProps>
  {
  public:
    explicit DocumentRoot(const DocumentProps &props) : StdCompositionBoundaryNodeBase<DocumentProps>(props)
    {
      this->props.facts->root = this;
      if (this->props.facts->roster)
        this->state(this->showOpener_, false);
    }
    void openDuringSceneRun() { this->showOpener_.set(true); }
    virtual void composeNode(NodeComposition &composition)
    {
      ++this->props.facts->compositions;
      if (this->props.facts->roster)
        composition.declare(Show(*this->showOpener_.state())
            << BoundaryDefinition<OpenDocumentProps, OpenDocumentRoot>(OpenDocumentProps(*this->props.facts)));
    }
    virtual void detachNode(NodeComposition &)
    {
      ++this->props.facts->detaches;
      this->props.facts->detachedInsideTurn = Operation::hasActive();
    }
  private:
    NodeState<bool> showOpener_;
  };
  class TrackedWindow : public NullWindow
  {
  public:
    TrackedWindow(PlatformContext *context, const WindowProps &props, bool &destroyed)
        : NullWindow(context, props), destroyed_(destroyed) {}
    virtual ~TrackedWindow() { this->destroyed_ = true; }
  private:
    bool &destroyed_;
  };
  class TrackingContext : public NullPlatformContext
  {
  public:
    int creates;
    bool destroyed[16];
    /** One refusal of the rail's own createWindow (the third 0 path of
        WindowDefinition::create). A clone-count fixture would depend on how
        many times WindowProps is copied on the way from the factory to
        create, which copy elision decides per compiler (MSVC Debug copies
        once more than GCC), so the refusal is armed on the context instead. */
    bool refuseNextCreate;
    TrackingContext() : creates(0), refuseNextCreate(false)
    {
      for (int i = 0; i < 16; ++i)
        this->destroyed[i] = false;
    }
    virtual Window *createWindow(const WindowProps &props)
    {
      if (this->refuseNextCreate)
      {
        this->refuseNextCreate = false;
        return 0;
      }
      LOKA_VERIFY(this->creates < 16);
      return new TrackedWindow(this, props, this->destroyed[this->creates++]);
    }
  };
  class Config : public AppConfigurable
  {
  public:
    DocumentRoster<Document, 4> roster;
    RootFacts roots[4];
    int factories;
    ItemId factoryIds[16];
    int refuseValue;
    bool reenter;
    bool staticFirst;
    ItemId initial;
    ItemId reentrant;
    ItemId closeDuringFactory;
    explicit Config(PlatformContext &context, bool bootstrap = false)
        : AppConfigurable(&context), factories(0), refuseValue(-1), reenter(false), staticFirst(bootstrap)
    {
      if (bootstrap)
        this->initial = this->roster.open(Document(0));
    }
    virtual void compose(AppComposition &c)
    {
      if (this->staticFirst)
        c << WindowDef(WindowProps());
      c << DocumentWindows(this->roster, &Config::documentWindow, this);
    }
    static WindowProps documentWindow(ItemId id, const Document &entry, void *data)
    {
      Config &config = *static_cast<Config *>(data);
      LOKA_VERIFY(config.factories < 16);
      config.factoryIds[config.factories++] = id;
      const int value = entry.value;
      if (config.reenter)
      {
        config.reenter = false;
        config.reentrant = config.roster.open(Document(2));
        LOKA_VERIFY(!config.reentrant.isNone());
      }
      if (!config.closeDuringFactory.isNone())
      {
        const ItemId id = config.closeDuringFactory;
        config.closeDuringFactory = ItemId::none();
        LOKA_VERIFY(config.roster.close(id) == EDIT_OK);
      }
      WindowProps props;
      props.scene(new Scene(BoundaryDefinition<DocumentProps, DocumentRoot>(DocumentProps(config.roots[value]))));
      if (value == config.refuseValue)
        static_cast<TrackingContext *>(config.getPlatformContext())->refuseNextCreate = true;
      return props;
    }
  };
  class TestApp : public NullApp
  {
  public:
    explicit TestApp(Config &config) : NullApp(&config) {}
    using App::admitAndApplyWindows;
    using App::reclaimWindows;
    using App::hasPendingWindowAdmission;
    using App::isWindowClosePending;
    AppComponentGroup &group() { return *this->group_; }
    NullWindow *window(ItemId id) { return static_cast<NullWindow *>(this->group_->find(id)); }
    void tail() { RunWindowAdmissionOperation(*this); }
  };
  class RefusingNullApp : public TestApp
  {
  public:
    Window *refused;
    int refuseAt;
    explicit RefusingNullApp(Config &config) : TestApp(config), refused(0), refuseAt(2) {}
  protected:
    virtual bool windowAdopted(Window *window)
    {
      const bool accepted = NullApp::windowAdopted(window);
      if (static_cast<int>(this->adoptedWindows().size()) != this->refuseAt)
        return accepted;
      this->refused = window;
      return false;
    }
  };
}

// Mutation: pass ItemId::none() to the factory instead of the row id.
void testDocumentWindowFactoryReceivesTheDocumentId()
{
  TrackingContext context;
  Config config(context);
  TestApp app(config);
  app.run();
  const ItemId id = config.roster.open(Document(0));
  app.tail();
  LOKA_VERIFY(config.factories == 1);
  LOKA_VERIFY(config.factoryIds[0] == id);
  const Document *entry = config.roster.find(config.factoryIds[0]);
  LOKA_VERIFY(entry && entry == config.roster.find(id) && entry->value == 0);
}

// Pin 1 mutation: skip the runtime walk.
void testDocumentWindowOpenAddsKeyedRow()
{
  TrackingContext context;
  Config config(context);
  TestApp app(config);
  app.run();
  const ItemId id = config.roster.open(Document(0));
  LOKA_VERIFY(!id.isNone());
  app.tail();
  LOKA_VERIFY(app.group().getComponents().size() == 1);
  LOKA_VERIFY(app.group().keyOf(app.window(id)) == id);
  LOKA_VERIFY(app.adoptedWindows().size() == 1 && app.adoptedWindows()[0] == app.window(id));
  LOKA_VERIFY(config.factories == 1);
}

// Pin 2 mutation: drop find(id)'s skip.
void testDocumentWindowExistingRootDoesNotRecompose()
{
  TrackingContext context;
  Config config(context);
  TestApp app(config);
  app.run();
  const ItemId first = config.roster.open(Document(0));
  app.tail();
  Window *original = app.window(first);
  const int compositions = config.roots[0].compositions;
  LOKA_VERIFY(compositions == 1);
  LOKA_VERIFY(!config.roster.open(Document(1)).isNone());
  app.tail();
  LOKA_VERIFY(config.factories == 2);
  LOKA_VERIFY(app.window(first) == original);
  LOKA_VERIFY(config.roots[0].compositions == compositions);
}

// Pin 3 mutation: omit windowGone on create refusal (retains the document).
// The refusal is the rail's createWindow returning 0; the scene the props
// carried is released with the props.
void testDocumentWindowCreateRefusalDropsDocument()
{
  TrackingContext context;
  Config config(context);
  TestApp app(config);
  app.run();
  config.refuseValue = 0;
  const ItemId id = config.roster.open(Document(0));
  app.tail();
  LOKA_VERIFY(config.roster.count() == 0 && !config.roster.find(id));
  LOKA_VERIFY(app.group().getComponents().size() == 0 && context.creates == 0);
  LOKA_VERIFY(!app.quitRequested() && !app.hasPendingWindowAdmission());
  LOKA_VERIFY(config.factories == 1);
  app.tail();
  LOKA_VERIFY(config.factories == 1);
}

// Pin 4 mutation: ignore adoption refusal. Close batching is deliberately unchanged.
void testDocumentWindowAdoptionRefusalDetachesThenReclaimsNextTail()
{
  TrackingContext context;
  Config config(context);
  RefusingNullApp app(config);
  app.run();
  const ItemId first = config.roster.open(Document(0));
  app.tail();
  const ItemId refused = config.roster.open(Document(1));
  app.tail();
  LOKA_VERIFY(app.refused && !app.window(refused));
  LOKA_VERIFY(app.group().getComponents().size() == 1 && app.window(first));
  LOKA_VERIFY(config.roster.count() == 1 && config.roster.find(first) && !config.roster.find(refused));
  LOKA_VERIFY(config.roots[1].detaches == 1 && config.roots[1].detachedInsideTurn);
  LOKA_VERIFY(!context.destroyed[1] && !app.quitRequested());
  LOKA_VERIFY(app.hasPendingWindowAdmission());
  app.tail();
  LOKA_VERIFY(context.destroyed[1] && !context.destroyed[0]);
  LOKA_VERIFY(config.roots[1].detaches == 1 && !app.hasPendingWindowAdmission());
}

// Pin 5 / structural mutation (a): omit the roster call from requestWindowClose.
void testDocumentWindowNativeCloseDoesNotResurrect()
{
  TrackingContext context;
  Config config(context);
  TestApp app(config);
  app.run();
  const ItemId first = config.roster.open(Document(0));
  const ItemId second = config.roster.open(Document(1));
  app.tail();
  app.window(first)->simulateNativeClose();
  LOKA_VERIFY(!app.window(first) && !config.roster.find(first));
  const ItemId third = config.roster.open(Document(2));
  app.tail();
  LOKA_VERIFY(config.factories == 3 && app.group().getComponents().size() == 2);
  LOKA_VERIFY(app.window(second) && app.window(third) && !app.window(first));
}

// Pin 6 mutation: skip snapshot assignment on content-only changes; this is a
// zero-creates characterization, without a production compare-count probe.
void testDocumentWindowContentUpdateCreatesNothing()
{
  TrackingContext context;
  Config config(context);
  TestApp app(config);
  app.run();
  const ItemId id = config.roster.open(Document(0));
  app.tail();
  Window *window = app.window(id);
  LOKA_VERIFY(config.roster.update(id, Document(1)) == EDIT_OK);
  LOKA_VERIFY(config.roster.find(id)->value == 1);
  app.tail();
  app.tail();
  LOKA_VERIFY(config.factories == 1 && context.creates == 1);
  LOKA_VERIFY(app.window(id) == window && !context.destroyed[0]);
}

// Pin 7 mutation: skip the seat's windowAdopted call (silent Null back-pointer).
void testDocumentWindowHookInstallsNativeCloseBackPointer()
{
  TrackingContext context;
  Config config(context);
  TestApp app(config);
  app.run();
  const ItemId id = config.roster.open(Document(0));
  app.tail();
  NullWindow *window = app.window(id);
  LOKA_VERIFY(window && window->app() == &app);
  window->simulateNativeClose();
  LOKA_VERIFY(app.group().getComponents().size() == 0);
  app.tail();
}

// Pin 8 mutation: skip the launch walk.
void testDocumentWindowBootstrapStaticBeforeKeyed()
{
  TrackingContext context;
  Config config(context, true);
  TestApp app(config);
  app.run();
  const AppComponentGroup::Components rows = app.group().getComponents();
  LOKA_VERIFY(rows.size() == 2 && app.adoptedWindows().size() == 2);
  LOKA_VERIFY(app.group().keyOf(rows[0]).isNone());
  LOKA_VERIFY(app.group().keyOf(rows[1]) == config.initial);
  LOKA_VERIFY(rows[0] == app.adoptedWindows()[0] && rows[1] == app.adoptedWindows()[1]);
}

// Pin 9 / c0 pin 5 mutation: drop the pending-admission KeySnapshot compare.
void testDocumentWindowRosterEditAfterLastAdmissionIsPendingWork()
{
  TrackingContext context;
  Config config(context);
  TestApp app(config);
  app.run();
  ItemId id;
  {
    Operation turn;
    turn.settle();
    app.admitAndApplyWindows();
    app.reconcileFocus();
    app.admitAndApplyWindows();
    turn.close();
    id = config.roster.open(Document(0));
    app.reclaimWindows();
  }
  LOKA_VERIFY(app.hasPendingWindowAdmission() && config.factories == 0);
  app.tail();
  LOKA_VERIFY(config.factories == 1 && app.window(id));
  {
    Operation turn;
    turn.settle();
    app.admitAndApplyWindows();
    app.reconcileFocus();
    app.admitAndApplyWindows();
    turn.close();
    LOKA_VERIFY(config.roster.close(id) == EDIT_OK);
    app.reclaimWindows();
  }
  LOKA_VERIFY(app.hasPendingWindowAdmission() && app.window(id));
  app.tail();
  LOKA_VERIFY(!app.window(id) && config.factories == 1);
}

// Pin 10 mutation: enumerate live desired keys/count rather than the copied set.
void testDocumentWindowReentrantOpenUsesNextWalk()
{
  TrackingContext context;
  Config config(context);
  TestApp app(config);
  app.run();
  ItemId first, second;
  {
    Operation turn;
    turn.settle();
    app.admitAndApplyWindows();
    // Seed in the tail's second admission, so the next walk is the next tail.
    first = config.roster.open(Document(0));
    second = config.roster.open(Document(1));
    config.reenter = true;
    app.reconcileFocus();
    app.admitAndApplyWindows();
    turn.close();
    app.reclaimWindows();
  }
  LOKA_VERIFY(config.roster.count() == 3 && config.factories == 2);
  LOKA_VERIFY(app.window(first) && app.window(second) && !app.window(config.reentrant));
  app.tail();
  LOKA_VERIFY(config.factories == 3 && app.window(config.reentrant));
  app.tail();
  LOKA_VERIFY(config.factories == 3);
}

// Pin 11 mutation: route launch refusal through requestWindowClose.
void testDocumentWindowLaunchRefusalUsesBootstrapDoor()
{
  TrackingContext context;
  Config config(context);
  const ItemId id = config.roster.open(Document(0));
  RefusingNullApp app(config);
  app.refuseAt = 1;
  app.run();
  LOKA_VERIFY(app.refused && app.bootstrapRefusedWindows().size() == 1);
  LOKA_VERIFY(app.bootstrapRefusedWindows()[0] == app.refused);
  LOKA_VERIFY(app.window(id) == app.refused && config.roster.count() == 1);
  LOKA_VERIFY(!context.destroyed[0] && !app.quitRequested());
}

// Pin 12: bounded admission; a second declaration asserts (release: first wins).
void testDocumentWindowRosterCapacity()
{
  TrackingContext context;
  Config config(context);
  TestApp app(config);
  app.run();
  for (int i = 0; i < 4; ++i)
    LOKA_VERIFY(!config.roster.open(Document(i)).isNone());
  app.tail();
  LOKA_VERIFY(config.factories == 4 && app.group().getComponents().size() == 4);
  LOKA_VERIFY(config.roster.open(Document(0)).isNone());
  app.tail();
  LOKA_VERIFY(config.factories == 4 && app.group().getComponents().size() == 4);
}

// Pin 13 / structural mutation (b): move the document walk before Scene run.
// Like PreparedReveal in SceneOwnershipTests, a state-driven Show mounts a
// boundary during admission. The existing parent boundary never recomposes.
void testDocumentWindowOpenDuringSceneRunIsAdmittedInTheSameAdmission()
{
  TrackingContext context;
  Config config(context);
  config.roots[0].roster = &config.roster;
  TestApp app(config);
  app.run();
  const ItemId first = config.roster.open(Document(0));
  app.tail();
  RootFacts &facts = config.roots[0];
  LOKA_VERIFY(facts.root && facts.compositions == 1 && facts.openerCompositions == 0);
  const int factories = config.factories;
  {
    Operation turn;
    facts.root->openDuringSceneRun();
    turn.settle();
    LOKA_VERIFY(app.window(first)->hasPendingSceneInvalidation());
    LOKA_VERIFY(!facts.opened && config.factories == factories);
    app.admitAndApplyWindows();
    LOKA_VERIFY(facts.opened && facts.openerCompositions == 1);
    LOKA_VERIFY(app.window(facts.openedId) && app.group().getComponents().size() == 2);
    LOKA_VERIFY(config.factories == factories + 1);
    app.reconcileFocus();
    app.admitAndApplyWindows();
    turn.close();
    app.reclaimWindows();
  }
  LOKA_VERIFY(app.window(first) && app.window(facts.openedId));
  LOKA_VERIFY(config.factories == factories + 1 && facts.compositions == 1);
  LOKA_VERIFY(facts.openerCompositions == 1);
}

// c0 pin 1 mutation: remove the decrease branch.
void testDocumentWindowCloseRemovesRowNextTail()
{
  TrackingContext context;
  Config config(context);
  TestApp app(config);
  app.run();
  const ItemId first = config.roster.open(Document(0));
  const ItemId second = config.roster.open(Document(1));
  app.tail();
  LOKA_VERIFY(config.roster.close(second) == EDIT_OK);
  app.tail();
  LOKA_VERIFY(app.group().getComponents().size() == 1 && app.window(first));
  LOKA_VERIFY(!app.window(second) && config.roster.count() == 1);
  LOKA_VERIFY(config.roots[1].detaches == 1 && config.roots[1].detachedInsideTurn);
  LOKA_VERIFY(!context.destroyed[1] && !app.quitRequested());
  LOKA_VERIFY(config.factories == 2);
  app.tail();
  LOKA_VERIFY(context.destroyed[1] && !context.destroyed[0]);
  LOKA_VERIFY(config.roots[1].detaches == 1 && !app.hasPendingWindowAdmission());
  LOKA_VERIFY(config.factories == 2);
}

// c0 pin 2 mutation: decrease before increase empties the group and quits (#1209 P2).
void testDocumentWindowCloseThenOpenInOneTurnDoesNotQuit()
{
  TrackingContext context;
  Config config(context);
  TestApp app(config);
  app.run();
  const ItemId first = config.roster.open(Document(0));
  app.tail();
  Window *old = app.window(first);
  ItemId second;
  {
    Operation turn;
    LOKA_VERIFY(config.roster.close(first) == EDIT_OK);
    second = config.roster.open(Document(1));
  }
  LOKA_VERIFY(!second.isNone());
  app.tail();
  LOKA_VERIFY(app.group().getComponents().size() == 1 && app.window(second));
  LOKA_VERIFY(!app.window(first) && app.isWindowClosePending(old));
  LOKA_VERIFY(!app.quitRequested() && config.factories == 2);
}

// c0 pin 3 characterization: native removal already happened; page 2 pin 5
// owns the structural mutation that drops requestWindowClose's roster call.
void testDocumentWindowCloseAfterNativeCloseIsNotFound()
{
  TrackingContext context;
  Config config(context);
  TestApp app(config);
  app.run();
  const ItemId first = config.roster.open(Document(0));
  const ItemId second = config.roster.open(Document(1));
  app.tail();
  app.window(second)->simulateNativeClose();
  const ListRevision revision = config.roster.revision().get();
  LOKA_VERIFY(config.roster.close(second) == EDIT_ID_NOT_FOUND);
  LOKA_VERIFY(config.roster.revision().get() == revision);
  app.tail();
  LOKA_VERIFY(app.group().getComponents().size() == 1 && app.window(first));
  LOKA_VERIFY(!app.window(second) && config.factories == 2);
  LOKA_VERIFY(config.roots[0].detaches == 0 && config.roots[1].detaches == 1);
  LOKA_VERIFY(!app.hasPendingWindowAdmission() && !app.quitRequested());
}

// c0 pin 4 mutation: decrease reads live roster identities instead of desiredAt.
void testDocumentWindowCloseDuringWalkTakesNextWalk()
{
  TrackingContext context;
  Config config(context);
  TestApp app(config);
  app.run();
  const ItemId first = config.roster.open(Document(0));
  app.tail();
  ItemId second;
  {
    Operation turn;
    turn.settle();
    app.admitAndApplyWindows();
    // As in page 2 pin 10, mutate during the second admission of this tail.
    second = config.roster.open(Document(1));
    config.closeDuringFactory = first;
    app.reconcileFocus();
    app.admitAndApplyWindows();
    turn.close();
    app.reclaimWindows();
  }
  LOKA_VERIFY(app.group().getComponents().size() == 2 && app.window(first));
  LOKA_VERIFY(app.window(second) && !config.roster.find(first));
  LOKA_VERIFY(app.hasPendingWindowAdmission() && config.factories == 2);
  LOKA_VERIFY(config.roots[0].detaches == 0);
  app.tail();
  LOKA_VERIFY(app.group().getComponents().size() == 1 && app.window(second));
  LOKA_VERIFY(!app.window(first) && config.roots[0].detaches == 1);
  app.tail();
  LOKA_VERIFY(config.factories == 2 && !app.hasPendingWindowAdmission());
}

// c0 pin 6 characterization: the row bound includes all stale rows at capacity.
void testDocumentWindowCloseAllAndReopenInOneTurn()
{
  TrackingContext context;
  Config config(context);
  TestApp app(config);
  app.run();
  ItemId oldIds[4], newIds[4];
  for (int i = 0; i < 4; ++i)
    oldIds[i] = config.roster.open(Document(i));
  app.tail();
  {
    Operation turn;
    for (int i = 0; i < 4; ++i)
      LOKA_VERIFY(config.roster.close(oldIds[i]) == EDIT_OK);
    for (int i = 0; i < 4; ++i)
    {
      newIds[i] = config.roster.open(Document(i));
      LOKA_VERIFY(!newIds[i].isNone());
    }
  }
  app.tail();
  LOKA_VERIFY(app.group().getComponents().size() == 4 && config.roster.count() == 4);
  for (int i = 0; i < 4; ++i)
    LOKA_VERIFY(app.window(newIds[i]) && !app.window(oldIds[i]));
  LOKA_VERIFY(!app.quitRequested() && config.factories == 8);
}
