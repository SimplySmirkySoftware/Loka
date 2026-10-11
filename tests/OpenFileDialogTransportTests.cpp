#include "OpenFileDialogTransportTests.hpp"
#include "support/TestVerify.hpp"
#include "support/WindowAdmissionTestApp.hpp"
#include "platform/null/NullWindow.hpp"
#include "app/core/DialogResultTransport.hpp"
#include "app/nodes/boundary/StdComposition.hpp"
#include "app/nodes/nestable/Show.hpp"
#include "app/scene/projection/RetainedNodeHandler.hpp"
#include "testing/core/StateTrackerTestAccess.hpp"
#include "testing/scene/SceneTestFlow.hpp"
#include <cassert>
#include <vector>
#include <cstring>
#include "core/LokaAlloc.hpp"
#include "platform/file/FileLocatorAccess.hpp"

#include "support/DialogResultTestAccess.hpp"

namespace
{
  using namespace loka::app;
  using namespace loka::app::scene;
  typedef DialogResultTransport Transport;
  typedef loka::app::testing::DialogResultTestAccess Access;
  NullWindow *g_window = 0;
  bool g_destroyOnDetach = false;
  FileDialogOptions g_options;
  class DialogOwner;
  DialogOwner *g_owner = 0;
  class DialogContext;
  DialogContext *g_context = 0;
  std::vector<int> *g_events = 0;

  /** Null native-equivalent producer: actual dialog node, shared transport. */
  class DialogContext : public NodeContext
  {
  public:
    explicit DialogContext(OpenFileDialogNode *node)
        : node_(node),
          registration_(0)
    {
      g_context = this;
    }
    virtual ~DialogContext()
    {
      this->cancel();
      g_context = 0;
      g_events->push_back(3);
    }
    void readLifecycleFactOnAttach()
    {
      if (this->node_->lifecycleFact() == NODE_FACT_ATTACHED)
        this->presentation_.markDetached();
    }
    virtual void onFactChanged(NodeLifecycleFact, NodeLifecycleFact next)
    {
      g_events->push_back(4);
      if (next == NODE_FACT_ATTACHED)
        this->presentation_.markDetached();
      else
      {
        this->presentation_.markDetached();
        this->cancel();
      }
    }
    virtual void onPropsApplied()
    {
      if (this->registration_ && !this->registration_->matches(this->node_->props))
        this->cancel();
    }
    void attach()
    {
      // Deliberate Win32 producer twin: attach records intent; projection
      // attempts enrollment once, with phase and binding guarding repeats.
      if (this->registration_ || this->node_->lifecycleFact() != NODE_FACT_ATTACHED
          || !this->presentation_.beginPresent())
        return;
      this->registration_ = g_window->dialogResults().reserve(this->node_->props);
      if (!this->registration_)
      {
        this->presentation_.markDetached();
        return;
      }
      this->presentation_.markPresented();
      g_events->push_back(5);
    }
    void cancel()
    {
      Transport::Registration *registration = this->registration_;
      this->registration_ = 0;
      delete registration;
    }
    void produce()
    {
      Transport::ReturnPort port(this->registration_);
      // The native producer owns no identity after return: transport must
      // carry this entire File, not reconstruct it from its display text.
      const unsigned char address[] = {0, 0xa7};
      loka::file::File chosen;
      LOKA_VERIFY(loka::platform::file::FileLocatorAccess::capture(
          loka::core::String::Literal("accepted-dialog-result.png"), loka::file::File::KIND_FILE,
          address, sizeof(address), chosen));
      LOKA_VERIFY(port.seal(FileChooserResult::File(chosen)) == g_window);
    }
    OpenFileDialogPresentationPhase presentation_;
    OpenFileDialogNode *node_;
    Transport::Registration *registration_;
  };

  class DialogHandler : public RetainedNodeHandler<DialogHandler, OpenFileDialogNode, DialogContext>
  {
  public:
    static void afterAttach(DialogContext *context) { context->attach(); }
    static void refresh(DialogContext *context, const LayoutState &) { context->attach(); }
    static OpenFileDialogNode *cast(Node *node)
    {
      return node->asOpenFileDialogNode();
    }
    static DialogContext *create(OpenFileDialogNode *node, IPlatformController *, const LayoutState &)
    {
      return new DialogContext(node);
    }
  };

