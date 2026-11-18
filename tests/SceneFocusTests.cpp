#include "SceneFocusTests.hpp"
#include "app/nodes/boundary/StdComposition.hpp"
#include "support/TestVerify.hpp"
#include "support/LifecycleFactTestAccess.hpp"
#include "support/WindowAdmissionTestApp.hpp"
#include "testing/scene/SceneTestFlow.hpp"
#include "testing/app/SceneManagerTestAccess.hpp"
#include "platform/null/NullWindow.hpp"
#include "platform/null/NullPlatformContext.hpp"
#include "app/nodes/nestable/Show.hpp"
#include "app/nodes/nestable/BoundarySection.hpp"

#include "testing/scene/SceneFocusTestAccess.hpp"

namespace
{
  using namespace loka::app::scene;
  typedef SceneFocusTestAccess Access;
  typedef loka::dsl::testing::SceneTestAccess SceneAccess;

  struct ProbeRow : FocusRow
  {
    ProbeRow()
        : leaves(0)
    {
    }
    int leaves;
    virtual void leaveAttached()
    {
      LOKA_VERIFY(!Access::sourced(*this));
      ++this->leaves;
      FocusRow::leaveAttached();
    }
  };
  class Participant;
  struct ParticipantTag
  {
  };
  struct ParticipantProps : NodePropsBase<ParticipantProps>
  {
    typedef ParticipantTag TypeTag;
    typedef Participant NodeType;
    ParticipantProps(Participant **out = 0, FocusRow *external = 0)
        : out(out),
          external(external)
    {
    }
    Participant **out;
    FocusRow *external;
    bool operator<(const PropsBase &rhs) const
    {
      return this->propsTypeId() < rhs.propsTypeId();
    }
  };
  class Participant : public Node
  {
  public:
    typedef ParticipantTag TypeTag;
    explicit Participant(const ParticipantProps &p)
        : props(p),
          queries(0)
    {
      if (this->props.out)
        *this->props.out = this;
    }
    ParticipantProps props;
    ProbeRow row;
    int queries;
    virtual FocusRow *asFocusParticipant()
    {
      ++this->queries;
      return this->props.external ? this->props.external : &this->row;
    }
  };
  typedef NodeDefinition<ParticipantProps, Participant> ParticipantDefinition;

  struct Fixture
  {
    Fixture()
        : participant(0),
          visible(true),
          destroyOnHide(false),
          loseRail(false),
          external(0)
    {
    }
    Participant *participant;
    loka::core::MutableState<bool> visible;
    bool destroyOnHide;
    bool loseRail;
    FocusRow *external;
  };
  Fixture *fixture = 0;
  class Root : public BoundaryNodeFor<Root>
  {
  public:
    explicit Root(const BoundaryPropsFor<Root> &p)
        : BoundaryNodeFor<Root>(p),
          queries(0)
    {
    }
    FocusRow row;
    int queries;
    virtual FocusRow *asFocusParticipant()
    {
      ++this->queries;
      return &this->row;
    }
    virtual void declareBindings(BindingToken &token)
    {
      if (fixture->loseRail)
        token.watch(*this->scene()->getAttachedState(), this, &Root::loseRail, true);
    }
    void loseRail()
    {
      if (!this->scene()->getAttachedState()->get())
        static_cast<NullWindow *>(this->scene()->getWindow())->destroyScenePlatform();
    }
    virtual void composeNode(NodeComposition &composition)
    {
      loka::app::ShowDefinition show(&fixture->visible);
      if (fixture->destroyOnHide)
        show.destroyOnDetach();
      show << ParticipantDefinition(ParticipantProps(&fixture->participant, fixture->external));
      composition.declare(show);
    }
  };
  void setVisible(Scene &scene, Fixture &f, bool value)
  {
    loka::core::StateTrackerGuard guard(SceneAccess::rootBoundary(scene)->tracker());
    f.visible.set(value);
  }
  void mount(Scene &scene, NullScenePlatformController &controller)
  {
    scene.mount(&controller);
    SceneAccess::updateAttached(scene, true);
  }

