#include "support/MenuStructure.hpp"
#include "SceneMenuBarTests.hpp"

#include "app/nodes/boundary/StdComposition.hpp"
#include "app/scene/node/ComponentNode.hpp"
#include "platform/null/NullPlatformContext.hpp"
#include "platform/null/NullWindow.hpp"
#include "support/WindowAdmissionTestApp.hpp"
#include "support/TestVerify.hpp"
#include "testing/scene/SceneTestFlow.hpp"
#include "testing/app/SceneManagerTestAccess.hpp"
#if defined(__linux__) && !defined(__SANITIZE_ADDRESS__)
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#endif

namespace SceneMenuBarTests
{
  using namespace loka::app;
  using namespace loka::app::scene;
  typedef loka::dsl::testing::SceneTestAccess Access;

  enum Declaration { NORMAL, SECOND, CHILD, COMPONENT };
  class ValueState : public loka::core::MutableState<bool>
  {
  public:
    explicit ValueState(bool value) : loka::core::MutableState<bool>(value) {}
    size_t subscriptions() const { return this->deferredHandlers.size(); }
  };
  struct Fixture
  {
    Fixture() : declaration(NORMAL), refuseNode(false), composes(0), calls(0), detaches(0),
                disconnectedAtDetach(false), enabled(true), checked(false), attachment(0) {}
    Declaration declaration;
    bool refuseNode;
    int composes, calls, detaches;
    bool disconnectedAtDetach;
    ValueState enabled, checked;
    NullMenuAttachment *attachment;
    std::vector<bool> refusalWhiteFlags;
  };
  static Fixture *fixture = 0;

  inline MenuBarDefinition bar(loka::core::EmitterState *emitter = 0)
  {
    MenuBarDefinition result;
    MenuDefinition menu("File");
    MenuItemDefinition item("Run");
    item.enabled(&fixture->enabled).onClick(emitter);
    menu << item.attr(MenuItemAttr().checked(&fixture->checked));
    result << menu;
    return result;
  }

  class DetachProbe;
  struct ProbeTag {};
  struct ProbeProps : NodePropsBase<ProbeProps>
  {
    typedef ProbeTag TypeTag;
    typedef DetachProbe NodeType;
    bool operator<(const PropsBase &rhs) const { return this->propsTypeId() < rhs.propsTypeId(); }
  };
  class DetachProbe : public ComponentNodeWithProps<ProbeProps>
  {
  public:
    explicit DetachProbe(const ProbeProps &p) : ComponentNodeWithProps<ProbeProps>(p) {}
    virtual void composeChildren(NodeComposition &c)
    {
      if (fixture->declaration == COMPONENT)
      {
        const bool accepted = c.menuBar(bar());
#if !defined(LOKA_LIFECYCLE_AUDIT) || defined(NDEBUG)
        LOKA_VERIFY(!accepted);
#else
        (void)accepted;
#endif
      }
      c.declare(FragmentDefinition());
    }
    virtual void detachNode(NodeComposition &)
    {
      if (fixture->attachment)
      {
        ++fixture->detaches;
        fixture->disconnectedAtDetach = !fixture->attachment->connected();
        LOKA_VERIFY(fixture->disconnectedAtDetach);
      }
    }
  };

  struct RefusableNode : FragmentDefinition
  {
    typedef RefusableNode CloneType;
    virtual NodeDefinitionBase *clone() const { return new RefusableNode(*this); }
    virtual size_t nodeSize() const { return 0; }
    virtual Node *create() const
    {
      return fixture->refuseNode ? 0 : FragmentDefinition::create();
    }
  };

  class Child : public BoundaryNodeFor<Child>
  {
  public:
    explicit Child(const BoundaryPropsFor<Child> &p) : BoundaryNodeFor<Child>(p) {}
    virtual void composeNode(NodeComposition &c)
    {
      const bool accepted = c.menuBar(bar());
#if !defined(LOKA_LIFECYCLE_AUDIT) || defined(NDEBUG)
      LOKA_VERIFY(!accepted);
#else
      (void)accepted; // Only the production assertion may satisfy the death pin.
#endif
      c.declare(FragmentDefinition());
    }
  };