  typedef BoundaryPropsFor<DialogOwner> DialogOwnerProps;
  class DialogOwner : public StdCompositionBoundaryNodeBase<DialogOwnerProps>
  {
  public:
    explicit DialogOwner(const DialogOwnerProps &props)
        : StdCompositionBoundaryNodeBase<DialogOwnerProps>(props)
    {
      this->state(this->shown_, true);
      this->state(this->result_, FileChooserResult());
      g_owner = this;
    }
    virtual ~DialogOwner()
    {
      g_owner = 0;
    }
    virtual void composeNode(NodeComposition &composition)
    {
      ShowDefinition seat = Show(*this->shown_.state());
      if (g_destroyOnDetach)
        seat.destroyOnDetach();
      OpenFileDialogProps props;
      props.options_ = g_options;
      composition.declare(seat << OpenFileDialog(props).result(this->result_).onResult(&this->emitter_));
    }
    NodeState<bool> shown_;
    NodeState<FileChooserResult> result_;
    loka::core::EmitterState emitter_;
  };

  void countEvent(void *data)
  {
    ++*static_cast<int *>(data);
  }

  struct Fixture
  {
    explicit Fixture(bool destroyOnDetach = false, const FileDialogOptions &options = FileDialogOptions())
        : window(0, WindowProps()),
          app(window)
    {
      g_destroyOnDetach = destroyOnDetach;
      g_options = options;
      g_events = &this->events;
      g_window = &this->window;
      LOKA_VERIFY(this->window.scenePlatformController()->registerNodeHandler(&this->handler));
      LOKA_VERIFY(this->window.sceneManager()->commitTransaction(
          0, new Scene(new NodeDefinition<DialogOwnerProps, DialogOwner>(DialogOwnerProps()))));
      this->app.flush();
      LOKA_VERIFY(g_owner && g_context);
    }
    ~Fixture()
    {
      this->window.destroyScenePlatform();
      g_window = 0;
      g_events = 0;
    }
    std::vector<int> events;
    DialogHandler handler;
    NullWindow window;
    WindowAdmissionTestApp app;
  };

  int g_enrollmentRefusals = 0;
  int g_enrollmentAttempts = 0;
  int g_enrollmentLive = 0;
  void *enrollmentAlloc(size_t size, const loka::core::LokaAllocationSite &site)
  {
    if (std::strcmp(site.ownerTag, "DialogResultTransport") == 0)
    {
      ++g_enrollmentAttempts;
      if (g_enrollmentRefusals)
      {
        --g_enrollmentRefusals;
        return 0;
      }
      ++g_enrollmentLive;
    }
    return new (std::nothrow) char[size];
  }
  void enrollmentFree(void *storage, const loka::core::LokaAllocationSite &site)
  {
    if (std::strcmp(site.ownerTag, "DialogResultTransport") == 0)
      --g_enrollmentLive;
    delete[] static_cast<char *>(storage);
  }

  void cancelObserver(void *)
  {
    g_context->cancel();
  }

  class OwnedApp : public App
  {
  public:
    using App::admitAndApplyWindows;
    using App::reclaimWindows;
    OwnedApp()
        : App(0)
    {
      this->group_ = new AppComponentGroup(std::vector<AppComponent *>());
    }
    void adopt(Window *window)
    {
      this->group_->adopt(window);
    }
    virtual void quit() {}
    void flush()
    {
      this->flushWindowInvalidations();
    }
    bool pending() const
    {
      return this->hasPendingWindowAdmission();
    }
  };

  class DeathWindow : public NullWindow
  {
  public:
    explicit DeathWindow(int &deaths)
        : NullWindow(0, WindowProps()),
          deaths_(deaths)
    {
    }
    virtual ~DeathWindow()
    {
      ++this->deaths_;
    }

  private:
    int &deaths_;
  };

  struct CloseProbe
  {
    OwnedApp *app;
    Window *window;
    int *deaths;
    static void close(void *data)
    {
      CloseProbe &probe = *static_cast<CloseProbe *>(data);
      probe.app->requestWindowClose(probe.window);
      probe.app->flush();
      LOKA_VERIFY(*probe.deaths == 0);
    }
  };
} // namespace

void testOpenFileDialogTransportRetiredNodeDropsProducedResult()
{
  Fixture fixture(true);
  int writes = 0, emits = 0;
  g_owner->result_.bind(&countEvent, &writes, false);
  g_owner->emitter_.bind(&countEvent, &emits, false);
  g_context->produce();
  LOKA_VERIFY(Access::census(fixture.window.dialogResults()) == 1);
  // Retire the actual node via its lifecycle door while ancestor targets survive.
  g_owner->shown_.set(false);
  fixture.window.flushSceneInvalidation();
  LOKA_VERIFY(!g_context);
  const size_t contextEvents = fixture.events.size();
  fixture.app.flush();
  LOKA_VERIFY(writes == 0 && emits == 0);
  LOKA_VERIFY(fixture.events.size() == contextEvents);
  LOKA_VERIFY(Access::census(fixture.window.dialogResults()) == 0);
  g_owner->result_.unbind(&countEvent, &writes);
  g_owner->emitter_.unbind(&countEvent, &emits);
}