  class RefusingSection : public loka::app::BoundarySectionNode
  {
  public:
    RefusingSection()
        : BoundarySectionNode(loka::app::SectionProps(911)),
          attaches(0)
    {
    }
    int attaches;
    virtual bool attachStateOwner(BoundaryNode *boundary, IStateOwner *parent)
    {
      ++this->attaches;
      if (this->attaches == 2)
        return false;
      return BoundarySectionNode::attachStateOwner(boundary, parent);
    }
  };
  class RefusingParticipant : public Participant, public BoundaryInnerStateOwner
  {
  public:
    RefusingParticipant()
        : Participant(ParticipantProps())
    {
    }
    virtual IStateOwner *asStateOwner()
    {
      return this;
    }
    virtual bool attachStateOwner(BoundaryNode *, IStateOwner *)
    {
      return false;
    }
    virtual void noteStateAllocationFailure() {}
  };
  class CountedScene : public Scene
  {
  public:
    explicit CountedScene(int &deaths)
        : Scene(Boundary<Root>()),
          deaths_(deaths)
    {
    }
    ~CountedScene()
    {
      ++this->deaths_;
    }

  private:
    int &deaths_;
  };
  struct AdmissionAction
  {
    WindowAdmissionTestApp *app;
    Window *window;
    Scene *current;
    Scene *replacement;
    int *deaths;
  };
  void replaceWhilePublishing(void *data)
  {
    AdmissionAction &action = *static_cast<AdmissionAction *>(data);
    LOKA_VERIFY(action.current->isBusy());
    LOKA_VERIFY(!action.current->isRunInProgress());
    LOKA_VERIFY(action.window->sceneManager()->requestReplacement(action.replacement));
    action.app->flush();
    LOKA_VERIFY(action.window->scene() == action.current);
    LOKA_VERIFY(*action.deaths == 0);
  }
  class CloseApp : public WindowAdmissionTestApp
  {
  public:
    explicit CloseApp(Window &window)
        : WindowAdmissionTestApp(window),
          closed(0)
    {
    }
    int closed;
    virtual void windowClosed(Window *window)
    {
      ++this->closed;
      App::windowClosed(window);
    }
  };
  struct CloseAction
  {
    CloseApp *app;
    Window *window;
  };
  void closeWhilePublishing(void *data)
  {
    CloseAction &action = *static_cast<CloseAction *>(data);
    Node *root = SceneAccess::rootNode(*action.window->scene());
    LOKA_VERIFY(root);
    loka::core::Operation turn;
    action.app->requestWindowClose(action.window);
    action.app->admitAndApplyWindows();
    LOKA_VERIFY(SceneAccess::rootNode(*action.window->scene()) == root);
    turn.close();
    action.app->reclaimWindows();
    LOKA_VERIFY(action.app->closed == 0);
  }
  void assertPublishing(void *data)
  {
    LOKA_VERIFY(static_cast<SceneFocus *>(data)->isPublishing());
  }
  void nestedPublication(void *data)
  {
    SceneFocus &focus = *static_cast<SceneFocus *>(data);
    Access::duringPublication(focus, &assertPublishing, data);
    LOKA_VERIFY(focus.isPublishing());
  }
} // namespace

void testSceneFocusBareAndMount()
{
  Participant bare((ParticipantProps()));
  NullScenePlatformController controller;
  controller.onChange(&bare, NODE_DIRTY_INITIAL, true);
  ComponentContext context;
  BoundaryNode::composeSubtree(&bare, context, COMPOSE_EVENT_ATTACH, 0);
  LOKA_VERIFY(!Access::owner(bare.row));
  LOKA_VERIFY(bare.queries == 0);
  Fixture f;
  fixture = &f;
  Scene scene((Boundary<Root>()));
  mount(scene, controller);
  LOKA_VERIFY(f.participant);
  LOKA_VERIFY(Access::owner(f.participant->row) == &scene.focus());
  LOKA_VERIFY(Access::head(scene.focus()) == &f.participant->row);
  Root *root = static_cast<Root *>(SceneAccess::rootBoundary(scene));
  LOKA_VERIFY(root->queries == 0);
  LOKA_VERIFY(!Access::owner(root->row));
  SceneAccess::unmount(scene);
}

