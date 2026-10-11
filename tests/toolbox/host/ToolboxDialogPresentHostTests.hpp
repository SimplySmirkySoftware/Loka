#include "ToolboxInputDoor.hpp"
#include "support/LifecycleFactTestAccess.hpp"
#include "ToolboxActivationPhase.hpp"
#include "support/WindowAdmissionTestApp.hpp"

namespace loka { namespace testing {
struct ToolboxDialogTestAccess
{
  static bool unlinked(const ToolboxDialogEnrollment &row)
  { return row.prev_ == 0 && row.next_ == 0; }
  static bool empty(const ToolboxPendingDialogs &list)
  { return list.sentinel_.next_ == &list.sentinel_; }
};
} }

static unsigned flushedWindows;
static unsigned requiredFlushes;

class ToolboxWindow : public Window
{
public:
  ToolboxScenePlatformController controller;
  bool hasController;
  explicit ToolboxWindow(const WindowProps &props = WindowProps(), PlatformContext *platform = 0)
      : Window(platform, props), hasController(true) {}
  virtual ToolboxWindow *asToolboxWindow() { return this; }
  ToolboxScenePlatformController *scenePlatformController() { return hasController ? &controller : 0; }
  void flushInvalidate() { ++flushedWindows; ToolboxInputDoor::render(this->controller); }
};
class ToolboxApp : public WindowAdmissionTestApp
{
public:
  CursorOwner cursorOwner_;
  bool running_;
  ToolboxApp(Window &first, Window *second = 0) : WindowAdmissionTestApp(first, second), running_(true) {}
  void present(ActivationPhase, loka::core::Operation &);
  bool hasPendingDialogs() const;
  void addComponent(AppComponent *component) { this->group_->adopt(component); }
  void noGroupTick()
  {
    AppComponentGroup *saved = this->group_;
    this->group_ = 0;
    this->tick();
    this->group_ = saved;
  }
  void tick(ActivationPhase phase = ACTIVATION_FOREGROUND)
  { loka::core::Operation turn; this->present(phase, turn); }
};
#include "ToolboxPresent.cpp"

static ToolboxScenePlatformController *modalController;
static loka::app::OpenFileDialogNode *destroyInModal;
static ToolboxApp *modalApp;
static Window *closeOnDelivery;
static unsigned modalChecks;
static void CheckModal()
{
  ++modalChecks;
  LOKA_VERIFY(!modalController->borrowPhase().open());
  LOKA_VERIFY(flushedWindows >= requiredFlushes);
  if (destroyInModal)
  {
    // Context replacement destroys it synchronously, through the real Node door.
    loka::app::scene::NotifySubtreeNodeDetached(destroyInModal);
    loka::app::scene::LifecycleFactTestAccess::DeliverFacts(destroyInModal);
    destroyInModal->setContext(0);
    destroyInModal = 0;
  }

}

static void CloseWindowOnDelivery(void *)
{
  if (closeOnDelivery)
  {
    modalApp->requestWindowClose(closeOnDelivery);
    closeOnDelivery = 0;
  }
}

static void QuitOnDelivery(void *)
{
  if (modalApp)
  {
    modalApp->running_ = false;
    modalApp = 0;
  }
}