void testOpenFileDialogTransportRetainedDetachAndFreshReattach()
{
  Fixture fixture;
  int writes = 0, emits = 0;
  g_owner->result_.bind(&countEvent, &writes, false);
  g_owner->emitter_.bind(&countEvent, &emits, false);
  DialogContext *context = g_context;
  g_context->produce();
  g_owner->shown_.set(false);
  fixture.window.flushSceneInvalidation();
  LOKA_VERIFY(g_context == context && !context->registration_);
  fixture.app.flush();
  LOKA_VERIFY(writes == 0 && emits == 0 && Access::census(fixture.window.dialogResults()) == 0);
  g_owner->shown_.set(true);
  fixture.app.flush();
  LOKA_VERIFY(g_context == context && context->registration_);
  g_context->produce();
  fixture.app.flush();
  LOKA_VERIFY(writes == 1 && emits == 1);
  // Resolve the delivered value after the producer temporaries and queued
  // envelope have gone. The fake rail's address is counted, including zero.
  const loka::file::File delivered = g_owner->result_.get().item;
  const unsigned char *address = 0;
  std::size_t size = 0;
  LOKA_VERIFY(loka::platform::file::FileLocatorAccess::query(delivered, address, size));
  LOKA_VERIFY(size == 2 && address[0] == 0 && address[1] == 0xa7);
  fixture.app.flush();
  LOKA_VERIFY(writes == 1 && emits == 1 && Access::census(fixture.window.dialogResults()) == 0);
  g_owner->result_.unbind(&countEvent, &writes);
  g_owner->emitter_.unbind(&countEvent, &emits);
}

void testOpenFileDialogTransportOwnerReclaimDropsProducedResult()
{
  Fixture fixture;
  g_context->produce();
  fixture.window.teardownScene();
  LOKA_VERIFY(!g_owner && !g_context);
  fixture.app.flush();
  LOKA_VERIFY(Access::census(fixture.window.dialogResults()) == 0);
}

void testOpenFileDialogTransportObserverCancellationSuppressesEmitter()
{
  Fixture fixture;
  int emits = 0;
  g_owner->result_.bind(&cancelObserver, 0, false);
  g_owner->emitter_.bind(&countEvent, &emits, false);
  g_context->produce();
  fixture.app.flush();
  LOKA_VERIFY(g_owner->result_.get().kind == FileChooserResult::RESULT_FILE && emits == 0);
  fixture.app.flush();
  LOKA_VERIFY(Access::census(fixture.window.dialogResults()) == 0);
  g_owner->result_.unbind(&cancelObserver, 0);
  g_owner->emitter_.unbind(&countEvent, &emits);
}

void testOpenFileDialogTransportCloseMakesProgressWithoutInput()
{
  int deaths = 0, emits = 0;
  OwnedApp app;
  DeathWindow *window = new DeathWindow(deaths);
  app.adopt(window);
  loka::core::MutableState<FileChooserResult> result;
  loka::core::PushStateTracker tracker;
  tracker.addState(&result);
  loka::core::EmitterState emitter;
  emitter.bind(&countEvent, &emits, false);
  NodeState<FileChooserResult> channel(&result, &tracker);
  CloseProbe probe = {&app, window, &deaths};
  channel.bind(&CloseProbe::close, &probe, false);
  Transport::Registration *registration =
      window->dialogResults().reserve(OpenFileDialogProps().result(channel).onResult(&emitter));
  Transport::ReturnPort port(registration);
  LOKA_VERIFY(port.seal(FileChooserResult::Canceled()) == window);
  Transport::Registration *later = window->dialogResults().reserve(OpenFileDialogProps().onResult(&emitter));
  Transport::ReturnPort laterPort(later);
  LOKA_VERIFY(laterPort.seal(FileChooserResult::Canceled()) == window);
  app.flush();
  LOKA_VERIFY(deaths == 0 && emits == 0 && app.pending());
  app.flush();
  LOKA_VERIFY(deaths == 1 && !app.pending());
  delete registration;
  delete later;
  channel.unbind(&CloseProbe::close, &probe);
  emitter.unbind(&countEvent, &emits);
}

void testOpenFileDialogTransportReservedReturnIsRevoked()
{
  NullWindow window(0, WindowProps());
  WindowAdmissionTestApp app(window);
  loka::core::EmitterState emitter;
  Transport::Registration *registration = window.dialogResults().reserve(OpenFileDialogProps().onResult(&emitter));
  Transport::ReturnPort port(registration);
  LOKA_VERIFY(!window.dialogResults().hasRunnableWork());
  window.dialogResults().close();
  app.flush();
  LOKA_VERIFY(Access::census(window.dialogResults()) == 0);
  LOKA_VERIFY(port.seal(FileChooserResult::Canceled()) == 0);
  delete registration;

  Fixture fixture;
  Transport::ReturnPort contextPort(g_context->registration_);
  g_owner->shown_.set(false);
  fixture.window.flushSceneInvalidation();
  fixture.app.flush();
  LOKA_VERIFY(Access::census(fixture.window.dialogResults()) == 0);
  LOKA_VERIFY(contextPort.seal(FileChooserResult::Canceled()) == 0);
}