void testSceneFocusParkingAndRetirement()
{
  Fixture f;
  fixture = &f;
  NullScenePlatformController controller;
  Scene scene((Boundary<Root>()));
  mount(scene, controller);
  Participant *node = f.participant;
  setVisible(scene, f, false);
  LOKA_VERIFY(node->lifecycleFact() == NODE_FACT_DETACHED_RETAINED);
  LOKA_VERIFY(Access::owner(node->row) == &scene.focus());
  setVisible(scene, f, true);
  LOKA_VERIFY(f.participant == node);
  LOKA_VERIFY(node->lifecycleFact() == NODE_FACT_ATTACHED);
  LOKA_VERIFY(Access::owner(node->row) == &scene.focus());
  LOKA_VERIFY(!Access::next(node->row));
  LifecycleFactTestAccess::MarkSubtreeRetired(node);
  LOKA_VERIFY(!Access::owner(node->row));
  LOKA_VERIFY(!Access::head(scene.focus()));
  SceneAccess::unmount(scene);
}

void testSceneFocusDestroyOnHide()
{
  Fixture f;
  fixture = &f;
  FocusRow external;
  f.external = &external;
  f.destroyOnHide = true;
  NullScenePlatformController controller;
  Scene scene((Boundary<Root>()));
  mount(scene, controller);
  LOKA_VERIFY(Access::owner(external) == &scene.focus());
  setVisible(scene, f, false);
  LOKA_VERIFY(!Access::owner(external));
  SceneAccess::unmount(scene);
}

void testSceneFocusOwnerRefusalAndLeafFilter()
{
  Fixture f;
  fixture = &f;
  NullScenePlatformController controller;
  Scene scene((Boundary<Root>()));
  mount(scene, controller);
  BoundaryNode *boundary = SceneAccess::rootBoundary(scene);
  ComponentContext context;
  context.setBoundary(boundary);
  context.setStateOwner(boundary);
  context.setScene(&scene);
  Participant updateOnly((ParticipantProps()));
  BoundaryNode::composeSubtree(&updateOnly, context, COMPOSE_EVENT_UPDATE, boundary);
  LOKA_VERIFY(updateOnly.queries == 0);
  LOKA_VERIFY(!Access::owner(updateOnly.row));
  RefusingParticipant refusedLeaf;
  BoundaryNode::composeSubtree(&refusedLeaf, context, COMPOSE_EVENT_ATTACH, boundary);
  LOKA_VERIFY(refusedLeaf.queries == 0);
  LOKA_VERIFY(!Access::owner(refusedLeaf.row));
  // Born ATTACHED but never joined: retiring it cuts a read source without
  // running the living publication hook (only members can be published).
  FocusLink refusedReader;
  Access::source(refusedReader, refusedLeaf.row);
  loka::app::scene::LifecycleFactTestAccess::MarkSubtreeRetired(&refusedLeaf);
  LOKA_VERIFY(!refusedReader.peerRow());
  LOKA_VERIFY(refusedLeaf.row.leaves == 0);
  RefusingSection section;
  LOKA_VERIFY(section.attachStateOwner(boundary, boundary));
  Participant *child = new Participant(ParticipantProps());
  section.addChild(child);
  BoundaryNode::composeSubtree(&section, context, COMPOSE_EVENT_ATTACH, boundary);
  LOKA_VERIFY(section.attaches == 2);
  LOKA_VERIFY(child->queries == 0);
  LOKA_VERIFY(!Access::owner(child->row));
  // Unlike a direct Scene root, this non-leaf passes through the compose door.
  Root nested((BoundaryPropsFor<Root>()));
  BoundaryNode::composeSubtree(&nested, context, COMPOSE_EVENT_ATTACH, boundary);
  LOKA_VERIFY(nested.queries == 0);
  LOKA_VERIFY(!Access::owner(nested.row));
  BoundaryNode::composeSubtree(&nested, context, COMPOSE_EVENT_DETACH, boundary);
  SceneAccess::unmount(scene);
}