/** App-owned result storage outlives destruction of the native context. */
struct DialogPin
{
  loka::core::PushStateTracker tracker;
  loka::core::MutableState<loka::app::FileChooserResult> storage;
  loka::core::EmitterState emitter;
  unsigned writes, emits;
  loka::app::OpenFileDialogNode node;
  ToolboxScenePlatformController &controller;
  DialogPin(ToolboxScenePlatformController &c, bool save = false)
      : writes(0), emits(0),
        node(loka::app::OpenFileDialogProps()
            .result(loka::app::scene::NodeState<loka::app::FileChooserResult>(&storage, &tracker))
            .onResult(&emitter)), controller(c)
  {
    tracker.addState(&storage);
    storage.bind(&CountDelivery, &writes, false);
    emitter.bind(&CountDelivery, &emits, false);
    if (save) node.props.options_ = loka::app::FileDialogOptions(loka::app::FILE_DIALOG_SAVE, String::Literal("first"));
  }
  ~DialogPin()
  {
    node.setContext(0);
    storage.unbind(&CountDelivery, &writes);
    emitter.unbind(&CountDelivery, &emits);
    tracker.removeState(&storage);
  }
  ToolboxOpenFileDialogContext *context()
  { return static_cast<ToolboxOpenFileDialogContext *>(node.getContext()); }
  static void attach(void *data)
  {
    DialogPin &pin = *static_cast<DialogPin *>(data);
    LOKA_VERIFY(pin.controller.borrowPhase().open());
    loka::app::scene::PlatformNodeHandlerRegistry registry;
    LOKA_VERIFY(RegisterToolboxOpenFileDialogNodeHandler(registry));
    loka::app::scene::IPlatformNodeHandler *handler = registry.find(&pin.node);
    LOKA_VERIFY(handler != 0);
    const loka::app::scene::LayoutState layout;
    LOKA_VERIFY(handler->ensureContext(&pin.node, &pin.controller, layout) != 0);
    pin.context()->readLifecycleFactOnAttach();
  }
  void draw(bool update = false)
  {
    controller.onRender = &attach;
    controller.renderData = this;
    if (update)
    {
      const Rect rect = {0, 0, 100, 100};
      ToolboxInputDoor::renderDirty(controller, rect);
    }
    else ToolboxInputDoor::render(controller);
    controller.onRender = 0;
  }
  void detach()
  {
    loka::app::scene::NotifySubtreeNodeDetached(&node);
    loka::app::scene::LifecycleFactTestAccess::DeliverFacts(&node);
  }
  void reattach()
  {
    loka::app::scene::NotifySubtreeNodeAttached(&node);
    loka::app::scene::LifecycleFactTestAccess::DeliverFacts(&node);
  }
};

#include "app/nodes/boundary/StdComposition.hpp"
#include "app/nodes/nestable/Show.hpp"
#include "testing/scene/SceneTestFlow.hpp"

