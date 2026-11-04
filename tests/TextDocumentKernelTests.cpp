#include "TextDocumentKernelTests.hpp"
#include "support/TextDocumentAccess.hpp"
#include "testing/app/TextPositionTestAccess.hpp"
#include "support/WindowAdmissionTestApp.hpp"
#include "support/TestVerify.hpp"
#include "platform/null/NullWindow.hpp"
#include "platform/null/NullPlatformContext.hpp"
#include "testing/scene/SceneTestFlow.hpp"
#include "testing/app/SceneManagerTestAccess.hpp"
#include "app/nodes/boundary/StdComposition.hpp"
#include "core/util/StateTrackerGuard.hpp"
#if defined(__linux__) && defined(LOKA_LIFECYCLE_AUDIT) && !defined(NDEBUG) && !defined(__SANITIZE_ADDRESS__)
#include <sys/wait.h>
#include <unistd.h>
#include <csignal>
#endif
namespace
{
  using namespace loka::app;
  using namespace loka::app::scene;
  using namespace loka::app::testing;
  typedef loka::dsl::testing::SceneTestAccess SceneAccess;
  bool refuseCandidate = false;
  bool railLive = true;
  class Root : public BoundaryNodeFor<Root>
  {
  public:
    TextDocument text;
    explicit Root(const BoundaryPropsFor<Root> &p)
        : BoundaryNodeFor<Root>(p)
    {
      this->document(this->text);
    }
    virtual void composeNode(NodeComposition &)
    {
      if (refuseCandidate)
        railLive = false;
    }
  };
  class NestedRoot : public BoundaryNodeFor<NestedRoot>
  {
  public:
    explicit NestedRoot(const BoundaryPropsFor<NestedRoot> &p)
        : BoundaryNodeFor<NestedRoot>(p)
    {
    }
    virtual void composeNode(NodeComposition &c)
    {
      c.declare(Boundary<Root>());
    }
  };
  class Controller : public NullScenePlatformController
  {
  public:
    unsigned creates, reads, applies;
    TextDocument *editOnApply;
    bool refuseApply;
    Controller()
        : creates(0),
          reads(0),
          applies(0),
          editOnApply(0),
          refuseApply(false)
    {
    }
    virtual bool createTextStore(TextDocumentRegistration &registration)
    {
      ++this->creates;
      return NullScenePlatformController::createTextStore(registration);
    }
    virtual bool readTextStore(TextStoreRow &row, TextStoreReading &out)
    {
      ++this->reads;
      return NullScenePlatformController::readTextStore(row, out);
    }
    virtual bool applyTextRequest(TextStoreRow &row, unsigned long target)
    {
      ++this->applies;
      if (this->editOnApply)
        TextDocumentAccess::edit(*this->editOnApply, TextChangedRange(1, 1, 3));
      return !this->refuseApply && NullScenePlatformController::applyTextRequest(row, target);
    }
  };
  WindowProps props()
  {
    WindowProps p;
    p.scene(new Scene(Boundary<Root>()));
    return p;
  }
  class TestWindow : public NullWindow
  {
  public:
    TestWindow(PlatformContext *context, const WindowProps &p, NullScenePlatformController *controller)
        : NullWindow(context, p, controller)
    {
    }

