#include "app/ConfirmDialog.hpp"
#include "support/DialogContext.hpp"
#include "support/DialogResultTestAccess.hpp"
#include "support/WindowAdmissionTestApp.hpp"
#include "support/TestVerify.hpp"
#include "app/nodes/boundary/StdComposition.hpp"
#include "app/nodes/nestable/Show.hpp"
#include "app/scene/projection/RetainedNodeHandler.hpp"
#include "app/style/SurfaceGround.hpp"
#include <cstring>

namespace
{
  using namespace loka::app;
  using namespace loka::app::scene;
  typedef DialogResultTransport Transport;
  typedef loka::app::testing::DialogResultTestAccess Access;
  typedef dialog_testing::DialogContext<ConfirmDialogNode, Transport::ConfirmRegistration,
                                        Transport::ConfirmReturnPort> ConfirmContext;
  NullWindow *g_window = 0;
  std::vector<int> *g_events = 0;
  ConfirmContext *g_context = 0;
  class DialogOwner;
  DialogOwner *g_owner = 0;

  class DialogHandler : public RetainedNodeHandler<DialogHandler, ConfirmDialogNode, ConfirmContext>
  {
  public:
    static void afterAttach(ConfirmContext *context) { context->attach(); }
    static void refresh(ConfirmContext *context, const LayoutState &) { context->attach(); }
    static ConfirmDialogNode *cast(Node *node) { return node->asConfirmDialogNode(); }
    static ConfirmContext *create(ConfirmDialogNode *node, IPlatformController *, const LayoutState &)
    {
      g_context = new ConfirmContext(node, *g_window, *g_events);
      return g_context;
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
      this->state(this->result_, ConfirmResult::none());
      g_owner = this;
    }
    virtual void composeNode(NodeComposition &composition)
    {
      composition.declare(Show(*this->shown_.state())
          << ConfirmDialog(ConfirmDialogOptions(loka::core::String::Literal("Save changes?")))
                 .result(this->result_).onResult(&this->emitter_));
    }
    NodeState<bool> shown_;
    NodeState<ConfirmResult> result_;
    loka::core::EmitterState emitter_;
  };
  struct Fixture
  {
    Fixture() : window(0, WindowProps()), app(window)
    {
      g_window = &this->window;
      g_events = &this->events;
      LOKA_VERIFY(this->window.scenePlatformController()->registerNodeHandler(&this->handler));
      LOKA_VERIFY(this->window.sceneManager()->commitTransaction(
          0, new Scene(new NodeDefinition<DialogOwnerProps, DialogOwner>(DialogOwnerProps()))));
      this->app.flush();
      LOKA_VERIFY(g_owner && g_context && g_context->registration_);
    }
    ~Fixture()
    {
      this->window.destroyScenePlatform();
      g_owner = 0;
      g_context = 0;
      g_window = 0;
      g_events = 0;
    }
    std::vector<int> events;
    DialogHandler handler;
    NullWindow window;
    WindowAdmissionTestApp app;
  };
  void count(void *data) { ++*static_cast<int *>(data); }

  int g_refusals = 0, g_attempts = 0, g_live = 0;
  void *allocate(size_t size, const loka::core::LokaAllocationSite &site)
  {
    if (std::strcmp(site.ownerTag, "DialogResultTransport") == 0)
    {
      ++g_attempts;
      if (g_refusals) { --g_refusals; return 0; }
      ++g_live;
    }
    return new (std::nothrow) char[size];
  }
  void release(void *storage, const loka::core::LokaAllocationSite &site)
  {
    if (std::strcmp(site.ownerTag, "DialogResultTransport") == 0)
      --g_live;
    delete[] static_cast<char *>(storage);
  }
}

void testConfirmDialogTransportDeliversEachResultOnce()
{
  const ConfirmResult choices[] = { ConfirmResult::Save(), ConfirmResult::Discard(),
      ConfirmResult::Cancel(), ConfirmResult::Error(431) };
  for (unsigned i = 0; i != 4; ++i)
  {
    Fixture f;
    int writes = 0, emits = 0;
    g_owner->result_.bind(&count, &writes, false);
    g_owner->emitter_.bind(&count, &emits, false);
    Transport::ConfirmReturnPort port(g_context->registration_);
    LOKA_VERIFY(port.seal(ConfirmResult::none()) == 0);
    f.app.flush();
    LOKA_VERIFY(writes == 0 && emits == 0 && g_owner->result_.get() == ConfirmResult::none());
    LOKA_VERIFY(port.seal(choices[i]) == &f.window);
    LOKA_VERIFY(port.seal(ConfirmResult::Error(99)) == 0);
    f.app.flush();
    LOKA_VERIFY(g_owner->result_.get() == choices[i] && writes == 1 && emits == 1);
    f.app.flush();
    LOKA_VERIFY(writes == 1 && emits == 1 && Access::census(f.window.dialogResults()) == 0);
    g_owner->result_.unbind(&count, &writes);
    g_owner->emitter_.unbind(&count, &emits);
  }
}