class DialogSceneRoot;
typedef loka::app::scene::BoundaryPropsFor<DialogSceneRoot> DialogSceneProps;
class DialogSceneRoot : public loka::app::scene::StdCompositionBoundaryNodeBase<DialogSceneProps>
{
public:
  loka::app::scene::NodeState<bool> shown;
  loka::core::EmitterState completion;
  explicit DialogSceneRoot(const DialogSceneProps &props)
      : loka::app::scene::StdCompositionBoundaryNodeBase<DialogSceneProps>(props)
  { this->state(this->shown, true); }
  virtual void composeNode(loka::app::scene::NodeComposition &composition)
  {
    composition.declare(loka::app::Show(*this->shown.state()).destroyOnDetach()
                        << loka::app::OpenFileDialog().onResult(&this->completion));
  }
};
class DialogSceneWindow : public ToolboxWindow
{
public:
  explicit DialogSceneWindow(PlatformContext *platform)
      : ToolboxWindow(WindowProps().scene(new loka::app::scene::Scene(
            loka::app::scene::Boundary<DialogSceneRoot>(DialogSceneProps()))), platform)
  { this->scene()->mount(&this->controller); }
  virtual ~DialogSceneWindow()
  { this->unmountSceneForTeardown(*this->scene()); }
  virtual bool hasLiveScenePlatform() const { return true; }
  virtual bool mountReplacementScene(loka::app::scene::Scene *next)
  { next->mount(&this->controller); return true; }
  DialogSceneRoot *root()
  { return static_cast<DialogSceneRoot *>(loka::dsl::testing::SceneTestAccess::rootBoundary(*this->scene())); }
};
static loka::app::OpenFileDialogNode *FindDialog(loka::app::scene::Node *node)
{
  if (!node) return 0;
  if (node->asOpenFileDialogNode()) return node->asOpenFileDialogNode();
  loka::app::scene::INestable *nestable = node->asNestable();
  for (loka::app::scene::Node *child = nestable ? nestable->childrenHead() : 0;
       child; child = child->nextInComposition)
  {
    loka::app::OpenFileDialogNode *found = FindDialog(child);
    if (found) return found;
  }
  return 0;
}
static void AttachSceneDialog(void *data)
{
  DialogSceneWindow &window = *static_cast<DialogSceneWindow *>(data);
  loka::app::OpenFileDialogNode *node = FindDialog(window.root());
  LOKA_VERIFY(node != 0);
  loka::app::scene::PlatformNodeHandlerRegistry registry;
  LOKA_VERIFY(RegisterToolboxOpenFileDialogNodeHandler(registry));
  const loka::app::scene::LayoutState layout;
  LOKA_VERIFY(registry.find(node)->ensureContext(node, &window.controller, layout) != 0);
}
static void ScenePin(bool replace)
{
  ToolboxPlatformContext platform;
  DialogSceneWindow window(&platform);
  ToolboxApp app(window);
  app.flush();
  modalController = &window.controller;
  requiredFlushes = 1;
  duringModal = &CheckModal;
  window.controller.onRender = &AttachSceneDialog;
  window.controller.renderData = &window;
  ToolboxInputDoor::render(window.controller);
  window.controller.onRender = 0;
  LOKA_VERIFY(getCalls + putCalls == 0);
  LOKA_VERIFY(!loka::testing::ToolboxDialogTestAccess::empty(window.controller.pendingDialogs()));
  if (replace)
  {
    loka::app::scene::Scene *replacement = new loka::app::scene::Scene(
        loka::app::scene::Boundary<DialogSceneRoot>(DialogSceneProps()));
    LOKA_VERIFY(window.sceneManager()->commitTransaction(window.scene(), replacement));
  }
  else window.root()->shown.set(false);
  app.tick();
  LOKA_VERIFY(getCalls + putCalls == 0);
  LOKA_VERIFY(loka::testing::ToolboxDialogTestAccess::empty(window.controller.pendingDialogs()));
  duringModal = 0;
}

