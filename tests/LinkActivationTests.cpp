#include "LinkActivationTests.hpp"
#include "testing/app/AppTestAccess.hpp"
#include "app/nodes/AttributedText.hpp"
#include "app/nodes/boundary/StdComposition.hpp"
#include "app/nodes/nestable/Show.hpp"
#include "app/scene/state/FlowSlot.hpp"
#include "app/url/OpenWebLink.hpp"
#include "app/url/URLOpener.hpp"
#include "platform/null/NullInputDoor.hpp"
#include "platform/null/NullWindow.hpp"
#include "platform/null/NullPlatformContext.hpp"
#include "platform/null/NullScenePlatformController.hpp"
#include "testing/scene/SceneTestFlow.hpp"
#include "support/WindowAdmissionTestApp.hpp"
#include "support/LifecycleFactTestAccess.hpp"
#include "support/TestVerify.hpp"
#include <cstdio>
#if defined(__linux__) && !defined(__SANITIZE_ADDRESS__) && !defined(NDEBUG)
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#endif

namespace
{
  using namespace loka::app;
  using namespace loka::app::scene;
  using namespace loka::core;
  using namespace loka::dsl;
  LinkTarget anchor()
  {
    return LinkTarget(String("#intro"));
  }
  LinkTarget web()
  {
    return LinkTarget(String("https://example.com/"));
  }
  AttributedString links()
  {
    AttributedString::Builder b(4);
    LOKA_VERIFY(b.appendLink(String("intro"), Link, anchor()));
    LOKA_VERIFY(b.appendLink(String("web"), Link, web()));
    LOKA_VERIFY(b.append(String("plain"), TextStyle()));
    LOKA_VERIFY(b.appendLink(String(""), Link, anchor()));
    return b.build();
  }
  AttributedTextNode *findText(Node *node)
  {
    if (node->asAttributedTextNode())
      return node->asAttributedTextNode();
    INestable *children = node->asNestable();
    for (Node *child = children ? children->childrenHead() : 0; child; child = child->nextInComposition)
      if (AttributedTextNode *found = findText(child))
        return found;
    return 0;
  }
  void (*retiringAction)(void *) = 0;
  void *retiringData = 0;
  unsigned retiredRuns = 0;
  class LinkRoot;
  LinkRoot *root = 0;
  class LinkRoot : public BoundaryNodeFor<LinkRoot>
  {
  public:
    NodeState<LinkTarget> a, b;
    NodeState<bool> shown;
    FlowSlot<FlowChain<LinkTarget, bool> > flow;
    URLOpener<1> opener;
    unsigned notificationsA, notificationsB, runs;
    int mode;
    bool ready;
    AttributedTextNode *paragraph;
    NullAttributedTextContext *input;
    explicit LinkRoot(const BoundaryPropsFor<LinkRoot> &p)
        : BoundaryNodeFor<LinkRoot>(p),
          flow(*this),
          notificationsA(0),
          notificationsB(0),
          runs(0),
          mode(0),
          ready(false),
          paragraph(0),
          input(0)
    {
      root = this;
      this->state(this->a, LinkTarget());
      this->state(this->b, LinkTarget());
      this->state(this->shown, true);
    }
    virtual void declareBindings(BindingToken &token)
    {
      token.watch(*this->a.state(), this, &LinkRoot::countA);
      token.watch(*this->b.state(), this, &LinkRoot::countB);
    }
    virtual void composeNode(NodeComposition &c)
    {
      this->flow
          .set(Flow() | Match<LinkTarget, bool>(1).arm(&isAnchor, this, &handle).otherwise(&OpenWebLink, &this->opener))
          .bindTrigger(this->a.state())
          .withTracker(this->tracker());
      c.declare(Show(*this->shown.state()) << AttributedText(links()).activatedLink(this->a));
    }
    void countA()
    {
      ++this->notificationsA;
    }
    void countB()
    {
      ++this->notificationsB;
    }
    static bool isAnchor(const LinkTarget &value, void *)
    {
      return value.startsWith("#");
    }
    static StepRunStatus handle(const LinkTarget &, bool &out, FlowError &, void *p)
    {
      LinkRoot &self = *static_cast<LinkRoot *>(p);
      ++self.runs;
      out = true;
      if (self.mode == 1)
      {
        AttributedText replacement(Linked(String("new"), Link, web()));
        replacement.activatedLink(self.b);
        LOKA_VERIFY(replacement.applyPropsToNode(self.paragraph));
        LOKA_VERIFY(NullInputDoor::activateLink(*self.input, 0));
      }
      if (self.mode == 2 && !self.ready)
        return FLOW_STEP_PENDING;
      if (self.mode == 3)
      {
        AttributedText replacement(Linked(String("new"), Link, web()));
        replacement.activatedLink(self.a);
        LOKA_VERIFY(replacement.applyPropsToNode(self.paragraph));
        LOKA_VERIFY(NullInputDoor::activateLink(*self.input, 0));
      }
      if (self.mode == 4)
      {
        ++retiredRuns;
        retiringAction(retiringData);
      }
      return FLOW_STEP_SUCCEEDED;
    }
  };
  struct Fixture
  {
    NullPlatformContext context;
    NullScenePlatformController platform;
    NullWindow window;
    WindowAdmissionTestApp app;
    static WindowProps props()
    {
      WindowProps p;
      p.scene(new Scene(Boundary<LinkRoot>()));
      return p;
    }
    Fixture()
        : window(&context, props(), &platform),
          app(window)
    {
      this->app.flush();
      root->paragraph = findText(loka::dsl::testing::SceneTestAccess::rootBoundary(*this->window.scene()));
      LOKA_VERIFY(root->paragraph);
      LayoutState layout;
      layout.width = 200;
      this->platform.projectLayoutForTesting(root->paragraph, layout);
      root->input = static_cast<NullAttributedTextContext *>(root->paragraph->getContext());
      LOKA_VERIFY(root->input);
    }
    bool activate(std::size_t index)
    {
      return NullInputDoor::activateLink(*root->input, index);
    }
  };
} // namespace