void testConfirmDialogTransportReservedReturnIsRevoked()
{
  for (unsigned cause = 0; cause != 3; ++cause)
  {
    Fixture f;
    int writes = 0, emits = 0;
    g_owner->result_.bind(&count, &writes, false);
    g_owner->emitter_.bind(&count, &emits, false);
    Transport::ConfirmReturnPort port(g_context->registration_);
    if (cause == 0)
    {
      f.window.dialogResults().close();
      LOKA_VERIFY(!f.window.dialogResults().reserve(ConfirmDialogProps().result(g_owner->result_)));
    }
    else if (cause == 1)
    {
      ConfirmDialog changed(ConfirmDialogOptions(loka::core::String::Literal("Retarget")));
      changed.result(g_owner->result_).onResult(&g_owner->emitter_);
      LOKA_VERIFY(changed.applyPropsToNode(g_context->node_));
      LOKA_VERIFY(!g_context->registration_);
    }
    else
    {
      g_owner->shown_.set(false);
      f.window.flushSceneInvalidation();
      LOKA_VERIFY(!g_context->registration_);
    }
    f.app.flush();
    LOKA_VERIFY(Access::census(f.window.dialogResults()) == 0);
    LOKA_VERIFY(port.seal(ConfirmResult::Save()) == 0);
    f.app.flush();
    LOKA_VERIFY(writes == 0 && emits == 0 && g_owner->result_.get() == ConfirmResult::none());
    g_owner->result_.unbind(&count, &writes);
    g_owner->emitter_.unbind(&count, &emits);
  }
}

namespace
{
  class OwnedApp : public App
  {
  public:
    OwnedApp() : App(0) { this->group_ = new AppComponentGroup(std::vector<AppComponent *>()); }
    void adopt(Window *window) { this->group_->adopt(window); }
    virtual void quit() {}
    void flush() { this->flushWindowInvalidations(); }
  };
  struct Closer
  {
    OwnedApp *app;
    Window *window;
    Transport::ConfirmRegistration **registration;
    static void close(void *data)
    {
      Closer &self = *static_cast<Closer *>(data);
      delete *self.registration;
      *self.registration = 0;
      self.app->requestWindowClose(self.window);
      self.app->flush();
    }
  };
}
void testConfirmDialogTransportCallbackClosesWindow()
{
  OwnedApp app;
  NullWindow *window = new NullWindow(0, WindowProps());
  app.adopt(window);
  loka::core::MutableState<ConfirmResult> state;
  loka::core::PushStateTracker tracker;
  tracker.addState(&state);
  NodeState<ConfirmResult> channel(&state, &tracker);
  loka::core::EmitterState emitter;
  int emits = 0;
  emitter.bind(&count, &emits, false);
  Transport::ConfirmRegistration *registration =
      window->dialogResults().reserve(ConfirmDialogProps().result(channel).onResult(&emitter));
  Closer closer = { &app, window, &registration };
  channel.bind(&Closer::close, &closer, false);
  Transport::ConfirmReturnPort port(registration);
  LOKA_VERIFY(port.seal(ConfirmResult::Save()) == window);
  app.flush();
  LOKA_VERIFY(!registration && emits == 0 && channel.get() == ConfirmResult::Save());
  app.flush();
  channel.unbind(&Closer::close, &closer);
  emitter.unbind(&count, &emits);
}

