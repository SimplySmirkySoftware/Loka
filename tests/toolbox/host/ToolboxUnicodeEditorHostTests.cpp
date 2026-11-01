#include "support/StandaloneMountTestSupport.hpp"
#include "testing/scene/SceneTestFlow.hpp"
#include "app/nodes/boundary/StdComposition.hpp"
#include "ToolboxTextensionFake.hpp"
#include "ToolboxUnicodeStartup.hpp"
#include "ToolboxTextCapacity.hpp"
#include "app/scene/projection/RetainedNodeHandler.hpp"
#include <cstdio>
#include <unistd.h>
#include <Sound.h>
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
  RequestQueue<EditorCommand, 4> commands;
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
    StateBatchBase::CreateImmediateState(this, commands, EditorCommand::None());
    node.props = TextEditorProps(lines, cursor).moveCaretTo(request).command(commands);
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
    LOKA_VERIFY(f.context->hasNativeHandle()); // window-edge seat: the frame stays inside the port
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
/** A seat on a port edge yields a frame one pixel inside that edge; an interior
    seat keeps the TE geometry. Textension shows no caret and no active bar for
    a frame that spans the port's width (Mac OS 9.2). */
static Rect lastBounds()
{
  const std::vector<TextensionFake::Call> &events = TextensionFake::events();
  for (std::size_t i = events.size(); i != 0; --i)
    if (events[i - 1].name == "bounds")
      return events[i - 1].frame;
  LOKA_VERIFY(!"a bounds call was recorded");
  return Rect();
}
static void edgeInset()
{
  TextensionFake::reset();
  Fixture f;
  f.layout(0, 0, 360, 80); // the fixture port is 360 x 260
  f.render();
  LOKA_VERIFY(f.context->hasNativeHandle());
  const Rect edge = TextensionFake::creations().back().frame;
  LOKA_VERIFY(edge.top == 1 && edge.left == 1 && edge.bottom == 80 && edge.right == 359);
  f.layout(10, 20, 200, 80);
  f.render();
  const Rect interior = lastBounds();
  LOKA_VERIFY(interior.top == 20 && interior.left == 10 && interior.bottom == 100 && interior.right == 210);
  f.layout(0, 180, 360, 80); // bottom edge, right edge, size box
  f.render();
  const Rect bottom = lastBounds();
  LOKA_VERIFY(bottom.top == 180 && bottom.left == 1 && bottom.right == 359 && bottom.bottom == 246);
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
      LOKA_VERIFY(f.context->click(event.where, event) == EDITOR_OK);
      break;
    case 2:
      LOKA_VERIFY(f.context->key('x') == EDITOR_UNAVAILABLE);
      break;
    case 3:
      LOKA_VERIFY(f.context->paste("x", 1) == EDITOR_UNAVAILABLE);
      break;
    case 4:
      LOKA_VERIFY(f.context->scrollKey(SCROLL_PAGE_DOWN) == EDITOR_OK);
      break;
    case 5:
    case 6:
    case 7:
    case 8:
    case 9:
      LOKA_VERIFY(f.window.handleEditCommand(static_cast<EditVerb>(action - 5)));
      break;
    }
    if (action == 4) // Unicode scroll keys are consumed by offerKeyEvent first.
      continue;
    LOKA_VERIFY(settles.size() == 1 && settles.at(0).count == 1 && settles.at(0).stimulus == SETTLE_INPUT);
    LOKA_VERIFY(settles.at(0).takes[0].kind() != Reply<LineCursor>::REFUSED);
    LOKA_VERIFY(count("key") == (action == 0 ? 1 : 0) && count("click") == (action == 1 ? 1 : 0));
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
#if defined(LOKA_DIAG) || defined(LOKA_RETRO68_DIAGNOSTICS)
  const unsigned sampleCalls = 1;
#else
  const unsigned sampleCalls = 0;