void testLinkActivationDelivery()
{
  Fixture f;
  Operation::Regime regime;
  Operation turn;
  LOKA_VERIFY(f.activate(0));
  LOKA_VERIFY(root->a.get() == anchor() && root->notificationsA == 1 && root->runs == 1);
  LOKA_VERIFY(f.activate(0));
  LOKA_VERIFY(root->notificationsA == 2 && root->runs == 2);
  LOKA_VERIFY(f.activate(1));
  LOKA_VERIFY(root->a.get() == web() && root->opener.pending() == 1);
  for (unsigned i = 2; i != 5; ++i)
    LOKA_VERIFY(!f.activate(i));
  LOKA_VERIFY(root->notificationsA == 3 && root->a.get() == web());
  AttributedText noBox(links());
  LOKA_VERIFY(noBox.applyPropsToNode(root->paragraph));
  LOKA_VERIFY(!f.activate(0));
  LOKA_VERIFY(root->notificationsA == 3);
  AttributedText noText;
  noText.activatedLink(root->a);
  LOKA_VERIFY(noText.applyPropsToNode(root->paragraph));
  LOKA_VERIFY(!f.activate(0));
  LOKA_VERIFY(root->notificationsA == 3);
}

void testLinkActivationProps()
{
  Fixture f;
  Operation::Regime regime;
  Operation turn;
  LOKA_VERIFY(f.activate(0));
  LOKA_VERIFY(root->notificationsA == 1);
  MutableState<AttributedString> borrowed(links());
  for (unsigned owned = 0; owned != 2; ++owned)
  {
    AttributedTextProps a = owned ? AttributedTextProps(links()) : AttributedTextProps(&borrowed);
    a.activatedLink(root->a);
    AttributedTextProps b(a);
    LOKA_VERIFY(b.activatedLink_.state() == root->a.state());
    b.activatedLink(root->b);
    LOKA_VERIFY((a < b) || (b < a));
    a = b;
    LOKA_VERIFY(a.activatedLink_.state() == root->b.state());
    LOKA_VERIFY(a.text_ == (owned ? &a.ownedText : &borrowed));
    AttributedTextDefinitionWithAttr styled = (AttributedText(a) + BlockStyle()).activatedLink(root->b);
    LOKA_VERIFY(styled.applyPropsToNode(root->paragraph));
    LOKA_VERIFY(f.activate(0));
    LOKA_VERIFY(root->notificationsA == 1 && root->notificationsB == owned + 1);
  }
  AttributedTextDefinitionWithAttr styled = AttributedText(links()).activatedLink(root->a) + BlockStyle();
  LOKA_VERIFY(styled.props.activatedLink_.state() == root->a.state());
}

