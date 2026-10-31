#include "support/StandaloneMountTestSupport.hpp"
#include "testing/scene/SceneTestFlow.hpp"
#include "app/nodes/boundary/StdComposition.hpp"
#include "ToolboxTextensionFake.hpp"
#include "ToolboxInputDoor.hpp"
#include "ToolboxBuiltInSupport.hpp"
#include "ToolboxIterationHost.hpp"
#include "support/TextBufferFixture.hpp"
#include "support/TextEditorStateOwner.hpp"
#include "support/TestVerify.hpp"
#include "support/LokaAllocFailure.hpp"
#include "support/LifecycleFactTestAccess.hpp"
#include "support/WindowAdmissionTestApp.hpp"
#include "ToolboxPresentHost.hpp"
#include "ToolboxDialogPresenter.hpp"
#include "app/EditMenuDispatch.hpp"
#include <algorithm>
#include <cstring>
using namespace loka::app;
using namespace loka::app::scene;
using namespace loka::core;
struct UnicodeWindow : ToolboxWindow
{
  bool answersPort;
  UnicodeWindow()
      : answersPort(true)
  {
  }
  virtual ToolboxWindow *asToolboxWindow()
  {
    return this;
  }
  virtual bool queryPortRect(Rect &out) const
  {
    return this->answersPort && ToolboxWindow::queryPortRect(out);
  }
};
struct Fixture : HeadlessStateOwner
{
  Reported<LineCursor> cursor;
  RequestWithReply<LineCursor> request;
  loka::testing::TextBufferFixture lines;
  UnicodeWindow window;
  ToolboxScenePlatformController controller;
  TextEditorNode node;
  ToolboxUnicodeTextEditorContext *context;
  GrafPtr previousPort;
  Fixture()
      : controller(&window),
        node(TextEditorProps(lines, cursor)),
        context(0)
  {
    GetPort(&previousPort);
    SetPort(window.window());
    window.scenePlatformController_ = &controller;
    StateBatchBase::CreateImmediateState(this, cursor, LineCursor::None());
    StateBatchBase::CreateImmediateState(this, request, LineCursor::None());
    node.props = TextEditorProps(lines, cursor).moveCaretTo(request);
    LOKA_VERIFY(lines.attach(this->tracker()->asPushTracker(), 256, 8192) == ATTACH_OK);
    LOKA_VERIFY(lines.insert(0, String("test")) == EDIT_OK);
    NotifySubtreeNodeAttached(&node);
    LOKA_VERIFY(RegisterToolboxBuiltInSupport(controller));
    LayoutState state;
    state.x = 10;
    state.y = 20;
    state.width = 200;
    state.height = 80;
    IPlatformNodeHandler *handler = controller.nodeHandlerRegistry_.find(&node);
    LOKA_VERIFY(handler);
    context = static_cast<ToolboxUnicodeTextEditorContext *>(handler->ensureContext(&node, &controller, state));
    LOKA_VERIFY(context);
    context->layout(&controller, state);
  }
  void layout(short x = 10, short y = 20, short width = 200, short height = 80)
  {
    LayoutState state;
    state.x = x;
    state.y = y;
    state.width = width;
    state.height = height;
    context->layout(&controller, state);
  }
  void render()
  {
    SetPort(window.window());
    controller.rootNode_ = &node;
    controller.renderContext = context;
    ToolboxInputDoor::render(controller);
  }
  void focus()
  {
    controller.activateEditControl(0);
  }
  ~Fixture()
  {
    context->onFactChanged(NODE_FACT_ATTACHED, NODE_FACT_RETIRED);
    controller.flushRetiredNativeHandles();
    SetPort(previousPort);
  }
};