#endif
  LOKA_VERIFY(calls[4 + sampleCalls].name == "focus-on" && calls[4 + sampleCalls].object == calls[1].object);
  LOKA_VERIFY(calls[5 + sampleCalls].name == "idle" && calls[5 + sampleCalls].object == calls[1].object);
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
  LOKA_VERIFY(loka::app::testing::SettleTrace<LineCursor>::instance().size() == 1 && count("click") == 1);
  toolbox_host::frontWindow = 0;
  TextensionFake::clearCalls();
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
static std::vector<unsigned short> nativeText(const char *text)
{
  return std::vector<unsigned short>(text, text + std::strlen(text));
}
static std::string logicalText(const Fixture &f)
{
  std::string result;
  for (unsigned short i = 0; i < f.lines.size(); ++i)
  {
    if (i)
      result += '\r';
    const StringBuffer bytes = f.lines.at(i).value.bufferWithEncoding(StringEncodingUtf8);
    result.append(static_cast<const char *>(bytes.data()), bytes.length());
  }
  return result;
}
static void keyInput(Fixture &f, const std::vector<unsigned short> &units)
{
  TextensionFake::nextKey(units);
  EventRecord record = {3, 'x', 0, {30, 30}, 0};
  LOKA_VERIFY(f.context->offerKeyEvent(record));
}
static void testUnicodeEditorProjectionAtomicity()
{
  const char *failures[] = {"set-data", "data-size", "set-selection"};
  for (unsigned i = 0; i < 3; ++i)
  {
    TextensionFake::reset();
    Fixture f;
    f.render();
    LOKA_VERIFY(TextensionFake::body(TextensionFake::latest()) == nativeText("test"));
    LOKA_VERIFY(f.lines.update(f.lines.at(0).id, String("owner")) == EDIT_OK);
    TextensionFake::clearCalls();
    TextensionFake::failNext(failures[i], -1);
    f.context->onPropsApplied();
    LOKA_VERIFY(logicalText(f) == "owner");
    LOKA_VERIFY(count("new") == 1 && count("set-data") == 2);
    LOKA_VERIFY(f.context->hasNativeHandle());
    LOKA_VERIFY(TextensionFake::body(TextensionFake::latest()) == nativeText("owner"));
    LOKA_VERIFY(f.controller.unicodeEditors_.hasPending());
  }
}
static void testUnicodeEditorNewObjectProjection()
{
  TextensionFake::reset();
  Fixture f;
  f.render();
  f.controller.retireTextEditorControl(f.context, f.context->lifetimeHint());
  TextensionFake::clearCalls();
  f.render();
  LOKA_VERIFY(count("new") == 1 && count("set-data") == 1);
  LOKA_VERIFY(TextensionFake::body(TextensionFake::latest()) == nativeText("test"));
}
static void testUnicodeEditorReadBackRefusals()
{
  for (unsigned kind = 0; kind < 8; ++kind)
  {
    TextensionFake::reset();
    Fixture f;
    f.render();
    const TXNObject old = TextensionFake::latest();
    const ItemId identity = f.lines.at(0).id;
    std::vector<unsigned short> units(1, 'z');
    if (kind == 0)
      TextensionFake::lieSize(3);
    if (kind == 1)
      TextensionFake::lieHandle(999);
    if (kind == 2)
      units.assign(9000, 'z');
    if (kind == 3)
      units.assign(257, '\r');
    if (kind == 4)
      units.assign(1, 0xD800);
    if (kind == 5)
      units.assign(1, 0);
    if (kind == 6)
      units.assign(4000, 0x3042); // valid UTF-16, UTF-8 exceeds byte capacity
    if (kind == 7)
      TextensionFake::failNext("get-data", -1);
    TextensionFake::clearCalls();
    keyInput(f, units);
    LOKA_VERIFY(logicalText(f) == "test" && f.lines.at(0).id == identity);
    LOKA_VERIFY(TextensionFake::latest() != old);
    LOKA_VERIFY(count("new") == 1 && count("set-data") == 1);
    LOKA_VERIFY(f.controller.unicodeEditors_.hasPending());
    if (kind == 2)
      LOKA_VERIFY(count("get-data") == 0);               // budget before Handle allocation/conversion
    LOKA_VERIFY(f.controller.editControls_.size() == 1); // replacement preserves the row
    f.controller.flushRetiredNativeHandles();
    LOKA_VERIFY(count("delete") == 1);
  }
}
static void testUnicodeEditorAcceptedEdits()
{
  const char *bodies[] = {"insert", "", "a\rb", "ab", "tail\r", "", "q"};
  TextensionFake::reset();
  Fixture f;
  f.render();
  for (unsigned i = 0; i < sizeof(bodies) / sizeof(*bodies); ++i)
  {
    TXNSelectAll(TextensionFake::latest());
    TextensionFake::clearCalls();
    const ListRevision before = f.lines.revision().get();
    keyInput(f, nativeText(bodies[i]));
    LOKA_VERIFY(logicalText(f) == bodies[i]);
    LOKA_VERIFY(count("new") == 0 && count("set-data") == 0);
    LOKA_VERIFY(f.lines.revision().get().content == before.content + 1);
    const std::string body(bodies[i]);
    const std::size_t split = body.rfind('\r');
    LOKA_VERIFY(f.cursor.state()->get()
                == LineCursor(f.lines.at(f.lines.size() - 1).id,
                              static_cast<int>(split == std::string::npos ? body.size() : body.size() - split - 1)));
  }
  TXNSelectAll(TextensionFake::latest());
  std::vector<unsigned short> pair;
  pair.push_back(0xD83D);
  pair.push_back(0xDE00);
  keyInput(f, pair);
  LOKA_VERIFY(logicalText(f) == "\xF0\x9F\x98\x80");
  LOKA_VERIFY(f.cursor.state()->get().column == 4);
}
static void testUnicodeEditorNoDiffKeepsCommitted()
{
  TextensionFake::reset();
  Fixture f;
  f.render();
  TextensionFake::nextClick(2, 2);
  EventRecord event = {1, 0, 0, {30, 30}, 0};
  LOKA_VERIFY(f.context->click(event.where, event) == EDITOR_OK);
  LOKA_VERIFY(f.cursor.state()->get().column == 2);
  TextensionFake::clearCalls();
  keyInput(f, nativeText("X"));
  LOKA_VERIFY(logicalText(f) == "teXst");
  LOKA_VERIFY(count("new") == 0 && count("set-data") == 0);
}
static void testUnicodeEditorNormalization()
{
  const char *variants[] = {"a\nbc", "a\r\nbc"};
  for (unsigned i = 0; i < 2; ++i)
  {
    TextensionFake::reset();
    Fixture f;
    f.render();
    TXNSelectAll(TextensionFake::latest());
    TextensionFake::clearCalls();
    keyInput(f, nativeText(variants[i]));
    LOKA_VERIFY(logicalText(f) == "a\rbc");
    LOKA_VERIFY(f.cursor.state()->get() == LineCursor(f.lines.at(1).id, 2));
    LOKA_VERIFY(TextensionFake::body(TextensionFake::latest()) == nativeText("a\rbc"));
    LOKA_VERIFY(count("set-data") == 1 && count("new") == 0);
  }
}
static void testUnicodeEditorRepairSelectionFailure()
{
  TextensionFake::reset();
  Fixture f;
  f.render();
  TextensionFake::clearCalls();
  TextensionFake::failNth("set-selection", 2, -1);
  keyInput(f, std::vector<unsigned short>(1, 0xD800));
  LOKA_VERIFY(count("new") == 1); // no recursive repair after restoration fails
  LOKA_VERIFY(!f.context->hasNativeHandle() && f.controller.editControls_.size() == 0);
  LOKA_VERIFY(logicalText(f) == "test");
}
static void testUnicodeEditorRepairRecovery()
{
  TextensionFake::reset();
  Fixture f;
  f.render();
  TextensionFake::failNext("new", -1);
  keyInput(f, std::vector<unsigned short>(1, 0));
  LOKA_VERIFY(!f.context->hasNativeHandle() && f.controller.editControls_.size() == 0);
  TextensionFake::clearCalls();
  f.context->retryProjection();
  LOKA_VERIFY(count("new") == 0 && count("set-data") == 0);
  f.render();
  LOKA_VERIFY(f.context->hasNativeHandle() && count("new") == 1 && count("set-data") == 1);
  LOKA_VERIFY(TextensionFake::body(TextensionFake::latest()) == nativeText("test"));
}
static void testUnicodeEditorBufferLifetime()
{
  using namespace loka::core::testing;
  const int before = lokaAllocRawLive();
  failLokaAllocRaw("ToolboxUnicodeProjection", "LogicalText", 2);
  {
    TextensionFake::reset();
    Fixture f;
    f.render();
    LOKA_VERIFY(f.context->hasNativeHandle()); // bounded repair retries the refused pair once
  }
  LOKA_VERIFY(lokaAllocRawLive() == before);
  allowLokaAllocRaw();
}
static Fixture *writeFixture;
static void ownerWrite()
{
  LOKA_VERIFY(writeFixture->lines.update(writeFixture->lines.at(0).id, String("owner")) == EDIT_OK);
}
static void testUnicodeEditorStaleRevision()
{
  TextensionFake::reset();
  Fixture f;
  f.render();
  writeFixture = &f;
  TextensionFake::afterKey = ownerWrite;
  TextensionFake::clearCalls();
  keyInput(f, nativeText("x"));
  TextensionFake::afterKey = 0;
  LOKA_VERIFY(logicalText(f) == "owner");
  LOKA_VERIFY(count("new") == 1 && count("get-data") == 0);
  LOKA_VERIFY(TextensionFake::body(TextensionFake::latest()) == nativeText("owner"));
}
static void testUnicodeEditorCaretRequest()
{
  TextensionFake::reset();
  Fixture f;
  f.render();
  {
    StateTrackerGuard guard(f.tracker());
    f.request.set(LineCursor(f.lines.at(0).id, 3));
  }
  TextensionFake::clearCalls();
  f.context->onPropsApplied();
  LOKA_VERIFY(f.cursor.state()->get().column == 3);
  LOKA_VERIFY(count("set-selection") == 1 && count("show-selection") == 1);
}