  protected:
    virtual bool hasLiveScenePlatform() const
    {
      return railLive && NullWindow::hasLiveScenePlatform();
    }
  };
  struct Fixture
  {
    Controller controller;
    NullPlatformContext context;
    TestWindow window;
    WindowAdmissionTestApp app;
    Fixture()
        : window(&this->context, props(), &this->controller),
          app(this->window)
    {
      this->app.flush();
      this->app.reconcileAtCompletion();
    }
    Root &root()
    {
      return *static_cast<Root *>(SceneAccess::rootBoundary(*this->window.scene()));
    }
    TextDocument &text()
    {
      return this->root().text;
    }
  };
  void count(void *data)
  {
    ++*static_cast<unsigned *>(data);
  }
} // namespace
void testTextKernelCompletion()
{
  Fixture f;
  TextDocument &doc = f.text();
  LOKA_VERIFY(f.controller.creates == 1);
  LOKA_VERIFY(doc.fact()->get().state() == TEXT_DOCUMENT_READY);
  LOKA_VERIFY(doc.fact()->get().textRevision() == 0);
  unsigned long length = 99;
  LOKA_VERIFY(doc.queryLength(length) && length == 0);
  const TextPosition old = doc.fact()->get().caret();
  unsigned writes = 0;
  doc.fact()->bind(&count, &writes, false);
  TextDocumentAccess::edit(doc, TextChangedRange(0, 0, 8));
  TextDocumentAccess::edit(doc, TextChangedRange(3, 2, 4));
  TextDocumentAccess::selection(doc, 7, 2);
  length = 99;
  LOKA_VERIFY(!doc.queryLength(length) && length == 99);
  int order = 99;
  LOKA_VERIFY(!doc.queryOrder(old, old, order) && order == 99);
  doc.post(old);
  f.app.reconcileAtCompletion();
  LOKA_VERIFY(writes == 1);
  LOKA_VERIFY(doc.fact()->get().textRevision() == 1);
  LOKA_VERIFY(f.controller.applies == 0 && !TextDocumentAccess::pending(doc));
  LOKA_VERIFY(doc.queryLength(length) && length == 10);
  LOKA_VERIFY(!doc.queryOrder(old, doc.fact()->get().caret(), order));
  LOKA_VERIFY(doc.queryOrder(doc.fact()->get().selectionAnchor(), doc.fact()->get().caret(), order) && order == -1);
  TextChangedRange range;
  LOKA_VERIFY(doc.fact()->get().queryChangedRange(range));
  LOKA_VERIFY(range == TextChangedRange(0, 0, 10));
  // No view exists: the document still delivers a current caret request.
  doc.post(doc.fact()->get().selectionAnchor());
  f.app.reconcileAtCompletion();
  LOKA_VERIFY(f.controller.applies == 1 && writes == 2);
  LOKA_VERIFY(doc.fact()->get().caret() == doc.fact()->get().selectionAnchor());
  f.app.reconcileAtCompletion();
  LOKA_VERIFY(writes == 2);
  doc.fact()->unbind(&count, &writes);
}
void testTextKernelCreationFailure()
{
  Fixture source;
  failTextStoreCreations(1);
  Fixture f;
  TextDocument &doc = f.text();
  doc.post(source.text().fact()->get().caret());
  f.app.reconcileAtCompletion();
  LOKA_VERIFY(doc.fact()->get().state() == TEXT_DOCUMENT_UNAVAILABLE);
  LOKA_VERIFY(!TextDocumentAccess::pending(doc));
  for (unsigned i = 0; i != 3; ++i)
  {
    f.app.flush();
    f.app.reconcileAtCompletion();
  }
  LOKA_VERIFY(f.controller.creates == 1 && f.controller.applies == 0);
  SceneAccess::projectInitial(*f.window.scene());
  LOKA_VERIFY(f.controller.creates == 1);
  unsigned long length = 11;
  LOKA_VERIFY(!doc.queryLength(length) && length == 11);
}
void testTextKernelCandidate()
{
  Fixture f;
  Scene *old = f.window.scene();
  refuseCandidate = true;
  Scene *candidate = new Scene(Boundary<Root>());
  LOKA_VERIFY(f.window.sceneManager()->commitTransaction(0, candidate));
  f.app.flush();
  refuseCandidate = false;
  railLive = true;
  LOKA_VERIFY(SceneManagerTestAccess::lastPrepareRefusal(*f.window.sceneManager()) == candidate);
  LOKA_VERIFY(f.window.scene() == old && f.controller.creates == 1);
  LOKA_VERIFY(!SceneAccess::rootNode(*candidate));
}
void testTextKernelReplacement()
{
  Fixture f;
  TextDocumentAccess::edit(f.text(), TextChangedRange(0, 0, 9));
  f.app.reconcileAtCompletion();
  Scene *candidate = new Scene(Boundary<Root>());
  LOKA_VERIFY(f.window.sceneManager()->commitTransaction(0, candidate));
  f.app.flush();
  f.app.reconcileAtCompletion();
  LOKA_VERIFY(f.window.scene() == candidate && f.controller.creates == 2);
  LOKA_VERIFY(f.text().fact()->get().state() == TEXT_DOCUMENT_READY);
  unsigned long length = 99;
  LOKA_VERIFY(f.text().queryLength(length) && length == 0);
  TextDocumentAccess::edit(f.text(), TextChangedRange(0, 0, 4));
  f.app.flush();
  f.app.reconcileAtCompletion();
  LOKA_VERIFY(f.text().queryLength(length) && length == 4);
}
namespace
{
  void nonRoot(bool verifyRefusal = true)
  {
    NullPlatformContext context;
    Controller controller;
    WindowProps p;
    p.scene(new Scene(Boundary<NestedRoot>()));
    NullWindow window(&context, p, &controller);
    WindowAdmissionTestApp app(window);
    app.flush();
    app.reconcileAtCompletion();
    Root *child = static_cast<Root *>(SceneAccess::rootBoundary(*window.scene())->childrenHead());
    if (verifyRefusal)
    {
      LOKA_VERIFY(child && child->text.fact()->get().state() == TEXT_DOCUMENT_UNAVAILABLE);
      LOKA_VERIFY(controller.creates == 0);
    }
  }
} // namespace
void testTextKernelNonRoot()
{
#if defined(LOKA_LIFECYCLE_AUDIT) && !defined(NDEBUG)
#if defined(__linux__) && !defined(__SANITIZE_ADDRESS__)
  const pid_t child = fork();
  LOKA_VERIFY(child >= 0);
  if (!child)
  {
    nonRoot(false);
    _exit(0);
  }
  int status = 0;
  LOKA_VERIFY(waitpid(child, &status, 0) == child);
  LOKA_VERIFY(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
#else
  (void)&nonRoot;
  std::printf("[skip] document audit death pin requires Linux without ASan.\n");
#endif
#else
  nonRoot();
#endif
}
void testTextKernelVisibility()
{
  NullPlatformContext context;
  NullWindow window(&context, props());
  WindowAdmissionTestApp app(window);
  app.flush();
  app.reconcileAtCompletion();
  Root *root = static_cast<Root *>(SceneAccess::rootBoundary(*window.scene()));
  TextDocumentAccess::edit(root->text, TextChangedRange(0, 0, 8));
  app.reconcileAtCompletion();
  LOKA_VERIFY(root->text.fact()->get().textRevision() == 1);
  {
    loka::core::StateTrackerGuard guard(window.getTracker());
    window.visibilityState().set(false);
  }
  app.flush();
  LOKA_VERIFY(!SceneAccess::rootNode(*window.scene()));
  {
    loka::core::StateTrackerGuard guard(window.getTracker());
    window.visibilityState().set(true);
  }
  app.flush();
  app.reconcileAtCompletion();
  root = static_cast<Root *>(SceneAccess::rootBoundary(*window.scene()));
  LOKA_VERIFY(root->text.fact()->get().state() == TEXT_DOCUMENT_READY);
  LOKA_VERIFY(root->text.fact()->get().textRevision() == 0);
  unsigned long length = 99;
  LOKA_VERIFY(root->text.queryLength(length) && length == 0);
}
void testTextKernelReclaim()
{
  Fixture f;
  unsigned writes = 0;
  f.text().fact()->bind(&count, &writes, false);
  f.controller.destroy();
  LOKA_VERIFY(!TextDocumentAccess::store(f.text()));
  f.controller.drainNativeRetirements();
  LOKA_VERIFY(writes == 0);
  unsigned long length = 99;
  LOKA_VERIFY(!f.text().queryLength(length));
  f.text().fact()->unbind(&count, &writes);
  // Both endpoint destruction orders and repeated cuts are callback-free.
  TextStoreLink link;
  {
    TextStoreLedger ledger;
    NullTextStore *row = new NullTextStore(ledger, f.controller);
    row->connect(link);
    row->retire();
    row->retire();
    LOKA_VERIFY(!link.row());
    ledger.drain();
  }
  {
    TextStoreLedger ledger;
    NullTextStore *row = new NullTextStore(ledger, f.controller);
    row->connect(link);
  }
  LOKA_VERIFY(!link.row());
  {
    TextStoreLedger ledger;
    NullTextStore *row = new NullTextStore(ledger, f.controller);
    {
      TextStoreLink other;
      row->connect(other);
    }
    ledger.retireAll();
    ledger.drain();
  }
}
namespace
{
  struct Reentrant
  {
    Fixture &fixture;
    unsigned calls;
    explicit Reentrant(Fixture &f)
        : fixture(f),
          calls(0)
    {
    }
    static void run(void *data)
    {
      Reentrant &self = *static_cast<Reentrant *>(data);
      ++self.calls;
      LOKA_VERIFY(self.fixture.window.scene()->isBusy());
      Scene &scene = *self.fixture.window.scene();
      scene.requestInvalidate(NODE_DIRTY_PROPS);
      LOKA_VERIFY(!scene.flushInvalidation());
      const unsigned reads = self.fixture.controller.reads;
      self.fixture.app.reconcileAtCompletion();
      LOKA_VERIFY(self.fixture.controller.reads == reads);
    }
  };
} // namespace
void testTextKernelGates()
{
  Fixture f;
  TextDocumentAccess::edit(f.text(), TextChangedRange(0, 0, 2));
  const unsigned reads = f.controller.reads;
  {
    BorrowScope borrow(f.controller);
    f.app.reconcileAtCompletion();
    LOKA_VERIFY(f.controller.reads == reads);
  }
  LOKA_VERIFY(f.text().fact()->get().textRevision() == 0);
  Reentrant probe(f);
  f.text().fact()->bind(&Reentrant::run, &probe, false);
  f.app.reconcileAtCompletion();
  LOKA_VERIFY(probe.calls == 1 && f.text().fact()->get().textRevision() == 1);
  f.text().fact()->unbind(&Reentrant::run, &probe);
}

void testTextKernelReadApplyRead()
{
  Fixture f;
  TextDocument &doc = f.text();
  TextDocumentAccess::edit(doc, TextChangedRange(0, 0, 5));
  unsigned writes = 0;
  doc.fact()->bind(&count, &writes, false);
  // The publisher compares against the post-read revision. This test-only
  // position permits an apply even though the first reading changed the body.
  doc.post(TextPositionTestAccess::make(1, 2));
  f.controller.editOnApply = &doc;
  f.controller.refuseApply = true;
  f.app.reconcileAtCompletion();
  LOKA_VERIFY(f.controller.applies == 1 && writes == 1);
  LOKA_VERIFY(doc.fact()->get().textRevision() == 1);
  TextChangedRange range;
  LOKA_VERIFY(doc.fact()->get().queryChangedRange(range));
  LOKA_VERIFY(range == TextChangedRange(0, 0, 7));
  unsigned long length = 0;
  LOKA_VERIFY(doc.queryLength(length) && length == 7);
  LOKA_VERIFY(!TextDocumentAccess::pending(doc));
  doc.fact()->unbind(&count, &writes);
}