namespace
{
  struct CommitProbe
  {
    WindowAdmissionTestApp *app;
    loka::core::PushStateTracker *tracker;
    loka::core::StateBase *result;
    std::vector<int> *commits;
    static void admit(void *data)
    {
      CommitProbe &probe = *static_cast<CommitProbe *>(data);
      const loka::core::TrackerPhase phase = probe.tracker->phase();
      LOKA_VERIFY(phase == loka::core::TRACKER_COMMIT);
      const loka::core::PushStateTracker::StateList &dirty = probe.tracker->committedSet().identities();
      if (probe.commits->empty())
      {
        probe.commits->push_back(1);
        probe.app->flush();
        LOKA_VERIFY(loka::core::testing::PushStateTrackerTestAccess::nextDirtyCount(*probe.tracker) == 1);
        LOKA_VERIFY(dirty.size() == 1 && dirty[0] != probe.result);
      }
      else
      {
        probe.commits->push_back(2);
        LOKA_VERIFY(dirty.size() == 1 && dirty[0] == probe.result);
      }
    }
  };

  struct ReentrantProducer
  {
    Transport *transport;
    Transport::Registration *registration;
    loka::core::EmitterState *emitter;
    WindowAdmissionTestApp *app;
    static void produce(void *data)
    {
      ReentrantProducer &probe = *static_cast<ReentrantProducer *>(data);
      probe.registration = probe.transport->reserve(OpenFileDialogProps().onResult(probe.emitter));
      Transport::ReturnPort port(probe.registration);
      LOKA_VERIFY(port.seal(FileChooserResult::Canceled()) != 0);
      probe.app->flush(); // Nested admission returns, leaving the new batch pending.
    }
  };
} // namespace

void testOpenFileDialogTransportCommitIntakeUsesNext()
{
  NullWindow window(0, WindowProps());
  WindowAdmissionTestApp app(window);
  loka::core::MutableState<int> trigger(0);
  loka::core::MutableState<FileChooserResult> result;
  loka::core::PushStateTracker tracker;
  tracker.addState(&trigger);
  tracker.addState(&result);
  NodeState<FileChooserResult> channel(&result, &tracker);
  loka::core::EmitterState emitter;
  int emits = 0;
  emitter.bind(&countEvent, &emits, false);
  std::vector<int> commits;
  CommitProbe probe = {&app, &tracker, &result, &commits};
  tracker.setInvalidateCallback(&CommitProbe::admit, &probe);
  Transport::Registration *registration =
      window.dialogResults().reserve(OpenFileDialogProps().result(channel).onResult(&emitter));
  Transport::ReturnPort port(registration);
  LOKA_VERIFY(port.seal(FileChooserResult::Error(665)) == &window);
  {
    loka::core::StateTrackerGuard guard(&tracker);
    trigger.set(1);
  }
  LOKA_VERIFY(commits.size() == 2 && commits[0] == 1 && commits[1] == 2 && emits == 1);
  tracker.setInvalidateCallback(0, 0);
  app.flush();
  delete registration;
  emitter.unbind(&countEvent, &emits);
}

void testOpenFileDialogTransportRetargetAndFiniteBatch()
{
  Fixture fixture;
  int emits = 0;
  loka::core::EmitterState replacement;
  replacement.bind(&countEvent, &emits, false);
  g_context->produce();
  OpenFileDialogDefinition same;
  same.result(g_owner->result_).onResult(&g_owner->emitter_);
  LOKA_VERIFY(same.applyPropsToNode(g_context->node_));
  LOKA_VERIFY(g_context->registration_);
  OpenFileDialogDefinition changed;
  changed.onResult(&replacement);
  LOKA_VERIFY(changed.applyPropsToNode(g_context->node_));
  LOKA_VERIFY(!g_context->registration_);
  fixture.app.flush();
  LOKA_VERIFY(g_owner->result_.get().kind == FileChooserResult::RESULT_NONE && emits == 0);

  loka::core::EmitterState first;
  ReentrantProducer probe = {&fixture.window.dialogResults(), 0, &replacement, &fixture.app};
  first.bind(&ReentrantProducer::produce, &probe, false);
  Transport::Registration *registration =
      fixture.window.dialogResults().reserve(OpenFileDialogProps().onResult(&first));
  Transport::ReturnPort port(registration);
  LOKA_VERIFY(port.seal(FileChooserResult::Canceled()) == &fixture.window);
  fixture.app.flush();
  LOKA_VERIFY(emits == 0);
  fixture.app.flush();
  LOKA_VERIFY(emits == 1);
  fixture.app.flush();
  LOKA_VERIFY(Access::census(fixture.window.dialogResults()) == 0);
  delete registration;
  delete probe.registration;
  first.unbind(&ReentrantProducer::produce, &probe);
  replacement.unbind(&countEvent, &emits);
}