static void testUnicodeEditorCopySelectAllKeepCommitted()
{
  TextensionFake::reset();
  Fixture f;
  f.render();
  TextensionFake::clearCalls();
  const ListRevision before = f.lines.revision().get();
  LOKA_VERIFY(f.context->editVerb(EDIT_COPY));
  LOKA_VERIFY(f.context->editVerb(EDIT_SELECT_ALL));
  LOKA_VERIFY(count("get-data") == 0 && count("set-data") == 0 && count("new") == 0);
  LOKA_VERIFY(f.lines.revision().get() == before);
  keyInput(f, nativeText("replacement"));
  LOKA_VERIFY(logicalText(f) == "replacement");
  LOKA_VERIFY(count("new") == 0 && count("set-data") == 0);
}
static void testUnicodeEditorUndo()
{
  TextensionFake::reset();
  Fixture f;
  f.render();
  keyInput(f, nativeText("x"));
  LOKA_VERIFY(logicalText(f) == "xtest");
  LOKA_VERIFY(f.context->editVerb(EDIT_UNDO));
  LOKA_VERIFY(logicalText(f) == "test");
  TextensionFake::lieSize(1);
  TextensionFake::clearCalls();
  LOKA_VERIFY(f.context->editVerb(EDIT_UNDO));
  LOKA_VERIFY(logicalText(f) == "test" && count("new") == 1);
}
static void scrapUnknown()
{
  loka::toolbox::UnicodeText::noteNativeScrap(-1);
  TextensionFake::clearCalls();
}
static void testUnicodeEditorPasteStartup()
{
  TextensionFake::reset();
  loka::toolbox::UnicodeText::Session session;
  Fixture f;
  f.render();
  TextensionFake::clearCalls();
  TextensionFake::publicText(nativeText("paste"));
  LOKA_VERIFY(f.context->editVerb(EDIT_PASTE));
  LOKA_VERIFY(count("scrap-from") == 1 && logicalText(f) == "pastetest");
  LOKA_VERIFY(f.context->editVerb(EDIT_PASTE));
  LOKA_VERIFY(count("scrap-from") == 1 && logicalText(f) == "pastepastetest");
}
static void testUnicodeEditorPasteResumeFailure()
{
  for (unsigned failure = 0; failure < 2; ++failure)
  {
    TextensionFake::reset();
    Fixture f;
    f.render();
    LOKA_VERIFY(f.context->editVerb(EDIT_SELECT_ALL));
    LOKA_VERIFY(f.context->editVerb(EDIT_COPY));
    TextensionFake::publicText(nativeText("public"));
    TextensionFake::failNext("scrap-from", -1);
    loka::toolbox::UnicodeText::scrapFromPublic();
    TextensionFake::clearCalls();
    const unsigned beeps = toolbox_host::beeps;
    if (failure)
      TextensionFake::failNext("scrap-from", -1);
    LOKA_VERIFY(f.context->editVerb(EDIT_PASTE));
    LOKA_VERIFY(count("scrap-from") == 1);
    LOKA_VERIFY(logicalText(f) == (failure ? "test" : "public"));
    LOKA_VERIFY(toolbox_host::beeps == beeps + failure);
  }
}
static void testUnicodeEditorPastePrivate()
{
  TextensionFake::reset();
  Fixture f;
  f.render();
  LOKA_VERIFY(f.context->editVerb(EDIT_SELECT_ALL));
  LOKA_VERIFY(f.context->editVerb(EDIT_COPY));
  TextensionFake::publicText(nativeText("wrong"));
  TextensionFake::clearCalls();
  LOKA_VERIFY(f.context->editVerb(EDIT_PASTE));
  LOKA_VERIFY(count("scrap-from") == 0 && logicalText(f) == "test");
  TextensionFake::failNext("scrap-to", -1);
  loka::toolbox::UnicodeText::scrapToPublic();
  LOKA_VERIFY(f.context->editVerb(EDIT_PASTE));
  LOKA_VERIFY(count("scrap-from") == 0 && logicalText(f) == "testtest");
}
static void testUnicodeEditorCutFailure()
{
  TextensionFake::reset();
  Fixture f;
  f.render();
  LOKA_VERIFY(f.context->editVerb(EDIT_SELECT_ALL));
  LOKA_VERIFY(f.context->editVerb(EDIT_COPY));
  TextensionFake::publicText(nativeText("public"));
  TextensionFake::clearCalls();
  TextensionFake::failNext("cut", -1);
  LOKA_VERIFY(f.context->editVerb(EDIT_CUT));
  LOKA_VERIFY(count("get-data") == 1 && logicalText(f) == "test");
  LOKA_VERIFY(f.context->editVerb(EDIT_PASTE));
  LOKA_VERIFY(count("scrap-from") == 1 && logicalText(f) == "public");
}
static void testUnicodeEditorPasteNormalization()
{
  TextensionFake::reset();
  Fixture f;
  f.render();
  scrapUnknown();
  TextensionFake::publicText(nativeText("a\r\nb"));
  TextensionFake::clearCalls();
  LOKA_VERIFY(f.context->editVerb(EDIT_SELECT_ALL));
  const ListRevision before = f.lines.revision().get();
  LOKA_VERIFY(f.context->editVerb(EDIT_PASTE));
  LOKA_VERIFY(logicalText(f) == "a\rb" && f.cursor.state()->get() == LineCursor(f.lines.at(1).id, 1));
  LOKA_VERIFY(f.lines.revision().get().content == before.content + 1);
  LOKA_VERIFY(count("set-data") == 1 && count("new") == 0);
}
static void injectOutsideDoor()
{
  TextensionFake::injectChange(nativeText("outside\rchanged\rwhole"));
  TextensionFake::afterService = TextensionFake::afterIdle = 0;
}
static void testUnicodeEditorOutsideDoor()
{
  for (unsigned stage = 0; stage < 2; ++stage)
  {
    TextensionFake::reset();
    Fixture f;
    f.render();
    f.focus();
    it::ToolboxWindow window;
    window.scenePlatformController()->data = &f.controller;
    window.scenePlatformController()->sync = sync;
    window.scenePlatformController()->idle = idle;
    it::AppComponentGroup group;
    group.components.push_back(&window);
    it::ToolboxApp app(group);
    app.active_ = &window;
    it::iteration_host::front = window.window();
    ToolboxIdleClock clock(0);
    EventRecord event = {it::nullEvent, 0, 0, {0, 0}, 0};
    if (stage == 0)
      TextensionFake::afterService = injectOutsideDoor;
    else
      TextensionFake::afterIdle = injectOutsideDoor;
    TextensionFake::clearCalls();
    loka::app::testing::SettleTrace<LineCursor>::instance().clear();
    FILE *log = std::tmpfile();
    LOKA_VERIFY(log);
    std::fflush(stderr);
    const int saved = dup(fileno(stderr));
    LOKA_VERIFY(saved >= 0 && dup2(fileno(log), fileno(stderr)) >= 0);
    app.iterate(clock, false, event); // service precedes idle, even in a background pass
    std::fflush(stderr);
    LOKA_VERIFY(dup2(saved, fileno(stderr)) >= 0);
    close(saved);
    std::rewind(log);
    char diagnostic[512] = {0};
    const std::size_t length = std::fread(diagnostic, 1, sizeof(diagnostic) - 1, log);
    std::fclose(log);
#if defined(LOKA_DIAG) || defined(LOKA_RETRO68_DIAGNOSTICS)
    LOKA_VERIFY(length && std::strstr(diagnostic, "[unicode-outside-door]"));
    LOKA_VERIFY(!std::strstr(std::strchr(diagnostic, '\n') + 1, "[unicode-outside-door]"));
#else
    LOKA_VERIFY(length == 0);
#endif
    LOKA_VERIFY(logicalText(f) == "test");
    LOKA_VERIFY(count("set-data") == 0 && loka::app::testing::SettleTrace<LineCursor>::instance().size() == 0);
    TextensionFake::clearCalls();
    const ListRevision before = f.lines.revision().get();
    if (stage == 0)
      keyInput(f, std::vector<unsigned short>());
    else
      LOKA_VERIFY(f.context->click(event.where, event) == EDITOR_OK);
    LOKA_VERIFY(logicalText(f) == "outside\rchanged\rwhole");
    LOKA_VERIFY(f.lines.revision().get().content == before.content + 1);
    LOKA_VERIFY(count("get-data") == 1 && count("set-data") == 0 && count("new") == 0);
  }
}
static void writeOnTake(void *data)
{
  Fixture &f = *static_cast<Fixture *>(data);
  if (f.request.get().isNone())
    LOKA_VERIFY(f.lines.update(f.lines.at(0).id, String("a longer owner")) == EDIT_OK);
}
static void testUnicodeEditorPrepareApplyRecheck()
{
  TextensionFake::reset();
  Fixture f;
  f.render();
  {
    StateTrackerGuard guard(f.tracker());
    f.request.set(LineCursor(f.lines.at(0).id, 12));
  }
  f.request.state()->bind(writeOnTake, &f, false);
  TextensionFake::clearCalls();
  f.context->onPropsApplied();
  f.request.state()->unbind(writeOnTake, &f);
  LOKA_VERIFY(TextensionFake::body(TextensionFake::latest()) == nativeText("a longer owner"));
  LOKA_VERIFY(f.cursor.state()->get().column == 12);
  TXNOffset start = 0, end = 0;
  TXNGetSelection(TextensionFake::latest(), &start, &end);
  LOKA_VERIFY(start == 12 && end == 12 && count("show-selection") == 1);
  LOKA_VERIFY(count("set-data") == 1);
}
static void testUnicodeEditorCommandUnavailable()
{
  TextensionFake::reset();
  Fixture f;
  f.render();
  LOKA_VERIFY(f.commands.post(EditorCommand(EditorCommand::PAGE_DOWN)) == POST_ACCEPTED);
  TextensionFake::clearCalls();
  f.context->onPropsApplied();
  LOKA_VERIFY(f.commands.reply().state()->get().kind() == Reply<EditorCommand>::REFUSED);
  LOKA_VERIFY(f.commands.reply().state()->get().reason() == EDITOR_UNAVAILABLE);
  LOKA_VERIFY(count("set-data") == 0 && count("set-selection") == 0);
}
static void testUnicodeEditorCapacity()
{
  const TextCapacity maximum = ToolboxTextCapacity();
  LOKA_VERIFY(maximum.lines == 65535 && maximum.bytes == INT_MAX / 2);
  const TextCapacity profile = TextCapacity::of(32768, 4 * 1024 * 1024).clampedTo(maximum);
  LOKA_VERIFY(profile.lines == 32768 && profile.bytes == 4 * 1024 * 1024);
}
static void testUnicodeEditorByteDoorsUnreachable()
{
  TextensionFake::reset();
  Fixture f;
  f.render();
  f.focus();
  TextensionFake::clearCalls();
  EventRecord event = {3, 'x', 0, {30, 30}, 0};
  LOKA_VERIFY(ToolboxInputDoor::offerKeyEvent(f.controller, event));
  LOKA_VERIFY(count("key") == 1 && logicalText(f) == "xtest");
  TextensionFake::publicText(nativeText("p"));
  scrapUnknown();
  LOKA_VERIFY(f.window.handleEditCommand(EDIT_PASTE));
  LOKA_VERIFY(count("paste") == 1 && logicalText(f) == "xptest");
  LOKA_VERIFY(f.context->key('x') == EDITOR_UNAVAILABLE);
  LOKA_VERIFY(f.context->paste("x", 1) == EDITOR_UNAVAILABLE);
}