static unsigned count(const char *name)
{
  return std::count(TextensionFake::calls().begin(), TextensionFake::calls().end(), std::string(name));
}
static void allocation()
{
  using namespace loka::core::testing;
  TextensionFake::reset();
  {
    Fixture f;
    failLokaAllocRaw("ToolboxEditControlLedger", "Rows", 1);
    f.render();
    LOKA_VERIFY(!f.context->hasNativeHandle() && f.controller.editControls_.size() == 0);
    LOKA_VERIFY(TextensionFake::calls().empty());
    allowLokaAllocRaw();
    failLokaAllocRaw("ToolboxEditControlLedger", "Rows", 0);
    TextensionFake::failNext("new", memFullErr);
    f.render();
    LOKA_VERIFY(!f.context->hasNativeHandle() && f.controller.editControls_.size() == 0);
    LOKA_VERIFY(count("new") == 1 && count("delete") == 0 && TextensionFake::live() == 0);
    LOKA_VERIFY(lokaAllocRawLive() == 0); // cancel releases prepared row storage.
    allowLokaAllocRaw();
    f.render();
    LOKA_VERIFY(f.context->hasNativeHandle() && f.controller.editControls_.size() == 1);
  }
  LOKA_VERIFY(TextensionFake::live() == 0);
}
static void placement()
{
  for (unsigned kind = 0; kind != 4; ++kind)
    for (unsigned reuse = 0; reuse != 2; ++reuse)
    {
      TextensionFake::reset();
      Fixture f;
      if (reuse)
      {
        f.render();
        f.focus();
      }
      TextensionFake::clearCalls();
      if (kind == 0)
        f.window.answersPort = false;
      if (kind == 1)
        f.layout(500, 500);
      if (kind == 2)
        SetRect(&f.controller.projectionClip, 10, 20, 100, 100);
      if (kind == 3)
        SetRect(&f.controller.projectionClip, 500, 500, 600, 600);
      if (kind >= 2)
        f.layout();
      toolbox_host::draws.clear();
      f.context->render(&f.controller);
      if (kind == 3)
        LOKA_VERIFY(toolbox_host::draws.empty());
      LOKA_VERIFY(!f.context->hasNativeHandle());
      LOKA_VERIFY(count("new") == 0 && count("draw") == 0);
      LOKA_VERIFY(count("focus-off") == reuse); // B7 precedes the render-tail retirement.
      f.render();
      f.controller.flushRetiredNativeHandles();
      LOKA_VERIFY(f.controller.editControls_.size() == 0 && count("delete") == reuse);
      f.window.answersPort = true;
      SetRect(&f.controller.projectionClip, -30000, -30000, 30000, 30000);
      f.layout();
      f.render();
      LOKA_VERIFY(f.context->hasNativeHandle() && count("new") == 1);
    }
  TextensionFake::reset();
  {
    Fixture f;
    f.layout(0, 0, 200, 80);
    f.render();
    LOKA_VERIFY(f.context->hasNativeHandle()); // window-edge frame extends one pixel past the port
    TextensionFake::clearCalls();
    f.render();
    LOKA_VERIFY(count("bounds") == 0 && count("new") == 0);
    f.layout(0, 0, 210, 80);
    f.render();
    LOKA_VERIFY(count("bounds") == 1 && count("new") == 0);
    f.render();
    LOKA_VERIFY(count("bounds") == 1);
  }
}
static void sizeBox()
{
  TextensionFake::reset();
  Fixture f;
  f.layout(300, 230, 60, 30);
  f.render();
  LOKA_VERIFY(f.context->hasNativeHandle());
  LOKA_VERIFY(TextensionFake::creations().back().frame.bottom == 246);
  f.layout(300, 242, 60, 18);
  f.render();
  LOKA_VERIFY(!f.context->hasNativeHandle());
}
static void emitted(void *data)
{
  ++*static_cast<unsigned *>(data);
}
static void input()
{
  TextensionFake::reset();
  Fixture f;
  f.render();
  f.focus();
  loka::app::testing::SettleTrace<LineCursor> &settles = loka::app::testing::SettleTrace<LineCursor>::instance();
  EmitterState binding;
  unsigned emits = 0;
  binding.bind(emitted, &emits, false);
  EventRecord event = {3, 'a', 0, {30, 30}, 0};
  for (unsigned action = 0; action != 10; ++action)
  {
    {
      StateTrackerGuard guard(f.tracker());
      f.request.set(LineCursor(f.lines.at(0).id, 0));
    }
    settles.clear();
    TextensionFake::clearCalls();
    switch (action)
    {
    case 0:
      LOKA_VERIFY(ToolboxInputDoor::offerKeyEvent(f.controller, event));
      break;
    case 1:
      LOKA_VERIFY(f.context->click(event.where, event) == EDITOR_UNAVAILABLE);
      break;
    case 2:
      LOKA_VERIFY(f.context->key('x') == EDITOR_UNAVAILABLE);
      break;
    case 3:
      LOKA_VERIFY(f.context->paste("x", 1) == EDITOR_UNAVAILABLE);
      break;
    case 4:
      LOKA_VERIFY(f.context->scrollKey(SCROLL_PAGE_DOWN) == EDITOR_UNAVAILABLE);
      break;
    case 5:
    case 6:
    case 7:
    case 8:
    case 9:
      LOKA_VERIFY(f.window.handleEditCommand(static_cast<EditVerb>(action - 5)));
      break;
    }
    LOKA_VERIFY(settles.size() == 1 && settles.at(0).count == 1 && settles.at(0).stimulus == SETTLE_INPUT);
    LOKA_VERIFY(settles.at(0).takes[0].reason() == EDITOR_UNAVAILABLE);
    LOKA_VERIFY(count("key") == 0 && count("click") == 0);
  }
  dispatchEditMenuCommand(&f.window, EDIT_COPY, &binding);
  LOKA_VERIFY(emits == 0);
  event.message = 9;
  settles.clear();
  LOKA_VERIFY(!ToolboxInputDoor::offerKeyEvent(f.controller, event) && settles.size() == 0);
  event.message = 'a';
  toolbox_host::frontWindow = 0;
  settles.clear();
  LOKA_VERIFY(!ToolboxInputDoor::offerKeyEvent(f.controller, event));
  LOKA_VERIFY(settles.size() == 0);
  f.controller.editControls_.clearFocus();
  LOKA_VERIFY(!f.window.handleEditCommand(EDIT_COPY));
  dispatchEditMenuCommand(&f.window, EDIT_COPY, &binding);
  LOKA_VERIFY(emits == 1);
  dispatchEditMenuCommand(0, EDIT_COPY, &binding);
  LOKA_VERIFY(emits == 2);
  binding.unbind(emitted, &emits);
}
static void park()
{
  TextensionFake::reset();
  Fixture f;
  f.render();
  f.focus();
  NotifySubtreeNodeDetached(&f.node);
  TextensionFake::clearCalls();
  LOKA_VERIFY(!ToolboxInputDoor::syncNativeFocus(f.controller, NATIVE_FOCUS_NORMAL));
  LOKA_VERIFY(count("focus-off") == 1);
  TextensionFake::clearCalls();
  LifecycleFactTestAccess::DeliverFacts(&f.node);
  LOKA_VERIFY(count("focus-off") == 2); // B5 then B6; repeated false is natively idempotent.
  LOKA_VERIFY(count("delete") == 0 && !f.context->hasNativeHandle());
  f.controller.flushRetiredNativeHandles();
  LOKA_VERIFY(count("delete") == 1 && TextensionFake::calls().back() == "delete");
}
namespace it = iteration_fixture;
static bool sync(void *data, NativeFocusMode mode)
{
  return ToolboxInputDoor::syncNativeFocus(*static_cast<ToolboxScenePlatformController *>(data), mode);
}
static void idle(void *data)
{
  ToolboxInputDoor::idleTextEdits(*static_cast<ToolboxScenePlatformController *>(data));
}
static bool traceHas(const char *name)
{
  return std::find(it::iteration_host::trace.begin(), it::iteration_host::trace.end(), name)
         != it::iteration_host::trace.end();
}
static void loop()
{
  TextensionFake::reset();
  it::ToolboxWindow window;
  it::AppComponentGroup group;
  group.components.push_back(&window);
  it::ToolboxApp app(group);
  app.active_ = &window;
  it::iteration_host::front = it::iteration_host::target = window.window();
  ToolboxIdleClock clock(0);
  const unsigned short kinds[] = {it::keyDown, it::mouseDown, it::updateEvt, it::activateEvt, it::osEvt};
  for (unsigned rewrite = 0; rewrite != 2; ++rewrite)
    for (unsigned i = 0; i != 5; ++i)
    {
      EventRecord event = {
          kinds[i], i == 4 ? 0x01000000UL : reinterpret_cast<unsigned long>(window.window()), 0, {0, 0}, 1};
      TextensionFake::clearCalls();
      TextensionFake::service(true, rewrite != 0);
      it::iteration_host::trace.clear();
      app.iterate(clock, true, event);
      LOKA_VERIFY(count("tsm") == 1);
      LOKA_VERIFY(!traceHas("offer-key") && !traceHas("find-window") && !traceHas("draw-update"));
      LOKA_VERIFY(!traceHas("diagnostic-tail"));
      if (i == 3)
        LOKA_VERIFY(traceHas("set-active"));
      if (i == 4)
        LOKA_VERIFY(count("scrap-to") == 1 && traceHas("hilite-off"));
    }
  EventRecord event = {it::osEvt, 0x01000001UL, 0, {0, 0}, 0};
  TextensionFake::clearCalls();
  app.iterate(clock, true, event);
  LOKA_VERIFY(count("scrap-from") == 1);
  event.what = it::nullEvent;
  TextensionFake::clearCalls();
  app.iterate(clock, false, event);
  LOKA_VERIFY(count("tsm") == 1);
  TextensionFake::service(false, false);
}
static GrafPtr selectedNative;
static void selected(it::WindowPtr)
{
  toolbox_host::frontWindow = selectedNative;
}
static void boundaries()
{
  TextensionFake::reset();
  Fixture x, y;
  x.render();
  x.focus();
  y.render();
  y.focus();
  it::ToolboxWindow wx, wy;
  wx.scenePlatformController()->data = &x.controller;
  wx.scenePlatformController()->sync = sync;
  wx.scenePlatformController()->idle = idle;
  wy.scenePlatformController()->data = &y.controller;
  wy.scenePlatformController()->sync = sync;
  wy.scenePlatformController()->idle = idle;
  it::AppComponentGroup group;
  group.components.push_back(&wx);
  group.components.push_back(&wy);
  it::ToolboxApp app(group);
  app.active_ = &wx;
  it::iteration_host::front = wy.window();
  ToolboxIdleClock clock(0);
  EventRecord event = {it::keyDown, 'a', 0, {0, 0}, 0};
  TextensionFake::clearCalls();
  loka::app::testing::SettleTrace<LineCursor>::instance().clear();
  app.iterate(clock, true, event);
  const std::vector<TextensionFake::Call> &calls = TextensionFake::events();
  LOKA_VERIFY(calls.size() >= 6);
  LOKA_VERIFY(calls[0].name == "focus-off" && calls[1].name == "focus-on");
  LOKA_VERIFY(calls[2].name == "focus-off" && calls[2].object == calls[0].object);
  LOKA_VERIFY(calls[3].name == "idle" && calls[3].object == calls[0].object);
  LOKA_VERIFY(calls[4].name == "focus-on" && calls[4].object == calls[1].object);
  LOKA_VERIFY(calls[5].name == "idle" && calls[5].object == calls[1].object);
  LOKA_VERIFY(count("tsm") == 0 && count("draw") == 0 && count("new") == 0 && count("bounds") == 0);
  LOKA_VERIFY(loka::app::testing::SettleTrace<LineCursor>::instance().size() == 0);
  event.what = it::nullEvent;
  TextensionFake::clearCalls();
  app.iterate(clock, false, event);
  LOKA_VERIFY(count("tsm") == 1);
  // SelectWindow changes native focus before App receives either activate record.
  toolbox_host::frontWindow = x.window.window();
  selectedNative = y.window.window();
  it::iteration_host::front = wx.window();
  it::iteration_host::target = wy.window();
  it::iteration_host::onSelect = selected;
  event.what = it::mouseDown;
  app.iterate(clock, true, event);
  LOKA_VERIFY(traceHas("select-window") && toolbox_host::frontWindow == y.window.window());
  event.what = it::keyDown;
  TextensionFake::clearCalls();
  app.iterate(clock, true, event);
  LOKA_VERIFY(calls[0].name == "focus-off" && calls[1].name == "focus-on");
  it::iteration_host::onSelect = 0;
  // Native selection precedes the activate records; App's cached active window remains X.
  toolbox_host::frontWindow = x.window.window();
  event.what = it::activateEvt;
  event.message = reinterpret_cast<unsigned long>(wy.window());
  event.modifiers = 0;
  TextensionFake::clearCalls();
  app.iterate(clock, true, event);
  LOKA_VERIFY(calls[0].name == "focus-on" && calls[1].name == "focus-off");
  event.message = reinterpret_cast<unsigned long>(wx.window());
  event.modifiers = 1;
  TextensionFake::clearCalls();
  app.iterate(clock, true, event);
  LOKA_VERIFY(calls[0].name == "focus-on" && calls[1].name == "focus-off");
}
struct Dialog : ToolboxDialogPresenter
{
  ToolboxDialogEnrollment enrollment;
  explicit Dialog(Node *node)
      : ToolboxDialogPresenter(node),
        enrollment(this)
  {
  }
  virtual void present(ToolboxApp &)
  {
    LOKA_VERIFY(count("focus-on") == 0 && count("focus-off") == 2 && count("scrap-to") == 1);
    LOKA_VERIFY(TextensionFake::calls().back() == "scrap-to");
  }
};
static void modal()
{
  TextensionFake::reset();
  Fixture x, y;
  x.render();
  x.focus();
  y.render();
  y.focus();
  Dialog dialog(&x.node);
  x.controller.pendingDialogs().enroll(dialog.enrollment);
  ToolboxApp app(x.window, &y.window);
  TextensionFake::clearCalls();
  {
    Operation operation;
    app.present(ACTIVATION_FOREGROUND, operation);
  }
  LOKA_VERIFY(count("scrap-from") == 1);
  TextensionFake::clearCalls();
  LOKA_VERIFY(app.syncNativeFocusAll(NATIVE_FOCUS_NORMAL));
  LOKA_VERIFY(count("focus-on") == 1 && count("focus-off") == 1);
}
static void drawing()
{
  TextensionFake::reset();
  Fixture f;
  f.render();
  TextensionFake::clearCalls();
  f.window.drawForUpdate();
  LOKA_VERIFY(TextensionFake::draws().size() == 1 && TextensionFake::draws()[0].updateDepth == 1);
  TextensionFake::clearCalls();
  f.window.drawForFlush();
  LOKA_VERIFY(TextensionFake::draws().size() == 1 && TextensionFake::draws()[0].updateDepth == 0);
  TextensionFake::clearCalls();
  f.window.drawDirty(f.context->chromeRect());
  LOKA_VERIFY(TextensionFake::draws().size() == 1 && TextensionFake::draws()[0].updateDepth == 0);
  f.window.answersPort = false;
  TextensionFake::clearCalls();
  toolbox_host::draws.clear();
  f.render();
  LOKA_VERIFY(count("draw") == 0 && !toolbox_host::draws.empty());
  LOKA_VERIFY(toolbox_host::draws.back().bytes == "Editor unavailable");
}
static void focusTransitions()
{
  TextensionFake::reset();
  Fixture f;
  f.render();
  f.focus();
  TextEditorNode second(TextEditorProps(f.lines, f.cursor));
  NotifySubtreeNodeAttached(&second);
  LayoutState state;
  state.x = 10;
  state.y = 120;
  state.width = 200;
  state.height = 80;
  IPlatformNodeHandler *handler = f.controller.nodeHandlerRegistry_.find(&second);
  ToolboxUnicodeTextEditorContext *context =
      static_cast<ToolboxUnicodeTextEditorContext *>(handler->ensureContext(&second, &f.controller, state));
  context->layout(&f.controller, state);
  context->render(&f.controller);
  TextensionFake::clearCalls();
  f.controller.activateEditControl(1);
  LOKA_VERIFY(TextensionFake::events().size() == 2);
  LOKA_VERIFY(TextensionFake::events()[0].name == "focus-off" && TextensionFake::events()[1].name == "focus-on");
  LOKA_VERIFY(TextensionFake::events()[0].object != TextensionFake::events()[1].object);
  EventRecord event = {1, 0, 0, {30, 30}, 0};
  {
    StateTrackerGuard guard(f.tracker());
    f.request.set(LineCursor(f.lines.at(0).id, 0));
  }
  loka::app::testing::SettleTrace<LineCursor>::instance().clear();
  TextensionFake::clearCalls();
  LOKA_VERIFY(ToolboxInputDoor::mouseDown(f.controller, event.where, event));
  LOKA_VERIFY(loka::app::testing::SettleTrace<LineCursor>::instance().size() == 1 && count("click") == 0);
  toolbox_host::frontWindow = 0;
  loka::app::testing::SettleTrace<LineCursor>::instance().clear();
  LOKA_VERIFY(ToolboxInputDoor::mouseDown(f.controller, event.where, event));
  LOKA_VERIFY(loka::app::testing::SettleTrace<LineCursor>::instance().size() == 0 && count("click") == 0);
  context->onFactChanged(NODE_FACT_ATTACHED, NODE_FACT_RETIRED);
  f.controller.flushRetiredNativeHandles();
}
/** ScrollView's projection scope, with the child translated by a scripted offset. */
struct ScrolledEditor : NativeNodeContext
{
  Fixture &fixture;
  short y;
  explicit ScrolledEditor(Fixture &f)
      : fixture(f),
        y(30)
  {
  }
  virtual void render(IPlatformController *)
  {
    ProjectionParentScope scope(fixture.window.window(), 0, 0, Frame(10, 20, 220, 100));
    ProjectionViewportScopeGuard guard(fixture.controller.projectionParentScopes_, scope);
    LOKA_VERIFY(guard.isActive());
    fixture.layout(20, y, 180, 60);
    fixture.context->render(&fixture.controller);
  }
};
static void scrolling()
{
  TextensionFake::reset();
  Fixture f;
  ScrolledEditor child(f);
  f.controller.rootNode_ = &f.node;
  f.controller.renderContext = &child;
  ToolboxInputDoor::render(f.controller);
  LOKA_VERIFY(f.context->hasNativeHandle());
  f.focus();
  TextensionFake::clearCalls();
  child.y = 160;
  ToolboxInputDoor::render(f.controller);
  f.controller.flushRetiredNativeHandles();
  LOKA_VERIFY(!f.context->hasNativeHandle() && count("delete") == 1 && count("draw") == 0);
  child.y = 30;
  ToolboxInputDoor::render(f.controller);
  LOKA_VERIFY(f.context->hasNativeHandle() && count("new") == 1 && TextensionFake::creations().size() == 2);
}
namespace
{
  class FocusRoot : public BoundaryNodeFor<FocusRoot>
  {
  public:
    explicit FocusRoot(const BoundaryPropsFor<FocusRoot> &props)
        : BoundaryNodeFor<FocusRoot>(props)
    {
    }
    virtual void composeNode(NodeComposition &) {}
  };
  class FocusWindow : public Window
  {
    ToolboxWindow *native_;