void testOpenFileDialogTransportShutdownRevokesBothPopulations()
{
  int deaths = 0, emits = 0;
  loka::core::EmitterState emitter;
  emitter.bind(&countEvent, &emits, false);
  Transport::Registration *firstRegistration = 0, *secondRegistration = 0;
  {
    OwnedApp app;
    DeathWindow *first = new DeathWindow(deaths), *second = new DeathWindow(deaths);
    app.adopt(first);
    app.adopt(second);
    firstRegistration = first->dialogResults().reserve(OpenFileDialogProps().onResult(&emitter));
    secondRegistration = second->dialogResults().reserve(OpenFileDialogProps().onResult(&emitter));
    Transport::ReturnPort firstPort(firstRegistration), secondPort(secondRegistration);
    LOKA_VERIFY(firstPort.seal(FileChooserResult::Canceled()) == first);
    LOKA_VERIFY(secondPort.seal(FileChooserResult::Canceled()) == second);
    app.requestWindowClose(first);
  }
  LOKA_VERIFY(deaths == 2 && emits == 0);
  delete firstRegistration;
  delete secondRegistration;
  emitter.unbind(&countEvent, &emits);
}

namespace
{
  class RunningController : public NullScenePlatformController
  {
  public:
    RunningController()
        : app(0),
          window(0),
          emits(0)
    {
    }
    virtual void beginApplyCycle()
    {
      if (!this->app)
        return;
      LOKA_VERIFY(this->window->scene()->isRunInProgress()); // loka-assert-ok: read-only run exclusion query
      this->app->flush();
      LOKA_VERIFY(*this->emits == 0);
    }
    WindowAdmissionTestApp *app;
    Window *window;
    int *emits;
  };
  struct RetargetObserver
  {
    Transport::Registration *replacement;
    loka::core::EmitterState *emitter;
    static void retarget(void *data)
    {
      RetargetObserver &probe = *static_cast<RetargetObserver *>(data);
      OpenFileDialogDefinition changed;
      changed.onResult(probe.emitter);
      LOKA_VERIFY(changed.applyPropsToNode(g_context->node_));
      // Explicitly start the replacement operation while the old write runs.
      g_context->presentation_.markDetached();
      g_context->attach();
      probe.replacement = g_context->registration_;
      g_context->produce();
    }
  };
  void destroyEmitter(void *data)
  {
    loka::core::EmitterState **emitter = static_cast<loka::core::EmitterState **>(data);
    delete *emitter;
    *emitter = 0;
  }
} // namespace

void testOpenFileDialogTransportSceneRunExcludesAdmission()
{
  RunningController controller;
  WindowProps props;
  props.scene(new Scene(loka::app::Button("run").clone()));
  NullWindow window(0, props, &controller);
  WindowAdmissionTestApp app(window);
  int emits = 0;
  loka::core::EmitterState emitter;
  emitter.bind(&countEvent, &emits, false);
  Transport::Registration *registration = window.dialogResults().reserve(OpenFileDialogProps().onResult(&emitter));
  Transport::ReturnPort port(registration);
  LOKA_VERIFY(port.seal(FileChooserResult::Canceled()) == &window);
  controller.app = &app;
  controller.window = &window;
  controller.emits = &emits;
  window.scene()->invalidate();
  LOKA_VERIFY(emits == 0);
  controller.app = 0;
  app.flush();
  LOKA_VERIFY(emits == 1);
  app.flush();
  delete registration;
  emitter.unbind(&countEvent, &emits);
}

void testOpenFileDialogTransportRetargetDuringWriteCannotUnlinkReplacement()
{
  Fixture fixture;
  int oldEmits = 0, newEmits = 0;
  loka::core::EmitterState replacement;
  replacement.bind(&countEvent, &newEmits, false);
  g_owner->emitter_.bind(&countEvent, &oldEmits, false);
  RetargetObserver probe = {0, &replacement};
  g_owner->result_.bind(&RetargetObserver::retarget, &probe, false);
  g_context->produce();
  fixture.app.flush();
  LOKA_VERIFY(oldEmits == 0 && newEmits == 0 && g_context->registration_ == probe.replacement);
  fixture.app.flush();
  LOKA_VERIFY(oldEmits == 0 && newEmits == 1);
  fixture.app.flush();
  LOKA_VERIFY(Access::census(fixture.window.dialogResults()) == 0);
  g_owner->result_.unbind(&RetargetObserver::retarget, &probe);
  g_owner->emitter_.unbind(&countEvent, &oldEmits);
  replacement.unbind(&countEvent, &newEmits);
}