static void testUnicodeEditorChangedRowIdentity()
{
  TextensionFake::reset();
  Fixture f;
  LOKA_VERIFY(f.lines.update(f.lines.at(0).id, String("aa")) == EDIT_OK);
  LOKA_VERIFY(f.lines.insert(1, String("bb")) == EDIT_OK);
  LOKA_VERIFY(f.lines.insert(2, String("cc")) == EDIT_OK);
  const ItemId first = f.lines.at(0).id, middle = f.lines.at(1).id, last = f.lines.at(2).id;
  f.render();
  LOKA_VERIFY(TXNSetSelection(TextensionFake::latest(), 4, 4) == noErr);
  keyInput(f, nativeText("X"));
  LOKA_VERIFY(logicalText(f) == "aa\rbXb\rcc");
  LOKA_VERIFY(f.lines.revision().get().change == ListChange(LIST_UPDATE, 1, 1));
  LOKA_VERIFY(f.lines.at(0).id == first && f.lines.at(1).id == middle && f.lines.at(2).id == last);
  LOKA_VERIFY(f.cursor.state()->get() == LineCursor(middle, 2));
  LOKA_VERIFY(TXNSetSelection(TextensionFake::latest(), 4, 4) == noErr);
  keyInput(f, nativeText("\r"));
  LOKA_VERIFY(logicalText(f) == "aa\rb\rXb\rcc");
  LOKA_VERIFY(f.lines.at(0).id == first && f.lines.at(1).id == middle && f.lines.at(3).id == last);
  LOKA_VERIFY(f.cursor.state()->get() == LineCursor(f.lines.at(2).id, 0));
  const ItemId split = f.lines.at(2).id;
  LOKA_VERIFY(TXNSetSelection(TextensionFake::latest(), 4, 5) == noErr);
  keyInput(f, std::vector<unsigned short>());
  LOKA_VERIFY(logicalText(f) == "aa\rbXb\rcc" && f.lines.find(split) < 0);
  LOKA_VERIFY(f.lines.at(0).id == first && f.lines.at(1).id == middle && f.lines.at(2).id == last);
  LOKA_VERIFY(f.cursor.state()->get() == LineCursor(middle, 1));
}
static void testUnicodeEditorEmptyBody()
{
  TextensionFake::reset();
  Fixture f;
  LOKA_VERIFY(f.lines.update(f.lines.at(0).id, String("")) == EDIT_OK);
  f.render();
  LOKA_VERIFY(TextensionFake::body(TextensionFake::latest()).empty());
  EventRecord event = {1, 0, 0, {30, 30}, 0};
  LOKA_VERIFY(f.context->click(event.where, event) == EDITOR_OK); // TXNGetData returns no Handle
  keyInput(f, nativeText("x"));
  LOKA_VERIFY(logicalText(f) == "x" && f.cursor.state()->get().column == 1);
  LOKA_VERIFY(f.context->editVerb(EDIT_SELECT_ALL));
  keyInput(f, std::vector<unsigned short>());
  LOKA_VERIFY(logicalText(f).empty() && f.lines.size() == 1 && f.cursor.state()->get().column == 0);
  LOKA_VERIFY(f.lines.remove(f.lines.at(0).id) == EDIT_OK);
  f.context->onPropsApplied();
  LOKA_VERIFY(f.context->click(event.where, event) == EDITOR_INVALID_CURSOR);
  LOKA_VERIFY(f.lines.size() == 0); // no row exists to name in an applyReplace range
}
static void testUnicodeEditorOwnerReplacement()
{
  TextensionFake::reset();
  Fixture f;
  loka::testing::TextBufferFixture replacement;
  LOKA_VERIFY(replacement.attach(f.tracker()->asPushTracker(), 256, 8192) == ATTACH_OK);
  LOKA_VERIFY(replacement.insert(0, String("replacement")) == EDIT_OK);
  f.render();
  f.node.props.buffer_ = &replacement;
  TextensionFake::clearCalls();
  f.context->onPropsApplied();
  LOKA_VERIFY(count("set-data") == 1);
  LOKA_VERIFY(TextensionFake::body(TextensionFake::latest()) == nativeText("replacement"));
  f.node.props.buffer_ = &f.lines;
}
static void testUnicodeEditorPasteUnavailable()
{
  TextensionFake::reset();
  Fixture f;
  f.render();
  scrapUnknown();
  TextensionFake::publicText(std::vector<unsigned short>());
  const unsigned beeps = toolbox_host::beeps;
  LOKA_VERIFY(f.context->editVerb(EDIT_PASTE));
  LOKA_VERIFY(toolbox_host::beeps == beeps + 1 && count("paste") == 0 && count("set-data") == 0);
  LOKA_VERIFY(logicalText(f) == "test");
}
static void testUnicodeEditorNormalizationFailureRepair()
{
  TextensionFake::reset();
  Fixture f;
  f.render();
  TXNSelectAll(TextensionFake::latest());
  TextensionFake::clearCalls();
  TextensionFake::failNext("set-data", -1);
  keyInput(f, nativeText("a\r\nb"));
  LOKA_VERIFY(logicalText(f) == "a\rb");
  LOKA_VERIFY(count("new") == 1 && count("set-data") == 2);
  LOKA_VERIFY(TextensionFake::body(TextensionFake::latest()) == nativeText("a\rb"));
}