void testLinkActivationReentrancy()
{
  for (int mode = 1; mode <= 3; ++mode)
  {
    Fixture f;
    Operation::Regime regime;
    Operation turn;
    root->mode = mode;
    LOKA_VERIFY(f.activate(0));
    LOKA_VERIFY(root->runs == 1);
    if (mode == 1)
    {
      LOKA_VERIFY(root->notificationsA == 1 && root->notificationsB == 1);
      LOKA_VERIFY(root->a.get() == anchor() && root->b.get() == web());
    }
    if (mode == 3)
    {
      LOKA_VERIFY(root->notificationsA == 2 && root->a.get() == web());
      LOKA_VERIFY(root->opener.pending() == 0);
    }
    if (mode == 2)
    {
      LOKA_VERIFY(f.activate(1));
      LOKA_VERIFY(root->runs == 1 && root->a.get() == web() && root->opener.pending() == 0);
      root->ready = true;
      LOKA_VERIFY(root->flow.resumeResult(1) == FLOW_RUN_SUCCEEDED);
      LOKA_VERIFY(root->runs == 2 && root->opener.pending() == 0);
      LOKA_VERIFY(f.activate(0));
      LOKA_VERIFY(root->runs == 3 && root->notificationsA == 3);
    }
  }
}

void testLinkActivationLifecycle()
{
  Fixture f;
  Operation::Regime regime;
  {
    Operation turn;
    LOKA_VERIFY(f.activate(0));
    NotifySubtreeNodeDetached(root->paragraph);
    LOKA_VERIFY(root->paragraph->lifecycleFact() == NODE_FACT_DETACHED_RETAINED);
    LOKA_VERIFY(!f.activate(0));
    LOKA_VERIFY(root->notificationsA == 1);
  }
  {
    Operation turn;
    root->shown.set(false);
  }
  f.app.operationLoop();
  {
    Operation turn;
    root->shown.set(true);
  }
  f.app.operationLoop();
  LOKA_VERIFY(root->notificationsA == 1);
  {
    Operation turn;
    LOKA_VERIFY(f.activate(0));
    LOKA_VERIFY(root->notificationsA == 2);
    LifecycleFactTestAccess::MarkSubtreeRetired(root->paragraph);
    LOKA_VERIFY(!f.activate(0));
    LOKA_VERIFY(root->notificationsA == 2);
  }
}

void testLinkActivationOpenWebLink()
{
  URLOpener<1> opener;
  FlowError error;
  bool posted = true;
  LOKA_VERIFY(OpenWebLink(anchor(), posted, error, &opener) == FLOW_STEP_SUCCEEDED);
  LOKA_VERIFY(!posted && opener.pending() == 0);
  posted = true;
  LOKA_VERIFY(OpenWebLink(web(), posted, error, 0) == FLOW_STEP_SUCCEEDED);
  LOKA_VERIFY(!posted);
  LOKA_VERIFY(OpenWebLink(web(), posted, error, &opener) == FLOW_STEP_SUCCEEDED);
  LOKA_VERIFY(posted && opener.pending() == 1);
  LOKA_VERIFY(OpenWebLink(web(), posted, error, &opener) == FLOW_STEP_SUCCEEDED);
  LOKA_VERIFY(!posted && opener.pending() == 1);
}