namespace
{
  struct Ordered
  {
    std::vector<int> *order;
    int value;
    static void note(void *data)
    {
      Ordered &self = *static_cast<Ordered *>(data);
      self.order->push_back(self.value);
    }
  };
  struct NextBatch
  {
    NullWindow *window;
    WindowAdmissionTestApp *app;
    NodeState<ConfirmResult> channel;
    Transport::ConfirmRegistration *registration;
    static void produce(void *data)
    {
      NextBatch &self = *static_cast<NextBatch *>(data);
      self.registration = self.window->dialogResults().reserve(ConfirmDialogProps().result(self.channel));
      Transport::ConfirmReturnPort port(self.registration);
      LOKA_VERIFY(port.seal(ConfirmResult::Discard()) == self.window);
      self.app->flush();
    }
  };
}
void testDialogTransportMixedPayloadBatchKeepsOrder()
{
  NullWindow window(0, WindowProps());
  WindowAdmissionTestApp app(window);
  loka::core::MutableState<FileChooserResult> fileA, fileB;
  loka::core::MutableState<ConfirmResult> confirm, later;
  loka::core::PushStateTracker tracker;
  tracker.addState(&fileA); tracker.addState(&fileB); tracker.addState(&confirm); tracker.addState(&later);
  NodeState<FileChooserResult> a(&fileA, &tracker), b(&fileB, &tracker);
  NodeState<ConfirmResult> c(&confirm, &tracker), d(&later, &tracker);
  std::vector<int> order;
  Ordered probes[] = { {&order, 1}, {&order, 2}, {&order, 3}, {&order, 4} };
  a.bind(&Ordered::note, &probes[0], false); c.bind(&Ordered::note, &probes[1], false);
  b.bind(&Ordered::note, &probes[2], false); d.bind(&Ordered::note, &probes[3], false);
  NextBatch next = { &window, &app, d, 0 };
  loka::core::EmitterState emitter;
  emitter.bind(&NextBatch::produce, &next, false);
  Transport::Registration *ra = window.dialogResults().reserve(OpenFileDialogProps().result(a).onResult(&emitter));
  Transport::ConfirmRegistration *rc = window.dialogResults().reserve(ConfirmDialogProps().result(c));
  Transport::Registration *rb = window.dialogResults().reserve(OpenFileDialogProps().result(b));
  Transport::ReturnPort pa(ra), pb(rb);
  Transport::ConfirmReturnPort pc(rc);
  LOKA_VERIFY(pa.seal(FileChooserResult::Canceled()) == &window);
  LOKA_VERIFY(pc.seal(ConfirmResult::Save()) == &window);
  LOKA_VERIFY(pb.seal(FileChooserResult::Error(77)) == &window);
  app.flush();
  LOKA_VERIFY(order.size() == 3 && order[0] == 1 && order[1] == 2 && order[2] == 3);
  LOKA_VERIFY(d.get() == ConfirmResult::none());
  app.flush();
  LOKA_VERIFY(order.size() == 4 && order[3] == 4);
  app.flush();
  LOKA_VERIFY(Access::census(window.dialogResults()) == 0);
  delete ra; delete rc; delete rb; delete next.registration;
  a.unbind(&Ordered::note, &probes[0]); c.unbind(&Ordered::note, &probes[1]);
  b.unbind(&Ordered::note, &probes[2]); d.unbind(&Ordered::note, &probes[3]);
  emitter.unbind(&NextBatch::produce, &next);
}

void testConfirmDialogTransportEnrollmentRefusalRetriesOnAdmission()
{
  g_refusals = g_attempts = g_live = 0;
  loka::core::LokaAllocSetBackend(&allocate, &release);
  {
    Fixture f;
    g_owner->shown_.set(false);
    f.app.flush(); f.app.flush();
    g_attempts = 0;
    g_refusals = 1;
    g_owner->shown_.set(true);
    LOKA_VERIFY(g_attempts == 1 && g_live == 0 && !g_context->registration_);
    LOKA_VERIFY(f.window.hasPendingSceneInvalidation());
    f.app.flush();
    LOKA_VERIFY(g_attempts == 2 && g_live == 1 && g_context->registration_);
    f.app.flush();
    LOKA_VERIFY(g_attempts == 2);
  }
  LOKA_VERIFY(g_live == 0);
  loka::core::LokaAllocSetBackend(0, 0);
}