/** Test capability uses the same retained-handler door; production exposes no hook. */
struct ProjectionKey : RetainedNodeHandler<ProjectionKey, TextEditorNode, ToolboxUnicodeTextEditorContext>
{
  static SeamKey<TextEditorNode> get()
  {
    return seamKey();
  }
};
static void testUnicodeEditorSnapshotCapacityRefusal()
{
  using namespace loka::core::testing;
  failLokaAllocRaw("ToolboxUnicodeProjection", "LogicalText", 0);
  {
    TextensionFake::reset();
    Fixture f;
    ToolboxUnicodeProjection snapshot;
    LOKA_VERIFY(snapshot.reserve(8192));
    ToolboxUnicodeProjection::Stamp stamp;
    LOKA_VERIFY(snapshot.stage(f.node, ProjectionKey::get(), stamp) == EDITOR_OK);
    snapshot.publish(stamp);
    failLokaAllocRaw("ToolboxUnicodeProjection", "LogicalText", 1);
    LOKA_VERIFY(!snapshot.reserve(4096));
    LOKA_VERIFY(snapshot.committed().length() == 4);
    LOKA_VERIFY(std::memcmp(snapshot.committed().bytes(), "test", 4) == 0);
    LOKA_VERIFY(snapshot.stamp().owner == stamp.owner && snapshot.stamp().revision == stamp.revision);
  }
  allowLokaAllocRaw();
}