void testOpenFileDialogTransportEmitterTokenChecksDeliveryLifetime()
{
  NullWindow window(0, WindowProps());
  WindowAdmissionTestApp app(window);
  loka::core::MutableState<FileChooserResult> result;
  loka::core::PushStateTracker tracker;
  tracker.addState(&result);
  NodeState<FileChooserResult> channel(&result, &tracker);
  loka::core::EmitterState *emitter = new loka::core::EmitterState();
  channel.bind(&destroyEmitter, &emitter, false);
  Transport::Registration *registration =
      window.dialogResults().reserve(OpenFileDialogProps().result(channel).onResult(emitter));
  Transport::ReturnPort port(registration);
  LOKA_VERIFY(port.seal(FileChooserResult::Canceled()) == &window);
  app.flush();
  LOKA_VERIFY(!emitter && channel.get().kind == FileChooserResult::RESULT_CANCELED);
  app.flush();
  LOKA_VERIFY(Access::census(window.dialogResults()) == 0);
  delete registration;
  channel.unbind(&destroyEmitter, &emitter);
}

void testOpenFileDialogTransportCrossWindowCloseCancelsLaterBatch()
{
  int deaths = 0, emits = 0;
  OwnedApp app;
  DeathWindow *first = new DeathWindow(deaths), *second = new DeathWindow(deaths);
  app.adopt(first);
  app.adopt(second);
  loka::core::EmitterState closer, observer;
  CloseProbe probe = {&app, second, &deaths};
  closer.bind(&CloseProbe::close, &probe, false);
  observer.bind(&countEvent, &emits, false);
  Transport::Registration *a = first->dialogResults().reserve(OpenFileDialogProps().onResult(&closer));
  Transport::Registration *b = second->dialogResults().reserve(OpenFileDialogProps().onResult(&observer));
  Transport::ReturnPort portA(a), portB(b);
  LOKA_VERIFY(portA.seal(FileChooserResult::Canceled()) == first);
  LOKA_VERIFY(portB.seal(FileChooserResult::Canceled()) == second);
  app.flush();
  LOKA_VERIFY(deaths == 0 && emits == 0 && app.pending());
  app.flush();
  LOKA_VERIFY(deaths == 1 && emits == 0);
  delete a;
  delete b;
  closer.unbind(&CloseProbe::close, &probe);
  observer.unbind(&countEvent, &emits);
}

void testOpenFileDialogTransportEnrollmentRefusalRetriesOnAdmission()
{
  g_enrollmentAttempts = 0;
  g_enrollmentLive = 0;
  g_enrollmentRefusals = 0;
  loka::core::LokaAllocSetBackend(&enrollmentAlloc, &enrollmentFree);
  {
    Fixture fixture;
    g_owner->shown_.set(false);
    fixture.app.flush();
    fixture.app.flush();
    fixture.events.clear();
    g_enrollmentAttempts = 0;
    g_enrollmentRefusals = 1;
    g_owner->shown_.set(true);
    LOKA_VERIFY(g_enrollmentAttempts == 1);
    LOKA_VERIFY(!g_context->registration_);
    LOKA_VERIFY(g_context->presentation_.value == OPEN_FILE_DIALOG_PRESENTATION_PENDING_ATTACH);
    LOKA_VERIFY(Access::census(fixture.window.dialogResults()) == 0 && g_enrollmentLive == 0);
    const bool pending = fixture.window.hasPendingSceneInvalidation();
    LOKA_VERIFY(pending);
    LOKA_VERIFY(fixture.events.size() == 1 && fixture.events[0] == 4);
    fixture.events.clear();
    g_enrollmentRefusals = 0;
    fixture.app.flush();
    LOKA_VERIFY(g_enrollmentAttempts == 2 && g_enrollmentLive == 1);
    LOKA_VERIFY(g_context->registration_);
    LOKA_VERIFY(g_context->presentation_.value == OPEN_FILE_DIALOG_PRESENTATION_PRESENTED);
    LOKA_VERIFY(fixture.events.size() == 1 && fixture.events[0] == 5);
    fixture.app.flush();
    LOKA_VERIFY(g_enrollmentAttempts == 2 && fixture.events.size() == 1);
  }
  LOKA_VERIFY(g_enrollmentLive == 0);
  loka::core::LokaAllocSetBackend(0, 0);
}