  public:
    FocusWindow(PlatformContext *platform, ToolboxScenePlatformController &controller)
        : Window(platform, WindowProps().scene(new Scene(Boundary<FocusRoot>()))),
          native_(controller.window_)
    {
      this->scene()->mount(&controller);
    }
    virtual ~FocusWindow()
    {
      this->unmountSceneForTeardown(*this->scene());
    }
    virtual bool hasLiveScenePlatform() const
    {
      return true;
    }
    virtual ToolboxWindow *asToolboxWindow()
    {
      return native_;
    }
  };
  struct FocusFixture : HeadlessStateOwner
  {
    loka::app::Focus<unsigned> focus;
    Reported<LineCursor> cursor;
    loka::testing::TextBufferFixture lines;
    ToolboxWindow native;
    ToolboxScenePlatformController controller;
    loka::testing::StandaloneMountTestPlatformContext platform;
    FocusWindow window;
    TextEditorNode node;
    ToolboxUnicodeTextEditorContext *context;
    GrafPtr previous;
    FocusFixture()
        : controller(&native),
          window(&platform, controller),
          node(TextEditorProps(lines, cursor)),
          context(0)
    {
      GetPort(&previous);
      SetPort(native.window());
      native.scenePlatformController_ = &controller;
      BoundaryNode *root = loka::dsl::testing::SceneTestAccess::rootBoundary(*window.scene());
      StateBatchBase::CreateImmediateState(root, focus);
      StateBatchBase::CreateImmediateState(this, cursor, LineCursor::None());
      LOKA_VERIFY(lines.attach(this->tracker()->asPushTracker(), 256, 8192) == ATTACH_OK);
      node.props = TextEditorProps(lines, cursor).focusedAs(focus, 1u);
      ComponentContext component;
      component.setBoundary(root);
      component.setStateOwner(root);
      component.setScene(window.scene());
      BoundaryNode::composeSubtree(&node, component, COMPOSE_EVENT_ATTACH, root);
      LOKA_VERIFY(RegisterToolboxBuiltInSupport(controller));
      LayoutState state;
      state.x = 10;
      state.y = 20;
      state.width = 200;
      state.height = 80;
      context = static_cast<ToolboxUnicodeTextEditorContext *>(
          controller.nodeHandlerRegistry_.find(&node)->ensureContext(&node, &controller, state));
      context->layout(&controller, state);
      context->render(&controller);
      controller.activateEditControl(0);
    }
    ~FocusFixture()
    {
      context->onFactChanged(NODE_FACT_ATTACHED, NODE_FACT_RETIRED);
      controller.flushRetiredNativeHandles();
      SetPort(previous);
    }
  };
  void focusSelectsWindow(void *data)
  {
    toolbox_host::frontWindow = static_cast<GrafPtr>(data);
  }
} // namespace
static void reconcile()
{
  TextensionFake::reset();
  FocusFixture x, y;
  ToolboxApp app(x.window, &y.window);
  toolbox_host::frontWindow = x.native.window();
  x.focus.state()->bind(focusSelectsWindow, y.native.window(), false);
  app.reconcileFocus();
  LOKA_VERIFY(x.focus.state()->get().is(1u) && toolbox_host::frontWindow == y.native.window());
  x.focus.state()->unbind(focusSelectsWindow, y.native.window());
  it::ToolboxWindow wx, wy;
  wx.scenePlatformController()->data = &x.controller;
  wx.scenePlatformController()->sync = sync;
  wx.scenePlatformController()->idle = idle;
  wy.scenePlatformController()->data = &y.controller;
  wy.scenePlatformController()->sync = sync;
  wy.scenePlatformController()->idle = idle;
  it::AppComponentGroup group;
  group.components.push_back(&wx);
  group.components.push_back(&wy);
  it::ToolboxApp iteration(group);
  iteration.active_ = &wx;
  ToolboxIdleClock clock(0);
  EventRecord event = {it::nullEvent, 0, 0, {0, 0}, 0};
  TextensionFake::clearCalls();
  iteration.iterate(clock, false, event);
  LOKA_VERIFY(TextensionFake::events()[0].name == "focus-off" && TextensionFake::events()[1].name == "focus-on");
}
int main(int argc, char **argv)
{
  const char *which = argc > 1 ? argv[1] : "all";
#define RUN(name)                                                                                                      \
  if (!std::strcmp(which, "all") || !std::strcmp(which, #name))                                                        \
  name()
  RUN(allocation);
  RUN(placement);
  RUN(input);
  RUN(park);
  RUN(loop);
  RUN(boundaries);
  RUN(modal);
  RUN(drawing);
  RUN(focusTransitions);
  RUN(scrolling);
  RUN(sizeBox);
  RUN(reconcile);
#undef RUN
  LOKA_VERIFY(TextensionFake::live() == 0);
}
void ExitToShell()
{
  LOKA_VERIFY(false);
}
OSErr Gestalt(OSType, long *response)
{
  *response = 0x0900;
  return noErr;
}
namespace loka
{
  namespace toolbox
  {
    void RunEmergencyQuitDialog(const char *, std::size_t)
    {
      LOKA_VERIFY(false);
    }
  } // namespace toolbox
} // namespace loka