void testDialogTransportReclaimsRetiredEntriesOfBothPayloads()
{
  g_refusals = g_attempts = g_live = 0;
  loka::core::LokaAllocSetBackend(&allocate, &release);
  {
    NullWindow window(0, WindowProps());
    WindowAdmissionTestApp app(window);
    loka::core::MutableState<ConfirmResult> state;
    loka::core::PushStateTracker tracker;
    tracker.addState(&state);
    NodeState<ConfirmResult> channel(&state, &tracker);
    int writes = 0;
    channel.bind(&count, &writes, false);
    for (unsigned path = 0; path != 3; ++path)
    {
      Transport::Registration *file = window.dialogResults().reserve(OpenFileDialogProps());
      Transport::ConfirmRegistration *confirm = window.dialogResults().reserve(ConfirmDialogProps().result(channel));
      Transport::ReturnPort filePort(file);
      Transport::ConfirmReturnPort confirmPort(confirm);
      if (path != 0)
      {
        loka::file::File selected = loka::file::File::FromPath(loka::core::String(std::string("owned path")));
        LOKA_VERIFY(filePort.seal(FileChooserResult::File(selected)) == &window);
        LOKA_VERIFY(confirmPort.seal(ConfirmResult::Error(8)) == &window);
      }
      if (path != 2)
      {
        delete file; delete confirm;
        file = 0; confirm = 0;
      }
      app.flush(); app.flush();
      LOKA_VERIFY(g_live == 0 && Access::census(window.dialogResults()) == 0);
      LOKA_VERIFY(writes == (path == 2 ? 1 : 0));
      delete file; delete confirm;
    }
    channel.unbind(&count, &writes);
    // Window destruction also revokes and reclaims reserved entries of both types.
    Transport::Registration *file = window.dialogResults().reserve(OpenFileDialogProps());
    Transport::ConfirmRegistration *confirm = window.dialogResults().reserve(ConfirmDialogProps().result(channel));
    window.dialogResults().close();
    delete file; delete confirm;
  }
  LOKA_VERIFY(g_live == 0);
  loka::core::LokaAllocSetBackend(0, 0);
}

void testConfirmDialogOptionsIdentityAndNullRefusal()
{
  Fixture f;
  ConfirmDialog definition(ConfirmDialogOptions(loka::core::String(std::string("Save changes?"))));
  definition.result(g_owner->result_).onResult(&g_owner->emitter_);
  LOKA_VERIFY(g_context->registration_->matches(definition.props));
  loka::core::OwnedDef<NodeDefinitionBase> clone(definition.clone());
  LOKA_VERIFY(clone.get() && !(definition.props < *clone->propsBase()) && !(*clone->propsBase() < definition.props));
  NullScenePlatformController controller;
  ConfirmDialogNode node(definition.props);
  Node *base = &node;
  LOKA_VERIFY(base->asConfirmDialogNode() == &node && base->kind() == NODE_KIND_CONFIRM_DIALOG);
  LOKA_VERIFY(GroundForKind(base->kind()) == SURFACE_GROUND_NATIVE);
  LayoutState layout;
  layout.y = 17;
  LOKA_VERIFY(!controller.prepareProjectedLayout(&node, layout));
  LOKA_VERIFY(node.layoutProjected(&controller, layout) == 17 && !node.getContext());
  loka::core::MutableState<ConfirmResult> untracked;
  LOKA_VERIFY(!f.window.dialogResults().reserve(ConfirmDialogProps().result(NodeState<ConfirmResult>(&untracked, 0))));
  LOKA_VERIFY(!f.window.dialogResults().reserve(ConfirmDialogProps()));
  ConfirmResult a = ConfirmResult::Save(), b = a;
  b.errorCode = 99;
  LOKA_VERIFY(a == b && ConfirmResult::Error(1) != ConfirmResult::Error(2));
}

namespace
{
  void destroyConfirmEmitter(void *data)
  {
    loka::core::EmitterState **emitter = static_cast<loka::core::EmitterState **>(data);
    delete *emitter;
    *emitter = 0;
  }
}
void testConfirmDialogTransportEmitterTokenChecksDeliveryLifetime()
{
  Fixture f;
  loka::core::EmitterState *emitter = new loka::core::EmitterState();
  ConfirmDialog changed(g_context->node_->props);
  changed.onResult(emitter);
  LOKA_VERIFY(changed.applyPropsToNode(g_context->node_));
  g_context->presentation_.markDetached();
  g_context->attach();
  LOKA_VERIFY(g_context->registration_);
  g_owner->result_.bind(&destroyConfirmEmitter, &emitter, false);
  LOKA_VERIFY(g_context->produce(ConfirmResult::Cancel()) == &f.window);
  f.app.flush();
  LOKA_VERIFY(!emitter && g_owner->result_.get() == ConfirmResult::Cancel());
  f.app.flush();
  LOKA_VERIFY(Access::census(f.window.dialogResults()) == 0);
  g_owner->result_.unbind(&destroyConfirmEmitter, &emitter);
}