static void PresentPin(const char *name)
{
  using namespace loka::app;
  using namespace loka::app::scene;
  typedef loka::testing::ToolboxDialogTestAccess Access;
  requiredFlushes = 1;
  if (!std::strcmp(name, "show") || !std::strcmp(name, "scene-replace"))
  { ScenePin(!std::strcmp(name, "scene-replace")); return; }
  if (!std::strcmp(name, "list-destroy"))
  {
#ifndef LOKA_LIFECYCLE_AUDIT
    ToolboxDialogEnrollment row(0);
    {
      ToolboxPendingDialogs list;
      list.enroll(row);
      LOKA_VERIFY(!Access::empty(list));
    }
    LOKA_VERIFY(Access::unlinked(row));
    row.unlink(); // Must not touch the former sentinel (also under ASan).
#else
    LOKA_VERIFY(false); // This pin is registered in the no-audit target only.
#endif
    return;
  }
  if (!std::strcmp(name, "snapshot"))
  {
    ToolboxWindow *firstWindow = new ToolboxWindow();
    ToolboxWindow secondWindow;
    ToolboxApp pair(*firstWindow, &secondWindow);
    requiredFlushes = 2;
    DialogPin first(firstWindow->controller);
    DialogPin second(secondWindow.controller, true);
    modalController = &firstWindow->controller;
    modalApp = &pair;
    duringModal = &CheckModal;
    first.draw(); second.draw();
    LOKA_VERIFY(getCalls + putCalls == 0);
    closeOnDelivery = firstWindow;
    first.storage.bind(&CloseWindowOnDelivery, 0, false);
    pair.tick();
    first.storage.unbind(&CloseWindowOnDelivery, 0);
    LOKA_VERIFY(getCalls == 1 && putCalls == 1 && first.emits == 1 && second.emits == 1);
    duringModal = 0;
    return;
  }
  ToolboxWindow window;
  ToolboxApp app(window);
  DialogPin first(window.controller, !std::strcmp(name, "save") || !std::strcmp(name, "props"));
  modalController = &window.controller;
  modalApp = &app;
  duringModal = &CheckModal;
  reply.sfGood = false;
  first.draw(!std::strcmp(name, "update"));
  LOKA_VERIFY(getCalls + putCalls == 0);
  LOKA_VERIFY(!Access::empty(window.controller.pendingDialogs()));
  if (!std::strcmp(name, "detach") || !std::strcmp(name, "destroy"))
  {
    if (!std::strcmp(name, "detach")) first.detach();
    else first.node.setContext(0);
    LOKA_VERIFY(Access::empty(window.controller.pendingDialogs()));
    app.tick();
    LOKA_VERIFY(getCalls + putCalls == 0);
  }
  else if (!std::strcmp(name, "non-toolbox"))
  {
    Window plain(0, WindowProps());
    ToolboxApp pair(plain, &window);
    AppComponent component;
    pair.addComponent(&component);
    pair.tick();
    LOKA_VERIFY(getCalls == 1 && first.emits == 1);
  }
  else if (!std::strcmp(name, "no-group"))
  {
    app.noGroupTick();
    LOKA_VERIFY(getCalls == 0 && !Access::empty(window.controller.pendingDialogs()));
    app.tick();
    LOKA_VERIFY(getCalls == 1);
  }
  else if (!std::strcmp(name, "retire"))
  {
    LifecycleFactTestAccess::MarkSubtreeRetired(&first.node);
    LifecycleFactTestAccess::DeliverFacts(&first.node);
    LOKA_VERIFY(Access::empty(window.controller.pendingDialogs()));
    app.tick();
    LOKA_VERIFY(getCalls == 0);
  }
  else if (!std::strcmp(name, "lifecycle"))
  {
    NotifySubtreeNodeDetached(&first.node); // The apply publication is still pending.
    app.tick();
    LOKA_VERIFY(getCalls == 0 && !Access::empty(window.controller.pendingDialogs()));
    LifecycleFactTestAccess::DeliverFacts(&first.node);
    LOKA_VERIFY(Access::empty(window.controller.pendingDialogs()));
  }
  else if (!std::strcmp(name, "background") || !std::strcmp(name, "quit")
           || !std::strcmp(name, "borrow") || !std::strcmp(name, "controller"))
  {
    if (!std::strcmp(name, "quit")) app.running_ = false;
    if (!std::strcmp(name, "controller")) window.hasController = false;
    if (!std::strcmp(name, "borrow"))
    {
      BorrowScope scope(window.controller);
      app.tick();
    }
    else app.tick(!std::strcmp(name, "background") ? ACTIVATION_BACKGROUND : ACTIVATION_FOREGROUND);
    LOKA_VERIFY(getCalls + putCalls == 0 && !Access::empty(window.controller.pendingDialogs()));
    if (!std::strcmp(name, "background"))
    {
      // #503: window presentation runs even while the pending dialog waits.
      LOKA_VERIFY(flushedWindows == 1);
    }
    app.running_ = true;
    window.hasController = true;
    app.tick();
    LOKA_VERIFY(getCalls == 1 && first.writes == 1 && first.emits == 1);
  }
  else if (!std::strcmp(name, "fifo"))
  {
    DialogPin second(window.controller, true);
    second.draw();
    LOKA_VERIFY(getCalls + putCalls == 0);
    app.tick();
    LOKA_VERIFY(getCalls == 1 && putCalls == 0 && first.emits == 1 && second.emits == 0);
    app.tick();
    LOKA_VERIFY(getCalls == 1 && putCalls == 1 && second.emits == 1);
    LOKA_VERIFY(Access::empty(window.controller.pendingDialogs()));
  }
  else if (!std::strcmp(name, "two-windows") || !std::strcmp(name, "close")
           || !std::strcmp(name, "quit-delivery"))
  {
    // The App owns a close-queued window until its end-of-present reclamation.
    ToolboxWindow *secondWindow = new ToolboxWindow();
    ToolboxApp pair(window, secondWindow);
    requiredFlushes = 2;
    {
    DialogPin second(secondWindow->controller, true);
    second.draw();
    if (!std::strcmp(name, "close"))
    {
      // First delivery closes a later snapshot row before it can present.
      modalApp = &pair;
      closeOnDelivery = secondWindow;
      first.storage.bind(&CloseWindowOnDelivery, 0, false);
      loka::core::Operation turn;
      pair.present(ACTIVATION_FOREGROUND, turn);
      first.storage.unbind(&CloseWindowOnDelivery, 0);
      LOKA_VERIFY(getCalls == 1 && putCalls == 0);
    }
    else if (!std::strcmp(name, "quit-delivery"))
    {
      // A delivery that quits stops the pass; the later row stays enrolled.
      modalApp = &pair;
      first.storage.bind(&QuitOnDelivery, 0, false);
      loka::core::Operation turn;
      pair.present(ACTIVATION_FOREGROUND, turn);
      first.storage.unbind(&QuitOnDelivery, 0);
      LOKA_VERIFY(getCalls == 1 && putCalls == 0 && second.emits == 0);
      LOKA_VERIFY(!Access::empty(secondWindow->controller.pendingDialogs()));
    }
    else
    {
      pair.tick();
      LOKA_VERIFY(getCalls == 1 && putCalls == 1 && first.emits == 1 && second.emits == 1);
      LOKA_VERIFY(pair.cursorOwner_.reconciles == 2);
    }
    }
    // Close-queued windows remain App-owned until its next captured tail.
    if (std::strcmp(name, "close")) delete secondWindow;
  }
  else
  {
    if (!std::strcmp(name, "modal-destroy")) destroyInModal = &first.node;
    if (!std::strcmp(name, "props"))
    {
      first.node.props.options_ = FileDialogOptions(FILE_DIALOG_SAVE, String::Literal("newest"));
      for (unsigned i = 0; i != 3; ++i)
      { first.context()->onPropsApplied(); first.draw(); }
    }
    app.tick();
    LOKA_VERIFY(getCalls + putCalls == 1 && first.writes == 1 && first.emits == 1);
    LOKA_VERIFY(app.cursorOwner_.reconciles == 1 && modalChecks == 1);
    LOKA_VERIFY(first.storage.get().kind == FileChooserResult::RESULT_CANCELED);
    LOKA_VERIFY(Access::empty(window.controller.pendingDialogs()));
    if (!std::strcmp(name, "props")) LOKA_VERIFY(savedDefault == "newest");
    if (!std::strcmp(name, "save"))
    {
      LOKA_VERIFY(putCalls == 1 && getCalls == 0);
      first.detach();
      first.node.props.options_ = FileDialogOptions(FILE_DIALOG_SAVE, String(std::string(32, 'x').c_str()));
      first.context()->onPropsApplied();
      first.reattach();
      app.tick();
      LOKA_VERIFY(putCalls == 1 && first.writes == 2 && first.emits == 2);
      LOKA_VERIFY(first.storage.get().kind == FileChooserResult::RESULT_ERROR);
      LOKA_VERIFY(first.storage.get().errorCode == paramErr);
    }
    if (!std::strcmp(name, "reattach"))
    {
      first.detach(); first.reattach(); first.draw();
      app.tick();
      LOKA_VERIFY(getCalls == 2 && first.emits == 2);
    }
    if (!std::strcmp(name, "phase")) first.draw();
    app.tick();
    LOKA_VERIFY(Access::empty(window.controller.pendingDialogs()));
    if (!std::strcmp(name, "phase")) LOKA_VERIFY(getCalls == 1 && first.emits == 1);
  }
  duringModal = 0;
}