  class Root : public BoundaryNodeFor<Root>
  {
  public:
    explicit Root(const BoundaryPropsFor<Root> &p) : BoundaryNodeFor<Root>(p) {}
    virtual void detachNode(NodeComposition &c)
    {
      if (c.scene() && !Access::composed(*c.scene()))
      {
        fixture->refusalWhiteFlags.push_back(Access::whiteFlagFullRebuildPending(*c.scene()));
        LOKA_VERIFY(!c.scene()->menuBar());
      }
    }
    virtual void declareBindings(BindingToken &token) { token.action(this->clicked_, this, &Root::clicked); }
    virtual void composeNode(NodeComposition &c)
    {
      ++fixture->composes;
      c.menuBar(bar(&this->clicked_));
      if (fixture->declaration == SECOND)
      {
        const bool accepted = c.menuBar(MenuBarDefinition());
#if !defined(LOKA_LIFECYCLE_AUDIT) || defined(NDEBUG)
        LOKA_VERIFY(!accepted);
#else
        (void)accepted; // Only the production assertion may satisfy the death pin.
#endif
      }
      RefusableNode content;
      content << NodeDefinition<ProbeProps, DetachProbe>(ProbeProps());
      if (fixture->declaration == CHILD)
        content << Boundary<Child>();
      c.declare(content);
    }
  private:
    void clicked() { ++fixture->calls; }
    loka::core::EmitterState clicked_;
  };

  inline WindowProps props()
  {
    WindowProps result;
    result.scene(new Scene(Boundary<Root>()));
    return result;
  }

  inline void writeValues(bool enabled, bool checked)
  {
    loka::core::PushStateTracker tracker;
    tracker.addState(&fixture->enabled);
    tracker.addState(&fixture->checked);
    loka::core::StateTrackerGuard guard(&tracker);
    fixture->enabled.set(enabled);
    fixture->checked.set(checked);
  }

  inline void refusal(Declaration declaration)
  {
    Fixture f;
    fixture = &f;
    f.declaration = declaration;
    NullPlatformContext context;
    NullWindow window(&context, props());
    WindowAdmissionTestApp app(window);
    app.operationLoop();
#if !defined(LOKA_LIFECYCLE_AUDIT) || defined(NDEBUG)
    const MenuBarDefinition *snapshot = window.scene()->menuBar();
    LOKA_VERIFY(snapshot && snapshot->menusCount() == 1);
#endif
  }

