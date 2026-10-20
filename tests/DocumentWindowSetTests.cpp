#include "DocumentWindowSetTests.hpp"
#include "app/core/AppComposition.hpp"
#include "app/core/WindowSeat.hpp"
#include "app/nodes/boundary/StdComposition.hpp"
#include "app/nodes/nestable/Show.hpp"
#include "platform/null/NullApp.hpp"
#include "platform/null/NullPlatformContext.hpp"
#include "support/WindowAdmissionTestApp.hpp"
#include "support/TestVerify.hpp"
#include "support/NativeCloseTestConfig.hpp"
#include "testing/app/WindowTestAccess.hpp"
#include "support/NullWindowTestAccess.hpp"
#include <cstdio>
#if defined(__linux__) && !defined(__SANITIZE_ADDRESS__) && !defined(NDEBUG)
#include <sys/wait.h>
#include <unistd.h>
#include <csignal>
#endif

namespace
{
  using namespace loka::core;
  using namespace loka::app;
  using namespace loka::app::scene;

  struct Document : DocumentFacts
  {
    int value;
    explicit Document(int v = 0) : value(v) {}
    Document(const char *title, bool edited)
        : DocumentFacts(String::Literal(title), edited),
          value(0)
    {
    }
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
    DocumentRoster<Document, 4> *closeRoster;
    ItemId id;
    explicit DocumentProps(RootFacts &f) : facts(&f), closeRoster(0) {}
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
    virtual void declareBindings(BindingToken &t)
    {
      if (this->props.closeRoster)
        t.watch(const_cast<State<CloseAsk> &>(this->props.closeRoster->closeAsked()),
                this, &DocumentRoot::onCloseAsked);
    }
    void onCloseAsked()
    {
      const CloseAsk ask = this->props.closeRoster->closeAsked().get();
      if (ask.id != this->props.id)
        return;
      const Document *entry = this->props.closeRoster->find(ask.id);
      if (!entry)
        return;
      if (entry->edited)
        this->props.closeRoster->declineClose(ask);
      else
        this->props.closeRoster->close(ask.id);
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
    /** Opens another Document(value) on every call, for the factory that
        never settles (#1233). */
    bool reenterEveryCall;
    bool staticFirst;
    ItemId initial;
    ItemId reentrant;
    ItemId closeDuringFactory;
    explicit Config(PlatformContext &context, bool bootstrap = false)
        : AppConfigurable(&context), factories(0), refuseValue(-1), reenter(false), reenterEveryCall(false),
          staticFirst(bootstrap)
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
      else if (config.reenterEveryCall)
      {
        config.reentrant = config.roster.open(Document(value));
        LOKA_VERIFY(!config.reentrant.isNone());
      }
      if (!config.closeDuringFactory.isNone())
      {
        const ItemId closing = config.closeDuringFactory;
        config.closeDuringFactory = ItemId::none();
        LOKA_VERIFY(config.roster.close(closing) == EDIT_OK);
      }
      WindowProps props;
      props.contentMargin(17 + value);
      DocumentProps root(config.roots[value]);
      root.closeRoster = &config.roster;
      root.id = id;
      props.scene(new Scene(BoundaryDefinition<DocumentProps, DocumentRoot>(root)));
      if (value == config.refuseValue)
        static_cast<TrackingContext *>(config.getPlatformContext())->refuseNextCreate = true;
      return props;
    }
  };
  class StaticWindowConfig : public AppConfigurable
  {
  public:
    explicit StaticWindowConfig(PlatformContext &context) : AppConfigurable(&context) {}
    virtual void compose(AppComposition &c) { c << WindowDef(WindowProps()); }
  };
  class QuittingNullApp : public NullApp
  {
  public:
    explicit QuittingNullApp(AppConfigurable &config) : NullApp(&config) {}
  protected:
    virtual void bootstrapWindowless() { App::bootstrapWindowless(); }
  };
  class TestApp : public NullApp
  {
  public:
    explicit TestApp(AppConfigurable &config) : NullApp(&config) {}
    using App::admitAndApplyWindows;
    using App::reclaimWindows;
    using App::hasPendingWindowAdmission;
    using App::windowlessAfterLaunch;
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
  LOKA_VERIFY(app.window(id)->contentMargin() == loka::app::layout::LayoutGap::of(17));
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
  // The refusal emptied roster and group at runtime: the last window is gone
  // and the App quits (#1236 P1); the drop itself is what this pin holds.
  LOKA_VERIFY(app.quitRequested() && !app.hasPendingWindowAdmission());
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
  const ItemId id = config.roster.open(Document("Refused", true));
  RefusingNullApp app(config);
  app.refuseAt = 1;
  app.run();
  LOKA_VERIFY(app.refused && app.bootstrapRefusedWindows().size() == 1);
  LOKA_VERIFY(app.bootstrapRefusedWindows()[0] == app.refused);
  LOKA_VERIFY(app.window(id) == app.refused && config.roster.count() == 1);
  LOKA_VERIFY(!context.destroyed[0] && !app.quitRequested());
  LOKA_VERIFY(!app.window(id)->editedState().get());
  LOKA_VERIFY(loka::app::testing::NullWindowTestAccess::editedProjectionCount(*app.window(id)) == 0);
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

// c2 pin 1 mutation: remove the unconditional launch door from App::run.
void testBootstrapWithEveryStaticWindowRefusedReportsWindowless()
{
  TrackingContext context;
  StaticWindowConfig config(context);
  TestApp app(config);
  context.refuseNextCreate = true;
  app.run();
  LOKA_VERIFY(app.group().getComponents().size() == 0);
  LOKA_VERIFY(app.bootstrapWindowlessReported());
  LOKA_VERIFY(app.bootstrapRefusedWindows().empty());
  LOKA_VERIFY(!app.quitRequested());
}

// c2 pin 2 mutation: move the launch check behind the seat/key guard.
// This pin stays green while pin 1 goes red: a no-seat launch must also report.
void testBootstrapWithTheSeededDocumentRefusedReportsWindowless()
{
  TrackingContext context;
  Config config(context, true);
  config.staticFirst = false;
  config.refuseValue = 0;
  TestApp app(config);
  app.run();
  LOKA_VERIFY(app.group().getComponents().size() == 0);
  LOKA_VERIFY(config.roster.count() == 0);
  LOKA_VERIFY(app.bootstrapWindowlessReported());
  LOKA_VERIFY(app.bootstrapRefusedWindows().empty());
  LOKA_VERIFY(!app.quitRequested());
}

// c2 pin 3 characterization: a successful launch does not report windowless.
void testBootstrapWithOneWindowDoesNotReportWindowless()
{
  TrackingContext context;
  Config config(context, true);
  config.staticFirst = false;
  TestApp app(config);
  app.run();
  LOKA_VERIFY(app.group().getComponents().size() == 1);
  LOKA_VERIFY(!app.bootstrapWindowlessReported());
  LOKA_VERIFY(!app.quitRequested());
}

// c2 pin 3 characterization: the Null record leaves runtime-open fixtures usable.
void testBootstrapWithAnEmptyRosterReportsWindowlessAndNullRecords()
{
  TrackingContext context;
  Config config(context);
  TestApp app(config);
  app.run();
  LOKA_VERIFY(app.group().getComponents().size() == 0);
  LOKA_VERIFY(app.bootstrapWindowlessReported());
  LOKA_VERIFY(!app.quitRequested());
  const ItemId id = config.roster.open(Document(0));
  LOKA_VERIFY(!id.isNone());
  app.tail();
  LOKA_VERIFY(app.group().getComponents().size() == 1 && app.window(id));
  LOKA_VERIFY(!app.quitRequested());
}

// c2 pin 4 mutation: empty the default bootstrapWindowless body.
void testBootstrapWindowlessDefaultQuits()
{
  TrackingContext context;
  Config config(context);
  QuittingNullApp app(config);
  app.run();
  LOKA_VERIFY(app.quitRequested());
}

// c2 pin 5 characterization: refused adoption retains a row at launch.
void testBootstrapRefusedAdoptionDoesNotAlsoReportWindowless()
{
  TrackingContext context;
  Config config(context, true);
  config.staticFirst = false;
  RefusingNullApp app(config);
  app.refuseAt = 1;
  app.run();
  LOKA_VERIFY(app.refused && app.bootstrapRefusedWindows().size() == 1);
  LOKA_VERIFY(app.bootstrapRefusedWindows()[0] == app.refused);
  LOKA_VERIFY(app.group().getComponents().size() == 1);
  LOKA_VERIFY(app.window(config.initial) == app.refused);
  LOKA_VERIFY(!app.bootstrapWindowlessReported());
  LOKA_VERIFY(!app.quitRequested());
}

// #1233 pin 1 mutation: drop the roster-count check from windowlessAfterLaunch.
// A launch factory's re-entrant open is pending, not windowless; the runtime
// clock admits it on the first tail.
void testBootstrapLeavesReentrantOpensPendingInsteadOfDeclaringWindowless()
{
  TrackingContext context;
  Config config(context, true);
  config.staticFirst = false;
  config.reenter = true;
  config.refuseValue = 0;
  TestApp app(config);
  app.run();
  LOKA_VERIFY(config.factories == 1);
  LOKA_VERIFY(!config.roster.find(config.initial) && config.roster.find(config.reentrant));
  LOKA_VERIFY(app.group().getComponents().size() == 0);
  LOKA_VERIFY(!app.bootstrapWindowlessReported() && !app.quitRequested());
  LOKA_VERIFY(app.hasPendingWindowAdmission() && !app.windowlessAfterLaunch());
  app.tail();
  LOKA_VERIFY(config.factories == 2 && app.window(config.reentrant));
  LOKA_VERIFY(!app.hasPendingWindowAdmission());
}

// #1233 pin 3 mutation: drop the runtime refusal quit in admitDocumentWindows.
// A seed refused at launch whose factory opened one replacement, refused in
// turn at the first tail, leaves no window and no document: that is the last
// window gone and the App quits, as the launch drain used to answer (PR #1236
// review P1).
void testRuntimeRefusalOfTheDeferredLastDocumentQuits()
{
  TrackingContext context;
  Config config(context, true);
  config.staticFirst = false;
  config.reenter = true;
  config.refuseValue = 0;
  TestApp app(config);
  app.run();
  LOKA_VERIFY(config.factories == 1 && !app.quitRequested());
  context.refuseNextCreate = true;
  app.tail();
  LOKA_VERIFY(config.factories == 2);
  LOKA_VERIFY(config.roster.count() == 0 && app.group().getComponents().size() == 0);
  LOKA_VERIFY(app.quitRequested() && !app.bootstrapWindowlessReported());
}

// #1233 pin 2 mutation: walk at launch until a walk attempts nothing. A factory
// that opens a new document on every refused create then never returns from
// run(); with one launch walk it costs one factory call per admission and the
// App reaches its rail's loop with the admission pending (the #1194 shape).
void testBootstrapWalksOnceWhenTheFactoryReopensOnEveryRefusal()
{
  TrackingContext context;
  Config config(context, true);
  config.staticFirst = false;
  config.reenterEveryCall = true;
  config.refuseValue = 0;
  TestApp app(config);
  app.run();
  LOKA_VERIFY(config.factories == 1);
  LOKA_VERIFY(config.roster.count() == 1 && !config.roster.find(config.initial));
  LOKA_VERIFY(app.group().getComponents().size() == 0);
  LOKA_VERIFY(!app.bootstrapWindowlessReported() && !app.quitRequested());
  LOKA_VERIFY(app.hasPendingWindowAdmission());
  app.tail(); // two admissions per tail, one factory call each
  LOKA_VERIFY(config.factories == 3 && config.roster.count() == 1);
  LOKA_VERIFY(app.group().getComponents().size() == 0 && app.hasPendingWindowAdmission());
}

namespace
{
  void ApplyEditedFacts(void *data)
  {
    Window &window = *static_cast<Window *>(data);
    loka::core::StateTrackerGuard guard(window.getTracker());
    LOKA_VERIFY(window.editedState().trackerOwner() == window.getTracker());
    loka::app::testing::WindowTestAccess::applyDocumentFacts(window,
                                                             loka::app::DocumentFacts(loka::core::String("Doc"), true));
    LOKA_VERIFY(window.editedState().get());
    LOKA_VERIFY(window.titleState().get().equals(loka::core::String("Doc")));
  }
  void ApplyEqualEditedFacts(void *data)
  {
    Window &window = *static_cast<Window *>(data);
    loka::core::StateTrackerGuard guard(window.getTracker());
    LOKA_VERIFY(window.editedState().trackerOwner() == window.getTracker());
    loka::app::testing::WindowTestAccess::applyDocumentFacts(window,
                                                             loka::app::DocumentFacts(loka::core::String("Doc"), true));
  }
} // namespace

void testWindowDocumentFactsProjectAtTurnClose()
{
  NullPlatformContext context;
  NullWindow window(&context, WindowProps());
  WindowAdmissionTestApp app(window);
  app.operationLoop(&ApplyEditedFacts, &window);
  LOKA_VERIFY(loka::app::testing::NullWindowTestAccess::projectedEdited(window));
  LOKA_VERIFY(loka::app::testing::NullWindowTestAccess::editedProjectionCount(window) == 1);
}

void testWindowDocumentFactsEqualApplyDoesNotProjectAgain()
{
  NullPlatformContext context;
  NullWindow window(&context, WindowProps());
  WindowAdmissionTestApp app(window);
  app.operationLoop(&ApplyEditedFacts, &window);
  // Separate turns discriminate equality suppression from per-turn coalescing.
  app.operationLoop(&ApplyEqualEditedFacts, &window);
  LOKA_VERIFY(loka::app::testing::NullWindowTestAccess::projectedEdited(window));
  LOKA_VERIFY(loka::app::testing::NullWindowTestAccess::editedProjectionCount(window) == 1);
}

void testWindowPlainDefaultsToUneditedWithoutProjection()
{
  loka::app::DocumentFacts facts;
  LOKA_VERIFY(!facts.edited);
  LOKA_VERIFY(facts.title.empty());
  facts = loka::app::DocumentFacts(loka::core::String("Doc"), true);
  LOKA_VERIFY(facts.edited && facts.title.equals(loka::core::String("Doc")));
  NullPlatformContext context;
  NullWindow window(&context, WindowProps());
  WindowAdmissionTestApp app(window);
  app.operationLoop();
  LOKA_VERIFY(!window.editedState().get());
  LOKA_VERIFY(!loka::app::testing::NullWindowTestAccess::projectedEdited(window));
  LOKA_VERIFY(loka::app::testing::NullWindowTestAccess::editedProjectionCount(window) == 0);
}

void testDocumentWindowFactsApplyJoinsTurn()
{
  TrackingContext context;
  Config config(context);
  const ItemId first = config.roster.open(Document("Seed", false));
  const ItemId other = config.roster.open(Document(1));
  TestApp app(config);
  app.run();
  NullWindow &window = *app.window(first);
  const unsigned projections = loka::app::testing::NullWindowTestAccess::editedProjectionCount(window);
  const unsigned otherProjections = loka::app::testing::NullWindowTestAccess::editedProjectionCount(*app.window(other));
  LOKA_VERIFY(window.titleState().get().equals(String::Literal("Seed")));
  LOKA_VERIFY(config.roster.update(first, Document("Renamed", true)) == EDIT_OK);
  {
    Operation turn;
    turn.settle();
    app.admitAndApplyWindows();
    LOKA_VERIFY(window.titleState().get().equals(String::Literal("Renamed")));
    LOKA_VERIFY(window.editedState().get());
    LOKA_VERIFY(window.getTracker()->phase() != TRACKER_IDLE);
    turn.close();
    LOKA_VERIFY(loka::app::testing::NullWindowTestAccess::editedProjectionCount(window) == projections + 1);
    app.reclaimWindows();
  }
  LOKA_VERIFY(loka::app::testing::NullWindowTestAccess::projectedEdited(window));
  LOKA_VERIFY(loka::app::testing::NullWindowTestAccess::editedProjectionCount(*app.window(other)) == otherProjections);
  LOKA_VERIFY(!app.window(other)->editedState().get());
  LOKA_VERIFY(config.roster.update(first, Document("Renamed", true)) == EDIT_OK);
  app.tail();
  LOKA_VERIFY(loka::app::testing::NullWindowTestAccess::editedProjectionCount(window) == projections + 1);
  LOKA_VERIFY(!app.hasPendingWindowAdmission());
}

void testDocumentWindowFactsLaunchWithoutTurn()
{
  TrackingContext context;
  Config config(context);
  const ItemId id = config.roster.open(Document("Launch", true));
  TestApp app(config);
  LOKA_VERIFY(!Operation::hasActive());
  app.run();
  LOKA_VERIFY(app.window(id)->titleState().get().equals(String::Literal("Launch")));
  LOKA_VERIFY(app.window(id)->editedState().get());
  LOKA_VERIFY(loka::app::testing::NullWindowTestAccess::projectedEdited(*app.window(id)));
  LOKA_VERIFY(loka::app::testing::NullWindowTestAccess::editedProjectionCount(*app.window(id)) == 1);
}

namespace
{
  void recordTitle(void *data)
  {
    std::vector<String> &values = *static_cast<std::vector<String> *>(data);
    values.push_back(String::Literal("notified"));
  }
  WindowProps plainDocumentWindow(ItemId, const Document &, void *)
  {
    return WindowProps();
  }
  WindowProps borrowedDocumentTitle(ItemId, const Document &, void *data)
  {
    return WindowProps().titleState(static_cast<MutableState<String> *>(data));
  }
  WindowProps borrowedDocumentDisplayTitle(ItemId, const Document &, void *data)
  {
    return WindowProps().displayTitleState(static_cast<MutableState<String> *>(data));
  }
  class CloseDocumentRoot;
  struct CloseDocumentProps : NodePropsBase<CloseDocumentProps>
  {
    typedef CloseDocumentRoot NodeType;
    struct TypeTag
    {
    };
    DocumentRoster<Document, 4> *roster;
    ItemId closing;
    CloseDocumentProps(DocumentRoster<Document, 4> &r, ItemId id)
        : roster(&r),
          closing(id)
    {
    }
    bool operator<(const PropsBase &rhs) const
    {
      if (rhs.propsTypeId() != this->propsTypeId())
        return this->propsTypeId() < rhs.propsTypeId();
      const CloseDocumentProps &other = static_cast<const CloseDocumentProps &>(rhs);
      if (this->roster != other.roster)
        return std::less<DocumentRoster<Document, 4> *>()(this->roster, other.roster);
      if (this->closing.generation != other.closing.generation)
        return this->closing.generation < other.closing.generation;
      return this->closing.seq < other.closing.seq;
    }
  };
  class CloseDocumentRoot : public StdCompositionBoundaryNodeBase<CloseDocumentProps>
  {
  public:
    explicit CloseDocumentRoot(const CloseDocumentProps &props)
        : StdCompositionBoundaryNodeBase<CloseDocumentProps>(props)
    {
    }
    virtual void composeNode(NodeComposition &)
    {
      LOKA_VERIFY(this->props.roster->close(this->props.closing) == EDIT_OK);
    }
  };
  class ClosingConfig : public AppConfigurable
  {
  public:
    DocumentRoster<Document, 4> roster;
    ItemId closing;
    explicit ClosingConfig(PlatformContext &context)
        : AppConfigurable(&context)
    {
    }
    virtual void compose(AppComposition &c)
    {
      c << DocumentWindows(this->roster, &factory, this);
    }
    static WindowProps factory(ItemId, const Document &entry, void *data)
    {
      ClosingConfig &config = *static_cast<ClosingConfig *>(data);
      WindowProps props;
      if (entry.value == 2)
        props.scene(new Scene(BoundaryDefinition<CloseDocumentProps, CloseDocumentRoot>(
            CloseDocumentProps(config.roster, config.closing))));
      return props;
    }
  };
} // namespace

void testDocumentWindowFactsCopySurvivesRosterEdits()
{
  DocumentRoster<Document, 4> roster;
  const ItemId id = roster.open(Document("Copied", true));
  DocumentWindowSeat<Document, 4> seat(roster, &plainDocumentWindow, 0);
  DocumentFacts copy;
  LOKA_VERIFY(seat.copyFacts(id, copy));
  LOKA_VERIFY(roster.update(id, Document("Changed", false)) == EDIT_OK);
  LOKA_VERIFY(roster.close(id) == EDIT_OK);
  LOKA_VERIFY(copy.title.equals(String::Literal("Copied")) && copy.edited);
  LOKA_VERIFY(!seat.copyFacts(id, copy));
  LOKA_VERIFY(copy.title.equals(String::Literal("Copied")) && copy.edited);
}

void testDocumentWindowTitleSeedAndEqualUpdate()
{
  TrackingContext context;
  DocumentRoster<Document, 4> roster;
  const ItemId id = roster.open(Document("Seed", false));
  DocumentWindowSeat<Document, 4> seat(roster, &plainDocumentWindow, 0);
  Window *seed = seat.createWindow(id, &context);
  LOKA_VERIFY(seed && seed->titleState().get().equals(String::Literal("Seed")));
  delete seed;

  Config config(context);
  const ItemId first = config.roster.open(Document("Seed", false));
  const ItemId other = config.roster.open(Document(1));
  TestApp app(config);
  app.run();
  std::vector<String> firstTitles, otherTitles;
  app.window(first)->titleState().bind(&recordTitle, &firstTitles, false);
  app.window(other)->titleState().bind(&recordTitle, &otherTitles, false);
  LOKA_VERIFY(config.roster.update(first, Document("Renamed", true)) == EDIT_OK);
  app.tail();
  LOKA_VERIFY(firstTitles.size() == 1 && otherTitles.empty());
  LOKA_VERIFY(config.roster.update(first, Document("Renamed", true)) == EDIT_OK);
  app.tail();
  LOKA_VERIFY(firstTitles.size() == 1 && otherTitles.empty());
  app.window(first)->titleState().unbind(&recordTitle, &firstTitles);
  app.window(other)->titleState().unbind(&recordTitle, &otherTitles);
}

void testDocumentWindowBorrowedTitleAsserts()
{
#if defined(__linux__) && !defined(__SANITIZE_ADDRESS__) && !defined(NDEBUG)
  const DocumentWindowSeat<Document, 4>::Factory factories[] = {&borrowedDocumentTitle, &borrowedDocumentDisplayTitle};
  for (unsigned i = 0; i < 2; ++i)
  {
    const pid_t child = fork();
    LOKA_VERIFY(child >= 0);
    if (child == 0)
    {
      NullPlatformContext context;
      DocumentRoster<Document, 4> roster;
      const ItemId id = roster.open(Document("Owned", false));
      MutableState<String> title(String::Literal("Borrowed"));
      DocumentWindowSeat<Document, 4> seat(roster, factories[i], &title);
      delete seat.createWindow(id, &context);
      _exit(0);
    }
    int status = 0;
    LOKA_VERIFY(waitpid(child, &status, 0) == child);
    LOKA_VERIFY(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
  }
#else
  (void)&borrowedDocumentTitle;
  (void)&borrowedDocumentDisplayTitle;
  std::puts("[skip] borrowed-title assert probe requires Linux without ASan and NDEBUG");
#endif
}

void testDocumentWindowMountCloseSkipsLiveFacts()
{
  TrackingContext context;
  ClosingConfig config(context);
  const ItemId survivor = config.roster.open(Document("Survivor", false));
  config.closing = config.roster.open(Document("Old", false));
  TestApp app(config);
  app.run();
  // Put the mounting closer before the removed row in this walk's snapshot:
  // remove/reopen the victim after the closer is appended.
  LOKA_VERIFY(config.roster.close(config.closing) == EDIT_OK);
  const ItemId closer = config.roster.open(Document(2));
  config.closing = config.roster.open(Document("Pending", true));
  {
    Operation turn;
    turn.settle();
    app.admitAndApplyWindows();
    LOKA_VERIFY(app.window(survivor) && app.window(closer));
    LOKA_VERIFY(!app.window(config.closing));
    turn.close();
    app.reclaimWindows();
  }
  app.tail();
  LOKA_VERIFY(!app.hasPendingWindowAdmission());
}

void testDocumentWindowPendingFactsAfterNativeClose()
{
  TrackingContext context;
  Config config(context);
  const ItemId id = config.roster.open(Document("Old", false));
  const ItemId survivor = config.roster.open(Document(1));
  TestApp app(config);
  app.run();
  NullWindow *window = app.window(id);
  LOKA_VERIFY(config.roster.update(id, Document("Pending", true)) == EDIT_OK);
  window->simulateNativeClose();
  LOKA_VERIFY(!app.window(id) && !config.roster.find(id));
  LOKA_VERIFY(config.roster.update(id, Document("Too late", true)) == EDIT_ID_NOT_FOUND);
  app.tail();
  LOKA_VERIFY(app.window(survivor) && !app.window(id));
  LOKA_VERIFY(config.factories == 2);
}

void testDocumentWindowFactoryEditSeedsLiveEntry()
{
  TrackingContext context;
  Config config(context);
  const ItemId first = config.roster.open(Document("First", false));
  const ItemId second = config.roster.open(Document("Second", false));
  LOKA_VERIFY(!config.roster.open(Document("Third", false)).isNone());
  DocumentWindowSeat<Document, 4> seat(config.roster, &Config::documentWindow, &config);
  config.closeDuringFactory = first;
  Window *window = seat.createWindow(second, &context);
  LOKA_VERIFY(window && window->titleState().get().equals(String::Literal("Second")));
  delete window;
  config.closeDuringFactory = second;
  LOKA_VERIFY(!seat.createWindow(second, &context));
}

void testDocumentWindowMountCloseExistingRowWaitsForNextAdmission()
{
  TrackingContext context;
  ClosingConfig config(context);
  config.closing = config.roster.open(Document("Existing", false));
  TestApp app(config);
  app.run();
  NullWindow *closing = app.window(config.closing);
  const ItemId closer = config.roster.open(Document(2));
  {
    Operation turn;
    turn.settle();
    app.admitAndApplyWindows();
    LOKA_VERIFY(!config.roster.find(config.closing));
    LOKA_VERIFY(app.window(config.closing) == closing);
    LOKA_VERIFY(closing->titleState().get().equals(String::Literal("Existing")));
    LOKA_VERIFY(!closing->editedState().get());
    LOKA_VERIFY(app.hasPendingWindowAdmission());
    turn.close();
    app.reclaimWindows();
  }
  app.tail();
  LOKA_VERIFY(!app.window(config.closing) && app.window(closer));
}

/** #1245 t1: the existing State::deferBind dispatch invokes native observers
    synchronously. Keep the required turn-close contract as a stopped pin;
    repairing that State/rail door is outside this work item's walls. */
namespace
{
  class ClosingAdoptionApp : public TestApp
  {
  public:
    explicit ClosingAdoptionApp(Config &config)
        : TestApp(config)
    {
    }

  protected:
    virtual bool windowAdopted(Window *window)
    {
      NullApp::windowAdopted(window);
      this->requestWindowClose(window);
      return true;
    }
  };
} // namespace

void testDocumentWindowAdoptionCallbackCloseSkipsFacts()
{
  TrackingContext context;
  Config config(context);
  ClosingAdoptionApp app(config);
  app.run();
  const ItemId id = config.roster.open(Document("Closed during adoption", true));
  app.tail();
  LOKA_VERIFY(!app.window(id) && !config.roster.find(id));
  LOKA_VERIFY(config.factories == 1);
  app.tail();
  LOKA_VERIFY(context.destroyed[0]);
}

void testDocumentWindowMountRemovesOwnEntrySkipsFacts()
{
  TrackingContext context;
  ClosingConfig config(context);
  const ItemId survivor = config.roster.open(Document("Survivor", false));
  TestApp app(config);
  app.run();
  Document closing("Seed survives", true);
  closing.value = 2;
  config.closing = config.roster.open(closing);
  {
    Operation turn;
    turn.settle();
    app.admitAndApplyWindows();
    LOKA_VERIFY(!config.roster.find(config.closing));
    LOKA_VERIFY(app.window(config.closing));
    LOKA_VERIFY(app.window(config.closing)->titleState().get().equals(String::Literal("Seed survives")));
    LOKA_VERIFY(!app.window(config.closing)->editedState().get());
    turn.close();
    app.reclaimWindows();
  }
  app.tail();
  LOKA_VERIFY(!app.window(config.closing) && app.window(survivor));
}

void testCloseAskedCleanDocumentCloses()
{
  TrackingContext context;
  Config config(context);
  const ItemId id = config.roster.open(Document(0));
  TestApp app(config);
  app.run();
  app.window(id)->simulateCloseAsked();
  const CloseAsk ask = config.roster.closeAsked().get();
  LOKA_VERIFY(ask.id == id && ask.seq == 1);
  app.tail();
  LOKA_VERIFY(!config.roster.find(id) && !app.window(id));
  LOKA_VERIFY(config.roster.closeAsked().get() == ask);
}

void testCloseAskedEditedDocumentRemains()
{
  TrackingContext context;
  Config config(context);
  const ItemId id = config.roster.open(Document("Edited", true));
  TestApp app(config);
  app.run();
  NullWindow *window = app.window(id);
  Scene *scene = window->scene();
  window->simulateCloseAsked();
  const CloseAsk ask = config.roster.closeAsked().get();
  app.tail();
  LOKA_VERIFY(config.roster.find(id) && app.window(id) == window && window->scene() == scene);
  LOKA_VERIFY(config.roster.closeAnswer().get() == CloseAnswer(ask.id, ask.seq));
  window->simulateCloseAsked();
  LOKA_VERIFY(config.roster.closeAsked().get().seq == ask.seq + 1);
}

void testCloseAskedUnkeyedWindowClosesAsToday()
{
  TrackingContext context;
  StaticWindowConfig config(context);
  TestApp app(config);
  app.run();
  NullWindow *window = static_cast<NullWindow *>(app.adoptedWindows()[0]);
  LOKA_VERIFY(!app.closeAsked(window));
  window->simulateCloseAsked();
  app.tail();
  LOKA_VERIFY(app.group().getComponents().size() == 0 && app.quitRequested());
}

void testQuitWithoutRosterQuitsNow()
{
  TrackingContext context;
  StaticWindowConfig config(context);
  TestApp app(config);
  app.run();
  app.requestQuit();
  LOKA_VERIFY(app.quitRequested() && app.group().getComponents().size() == 1);
}

void testQuitAsksSeriallyAndCancelEndsAttempt()
{
  TrackingContext context;
  Config config(context, true);
  Document edited(0);
  edited.edited = true;
  config.roster.update(config.initial, edited);
  const ItemId second = config.roster.open(Document(1));
  TestApp app(config);
  app.run();
  app.requestQuit();
  const CloseAsk ask = config.roster.closeAsked().get();
  LOKA_VERIFY(ask.id == config.initial);
  app.tail();
  LOKA_VERIFY(config.roster.closeAnswer().get() == CloseAnswer(ask.id, ask.seq));
  LOKA_VERIFY(!app.quitRequested() && config.roster.count() == 2);
  config.roster.close(config.initial);
  app.tail();
  LOKA_VERIFY(config.roster.closeAsked().get() == ask);
  LOKA_VERIFY(config.roster.find(second) && !app.quitRequested());
}

void testQuitSecondDeclineInSameTurnStillCancelsAttempt()
{
  TrackingContext context;
  Config config(context, true);
  Document edited(0);
  edited.edited = true;
  config.roster.update(config.initial, edited);
  const ItemId second = config.roster.open(Document(1));
  TestApp app(config);
  app.run();
  app.requestQuit();
  const CloseAsk attempt = config.roster.closeAsked().get();
  // A native close box on the same edited document before the tail: the node
  // declines again, and the answer box now carries the later sequence.
  app.window(config.initial)->simulateCloseAsked();
  const CloseAsk later = config.roster.closeAsked().get();
  LOKA_VERIFY(later.id == attempt.id && later.seq == attempt.seq + 1);
  LOKA_VERIFY(config.roster.closeAnswer().get() == CloseAnswer(later.id, later.seq));
  app.tail();
  LOKA_VERIFY(!app.quitRequested() && config.roster.count() == 2);
  config.roster.close(config.initial);
  app.tail();
  LOKA_VERIFY(config.roster.closeAsked().get() == later);
  LOKA_VERIFY(config.roster.find(second) && !app.quitRequested());
}

void testQuitOtherDocumentDeclineInSameTurnEndsAttempt()
{
  TrackingContext context;
  Config config(context, true);
  Document edited(0);
  edited.edited = true;
  config.roster.update(config.initial, edited);
  Document editedSecond(1);
  editedSecond.edited = true;
  const ItemId second = config.roster.open(editedSecond);
  TestApp app(config);
  app.run();
  app.requestQuit();
  const CloseAsk attempt = config.roster.closeAsked().get();
  LOKA_VERIFY(attempt.id == config.initial);
  // The asked document declined synchronously; a native close box on the
  // other edited document before the tail overwrites that answer.
  app.window(second)->simulateCloseAsked();
  const CloseAsk other = config.roster.closeAsked().get();
  LOKA_VERIFY(other.id == second && config.roster.closeAnswer().get() == CloseAnswer(other.id, other.seq));
  app.tail();
  LOKA_VERIFY(!app.quitRequested() && config.roster.count() == 2);
  // The attempt ended: closing the first document by hand asks nobody else.
  config.roster.close(config.initial);
  app.tail();
  LOKA_VERIFY(config.roster.closeAsked().get() == other);
  LOKA_VERIFY(config.roster.find(second) && !app.quitRequested());
}

void testQuitDeclinedThenClosedByHandBeforeTailEndsAttempt()
{
  TrackingContext context;
  Config config(context, true);
  Document edited(0);
  edited.edited = true;
  config.roster.update(config.initial, edited);
  const ItemId second = config.roster.open(Document(1));
  TestApp app(config);
  app.run();
  app.requestQuit();
  const CloseAsk attempt = config.roster.closeAsked().get();
  LOKA_VERIFY(config.roster.closeAnswer().get() == CloseAnswer(attempt.id, attempt.seq));
  // The declined document is closed by hand before the tail: its row is gone
  // and its decline is still published. The decline wins; nobody else is asked.
  config.roster.close(config.initial);
  app.tail();
  app.tail();
  LOKA_VERIFY(config.roster.closeAsked().get() == attempt);
  LOKA_VERIFY(config.roster.find(second) && !app.quitRequested());
}

void testQuitAllAcceptedExitsWithStaticWindow()
{
  TrackingContext context;
  Config config(context, true);
  const ItemId second = config.roster.open(Document(1));
  TestApp app(config);
  app.run();
  app.requestQuit();
  LOKA_VERIFY(config.roster.closeAsked().get().id == config.initial);
  LOKA_VERIFY(!app.quitRequested());
  app.tail();
  app.tail();
  LOKA_VERIFY(config.roster.closeAsked().get().id == second);
  LOKA_VERIFY(config.roster.closeAsked().get().seq == 2);
  LOKA_VERIFY(config.roster.count() == 0 && app.quitRequested());
  LOKA_VERIFY(app.group().getComponents().size() == 1);
}

// #1286 bot P2: with no static window the last accepted document empties the
// group, whose close tail already quits; the quit attempt must not quit again.
void testQuitAllAcceptedWithoutStaticWindowQuitsOnce()
{
  TrackingContext context;
  Config config(context, false);
  config.roster.open(Document(1));
  TestApp app(config);
  app.run();
  app.requestQuit();
  LOKA_VERIFY(!app.quitRequested());
  app.tail();
  app.tail();
  LOKA_VERIFY(config.roster.count() == 0 && app.quitRequested());
  LOKA_VERIFY(app.group().getComponents().size() == 0);
  LOKA_VERIFY(app.quitCalls() == 1);
}

void testHasEditedDocumentsReadsRosterFacts()
{
  TrackingContext context;
  StaticWindowConfig plain(context);
  TestApp noSeat(plain);
  noSeat.run();
  LOKA_VERIFY(!noSeat.hasEditedDocuments());
  Config config(context, true);
  TestApp app(config);
  app.run();
  LOKA_VERIFY(!app.hasEditedDocuments());
  const ItemId later = config.roster.open(Document(1));
  LOKA_VERIFY(!app.hasEditedDocuments());
  Document edited(1);
  edited.edited = true;
  LOKA_VERIFY(config.roster.update(later, edited) == EDIT_OK);
  LOKA_VERIFY(app.hasEditedDocuments());
}

void testQuitAtCompletionWaitsForAdmissionTail()
{
  TrackingContext context;
  NativeCloseTestConfig config(context, true);
  const ItemId id = config.roster.open(DocumentFacts(String("edited"), true));
  TestApp app(config);
  app.run();
  app.requestQuitAtCompletion();
  LOKA_VERIFY(config.roster.closeAsked().get() == CloseAsk::none());
  app.tail();
  LOKA_VERIFY(config.roster.closeAsked().get().id == id);
  LOKA_VERIFY(config.roster.closeAsked().get().seq == 1);
}

void testQuitAtCompletionJoinsLiveAttempt()
{
  TrackingContext context;
  NativeCloseTestConfig config(context, true);
  const ItemId id = config.roster.open(DocumentFacts(String("edited"), true));
  TestApp app(config);
  app.run();
  app.requestQuit();
  const CloseAsk ask = config.roster.closeAsked().get();
  app.requestQuitAtCompletion();
  app.tail();
  LOKA_VERIFY(config.roster.closeAsked().get() == ask);
  LOKA_VERIFY(!app.quitRequested());
  // A surviving attempt still absorbs a later reservation without another ask.
  app.requestQuitAtCompletion();
  app.tail();
  LOKA_VERIFY(config.roster.closeAsked().get() == ask);
  LOKA_VERIFY(config.roster.close(id) == EDIT_OK);
  app.tail();
  LOKA_VERIFY(app.quitCalls() == 1);
}

void testQuitAtCompletionTwiceAsksOnce()
{
  TrackingContext context;
  NativeCloseTestConfig config(context, true);
  const ItemId id = config.roster.open(DocumentFacts());
  TestApp app(config);
  app.run();
  app.requestQuitAtCompletion();
  app.requestQuitAtCompletion();
  LOKA_VERIFY(config.roster.closeAsked().get() == CloseAsk::none());
  app.tail();
  app.tail();
  LOKA_VERIFY(config.roster.closeAsked().get() == CloseAsk(id, 1));
}

void testMenuQuitWhileRequestedReplacesReservation()
{
  TrackingContext context;
  NativeCloseTestConfig config(context, true);
  const ItemId id = config.roster.open(DocumentFacts());
  TestApp app(config);
  app.run();
  app.requestQuitAtCompletion();
  app.requestQuit();
  LOKA_VERIFY(config.roster.closeAsked().get() == CloseAsk(id, 1));
  app.tail();
  LOKA_VERIFY(config.roster.closeAsked().get() == CloseAsk(id, 1));
  LOKA_VERIFY(config.roster.close(id) == EDIT_OK);
  app.tail();
  LOKA_VERIFY(app.quitCalls() == 1);
}

void testQuitAtCompletionAfterLastDocumentClosedQuitsOnce()
{
  for (int withStatic = 0; withStatic != 2; ++withStatic)
  {
    TrackingContext context;
    Config config(context, withStatic != 0);
    const ItemId id = withStatic ? config.initial : config.roster.open(Document(0));
    TestApp app(config);
    app.run();
    app.requestQuitAtCompletion();
    LOKA_VERIFY(config.roster.close(id) == EDIT_OK);
    app.tail();
    app.tail();
    LOKA_VERIFY(app.quitCalls() == 1);
    LOKA_VERIFY(app.group().getComponents().size() == static_cast<size_t>(withStatic));
  }
}

void testQuitAtCompletionShowsInPendingAdmission()
{
  TrackingContext context;
  NativeCloseTestConfig config(context, true);
  config.roster.open(DocumentFacts());
  TestApp app(config);
  app.run();
  app.tail();
  LOKA_VERIFY(!app.hasPendingWindowAdmission());
  // Model a query after the tail's last advance, before the rail's sleep check.
  app.requestQuitAtCompletion();
  LOKA_VERIFY(app.hasPendingWindowAdmission());
  app.tail();
  LOKA_VERIFY(!config.roster.closeAsked().get().id.isNone());
  LOKA_VERIFY(!app.hasPendingWindowAdmission());
}

void testQuitAtCompletionDeclineBeforeDeliveryDoesNotCancel()
{
  TrackingContext context;
  NativeCloseTestConfig config(context, true);
  const ItemId id = config.roster.open(DocumentFacts());
  TestApp app(config);
  app.run();
  LOKA_VERIFY(app.closeAsked(app.window(id)));
  const CloseAsk before = config.roster.closeAsked().get();
  app.requestQuitAtCompletion();
  config.roster.declineClose(before);
  app.tail();
  LOKA_VERIFY(config.roster.closeAsked().get() == CloseAsk(id, before.seq + 1));
  LOKA_VERIFY(!app.quitRequested());
}