namespace
{
  void hideDialogOnResult(void *) { g_owner->shown_.set(false); }
}
void testDialogSeatClockEligibility()
{
  for (unsigned clock = 0; clock != 2; ++clock)
  {
    Fixture f(true);
    int emits = 0;
    g_owner->result_.bind(&hideDialogOnResult, 0, false);
    g_owner->emitter_.bind(&countEvent, &emits, false);
    g_context->produce();
    if (clock)
    {
      loka::core::Operation turn;
      f.app.admitAndApplyWindows();
      LOKA_VERIFY(emits == 1 && g_context && !g_owner->shown_.get());
      turn.settle();
      f.app.admitAndApplyWindows();
      turn.close();
      f.app.reclaimWindows();
    }
    else
    {
      f.app.admitAndApplyWindows();
      LOKA_VERIFY(emits == 0);
      f.app.flush();
    }
    LOKA_VERIFY(!g_context);
    g_owner->result_.unbind(&hideDialogOnResult, 0);
    g_owner->emitter_.unbind(&countEvent, &emits);
  }
}
void testDialogSeatClockCancellationStillSuppresses()
{
  Fixture f;
  int emits = 0;
  g_owner->result_.bind(&cancelObserver, 0, false);
  g_owner->emitter_.bind(&countEvent, &emits, false);
  g_context->produce();
  loka::core::Operation turn;
  f.app.admitAndApplyWindows();
  LOKA_VERIFY(emits == 0 && g_owner->result_.get().kind == FileChooserResult::RESULT_FILE);
  turn.close();
  f.app.flush();
  LOKA_VERIFY(Access::census(f.window.dialogResults()) == 0);
  g_owner->result_.unbind(&cancelObserver, 0);
  g_owner->emitter_.unbind(&countEvent, &emits);
}

void testDialogSeatClockDeadTokenStillSuppresses()
{
  NullWindow window(0, WindowProps());
  WindowAdmissionTestApp app(window);
  loka::core::MutableState<FileChooserResult> result;
  loka::core::PushStateTracker tracker;
  tracker.addState(&result);
  NodeState<FileChooserResult> channel(&result, &tracker);
  loka::core::EmitterState *emitter = new loka::core::EmitterState();
  int emits = 0;
  emitter->bind(&countEvent, &emits, false);
  channel.bind(&destroyEmitter, &emitter, false);
  Transport::Registration *registration =
      window.dialogResults().reserve(OpenFileDialogProps().result(channel).onResult(emitter));
  Transport::ReturnPort port(registration);
  LOKA_VERIFY(port.seal(FileChooserResult::Canceled()) == &window);
  loka::core::Operation turn;
  app.admitAndApplyWindows();
  LOKA_VERIFY(!emitter && emits == 0 && channel.get().kind == FileChooserResult::RESULT_CANCELED);
  turn.close();
  app.reclaimWindows();
  app.flush();
  delete registration;
  channel.unbind(&destroyEmitter, &emitter);
}
void testDialogSeatClockWindowCloseStillSuppresses()
{
  int deaths = 0, emits = 0;
  OwnedApp app;
  DeathWindow *window = new DeathWindow(deaths);
  app.adopt(window);
  loka::core::MutableState<FileChooserResult> result;
  loka::core::PushStateTracker tracker;
  tracker.addState(&result);
  NodeState<FileChooserResult> channel(&result, &tracker);
  loka::core::EmitterState emitter;
  emitter.bind(&countEvent, &emits, false);
  CloseProbe probe = { &app, window, &deaths };
  channel.bind(&CloseProbe::close, &probe, false);
  Transport::Registration *registration =
      window->dialogResults().reserve(OpenFileDialogProps().result(channel).onResult(&emitter));
  Transport::ReturnPort port(registration);
  LOKA_VERIFY(port.seal(FileChooserResult::Canceled()) == window);
  {
    loka::core::Operation turn;
    app.admitAndApplyWindows();
    LOKA_VERIFY(deaths == 0 && emits == 0 && app.pending());
    turn.close();
    app.reclaimWindows();
  }
  app.flush();
  LOKA_VERIFY(deaths == 1 && emits == 0);
  delete registration;
  channel.unbind(&CloseProbe::close, &probe);
}