  inline void checkRefusal(Declaration declaration)
  {
#if defined(LOKA_LIFECYCLE_AUDIT) && !defined(NDEBUG)
#if defined(__linux__) && !defined(__SANITIZE_ADDRESS__)
    const pid_t child = fork();
    LOKA_VERIFY(child >= 0);
    if (child == 0)
    {
      refusal(declaration);
      _exit(0);
    }
    int status = 0;
    LOKA_VERIFY(waitpid(child, &status, 0) == child);
    LOKA_VERIFY(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
#else
    (void)declaration;
    std::fprintf(stderr, "[skip] Scene menu audit death pin requires Linux without ASan; release pins refusal.\n");
#endif
#else
    refusal(declaration);
#endif
  }
}

void testSceneMenuBarPublishedWithRoot()
{
  using namespace SceneMenuBarTests;
  Fixture f;
  fixture = &f;
  NullPlatformContext context;
  NullWindow window(&context, props());
  WindowAdmissionTestApp app(window);
  app.operationLoop();
  Scene &scene = *window.scene();
  const MenuBarDefinition *snapshot = scene.menuBar();
  LOKA_VERIFY(snapshot);
  const MenuBarDefinition expected = bar(snapshot->menusHead()->itemsHead()->onClickState);
  LOKA_VERIFY(loka::app::testing::EqualsStructure(*snapshot, expected));
  window.teardownScene();
  LOKA_VERIFY(!scene.menuBar());
}

void testSceneMenuBarCloneRefusalKeepsOldScene()
{
  using namespace SceneMenuBarTests;
  Fixture f;
  fixture = &f;
  NullPlatformContext context;
  NullWindow window(&context, props());
  WindowAdmissionTestApp app(window);
  app.operationLoop();
  Scene *old = window.scene();
  const MenuBarDefinition *oldBar = old->menuBar();
  LOKA_VERIFY(oldBar);
  NullMenuAttachment &attachment = window.scenePlatformController()->menuAttachment();
  LOKA_VERIFY(attachment.open(*oldBar));
  Scene *candidate = new Scene(Boundary<Root>());
  loka::app::testing::failMenuBarDefinitionClones(100);
  LOKA_VERIFY(window.sceneManager()->commitTransaction(0, candidate));
  app.operationLoop();
  LOKA_VERIFY(window.scene() == old && old->menuBar() == oldBar);
  LOKA_VERIFY(!Access::composed(*candidate) && !candidate->menuBar());
  LOKA_VERIFY(f.refusalWhiteFlags.size() == 2 && f.refusalWhiteFlags[0] && f.refusalWhiteFlags[1]);
  LOKA_VERIFY(attachment.connected());
  attachment.dispatch(1);
  LOKA_VERIFY(f.calls == 1);
  app.operationLoop();
  LOKA_VERIFY(!Access::composed(*candidate) && f.refusalWhiteFlags.size() == 4);
  LOKA_VERIFY(f.refusalWhiteFlags[2] && f.refusalWhiteFlags[3]);
  loka::app::testing::allowMenuBarDefinitionClones();
  app.operationLoop();
  LOKA_VERIFY(window.scene() == candidate && candidate->menuBar());

  // Mirror: the menu capture succeeds but child node allocation refuses.
  f.refuseNode = true;
  Scene *nodeRefusal = new Scene(Boundary<Root>());
  const MenuBarDefinition *installed = candidate->menuBar();
  LOKA_VERIFY(window.sceneManager()->commitTransaction(0, nodeRefusal));
  app.operationLoop();
  LOKA_VERIFY(window.scene() == candidate && candidate->menuBar() == installed);
  LOKA_VERIFY(!Access::composed(*nodeRefusal) && !nodeRefusal->menuBar());
  LOKA_VERIFY(f.refusalWhiteFlags.size() == 6 && f.refusalWhiteFlags[4] && f.refusalWhiteFlags[5]);
  f.refuseNode = false;
  app.operationLoop();
  LOKA_VERIFY(window.scene() == nodeRefusal && nodeRefusal->menuBar());

  // Inspect before SceneManager's refusal cleanup can clear a half-published
  // snapshot: initial NullWindow mount leaves the refused Scene mounted.
  f.refuseNode = true;
  {
    NullWindow mirror(&context, props());
    LOKA_VERIFY(!Access::composed(*mirror.scene()) && !mirror.scene()->menuBar());
    LOKA_VERIFY(Access::whiteFlagFullRebuildPending(*mirror.scene()));
  }
  f.refuseNode = false;
}

void testSceneMenuBarSecondDeclarationRefused()
{
  SceneMenuBarTests::checkRefusal(SceneMenuBarTests::SECOND);
}
void testSceneMenuBarFromChildBoundaryRefused()
{
  SceneMenuBarTests::checkRefusal(SceneMenuBarTests::CHILD);
  SceneMenuBarTests::checkRefusal(SceneMenuBarTests::COMPONENT);
}

void testSceneMenuBarOneComposePerMount()
{
  using namespace SceneMenuBarTests;
  Fixture f;
  fixture = &f;
  NullPlatformContext context;
  NullWindow window(&context, props());
  WindowAdmissionTestApp app(window);
  app.operationLoop();
  const MenuBarDefinition *first = window.scene()->menuBar();
  LOKA_VERIFY(first && f.composes == 1);
  writeValues(false, true);
  window.scene()->requestInvalidate();
  app.operationLoop();
  LOKA_VERIFY(window.scene()->menuBar() == first && f.composes == 1);
  window.teardownScene();
  LOKA_VERIFY(!window.scene()->menuBar());
  // A fresh clone is required on remount. Allocation address reuse is legal;
  // refusal discriminates a new object from resurrecting the old snapshot.
  loka::app::testing::failMenuBarDefinitionClones(100);
  window.mountScene();
  LOKA_VERIFY(!window.scene()->menuBar() && !Access::composed(*window.scene()));
  window.teardownScene();
  loka::app::testing::allowMenuBarDefinitionClones();
  window.mountScene();
  LOKA_VERIFY(window.scene()->menuBar() && f.composes == 3);
}

void testMenuAttachmentDisconnectsOnDetachBeforeRootTeardown()
{
  using namespace SceneMenuBarTests;
  Fixture f;
  fixture = &f;
  NullPlatformContext context;
  NullWindow window(&context, props());
  WindowAdmissionTestApp app(window);
  app.operationLoop();
  NullMenuAttachment &attachment = window.scenePlatformController()->menuAttachment();
  f.attachment = &attachment;
  LOKA_VERIFY(attachment.open(*window.scene()->menuBar()));
  LOKA_VERIFY(loka::app::testing::EqualsStructure(*attachment.applied(), *window.scene()->menuBar()));
  attachment.dispatch(1);
  LOKA_VERIFY(f.calls == 1 && attachment.subscriptionCount() == 1);
  LOKA_VERIFY(attachment.values(1) == std::make_pair(true, false));
  LOKA_VERIFY(f.enabled.subscriptions() == 1 && f.checked.subscriptions() == 1);
  writeValues(false, true);
  LOKA_VERIFY(attachment.values(1) == std::make_pair(false, true));
  window.teardownScene();
  LOKA_VERIFY(f.detaches == 1 && f.disconnectedAtDetach);
  LOKA_VERIFY(!attachment.connected() && attachment.subscriptionCount() == 0 && !attachment.applied());
  LOKA_VERIFY(f.enabled.subscriptions() == 0 && f.checked.subscriptions() == 0);
  attachment.dispatch(1);
  writeValues(true, false); // A surviving deferred callback would access freed subscription storage.
  LOKA_VERIFY(f.calls == 1);
  attachment.disconnect();
}

void testMenuAttachmentDispatchAfterSceneReplacement()
{
  using namespace SceneMenuBarTests;
  Fixture f;
  fixture = &f;
  NullPlatformContext context;
  NullWindow window(&context, props());
  WindowAdmissionTestApp app(window);
  app.operationLoop();
  NullMenuAttachment &attachment = window.scenePlatformController()->menuAttachment();
  f.attachment = &attachment;
  LOKA_VERIFY(attachment.open(*window.scene()->menuBar()));
  attachment.dispatch(1);
  LOKA_VERIFY(f.calls == 1);
  Scene *replacement = new Scene(Boundary<Root>());
  LOKA_VERIFY(window.sceneManager()->commitTransaction(0, replacement));
  app.operationLoop();
  LOKA_VERIFY(window.scene() == replacement && f.disconnectedAtDetach);
  attachment.dispatch(1);
  LOKA_VERIFY(f.calls == 1 && !attachment.connected());
  LOKA_VERIFY(attachment.open(*replacement->menuBar()));
  attachment.dispatch(1);
  LOKA_VERIFY(f.calls == 2);
  loka::app::testing::failNextMenuBarDefinitionClone();
  const bool opened = attachment.open(*replacement->menuBar());
  // A refused clone keeps the live projection: table, subscriptions, baseline.
  LOKA_VERIFY(!opened && attachment.applied() && attachment.connected());
  LOKA_VERIFY(f.enabled.subscriptions() == 1 && f.checked.subscriptions() == 1);
  attachment.dispatch(1);
  LOKA_VERIFY(f.calls == 3);
}