void testLinkActivationRegimes()
{
  {
    Fixture f;
    LOKA_VERIFY(f.activate(0));
    LOKA_VERIFY(root->notificationsA == 1 && root->tracker()->phase() == TRACKER_IDLE);
  }
#if defined(NDEBUG)
  Fixture f;
  Operation::Regime regime;
  LOKA_VERIFY(Operation::openActive(root->tracker()) == OPEN_NO_CLOCK);
  LOKA_VERIFY(f.activate(0));
  LOKA_VERIFY(root->notificationsA == 1 && root->tracker()->phase() == TRACKER_IDLE);
#elif defined(__linux__) && !defined(__SANITIZE_ADDRESS__)
  const pid_t child = fork();
  LOKA_VERIFY(child >= 0);
  if (child == 0)
  {
    Fixture f;
    Operation::Regime regime;
    f.activate(0);
    _exit(0);
  }
  int status = 0;
  LOKA_VERIFY(waitpid(child, &status, 0) == child);
  LOKA_VERIFY(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
#else
  std::printf("[skip] activation regime death pin requires Linux debug without ASan.\n");
#endif
}

namespace
{
  class ActivationLayout : public NullScenePlatformController
  {
  public:
    bool inject;
    ActivationLayout()
        : inject(false)
    {
    }
    virtual bool prepareProjectedLayout(Node *node, LayoutState &state)
    {
      const bool result = NullScenePlatformController::prepareProjectedLayout(node, state);
      if (this->inject && node->asAttributedTextNode())
      {
        BorrowScope borrow(*this);
        // Characterization of misuse: an open borrow does not prove input.
        LOKA_VERIFY(node->asAttributedTextNode()->writeActivatedLink(0));
      }
      return result;
    }
  };
  class DirtyCount : public DirtySourceRegistrar
  {
  public:
    unsigned calls;
    DirtyCount()
        : calls(0)
    {
    }
    virtual void markDirtyOnOwnInput(NodeDirtyFlags) {}
    virtual void markDirtyOnChange(StateBase *, NodeDirtyFlags)
    {
      ++this->calls;
    }
  };
  void retireScene(void *data)
  {
    Fixture &f = *static_cast<Fixture *>(data);
    AttributedTextNode *paragraph = root->paragraph;
    NullAttributedTextContext *input = root->input;
    // Existing Flow withdrawal fixture: retire synchronously, reclaim only
    // after the running arm returns through the real Scene replacement clock.
    LifecycleFactTestAccess::MarkSubtreeRetired(root);
    LOKA_VERIFY(paragraph->lifecycleFact() == NODE_FACT_RETIRED);
    LOKA_VERIFY(!NullInputDoor::activateLink(*input, 0));
    LOKA_VERIFY(f.window.sceneManager()->commitTransaction(0, new Scene(Boundary<LinkRoot>())));
  }

} // namespace
void testLinkActivationPlacement()
{
  Fixture f;
  ActivationLayout layout;
  Operation::Regime regime;
  Operation turn;
  LayoutState bounds;
  bounds.width = 200;
  f.platform.projectLayoutForTesting(root->paragraph, bounds);
  f.window.scene()->requestInvalidate();
  turn.settle();
  f.app.admitAndApplyWindows();
  LOKA_VERIFY(root->notificationsA == 0);
  DirtyCount dirty;
  root->paragraph->declareDirtySources(dirty);
  LOKA_VERIFY(dirty.calls == 0);
  layout.inject = true;
  layout.prepareProjectedLayout(root->paragraph, bounds);
  LOKA_VERIFY(root->notificationsA == 1);
}
void testLinkActivationRetiringArm()
{
  for (unsigned close = 0; close != 1; ++close)
  {
    Fixture f;
    Operation::Regime regime;
    retiringAction = &retireScene;
    retiringData = &f;
    retiredRuns = 0;
    root->mode = 4;
    {
      Operation turn;
      LOKA_VERIFY(f.activate(0));
      LOKA_VERIFY(retiredRuns == 1);
    }
    f.app.operationLoop();
    LOKA_VERIFY(retiredRuns == 1);
  }
  retiringAction = 0;
  retiringData = 0;
}

#include "app/core/AppComposition.hpp"
namespace
{
  class LinkConfig : public AppConfigurable
  {
  public:
    URLOpener<1> opener;
    explicit LinkConfig(PlatformContext &context)
        : AppConfigurable(&context)
    {
    }
    virtual void compose(AppComposition &c)
    {
      c << OpenURLs(this->opener);
    }
  };
} // namespace
void testLinkActivationCompletion()
{
  Fixture f;
  LinkConfig config(f.context);
  NullWindow window(&f.context, WindowProps());
  WindowAdmissionTestApp app(window, 0, &config);
  root->flow.set(Flow() | Match<LinkTarget, bool>(1).otherwise(&OpenWebLink, &config.opener))
      .bindTrigger(root->a.state())
      .withTracker(root->tracker());
  Operation::Regime regime;
  {
    Operation turn;
    LOKA_VERIFY(f.activate(1));
    LOKA_VERIFY(config.opener.pending() == 1 && f.context.openedURLs().empty());
  }
  loka::app::testing::AppTestAccess::completeTurn(app);
  LOKA_VERIFY(config.opener.pending() == 0 && f.context.openedURLs().size() == 1);
  LOKA_VERIFY(LinkTarget(config.opener.outcome().get().url.text()) == web());
}

#include "platform/null/NullApp.hpp"
#include "testing/app/AppTestAccess.hpp"
namespace
{
  class ClosingConfig : public AppConfigurable
  {
  public:
    explicit ClosingConfig(PlatformContext &context)
        : AppConfigurable(&context)
    {
    }
    virtual void compose(AppComposition &c)
    {
      c << WindowDef(WindowProps().scene(new Scene(Boundary<LinkRoot>())));
    }
  };
  void closeOwnedWindow(void *data)
  {
    NullApp &app = *static_cast<NullApp *>(data);
    app.requestWindowClose(app.adoptedWindows()[0]);
  }
} // namespace
void testLinkActivationClosingArm()
{
  NullPlatformContext context;
  ClosingConfig config(context);
  NullApp app(&config);
  app.run();
  loka::app::testing::AppTestAccess::completeTurn(app);
  NullWindow *window = static_cast<NullWindow *>(app.adoptedWindows()[0]);
  root->paragraph = findText(loka::dsl::testing::SceneTestAccess::rootBoundary(*window->scene()));
  root->input = static_cast<NullAttributedTextContext *>(root->paragraph->getContext());
  LOKA_VERIFY(root->input);
  root->mode = 4;
  retiringAction = &closeOwnedWindow;
  retiringData = &app;
  retiredRuns = 0;
  Operation::Regime regime;
  {
    Operation turn;
    LOKA_VERIFY(NullInputDoor::activateLink(*root->input, 0));
    LOKA_VERIFY(retiredRuns == 1);
    LOKA_VERIFY(loka::app::testing::AppTestAccess::pendingWindowCloseCount(app) == 1);
  }
  loka::app::testing::AppTestAccess::completeTurn(app);
  LOKA_VERIFY(retiredRuns == 1 && loka::app::testing::AppTestAccess::pendingWindowCloseCount(app) == 0);
  retiringAction = 0;
  retiringData = 0;
}