void testFileDialogOptionsIdentityAndNullRefusal()
{
  const loka::core::String name(std::string("Untitled"));
  OpenFileDialogDefinition saved;
  loka::core::OwnedDef<NodeDefinitionBase> clone;
  {
    OpenFileDialogDefinition temporary = SaveFileDialog(name)
        .filterPolicy(FILE_DIALOG_FILTER_ALL_FILES_TEXT);
    clone.reset(temporary.clone());
    LOKA_VERIFY(clone.get() != 0);
    saved = temporary;
  }
  const OpenFileDialogProps &cloned = static_cast<const OpenFileDialogProps &>(*clone->propsBase());
  LOKA_VERIFY(cloned.options_.compare(saved.props.options_) == 0);
  LOKA_VERIFY(saved.props.options_.purpose() == FILE_DIALOG_SAVE);
  LOKA_VERIFY(saved.props.options_.defaultName().equals(name));
  LOKA_VERIFY(saved.props.options_.filterPolicy() == FILE_DIALOG_FILTER_ALL_FILES_TEXT);
  OpenFileDialogProps variants[5];
  variants[0].options_ = FileDialogOptions(FILE_DIALOG_OPEN, name);
  variants[1].options_ = FileDialogOptions(FILE_DIALOG_SAVE, name);
  variants[2].options_ = FileDialogOptions(FILE_DIALOG_SAVE, loka::core::String::Literal("Other"));
  variants[3] = saved.props;
  variants[4].options_ = FileDialogOptions(FILE_DIALOG_OPEN, name, FILE_DIALOG_FILTER_ALL_FILES_TEXT);
  LOKA_VERIFY(variants[0].options_.defaultName().empty());
  LOKA_VERIFY(!(variants[0] < OpenFileDialogProps()) && !(OpenFileDialogProps() < variants[0]));
  NullWindow window(0, WindowProps());
  WindowAdmissionTestApp app(window);
  for (unsigned i = 0; i != 5; ++i)
  {
    Transport::Registration *registration = window.dialogResults().reserve(variants[i]);
    LOKA_VERIFY(registration != 0);
    OpenFileDialogProps equal = variants[i];
    equal.options_ = FileDialogOptions(variants[i].options_.purpose(),
        loka::core::String(std::string(i == 2 ? "Other" : "Untitled")),
        variants[i].options_.filterPolicy());
    LOKA_VERIFY(registration->matches(equal));
    LOKA_VERIFY(!(equal < variants[i]) && !(variants[i] < equal));
    for (unsigned j = 0; j != 5; ++j)
    {
      LOKA_VERIFY(registration->matches(variants[j]) == (i == j));
      LOKA_VERIFY(((variants[i] < variants[j]) || (variants[j] < variants[i])) == (i != j));
      LOKA_VERIFY(!((variants[i] < variants[j]) && (variants[j] < variants[i])));
    }
    delete registration;
    app.flush();
  }
  NullScenePlatformController controller;
  loka::core::EmitterState emitter;
  for (unsigned i = 0; i != 2; ++i)
  {
    OpenFileDialogDefinition definition = i ? SaveFileDialog(name) : OpenFileDialog();
    definition.onResult(&emitter);
    OpenFileDialogNode node(definition.props);
    LayoutState layout;
    LOKA_VERIFY(!controller.prepareProjectedLayout(&node, layout));
    LOKA_VERIFY(node.getContext() == 0);
  }
}

void testFileDialogOptionsRetarget()
{
  for (unsigned field = 0; field != 3; ++field)
  {
    Fixture fixture(false, FileDialogOptions(FILE_DIALOG_SAVE, loka::core::String::Literal("initial")));
    LOKA_VERIFY(g_context->registration_ != 0);
    g_context->produce();
    OpenFileDialogDefinition equal = SaveFileDialog(loka::core::String(std::string("initial")));
    equal.result(g_owner->result_).onResult(&g_owner->emitter_);
    LOKA_VERIFY(equal.applyPropsToNode(g_context->node_));
    LOKA_VERIFY(g_context->registration_ != 0);
    OpenFileDialogDefinition changed = equal;
    changed.props.options_ = FileDialogOptions(field == 0 ? FILE_DIALOG_OPEN : FILE_DIALOG_SAVE,
        loka::core::String::Literal(field == 1 ? "changed" : "initial"),
        field == 2 ? FILE_DIALOG_FILTER_ALL_FILES_TEXT : FILE_DIALOG_FILTER_DEFAULT);
    LOKA_VERIFY(changed.applyPropsToNode(g_context->node_));
    LOKA_VERIFY(g_context->registration_ == 0);
    g_context->attach();
    LOKA_VERIFY(g_context->registration_ == 0);
    fixture.app.flush();
    LOKA_VERIFY(g_owner->result_.get().kind == FileChooserResult::RESULT_NONE);
    g_owner->shown_.set(false);
    fixture.app.flush();
    g_owner->shown_.set(true);
    fixture.app.flush();
    LOKA_VERIFY(g_context->registration_ != 0);
    g_context->produce();
    fixture.app.flush();
    LOKA_VERIFY(g_owner->result_.get().kind == FileChooserResult::RESULT_FILE);
  }
}