void testSceneFocusRefusedCandidate()
{
  Fixture f;
  fixture = &f;
  ProbeRow external;
  NullPlatformContext context;
  WindowProps props;
  props.scene(new Scene(Boundary<Root>()));
  NullWindow window(&context, props);
  WindowAdmissionTestApp app(window);
  app.flush();
  Scene *applied = window.scene();
  f.loseRail = true;
  f.external = &external;
  Scene *candidate = new Scene(Boundary<Root>());
  LOKA_VERIFY(window.sceneManager()->requestReplacement(candidate));
  app.flush();
  LOKA_VERIFY(loka::app::testing::SceneManagerTestAccess::lastPrepareRefusal(*window.sceneManager()) == candidate);
  LOKA_VERIFY(window.scene() == applied);
  LOKA_VERIFY(!SceneAccess::rootNode(*candidate));
  LOKA_VERIFY(!Access::owner(external));
  LOKA_VERIFY(!Access::head(candidate->focus()));
  LOKA_VERIFY(external.leaves == 1);
}

void testSceneFocusTeardownDisconnect()
{
  Fixture f;
  fixture = &f;
  NullScenePlatformController controller;
  Scene *scene = new Scene(Boundary<Root>());
  mount(*scene, controller);
  ProbeRow *stranded = new ProbeRow();
  FocusLink reader;
  Access::join(scene->focus(), *stranded);
  Access::publish(scene->focus(), *stranded);
  Access::source(reader, *stranded);
  SceneAccess::unmount(*scene);
  LOKA_VERIFY(!Access::owner(*stranded));
  LOKA_VERIFY(!Access::published(*stranded));
  LOKA_VERIFY(!reader.peerRow());
  LOKA_VERIFY(stranded->leaves == 0);
  delete scene;
  delete stranded;
}

void testSceneFocusEmptyTeardownDisconnect()
{
  Scene *scene = new Scene(Boundary<Root>());
  ProbeRow *stranded = new ProbeRow();
  FocusLink reader;
  Access::join(scene->focus(), *stranded);
  Access::publish(scene->focus(), *stranded);
  Access::source(reader, *stranded);
  SceneAccess::unmount(*scene);
  LOKA_VERIFY(!Access::owner(*stranded));
  LOKA_VERIFY(!Access::published(*stranded));
  LOKA_VERIFY(!reader.peerRow());
  LOKA_VERIFY(stranded->leaves == 0);
  delete scene;
  delete stranded;
}

void testSceneFocusLinksAndSilentDestruction()
{
  FocusLink reader;
  SceneFocus focus;
  FocusRow *row = new FocusRow();
  Access::join(focus, *row);
  Access::publish(focus, *row);
  Access::source(reader, *row);
  LOKA_VERIFY(Access::published(*row));
  LOKA_VERIFY(reader.peerRow() == row);
  delete row;
  LOKA_VERIFY(!Access::head(focus));
  LOKA_VERIFY(!reader.peerRow());
  FocusRow survivor;
  {
    FocusLink temporary;
    Access::source(temporary, survivor);
  }
  LOKA_VERIFY(!Access::sourced(survivor));
  FocusRow first, second;
  Access::source(reader, first);
  Access::source(reader, second);
  LOKA_VERIFY(!Access::sourced(first));
  LOKA_VERIFY(reader.peerRow() == &second);
  FocusLink other;
  Access::source(other, second);
  LOKA_VERIFY(!reader.peerRow());
  LOKA_VERIFY(other.peerRow() == &second);
  other.cut();
  other.cut();
  SceneFocus *owner = new SceneFocus();
  Access::join(*owner, survivor);
  Access::source(reader, survivor);
  delete owner;
  LOKA_VERIFY(!Access::owner(survivor));
  LOKA_VERIFY(!reader.peerRow());
  Access::duringPublication(focus, &nestedPublication, &focus);
  LOKA_VERIFY(!focus.isPublishing());
}