static void testUnicodeEditorPreparationAtomicity()
{
  using namespace loka::core::testing;
  failLokaAllocRaw("ToolboxUnicodeProjection", "LogicalText", 0);
  {
    TextensionFake::reset();
    Fixture f;
    f.render();
    ToolboxUnicodeProjection snapshot;
    ToolboxUnicodeProjection::Stamp stamp;
    LOKA_VERIFY(snapshot.reserve(8192));
    LOKA_VERIFY(snapshot.stage(f.node, ProjectionKey::get(), stamp) == EDITOR_OK);
    stamp.changes = 42;
    snapshot.publish(stamp);
    loka::testing::TextBufferFixture replacement;
    LOKA_VERIFY(replacement.attach(f.tracker()->asPushTracker(), 128, 4096) == ATTACH_OK);
    LOKA_VERIFY(replacement.insert(0, String("replacement")) == EDIT_OK);
    f.node.props.buffer_ = &replacement;
    for (unsigned failure = 1; failure <= 3; ++failure)
    {
      failLokaAllocRaw("ToolboxUnicodeProjection", "LogicalText", failure < 3 ? failure : 0);
      {
        ToolboxUnicodeProjection::Preparation pending(snapshot, 4096);
        ToolboxUnicodeProjection::Stamp taken;
        const EditorResult result = pending.stage(f.node, ProjectionKey::get(), taken);
        if (failure < 3)
          LOKA_VERIFY(result == EDITOR_ALLOCATION);
        else
        {
          LOKA_VERIFY(result == EDITOR_OK);
          TextensionFake::failNext("set-data", -1);
          unsigned short units[11];
          std::size_t count = 0;
          LOKA_VERIFY(Utf8ToUtf16(pending.candidate().bytes(), pending.candidate().length(), units, 11, count));
          LOKA_VERIFY(
              TXNSetData(
                  TextensionFake::latest(), kTXNUnicodeTextData, units, count * 2, kTXNStartOffset, kTXNEndOffset)
              != noErr);
        }
      }
      LOKA_VERIFY(snapshot.committed().length() == 4 && std::memcmp(snapshot.committed().bytes(), "test", 4) == 0);
      LOKA_VERIFY(snapshot.stamp().owner == stamp.owner && snapshot.stamp().revision == stamp.revision
                  && snapshot.stamp().changes == 42);
    }
    {
      ToolboxUnicodeProjection::Preparation pending(snapshot, 4096);
      ToolboxUnicodeProjection::Stamp taken;
      LOKA_VERIFY(pending.stage(f.node, ProjectionKey::get(), taken) == EDITOR_OK);
      pending.publish(taken);
    }
    LOKA_VERIFY(snapshot.current(f.node) && snapshot.committed().length() == 11);
    LOKA_VERIFY(std::memcmp(snapshot.committed().bytes(), "replacement", 11) == 0);
    f.node.props.buffer_ = &f.lines;
  }
  allowLokaAllocRaw();
}

