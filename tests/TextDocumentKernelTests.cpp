#include "TextDocumentKernelTests.hpp"
#include "support/TextStoreConformance.hpp"
#include "support/BlobAllocationProbe.hpp"
#include "app/TextDocumentFile.hpp"
#include "core/State.hpp"
#ifdef __linux__
#include <unistd.h>
#endif
#include <climits>
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
#include "testing/core/StateTrackerTestAccess.hpp"
#include "testing/app/TextInputCommitTestHooks.hpp"
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
    struct Saved
    {
      loka::file::File file;
      unsigned long revision;
      Saved()
          : revision(0)
      {
      }
    } saved_;
    loka::file::File destination() const
    {
      const TextDocumentOrigin &origin = this->text.fact()->get().origin();
      if (this->saved_.revision >= origin.revision())
        return this->saved_.file;
      loka::file::File result;
      origin.querySource(result);
      return result;
    }
    bool dirty() const
    {
      const TextDocumentFact &fact = this->text.fact()->get();
      return fact.textRevision() != std::max(this->saved_.revision, fact.origin().revision());
    }
    void saved(const loka::file::File &file)
    {
      this->saved_.file = file;
      this->saved_.revision = this->text.fact()->get().textRevision();
    }
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
  class EmptyBoundaryNode : public BoundaryNodeFor<EmptyBoundaryNode>
  {
  public:
    explicit EmptyBoundaryNode(const BoundaryPropsFor<EmptyBoundaryNode> &p)
        : BoundaryNodeFor<EmptyBoundaryNode>(p)
    {
    }
    virtual void composeNode(NodeComposition &) {}
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
    virtual TextStoreBirth
    createTextStore(TextStoreSource &source, TextStoreRow *old, TextStoreRow *&fresh, TextStoreReading &reading)
    {
      ++this->creates;
      return NullScenePlatformController::createTextStore(source, old, fresh, reading);
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
  class TextTestApp : public WindowAdmissionTestApp
  {
  public:
    explicit TextTestApp(Window &window)
        : WindowAdmissionTestApp(window)
    {
    }
    using App::hasQuitAttempt;
  };
  struct Fixture
  {
    Controller controller;
    NullPlatformContext context;
    TestWindow window;
    TextTestApp app;
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
  TextPosition rangeStart, rangeEnd;
  LOKA_VERIFY(doc.fact()->get().queryChangedRange(rangeStart, rangeEnd));
  LOKA_VERIFY(rangeStart == TextPositionTestAccess::make(1, 0));
  LOKA_VERIFY(rangeEnd == TextPositionTestAccess::make(1, 10));
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
  TextPosition rangeStart, rangeEnd;
  LOKA_VERIFY(doc.fact()->get().queryChangedRange(rangeStart, rangeEnd));
  LOKA_VERIFY(rangeStart == TextPositionTestAccess::make(doc.fact()->get().textRevision(), 0));
  LOKA_VERIFY(rangeEnd == TextPositionTestAccess::make(doc.fact()->get().textRevision(), 7));
  unsigned long length = 0;
  LOKA_VERIFY(doc.queryLength(length) && length == 7);
  LOKA_VERIFY(!TextDocumentAccess::pending(doc));
  doc.fact()->unbind(&count, &writes);
}

void testTextCommitMarkedAndRequests()
{
  Fixture f;
  TextDocument &doc = f.text();
  const TextPosition old = doc.fact()->get().caret();
  TextDocumentAccess::uncommitted(doc, "hello");
  TextDocumentAccess::selection(doc, 3, 1);
  TextDocumentAccess::edit(doc, TextChangedRange(0, 0, 2));
  doc.post(old);
  f.app.reconcileAtCompletion();
  LOKA_VERIFY(doc.fact()->get().state() == TEXT_DOCUMENT_MARKED);
  LOKA_VERIFY(doc.fact()->get().textRevision() == 1);
  LOKA_VERIFY(doc.fact()->get().caret() == TextPositionTestAccess::make(1, 3));
  LOKA_VERIFY(doc.fact()->get().selectionAnchor() == TextPositionTestAccess::make(1, 1));
  LOKA_VERIFY(TextDocumentAccess::pending(doc) && f.controller.applies == 0);
  // Even a stale request waits while marked; the newer post wins in that slot.
  const TextPosition newer = doc.fact()->get().selectionAnchor();
  doc.post(newer);
  // A second body edit advances the revision while composition remains marked.
  TextDocumentAccess::edit(doc, TextChangedRange(0, 1, 1));
  f.app.reconcileAtCompletion();
  LOKA_VERIFY(doc.fact()->get().state() == TEXT_DOCUMENT_MARKED);
  LOKA_VERIFY(doc.fact()->get().textRevision() == 2);
  LOKA_VERIFY(doc.fact()->get().caret() == TextPositionTestAccess::make(2, 3));
  LOKA_VERIFY(doc.fact()->get().selectionAnchor() == TextPositionTestAccess::make(2, 1));
  LOKA_VERIFY(TextDocumentAccess::pending(doc) && f.controller.applies == 0);
  unsigned writes = 0;
  doc.fact()->bind(&count, &writes, false);
  LOKA_VERIFY(f.app.commitTextInput() == TEXT_INPUT_SETTLED);
  LOKA_VERIFY(writes == 1 && doc.fact()->get().state() == TEXT_DOCUMENT_READY);
  unsigned long length = 0;
  LOKA_VERIFY(doc.queryLength(length) && length == 7);
  // The command handler reads the committed body after the door returns.
  LOKA_VERIFY(TextDocumentAccess::committedText(doc) == "??hello");
  LOKA_VERIFY(doc.fact()->get().textRevision() == 3);
  LOKA_VERIFY(TextDocumentAccess::pending(doc) && f.controller.applies == 0);
  f.app.reconcileAtCompletion();
  LOKA_VERIFY(!TextDocumentAccess::pending(doc) && f.controller.applies == 0);
  doc.fact()->unbind(&count, &writes);
}

void testTextCommitUnchangedRevision()
{
  Fixture f;
  TextDocument &doc = f.text();
  TextDocumentAccess::edit(doc, TextChangedRange(0, 0, 8));
  TextDocumentAccess::selection(doc, 6, 2);
  f.app.reconcileAtCompletion();
  const TextPosition first = doc.fact()->get().caret();
  const TextPosition newer = doc.fact()->get().selectionAnchor();
  TextDocumentAccess::uncommitted(doc, "");
  doc.post(first);
  f.app.reconcileAtCompletion();
  doc.post(newer);
  LOKA_VERIFY(f.app.commitTextInput() == TEXT_INPUT_SETTLED);
  LOKA_VERIFY(TextDocumentAccess::pending(doc) && f.controller.applies == 0);
  LOKA_VERIFY(doc.fact()->get().textRevision() == 1);
  f.app.reconcileAtCompletion();
  LOKA_VERIFY(f.controller.applies == 1 && !TextDocumentAccess::pending(doc));
  LOKA_VERIFY(doc.fact()->get().caret() == newer);
}

namespace
{
  class CommitApp : public WindowAdmissionTestApp
  {
  public:
    CommitApp(Window &first, Window &second, Window *third = 0)
        : WindowAdmissionTestApp(first, &second)
    {
      if (third)
        this->add(*third);
    }
    void add(Window &window)
    {
      this->group_->adopt(&window);
    }
    virtual void windowClosed(Window *) {}
    ~CommitApp()
    {
      this->flush();
    }
  };
  struct CommitCallback
  {
    Fixture &fixture;
    explicit CommitCallback(Fixture &f)
        : fixture(f)
    {
    }
    static void run(void *data)
    {
      CommitCallback &self = *static_cast<CommitCallback *>(data);
      Fixture &f = self.fixture;
      LOKA_VERIFY(f.window.scene()->isBorrowOpen());
      LOKA_VERIFY(f.window.scene()->textDocuments().isPublishing());
      LOKA_VERIFY(f.app.commitTextInput() == TEXT_INPUT_UNAVAILABLE);
      TextDocumentAccess::edit(f.text(), TextChangedRange(0, 0, 2));
      Scene *original = f.window.scene();
      Scene *candidate = new Scene(Boundary<Root>());
      LOKA_VERIFY(f.window.sceneManager()->commitTransaction(0, candidate));
      f.app.flush();
      LOKA_VERIFY(f.window.scene() == original);
      original->requestInvalidate(NODE_DIRTY_PROPS);
      LOKA_VERIFY(!original->flushInvalidation());
    }
  };
  struct CloseDuringCommit
  {
    App &app;
    Window &window;
    static void run(void *data)
    {
      CloseDuringCommit &self = *static_cast<CloseDuringCommit *>(data);
      self.app.requestWindowClose(&self.window);
    }
  };
} // namespace

void testTextCommitCallbackAndTurn()
{
  Fixture f;
  unsigned writes = 0;
  f.text().fact()->bind(&count, &writes, false);
  TextDocumentAccess::uncommitted(f.text(), "abc");
  CommitCallback callback(f);
  TextDocumentAccess::onCommit(f.text(), &CommitCallback::run, &callback);
  {
    loka::core::Operation::Regime regime;
    LOKA_VERIFY(f.app.commitTextInput() == TEXT_INPUT_SETTLED);
    LOKA_VERIFY(!loka::core::Operation::hasActive());
  }
  LOKA_VERIFY(writes == 1 && TextDocumentAccess::commitCalls(f.text()) == 1);
  unsigned long length = 0;
  LOKA_VERIFY(f.text().queryLength(length) && length == 5);
  f.text().fact()->unbind(&count, &writes);
}

namespace
{
  struct CommitDeferredObserver
  {
    loka::core::PushStateTracker tracker;
    unsigned calls;
    CommitDeferredObserver()
        : calls(0)
    {
    }
    static void changed(void *data)
    {
      CommitDeferredObserver &self = *static_cast<CommitDeferredObserver *>(data);
      loka::core::StateTrackerGuard guard(&self.tracker);
      loka::core::testing::PushStateTrackerTestAccess::defer(self.tracker, &count, &self.calls);
    }
  };
} // namespace
void testTextCommitJoinsTurn()
{
  Fixture f;
  CommitDeferredObserver observer;
  f.text().fact()->bind(&CommitDeferredObserver::changed, &observer, false);
  TextDocumentAccess::uncommitted(f.text(), "abc");
  {
    loka::core::Operation::Regime regime;
    loka::core::Operation outer;
    LOKA_VERIFY(f.app.commitTextInput() == TEXT_INPUT_SETTLED);
    LOKA_VERIFY(observer.calls == 0 && loka::core::testing::OperationTestAccess::active() == &outer);
    outer.close();
    LOKA_VERIFY(observer.calls == 1);
  }
  f.text().fact()->unbind(&CommitDeferredObserver::changed, &observer);
}

void testTextCommitGatesAndOtherWindow()
{
  Fixture first, second;
  CommitApp app(first.window, second.window);
  TextDocumentAccess::uncommitted(first.text(), "a");
  TextDocumentAccess::uncommitted(second.text(), "bc");
  const unsigned reads = first.controller.reads;
  {
    BorrowScope borrow(first.controller);
    LOKA_VERIFY(app.commitTextInput() == TEXT_INPUT_UNAVAILABLE);
    LOKA_VERIFY(first.controller.reads == reads);
    LOKA_VERIFY(TextDocumentAccess::commitCalls(first.text()) == 0);
    LOKA_VERIFY(TextDocumentAccess::commitCalls(second.text()) == 1);
  }
  unsigned long length = 0;
  LOKA_VERIFY(second.text().queryLength(length) && length == 2);
  LOKA_VERIFY(app.commitTextInput() == TEXT_INPUT_SETTLED);
  LOKA_VERIFY(first.text().queryLength(length) && length == 1);
}

void testTextCommitWalkCloses()
{
  for (unsigned target = 0; target != 3; ++target)
  {
    Fixture first, second, third;
    CommitApp app(first.window, second.window, &third.window);
    Window *windows[] = {&first.window, &second.window, &third.window};
    CloseDuringCommit callback = {app, *windows[target]};
    // The middle visit closes an earlier, its own, or a later row.
    TextDocumentAccess::onCommit(second.text(), &CloseDuringCommit::run, &callback);
    LOKA_VERIFY(app.commitTextInput() == TEXT_INPUT_SETTLED);
    LOKA_VERIFY(TextDocumentAccess::commitCalls(first.text()) == 1);
    LOKA_VERIFY(TextDocumentAccess::commitCalls(second.text()) == 1);
    LOKA_VERIFY(TextDocumentAccess::commitCalls(third.text()) == (target == 2 ? 0u : 1u));
  }
  Fixture first, second, third;
  CommitApp app(first.window, second.window, &third.window);
  app.requestWindowClose(&second.window);
  LOKA_VERIFY(app.commitTextInput() == TEXT_INPUT_SETTLED);
  LOKA_VERIFY(TextDocumentAccess::commitCalls(first.text()) == 1);
  LOKA_VERIFY(TextDocumentAccess::commitCalls(second.text()) == 0);
  LOKA_VERIFY(TextDocumentAccess::commitCalls(third.text()) == 1);
}

void testTextCommitNoVisibleChange()
{
  Fixture ready;
  const TextDocumentFact before = ready.text().fact()->get();
  unsigned writes = 0;
  ready.text().fact()->bind(&count, &writes, false);
  LOKA_VERIFY(ready.app.commitTextInput() == TEXT_INPUT_SETTLED);
  LOKA_VERIFY(!(before != ready.text().fact()->get()) && writes == 0);
  LOKA_VERIFY(TextDocumentAccess::commitCalls(ready.text()) == 1);
  ready.text().fact()->unbind(&count, &writes);
  failTextStoreCreations(1);
  Fixture refused;
  const TextDocumentFact unavailable = refused.text().fact()->get();
  LOKA_VERIFY(refused.app.commitTextInput() == TEXT_INPUT_SETTLED);
  LOKA_VERIFY(!(unavailable != refused.text().fact()->get()));
  NullPlatformContext context;
  WindowProps p;
  p.scene(new Scene(Boundary<EmptyBoundaryNode>()));
  NullWindow empty(&context, p);
  WindowAdmissionTestApp app(empty);
  app.flush();
  LOKA_VERIFY(app.commitTextInput() == TEXT_INPUT_SETTLED);
  class WindowlessApp : public App
  {
  public:
    WindowlessApp()
        : App(0)
    {
    }
    virtual void quit() {}
  } windowless;
  LOKA_VERIFY(windowless.commitTextInput() == TEXT_INPUT_SETTLED);
}

void testTextReconcileClosesLaterWindow()
{
  Fixture first, second, third;
  CommitApp app(first.window, second.window, &third.window);
  CloseDuringCommit callback = {app, second.window};
  first.text().fact()->bind(&CloseDuringCommit::run, &callback, false);
  TextDocumentAccess::edit(first.text(), TextChangedRange(0, 0, 2));
  TextDocumentAccess::edit(third.text(), TextChangedRange(0, 0, 3));
  const unsigned reads = second.controller.reads;
  app.reconcileAtCompletion();
  LOKA_VERIFY(second.controller.reads == reads);
  LOKA_VERIFY(third.text().fact()->get().textRevision() == 1);
  first.text().fact()->unbind(&CloseDuringCommit::run, &callback);
}

void testTextMarkedReadingCombination()
{
  TextChangeNote changes;
  changes.note(TextStoreReading(false, 0, 0, 0, true));
  LOKA_VERIFY(changes.take().marked());
  changes.note(TextStoreReading(true, 0, 0, 0, true));
  changes.note(TextStoreReading(false, 0, 0, 0, false));
  const TextStoreReading combined = changes.take();
  LOKA_VERIFY(combined.changed() && !combined.marked());
}
namespace
{
  void commitDuringPublication(void *data)
  {
    Fixture &f = *static_cast<Fixture *>(data);
    LOKA_VERIFY(!f.window.scene()->isBorrowOpen());
    const unsigned reads = f.controller.reads;
    LOKA_VERIFY(f.app.commitTextInput() == TEXT_INPUT_UNAVAILABLE);
    f.app.reconcileAtCompletion();
    LOKA_VERIFY(f.controller.reads == reads && TextDocumentAccess::commitCalls(f.text()) == 0);
  }
  struct AddDuringCommit
  {
    CommitApp &app;
    Window &window;
    static void run(void *data)
    {
      AddDuringCommit &self = *static_cast<AddDuringCommit *>(data);
      self.app.add(self.window);
    }
  };
} // namespace
void testTextCommitPublicationGate()
{
  Fixture f;
  TextDocumentAccess::duringPublication(f.window.scene()->textDocuments(), &commitDuringPublication, &f);
}
void testTextCommitWalkAddsWindow()
{
  Fixture first, second, third;
  CommitApp app(first.window, second.window);
  AddDuringCommit callback = {app, third.window};
  TextDocumentAccess::onCommit(first.text(), &AddDuringCommit::run, &callback);
  TextDocumentAccess::uncommitted(third.text(), "added");
  LOKA_VERIFY(app.commitTextInput() == TEXT_INPUT_SETTLED);
  unsigned long length = 0;
  LOKA_VERIFY(third.text().queryLength(length) && length == 5);
}
namespace
{
  class CommitDuringAdmission : public Controller
  {
  public:
    App *app;
    TextInputCommit result;
    CommitDuringAdmission()
        : app(0),
          result(TEXT_INPUT_SETTLED)
    {
    }
    virtual TextStoreBirth
    createTextStore(TextStoreSource &source, TextStoreRow *old, TextStoreRow *&fresh, TextStoreReading &reading)
    {
      if (this->app)
        this->result = this->app->commitTextInput();
      return Controller::createTextStore(source, old, fresh, reading);
    }
  };
} // namespace
void testTextCommitAdmissionGate()
{
  Fixture other;
  CommitDuringAdmission controller;
  NullPlatformContext context;
  NullWindow window(&context, props(), &controller);
  WindowAdmissionTestApp app(window, &other.window);
  controller.app = &app;
  LOKA_VERIFY(window.sceneManager()->commitTransaction(0, new Scene(Boundary<Root>())));
  app.flush();
  LOKA_VERIFY(controller.creates == 2 && controller.result == TEXT_INPUT_UNAVAILABLE);
  LOKA_VERIFY(TextDocumentAccess::commitCalls(other.text()) == 0);
}
void testTextCommitRetainedRequestInvalidation()
{
  Fixture f;
  TextDocument &doc = f.text();
  doc.post(doc.fact()->get().caret());
  TextDocumentAccess::uncommitted(doc, "marked");
  f.app.reconcileAtCompletion();
  LOKA_VERIFY(TextDocumentAccess::pending(doc));
  TextDocumentAccess::store(doc)->retire();
  f.app.reconcileAtCompletion();
  LOKA_VERIFY(doc.fact()->get().state() == TEXT_DOCUMENT_UNAVAILABLE);
  LOKA_VERIFY(!TextDocumentAccess::pending(doc) && f.controller.applies == 0);

  Fixture replaced;
  replaced.text().post(replaced.text().fact()->get().caret());
  TextDocumentAccess::uncommitted(replaced.text(), "marked");
  replaced.app.reconcileAtCompletion();
  LOKA_VERIFY(TextDocumentAccess::pending(replaced.text()));
  Scene *candidate = new Scene(Boundary<Root>());
  LOKA_VERIFY(replaced.window.sceneManager()->commitTransaction(0, candidate));
  replaced.app.flush();
  replaced.app.reconcileAtCompletion();
  LOKA_VERIFY(replaced.window.scene() == candidate);
  LOKA_VERIFY(!TextDocumentAccess::pending(replaced.text()) && replaced.controller.applies == 0);
}
namespace
{
  struct ReplaceOtherScene
  {
    Fixture &other;
    Scene *candidate;
    static void run(void *data)
    {
      ReplaceOtherScene &self = *static_cast<ReplaceOtherScene *>(data);
      LOKA_VERIFY(self.other.window.sceneManager()->commitTransaction(0, self.candidate));
      self.other.app.flush();
      LOKA_VERIFY(self.other.window.scene() == self.candidate);
      TextDocumentAccess::uncommitted(self.other.text(), "replacement");
    }
  };
  struct QuitOrder
  {
    TextTestApp &app;
    loka::core::PushStateTracker tracker;
    unsigned observed;
    bool reserved;
    explicit QuitOrder(TextTestApp &owner)
        : app(owner),
          observed(0),
          reserved(false)
    {
    }
    static void settled(void *data)
    {
      QuitOrder &self = *static_cast<QuitOrder *>(data);
      LOKA_VERIFY(!self.reserved && !self.app.hasQuitAttempt());
      ++self.observed;
    }
    static void changed(void *data)
    {
      QuitOrder &self = *static_cast<QuitOrder *>(data);
      loka::core::StateTrackerGuard guard(&self.tracker);
      loka::core::testing::PushStateTrackerTestAccess::defer(self.tracker, &settled, &self);
    }
  };
} // namespace
void testTextCommitReplacesOtherScene()
{
  Fixture first, second;
  CommitApp app(first.window, second.window);
  ReplaceOtherScene callback = {second, new Scene(Boundary<Root>())};
  TextDocumentAccess::onCommit(first.text(), &ReplaceOtherScene::run, &callback);
  LOKA_VERIFY(app.commitTextInput() == TEXT_INPUT_SETTLED);
  LOKA_VERIFY(second.window.scene() == callback.candidate);
  LOKA_VERIFY(TextDocumentAccess::commitCalls(second.text()) == 1);
  LOKA_VERIFY(TextDocumentAccess::committedText(second.text()) == "replacement");
}
void testTextCommitSettlesBeforeQuitReservation()
{
  Fixture f;
  QuitOrder observer(f.app);
  f.text().fact()->bind(&QuitOrder::changed, &observer, false);
  TextDocumentAccess::uncommitted(f.text(), "quit");
  {
    loka::core::Operation::Regime regime;
    // Models the standalone macOS quit entry's common call order.
    LOKA_VERIFY(f.app.commitTextInput() == TEXT_INPUT_SETTLED);
    LOKA_VERIFY(observer.observed == 1);
    observer.reserved = true;
    f.app.requestQuitAtCompletion();
  }
  f.text().fact()->unbind(&QuitOrder::changed, &observer);
}

namespace
{
  void observeCommitTurn(void *data)
  {
    LOKA_VERIFY(loka::core::Operation::hasActive());
    LOKA_VERIFY(loka::testing::textInputCommitCount() == 1);
    ++*static_cast<unsigned *>(data);
  }
} // namespace
void testTextCommitObservationHook()
{
  Fixture f;
  unsigned probes = 0;
  loka::testing::observeTextInputCommits(&observeCommitTurn, &probes);
  LOKA_VERIFY(f.app.commitTextInput() == TEXT_INPUT_SETTLED);
  LOKA_VERIFY(probes == 1 && loka::testing::textInputCommitCount() == 1);
  loka::testing::observeTextInputCommits(0, 0);
}

namespace
{
  class WithdrawalRoot : public BoundaryNodeFor<WithdrawalRoot>
  {
  public:
    TextDocument first, second;
    explicit WithdrawalRoot(const BoundaryPropsFor<WithdrawalRoot> &p)
        : BoundaryNodeFor<WithdrawalRoot>(p)
    {
      this->document(this->first);
      this->document(this->second);
    }
    virtual void composeNode(NodeComposition &) {}
  };
  struct WithdrawDocument
  {
    TextDocument &document;
    unsigned calls;
    explicit WithdrawDocument(TextDocument &doc)
        : document(doc),
          calls(0)
    {
    }
    static void run(void *data)
    {
      WithdrawDocument &self = *static_cast<WithdrawDocument *>(data);
      ++self.calls;
      TextDocumentAccess::withdraw(self.document);
    }
  };
} // namespace
void testTextPublicationContinuesAfterWithdrawal()
{
  for (unsigned refresh = 0; refresh != 2; ++refresh)
  {
    Controller controller;
    NullPlatformContext context;
    WindowProps p;
    p.scene(new Scene(Boundary<WithdrawalRoot>()));
    TestWindow window(&context, p, &controller);
    TextTestApp app(window);
    app.flush();
    app.reconcileAtCompletion();
    WithdrawalRoot &root = *static_cast<WithdrawalRoot *>(SceneAccess::rootBoundary(*window.scene()));
    WithdrawDocument first(root.first), second(root.second);
    if (refresh)
    {
      TextDocumentAccess::onCommit(root.first, &WithdrawDocument::run, &first);
      TextDocumentAccess::onCommit(root.second, &WithdrawDocument::run, &second);
      LOKA_VERIFY(app.commitTextInput() == TEXT_INPUT_SETTLED);
      // Withdrawal cuts the document link, so refresh cannot publish after the callback.
      LOKA_VERIFY(root.first.fact()->get().state() == TEXT_DOCUMENT_READY);
      LOKA_VERIFY(root.second.fact()->get().state() == TEXT_DOCUMENT_READY);
      unsigned long length = 99;
      LOKA_VERIFY(!root.first.queryLength(length) && length == 99);
      LOKA_VERIFY(!root.second.queryLength(length) && length == 99);
    }
    else
    {
      root.first.fact()->bind(&WithdrawDocument::run, &first, false);
      root.second.fact()->bind(&WithdrawDocument::run, &second, false);
      TextDocumentAccess::edit(root.first, TextChangedRange(0, 0, 1));
      TextDocumentAccess::edit(root.second, TextChangedRange(0, 0, 1));
      app.reconcileAtCompletion();
      root.first.fact()->unbind(&WithdrawDocument::run, &first);
      root.second.fact()->unbind(&WithdrawDocument::run, &second);
      LOKA_VERIFY(root.first.fact()->get().textRevision() == 1);
      LOKA_VERIFY(root.second.fact()->get().textRevision() == 1);
    }
    LOKA_VERIFY(first.calls == 1 && second.calls == 1);
  }
}

namespace
{
  TextReplacement bodyReplacement(const std::string &body, TextPlacement placement = TextPlacement::Start())
  {
    loka::core::resource::Blob bytes = loka::core::resource::Blob::Create();
    LOKA_VERIFY(bytes.tryAssign(reinterpret_cast<const unsigned char *>(body.data()), body.size()));
    bytes.sealBytes();
    TextReplacement replacement;
    LOKA_VERIFY(TextReplacement::FromUtf8(bytes, placement, replacement) == TEXT_DOCUMENT_OK);
    return replacement;
  }
  class BirthController : public Controller
  {
  public:
    TextStoreReading birthReading;
    TextStoreRow *fresh;
    bool refuseReads;
    bool refuseAfterBirth;
    BirthController()
        : fresh(0),
          refuseReads(false),
          refuseAfterBirth(false)
    {
    }
    virtual TextStoreBirth
    createTextStore(TextStoreSource &source, TextStoreRow *old, TextStoreRow *&out, TextStoreReading &reading)
    {
      const TextStoreBirth result = Controller::createTextStore(source, old, out, reading);
      if (result == TEXT_STORE_BORN)
      {
        this->fresh = out;
        this->birthReading = reading;
        if (old && this->refuseAfterBirth)
          this->refuseReads = true;
      }
      return result;
    }
    virtual bool readTextStore(TextStoreRow &row, TextStoreReading &out)
    {
      return !this->refuseReads && Controller::readTextStore(row, out);
    }
  };
  struct BirthFixture
  {
    BirthController controller;
    NullPlatformContext context;
    TestWindow window;
    TextTestApp app;
    BirthFixture()
        : window(&context, props(), &controller),
          app(window)
    {
      app.flush();
      app.reconcileAtCompletion();
    }
    TextDocument &text()
    {
      return static_cast<Root *>(SceneAccess::rootBoundary(*window.scene()))->text;
    }
  };
  struct ReplyObserver
  {
    TextDocument &document;
    unsigned calls;
    unsigned long expectedRevision;
    explicit ReplyObserver(TextDocument &doc, unsigned long revision)
        : document(doc),
          calls(0),
          expectedRevision(revision)
    {
    }
    static void run(void *data)
    {
      ReplyObserver &self = *static_cast<ReplyObserver *>(data);
      ++self.calls;
      LOKA_VERIFY(self.document.fact()->get().textRevision() == self.expectedRevision);
      LOKA_VERIFY(self.document.replace(TextReplacement::Empty()) == POST_QUEUE_FULL);
    }
  };
  void replacementBirthPins()
  {
    BlobAllocationProbe allocation;
    BirthFixture f;
    TextDocument &doc = f.text();
    TextStoreRow *old = TextDocumentAccess::store(doc);
    unsigned replies = 0;
    doc.replaceReply()->bind(&count, &replies, false);
    const int baseline = allocation.live;
    {
      const TextReplacement replacement = bodyReplacement("abc", TextPlacement::End());
      doc.post(doc.fact()->get().caret());
      checkTextStoreConformance(f.controller, f.app, doc, *old, replacement, 3, 3);
      LOKA_VERIFY(f.controller.applies == 0);
      f.controller.drainNativeRetirements();
      LOKA_VERIFY(TextStoreTestAccess::retiredCount(*f.controller.fresh) == 0);
      LOKA_VERIFY(!TextStoreTestAccess::retired(*f.controller.fresh));
    }
    LOKA_VERIFY(allocation.live == baseline && allocation.byteFrees == 1);
    LOKA_VERIFY(replies == 1 && doc.replaceReply()->get().kind() == Reply<TextReplacement>::GRANTED);
    LOKA_VERIFY(doc.fact()->get().textRevision() == 1);
    TextPosition start, end;
    LOKA_VERIFY(doc.fact()->get().queryChangedRange(start, end));
    LOKA_VERIFY(start == TextPositionTestAccess::make(1, 0) && end == TextPositionTestAccess::make(1, 3));
    TextReader reader(doc);
    doc.post(doc.fact()->get().caret());
    TextDocumentAccess::edit(doc, TextChangedRange(0, 0, 2));
    f.controller.refuseAfterBirth = true;
    LOKA_VERIFY(doc.replace(bodyReplacement("hello", TextPlacement::AtUtf8Offset(2))) == POST_ACCEPTED);
    f.app.reconcileAtCompletion();
    LOKA_VERIFY(doc.fact()->get().textRevision() == 2 && f.controller.applies == 0);
    LOKA_VERIFY(doc.fact()->get().caret() == TextPositionTestAccess::make(2, 2));
    LOKA_VERIFY(!TextDocumentAccess::pending(doc));
    char scratch[4];
    unsigned long n = 0;
    LOKA_VERIFY(reader.read(scratch, sizeof scratch, n) == TEXT_READ_REFUSED);
    f.controller.refuseReads = false;
    f.app.reconcileAtCompletion();
    LOKA_VERIFY(doc.fact()->get().textRevision() == 2);
    doc.replaceReply()->unbind(&count, &replies);
  }
  void replacementRefusalPins()
  {
    Fixture f;
    TextDocument &doc = f.text();
    LOKA_VERIFY(doc.replace(bodyReplacement("body")) == POST_ACCEPTED);
    f.app.reconcileAtCompletion();
    TextStoreRow *old = TextDocumentAccess::store(doc);
    const TextDocumentOrigin origin = doc.fact()->get().origin();
    const TextStoreBirth births[] = {TEXT_STORE_REFUSED_ALLOCATION,
                                     TEXT_STORE_REFUSED_NOT_REPRESENTABLE,
                                     TEXT_STORE_REFUSED_CAPACITY,
                                     TEXT_STORE_REFUSED_UNSUPPORTED};
    const TextReplaceResult reasons[] = {
        TEXT_REPLACE_ALLOCATION, TEXT_REPLACE_NOT_REPRESENTABLE, TEXT_REPLACE_CAPACITY, TEXT_REPLACE_UNAVAILABLE};
    unsigned replies = 0;
    doc.replaceReply()->bind(&count, &replies, false);
    for (unsigned i = 0; i < 4; ++i)
    {
      failTextStoreBirths(births[i], 2);
      for (unsigned repeat = 0; repeat < 2; ++repeat)
      {
        const unsigned retired = TextStoreTestAccess::retiredCount(*old);
        LOKA_VERIFY(doc.replace(TextReplacement::Empty()) == POST_ACCEPTED);
        f.app.reconcileAtCompletion();
        LOKA_VERIFY(doc.replaceReply()->get().kind() == Reply<TextReplacement>::REFUSED);
        LOKA_VERIFY(doc.replaceReply()->get().reason() == reasons[i]);
        LOKA_VERIFY(doc.fact()->get().origin() == origin);
        LOKA_VERIFY(TextDocumentAccess::store(doc) == old && TextDocumentAccess::committedText(doc) == "body");
        LOKA_VERIFY(!TextStoreTestAccess::retired(*old));
        LOKA_VERIFY(TextStoreTestAccess::retiredCount(*old) == retired + 1);
      }
    }
    LOKA_VERIFY(replies == 8);
    f.controller.drainNativeRetirements();
    LOKA_VERIFY(TextStoreTestAccess::retiredCount(*old) == 0);
    doc.replaceReply()->unbind(&count, &replies);
    for (unsigned edit = 0; edit < 2; ++edit)
    {
      doc.post(TextPositionTestAccess::make(1, 2));
      if (edit)
        TextDocumentAccess::edit(doc, TextChangedRange(0, 0, 1));
      failTextStoreBirths(TEXT_STORE_REFUSED_ALLOCATION, 1);
      LOKA_VERIFY(doc.replace(TextReplacement::Empty()) == POST_ACCEPTED);
      f.app.reconcileAtCompletion();
      LOKA_VERIFY(doc.fact()->get().textRevision() == 1 + edit);
      LOKA_VERIFY(f.controller.applies == 1 && !TextDocumentAccess::pending(doc));
    }
    LOKA_VERIFY(doc.fact()->get().origin() == origin);
    BirthFixture unreadable;
    unreadable.controller.refuseReads = true;
    LOKA_VERIFY(unreadable.text().replace(TextReplacement::Empty()) == POST_ACCEPTED);
    unreadable.app.reconcileAtCompletion();
    LOKA_VERIFY(unreadable.text().replaceReply()->get().reason() == TEXT_REPLACE_UNAVAILABLE);
    failTextStoreCreations(1);
    Fixture refused;
    LOKA_VERIFY(refused.text().replace(TextReplacement::Empty()) == POST_ACCEPTED);
    refused.app.reconcileAtCompletion();
    LOKA_VERIFY(refused.text().replaceReply()->get().reason() == TEXT_REPLACE_UNAVAILABLE);
    LOKA_VERIFY(refused.controller.creates == 1);
  }
  void replacementAdmissionPins()
  {
    Fixture f;
    TextDocument &doc = f.text();
    TextDocument undeclared;
    LOKA_VERIFY(undeclared.replace(TextReplacement::Empty()) == POST_INVALID);
    LOKA_VERIFY(doc.replace(TextReplacement()) == POST_INVALID);
    LOKA_VERIFY(doc.replace(bodyReplacement("first")) == POST_ACCEPTED);
    LOKA_VERIFY(doc.replace(TextReplacement::Empty()) == POST_QUEUE_FULL);
    ReplyObserver observer(doc, 1);
    doc.fact()->bind(&ReplyObserver::run, &observer, false);
    doc.replaceReply()->bind(&ReplyObserver::run, &observer, false);
    f.app.reconcileAtCompletion();
    LOKA_VERIFY(observer.calls == 2 && TextDocumentAccess::committedText(doc) == "first");
    doc.fact()->unbind(&ReplyObserver::run, &observer);
    doc.replaceReply()->unbind(&ReplyObserver::run, &observer);
    doc.post(doc.fact()->get().caret());
    TextDocumentAccess::uncommitted(doc, "marked");
    LOKA_VERIFY(doc.replace(TextReplacement::Empty()) == POST_ACCEPTED);
    f.app.reconcileAtCompletion();
    LOKA_VERIFY(doc.replaceReply()->get().reason() == TEXT_REPLACE_COMPOSING);
    LOKA_VERIFY(TextDocumentAccess::pending(doc));
    LOKA_VERIFY(doc.replace(TextReplacement::Empty()) == POST_ACCEPTED);
    unsigned replies = 0;
    doc.replaceReply()->bind(&count, &replies, false);
    LOKA_VERIFY(f.app.commitTextInput() == TEXT_INPUT_SETTLED);
    LOKA_VERIFY(f.app.commitTextInput() == TEXT_INPUT_SETTLED);
    LOKA_VERIFY(replies == 0 && doc.replace(TextReplacement::Empty()) == POST_QUEUE_FULL);
    f.app.reconcileAtCompletion();
    LOKA_VERIFY(replies == 1 && doc.replaceReply()->get().kind() == Reply<TextReplacement>::GRANTED);
    doc.replaceReply()->unbind(&count, &replies);
  }
  void replacementPlacementPins()
  {
    const std::string bodies[] = {"",
                                  "abc",
                                  "a\xf0\x9f\x98\x80z",
                                  "a\r\nb\r\r\nc",
                                  "\xe2\x80\xa8\xe2\x80\xa9\xc2\x85",
                                  std::string(255, 'x') + "\xf0\x9f\x98\x80"};
    for (unsigned b = 0; b < sizeof bodies / sizeof *bodies; ++b)
    {
      const TextReplacement measured = bodyReplacement(bodies[b]);
      const unsigned long offsets[] = {0, 2, measured.logicalLength() + 1, ULONG_MAX, 257};
      for (unsigned i = 0; i < 7; ++i)
      {
        Fixture f;
        TextDocument &doc = f.text();
        const TextPlacement requested = i == 5   ? TextPlacement::Start()
                                        : i == 6 ? TextPlacement::End()
                                                 : TextPlacement::AtUtf8Offset(offsets[i]);
        const TextReplacement replacement = bodyReplacement(bodies[b], requested);
        TextStoreSource source(replacement);
        unsigned int scalar;
        bool separator;
        while (source.next(scalar, separator))
        {
        }
        const TextPlacement applied = source.appliedPlacement();
        LOKA_VERIFY(doc.replace(replacement) == POST_ACCEPTED);
        f.app.reconcileAtCompletion();
        const Reply<TextReplacement> &reply = doc.replaceReply()->get();
        LOKA_VERIFY(reply.kind()
                    == (source.clamped() ? Reply<TextReplacement>::CLAMPED : Reply<TextReplacement>::GRANTED));
        LOKA_VERIFY(reply.applied().placement() == applied);
        unsigned long offset = ULONG_MAX;
        LOKA_VERIFY(doc.queryUtf8Offset(doc.fact()->get().caret(), offset));
        LOKA_VERIFY(offset == (i == 5 ? 0 : i == 6 ? replacement.logicalLength() : applied.utf8Offset()));
        LOKA_VERIFY(doc.fact()->get().origin().revision() == 1);
        loka::file::File absent;
        LOKA_VERIFY(!doc.fact()->get().origin().querySource(absent));
      }
    }
  }
  void replacementExhaustionPins()
  {
    Fixture f;
    startTextDocumentRevision(f.text(), ULONG_MAX - 1);
    LOKA_VERIFY(f.text().fact()->get().textRevision() == ULONG_MAX - 1);
    LOKA_VERIFY(f.text().replace(TextReplacement::Empty()) == POST_ACCEPTED);
    f.app.reconcileAtCompletion();
    LOKA_VERIFY(f.text().fact()->get().textRevision() == ULONG_MAX);
#if defined(__linux__) && defined(LOKA_LIFECYCLE_AUDIT) && !defined(NDEBUG) && !defined(__SANITIZE_ADDRESS__)
    const pid_t child = fork();
    LOKA_VERIFY(child >= 0);
    if (!child)
    {
      LOKA_VERIFY(f.text().replace(TextReplacement::Empty()) == POST_ACCEPTED);
      f.app.reconcileAtCompletion();
      _exit(0);
    }
    int status = 0;
    LOKA_VERIFY(waitpid(child, &status, 0) == child);
    LOKA_VERIFY(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
#else
    std::puts("[skip] revision exhaustion death pin needs Linux fork, lifecycle audit, asserts and no ASan.");
#endif
  }
} // namespace

namespace
{
  TextDocument *awaitedDocument = 0;
  Scene *awaitedScene = 0;
  class AwaitRoot : public BoundaryNodeFor<AwaitRoot>
  {
  public:
    TextDocument text;
    explicit AwaitRoot(const BoundaryPropsFor<AwaitRoot> &p)
        : BoundaryNodeFor<AwaitRoot>(p)
    {
      this->document(this->text);
      awaitedDocument = &this->text;
    }
    virtual void attachNode(NodeComposition &)
    {
      awaitedScene = this->scene();
    }
    virtual void composeNode(NodeComposition &) {}
  };
  class AwaitController : public Controller
  {
  public:
    virtual TextStoreBirth
    createTextStore(TextStoreSource &source, TextStoreRow *old, TextStoreRow *&fresh, TextStoreReading &reading)
    {
      if (!old)
      {
        LOKA_VERIFY(awaitedDocument->replace(bodyReplacement("awaited")) == POST_ACCEPTED);
        TextDocumentAccess::complete(awaitedScene->textDocuments(), *this);
        LOKA_VERIFY(awaitedDocument->replaceReply()->get().kind() == Reply<TextReplacement>::NO_REPLY);
        LOKA_VERIFY(awaitedDocument->replace(TextReplacement::Empty()) == POST_QUEUE_FULL);
        LOKA_VERIFY(awaitedDocument->fact()->get().state() == TEXT_DOCUMENT_AWAITING_STORE);
      }
      return Controller::createTextStore(source, old, fresh, reading);
    }
  };
  void replacementAwaitedPin()
  {
    AwaitController controller;
    NullPlatformContext context;
    WindowProps p;
    p.scene(new Scene(Boundary<AwaitRoot>()));
    TestWindow window(&context, p, &controller);
    TextTestApp app(window);
    app.flush();
    app.reconcileAtCompletion();
    LOKA_VERIFY(awaitedDocument->replaceReply()->get().kind() == Reply<TextReplacement>::GRANTED);
    LOKA_VERIFY(TextDocumentAccess::committedText(*awaitedDocument) == "awaited");
    LOKA_VERIFY(controller.creates == 2);
    awaitedDocument = 0;
    awaitedScene = 0;
  }
  void replacementWithdrawalDuringFactPin()
  {
    Fixture f;
    TextDocument &doc = f.text();
    WithdrawDocument withdrawal(doc);
    unsigned replies = 0;
    doc.fact()->bind(&WithdrawDocument::run, &withdrawal, false);
    doc.replaceReply()->bind(&count, &replies, false);
    LOKA_VERIFY(doc.replace(bodyReplacement("withdraw during publication")) == POST_ACCEPTED);
    // The fact observer cuts the link before the publisher takes a reply seat.
    f.app.reconcileAtCompletion();
    LOKA_VERIFY(withdrawal.calls == 1);
    LOKA_VERIFY(doc.fact()->get().textRevision() == 1);
    LOKA_VERIFY(replies == 0 && doc.replaceReply()->get().kind() == Reply<TextReplacement>::NO_REPLY);
    LOKA_VERIFY(doc.replace(TextReplacement::Empty()) == POST_INVALID);
    doc.fact()->unbind(&WithdrawDocument::run, &withdrawal);
    doc.replaceReply()->unbind(&count, &replies);
  }
  void replacementCancellationPins()
  {
    for (unsigned event = 0; event < 3; ++event)
    {
      unsigned replies = 0;
      Fixture f;
      TextDocument &doc = f.text();
      LOKA_VERIFY(doc.replace(bodyReplacement("kept before cancellation")) == POST_ACCEPTED);
      f.app.reconcileAtCompletion();
      loka::core::State<Reply<TextReplacement> > *reply = doc.replaceReply();
      reply->bind(&count, &replies, false);
      LOKA_VERIFY(doc.replace(bodyReplacement("cancel")) == POST_ACCEPTED);
      if (event == 0)
      {
        const TextDocumentOrigin origin = doc.fact()->get().origin();
        TextDocumentAccess::withdraw(doc);
        LOKA_VERIFY(doc.replace(TextReplacement::Empty()) == POST_INVALID);
        f.app.reconcileAtCompletion();
        LOKA_VERIFY(doc.fact()->get().origin() == origin);
        reply->unbind(&count, &replies);
      }
      else if (event == 1)
      {
        doc.~TextDocument();
        f.app.reconcileAtCompletion();
        reply->unbind(&count, &replies);
        new (&doc) TextDocument();
      }
      else
      {
        Scene *candidate = new Scene(Boundary<Root>());
        LOKA_VERIFY(f.window.sceneManager()->commitTransaction(0, candidate));
        f.app.flush();
        f.app.reconcileAtCompletion();
        LOKA_VERIFY(f.window.scene() == candidate);
      }
      LOKA_VERIFY(replies == 0);
    }
  }
#ifdef __linux__
  struct ReplacementFile
  {
    std::string path;
    loka::file::File file;
    ReplacementFile()
    {
      char pattern[] = "/tmp/loka-replacement-XXXXXX";
      int fd = mkstemp(pattern);
      LOKA_VERIFY(fd >= 0);
      LOKA_VERIFY(write(fd, "body", 4) == 4);
      LOKA_VERIFY(close(fd) == 0);
      this->path = pattern;
      this->file = loka::file::File(loka::core::String(this->path));
    }
    ~ReplacementFile()
    {
      std::remove(this->path.c_str());
    }
    TextReplacement read(PlatformContext &context) const
    {
      TextReplacement result;
      LOKA_VERIFY(ReadTextReplacement(context, this->file, TextPlacement::Start(), result) == TEXT_DOCUMENT_OK);
      return result;
    }
  };
  struct OriginChoice : loka::core::DerivedState<TextDocumentOrigin>::EvalFn
  {
    loka::core::State<bool> &choose;
    TextDocument &a, &b;
    OriginChoice(loka::core::State<bool> &c, TextDocument &left, TextDocument &right)
        : choose(c),
          a(left),
          b(right)
    {
    }
    virtual TextDocumentOrigin operator()()
    {
      return (choose.get() ? b : a).fact()->get().origin();
    }
  };
  struct DeferredOpen
  {
    loka::core::PushStateTracker tracker;
    Root &root;
    TextReplacement next;
    loka::file::File expected;
    unsigned posts, replies;
    DeferredOpen(Root &r, const TextReplacement &replacement, const loka::file::File &file)
        : root(r),
          next(replacement),
          expected(file),
          posts(0),
          replies(0)
    {
    }
    static void queuePost(void *data)
    {
      DeferredOpen &self = *static_cast<DeferredOpen *>(data);
      loka::core::StateTrackerGuard guard(&self.tracker);
      loka::core::testing::PushStateTrackerTestAccess::defer(self.tracker, &post, data);
    }
    static void queueReply(void *data)
    {
      DeferredOpen &self = *static_cast<DeferredOpen *>(data);
      loka::core::StateTrackerGuard guard(&self.tracker);
      loka::core::testing::PushStateTrackerTestAccess::defer(self.tracker, &reply, data);
    }
    static void post(void *data)
    {
      DeferredOpen &self = *static_cast<DeferredOpen *>(data);
      if (self.posts)
        return;
      ++self.posts;
      LOKA_VERIFY(self.root.text.replace(self.next) == POST_ACCEPTED);
      LOKA_VERIFY(self.root.destination().sharesRepresentation(self.expected));
    }
    static void reply(void *data)
    {
      DeferredOpen &self = *static_cast<DeferredOpen *>(data);
      ++self.replies;
      LOKA_VERIFY(self.posts == 1);
      LOKA_VERIFY(self.root.destination().sharesRepresentation(self.expected));
    }
  };
  void replacementOriginPins()
  {
    ReplacementFile a, b, c;
    Fixture f, other;
    LOKA_VERIFY(f.text().replace(a.read(f.context)) == POST_ACCEPTED);
    f.app.reconcileAtCompletion();
    LOKA_VERIFY(f.root().destination().sharesRepresentation(a.file) && !f.root().dirty());
    LOKA_VERIFY(other.text().replace(b.read(other.context)) == POST_ACCEPTED);
    other.app.reconcileAtCompletion();
    LOKA_VERIFY(f.text().fact()->get().origin() != other.text().fact()->get().origin());
    loka::core::MutableState<bool> choose(false);
    loka::core::DerivedState<TextDocumentOrigin> selected(&choose, new OriginChoice(choose, f.text(), other.text()));
    unsigned selections = 0;
    selected.bind(&count, &selections, false);
    loka::core::PushStateTracker tracker;
    tracker.addState(&choose);
    tracker.addState(&selected);
    tracker.begin();
    choose.set(true);
    tracker.end();
    loka::file::File selectedFile;
    LOKA_VERIFY(selected.get().querySource(selectedFile));
    LOKA_VERIFY(selectedFile.sharesRepresentation(b.file) && selections == 1);
    selected.unbind(&count, &selections);
    const TextDocumentOrigin initial = f.text().fact()->get().origin();
    TextDocumentAccess::edit(f.text(), TextChangedRange(0, 0, 1));
    f.app.reconcileAtCompletion();
    LOKA_VERIFY(f.root().dirty() && f.text().fact()->get().origin() == initial);
    f.root().saved(b.file);
    LOKA_VERIFY(f.root().destination().sharesRepresentation(b.file) && !f.root().dirty());
    LOKA_VERIFY(f.text().replace(c.read(f.context)) == POST_ACCEPTED);
    f.app.reconcileAtCompletion();
    LOKA_VERIFY(f.root().destination().sharesRepresentation(c.file) && !f.root().dirty());
    for (unsigned refuse = 0; refuse < 2; ++refuse)
    {
      Fixture ordering;
      DeferredOpen observer(ordering.root(), b.read(ordering.context), a.file);
      ordering.text().fact()->bind(&DeferredOpen::queuePost, &observer, false);
      ordering.text().replaceReply()->bind(&DeferredOpen::queueReply, &observer, false);
      {
        // A native command turn settles deferred observers after publication.
        loka::core::Operation turn;
        LOKA_VERIFY(ordering.text().replace(a.read(ordering.context)) == POST_ACCEPTED);
        ordering.app.reconcileAtCompletion();
        turn.close();
      }
      LOKA_VERIFY(observer.posts == 1 && observer.replies == 1);
      LOKA_VERIFY(ordering.root().destination().sharesRepresentation(a.file));
      if (refuse)
        failTextStoreBirths(TEXT_STORE_REFUSED_ALLOCATION, 1);
      ordering.text().fact()->unbind(&DeferredOpen::queuePost, &observer);
      ordering.text().replaceReply()->unbind(&DeferredOpen::queueReply, &observer);
      ordering.app.reconcileAtCompletion();
      LOKA_VERIFY(ordering.root().destination().sharesRepresentation(refuse ? a.file : b.file));
    }
  }
#else
  void replacementOriginPins()
  {
    std::puts("[skip] replacement file integration uses generic Linux IO.");
  }
#endif
} // namespace
void testTextWholeReplacementContracts()
{
  replacementBirthPins();
  replacementRefusalPins();
  replacementAdmissionPins();
  replacementPlacementPins();
  replacementExhaustionPins();
  replacementCancellationPins();
  replacementWithdrawalDuringFactPin();
  replacementAwaitedPin();
  replacementOriginPins();
}