void testSceneFocusLifecycleHook()
{
  SceneFocus focus;
  Participant node((ParticipantProps()));
  FocusLink reader;
  Access::join(focus, node.row);
  Access::publish(focus, node.row);
  Access::source(reader, node.row);
  NotifySubtreeNodeDetached(&node);
  LOKA_VERIFY(node.row.leaves == 1);
  LOKA_VERIFY(!reader.peerRow());
  LOKA_VERIFY(!Access::published(node.row));
  LOKA_VERIFY(Access::owner(node.row) == &focus);
  NotifySubtreeNodeDetached(&node);
  LifecycleFactTestAccess::MarkSubtreeRetired(&node);
  LOKA_VERIFY(node.row.leaves == 1);
  LOKA_VERIFY(!Access::owner(node.row));
  // The kernel default is used directly, without the counting override.
  FocusRow plain;
  Participant defaultNode(ParticipantProps(0, &plain));
  Access::join(focus, plain);
  Access::publish(focus, plain);
  NotifySubtreeNodeDetached(&defaultNode);
  LOKA_VERIFY(!Access::published(plain));
}

void testSceneFocusBusyAdmission()
{
  Fixture f;
  fixture = &f;
  NullPlatformContext context;
  int deaths = 0;
  WindowProps props;
  props.scene(new CountedScene(deaths));
  NullWindow window(&context, props);
  WindowAdmissionTestApp app(window);
  app.flush();
  Scene *current = new CountedScene(deaths);
  LOKA_VERIFY(window.sceneManager()->requestReplacement(current));
  app.flush();
  LOKA_VERIFY(window.scene() == current);
  LOKA_VERIFY(deaths == 0);
  LOKA_VERIFY(window.sceneManager()->hasRetiredScenes());
  Scene *replacement = new Scene(Boundary<Root>());
  AdmissionAction action = {&app, &window, current, replacement, &deaths};
  Access::duringPublication(current->focus(), &replaceWhilePublishing, &action);
  app.flush();
  LOKA_VERIFY(window.scene() == replacement);
  LOKA_VERIFY(deaths == 1);
  app.flush();
  LOKA_VERIFY(deaths == 2);
}

void testSceneFocusBusyClose()
{
  Fixture f;
  fixture = &f;
  NullPlatformContext context;
  WindowProps props;
  props.scene(new Scene(Boundary<Root>()));
  NullWindow *window = new NullWindow(&context, props);
  CloseApp app(*window);
  app.flush();
  CloseAction action = {&app, window};
  Access::duringPublication(window->scene()->focus(), &closeWhilePublishing, &action);
  LOKA_VERIFY(app.closed == 0);
  app.operationLoop();
  LOKA_VERIFY(app.closed == 1);
  app.flush();
  LOKA_VERIFY(app.closed == 1);
}

void testSceneFocusReleaseRefusals()
{
#ifdef NDEBUG
  SceneFocus first, second;
  FocusRow row;
  Access::join(first, row);
  Access::join(second, row);
  LOKA_VERIFY(Access::owner(row) == &first);
  LOKA_VERIFY(!Access::head(second));
  Fixture f;
  fixture = &f;
  NullScenePlatformController controller;
  Scene scene((Boundary<Root>()));
  mount(scene, controller);
  Participant retired((ParticipantProps()));
  LifecycleFactTestAccess::MarkSubtreeRetired(&retired);
  const int queries = retired.queries;
  ComponentContext context;
  BoundaryNode::composeSubtree(&retired, context, COMPOSE_EVENT_ATTACH, SceneAccess::rootBoundary(scene));
  LOKA_VERIFY(!Access::owner(retired.row));
  LOKA_VERIFY(retired.queries == queries);
  SceneAccess::unmount(scene);
#else
  printf("[skip] Release refusal no-ops require NDEBUG; debug builds assert on misuse.\n");
#endif
}