static void testUnicodeEditorScrollKeysUseNativeDoor()
{
  TextensionFake::reset();
  Fixture f;
  f.render();
  f.focus();
  const unsigned char codes[] = {0x74, 0x79, 0x73, 0x77};
  for (unsigned i = 0; i < 4; ++i)
  {
    TextensionFake::nextCaret(i, i);
    TextensionFake::clearCalls();
    EventRecord event = {3, static_cast<unsigned long>(codes[i]) << 8, 0, {30, 30}, 0};
    LOKA_VERIFY(ToolboxInputDoor::offerKeyEvent(f.controller, event));
    LOKA_VERIFY(count("key") == 1 && count("get-data") == 1 && count("set-data") == 0);
    LOKA_VERIFY(logicalText(f) == "test" && f.cursor.state()->get().column == static_cast<int>(i));
  }
}
int main(int argc, char **argv)
{
  const char *which = argc > 1 ? argv[1] : "all";
#define RUN(name)                                                                                                      \
  if (!std::strcmp(which, "all") || !std::strcmp(which, #name))                                                        \
  name()
  RUN(testUnicodeEditorProjectionAtomicity);
  RUN(testUnicodeEditorNewObjectProjection);
  RUN(testUnicodeEditorReadBackRefusals);
  RUN(testUnicodeEditorAcceptedEdits);
  RUN(testUnicodeEditorNoDiffKeepsCommitted);
  RUN(testUnicodeEditorNormalization);
  RUN(testUnicodeEditorRepairSelectionFailure);
  RUN(testUnicodeEditorRepairRecovery);
  RUN(testUnicodeEditorBufferLifetime);
  RUN(testUnicodeEditorStaleRevision);
  RUN(testUnicodeEditorCaretRequest);
  RUN(testUnicodeEditorCopySelectAllKeepCommitted);
  RUN(testUnicodeEditorUndo);
  RUN(testUnicodeEditorPasteStartup);
  RUN(testUnicodeEditorPasteResumeFailure);
  RUN(testUnicodeEditorPastePrivate);
  RUN(testUnicodeEditorCutFailure);
  RUN(testUnicodeEditorPasteNormalization);
  RUN(testUnicodeEditorOutsideDoor);
  RUN(testUnicodeEditorPrepareApplyRecheck);
  RUN(testUnicodeEditorCommandUnavailable);
  RUN(testUnicodeEditorCapacity);
  RUN(testUnicodeEditorByteDoorsUnreachable);
  RUN(testUnicodeEditorChangedRowIdentity);
  RUN(testUnicodeEditorEmptyBody);
  RUN(testUnicodeEditorOwnerReplacement);
  RUN(testUnicodeEditorPasteUnavailable);
  RUN(testUnicodeEditorNormalizationFailureRepair);
  RUN(testUnicodeEditorSnapshotCapacityRefusal);
  RUN(testUnicodeEditorPreparationAtomicity);
  RUN(testUnicodeEditorScrollKeysUseNativeDoor);
  RUN(allocation);
  RUN(placement);
  RUN(edgeInset);
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
