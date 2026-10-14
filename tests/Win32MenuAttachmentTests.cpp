#include "Win32MenuAttachmentTests.hpp"
#include <cstdio>
#include <cwchar>
#include <functional>
#include "Win32App.hpp"
#include "Win32Window.hpp"
#include "Win32ScenePlatformController.hpp"
#include "app/nodes/boundary/StdComposition.hpp"
#include "platform/null/NullPlatformContext.hpp"
#include "platform/Win32DisplayScale.hpp"
#include "support/WindowAdmissionTestApp.hpp"
#include "support/TestVerify.hpp"
#include "testing/scene/SceneTestFlow.hpp"
#include "core/util/StateTrackerGuard.hpp"

namespace
{
  using namespace loka::app;
  using namespace loka::app::scene;
  using namespace loka::core;

  class MenuApp : public Win32App
  {
  public:
    MenuApp() : Win32App(0, GetModuleHandleW(NULL), SW_SHOW), quits(0) {}
    virtual ~MenuApp() {}
    using Win32App::projectMenu;
    using Win32App::TranslateOrDispatch;
    virtual void quit() { ++this->quits; }
    int quits;
    void own(Window *window)
    {
      if (!this->group_)
        this->group_ = new AppComponentGroup(std::vector<AppComponent *>());
      this->group_->adopt(window);
    }
  };

  void show(Win32Window &window, bool visible)
  {
    {
      StateTrackerGuard guard(window.getTracker());
      window.visibilityState().set(visible, true);
    }
    WindowAdmissionTestApp admission(window);
    admission.flush();
  }

  WindowProps props()
  {
    WindowProps result;
    result.frame(60, 60, 320, 200).visible(false);
    return result;
  }

  MenuBarDefinition bar(const char *title, EmitterState *emitter = 0)
  {
    MenuBarDefinition result;
    result << (Menu("File") << MenuItem(title).onClick(emitter));
    return result;
  }

  UINT firstCommand(Win32Window &window)
  {
    HMENU menu = GetMenu(window.hwnd());
    LOKA_VERIFY(menu);
    HMENU popup = GetSubMenu(menu, 0);
    LOKA_VERIFY(popup);
    const UINT id = GetMenuItemID(popup, 0);
    LOKA_VERIFY(id != static_cast<UINT>(-1));
    return id;
  }

  void verifyFrame(Win32Window &window, const Frame &before)
  {
    Frame after;
    LOKA_VERIFY(window.queryNativeContentFrame(after));
    LOKA_VERIFY(after == before);
    RECT client;
    LOKA_VERIFY(GetClientRect(window.hwnd(), &client));
    const loka::win32::Win32DisplayScale scale(
        loka::win32::Win32DisplayScale::forWindow(window.hwnd()).dpi(),
        loka::win32::DefaultRailMetrics());
    LOKA_VERIFY(client.right - client.left == scale.clientLengthToNative(before.width).px);
    LOKA_VERIFY(client.bottom - client.top == scale.clientLengthToNative(before.height).px);
  }

  struct Observation
  {
    Observation() : window(0), ownerAtShutdown(0), command(0), calls(0), detaches(0) {}
    Win32Window *window;
    App *ownerAtShutdown;
    UINT command;
    int calls;
    int detaches;
  };
  class MenuRoot;
  struct MenuRootTypeTag {};

  class MenuRootProps : public NodePropsBase<MenuRootProps>
  {
  public:
    typedef MenuRootTypeTag TypeTag;
    typedef MenuRoot NodeType;
    explicit MenuRootProps(Observation *observation = 0,
                           const char *title = "Run", EmitterState *emitter = 0)
        : observation_(observation), title_(title), emitter_(emitter) {}
    Observation *observation() const { return this->observation_; }
    const char *title() const { return this->title_; }
    EmitterState *emitter() const { return this->emitter_; }
    bool operator<(const PropsBase &rhs) const
    {
      if (rhs.propsTypeId() != this->propsTypeId())
        return this->propsTypeId() < rhs.propsTypeId();
      const MenuRootProps &other = static_cast<const MenuRootProps &>(rhs);
      if (this->observation_ != other.observation_)
        return std::less<Observation *>()(this->observation_, other.observation_);
      if (this->title_ != other.title_)
        return std::less<const char *>()(this->title_, other.title_);
      return std::less<EmitterState *>()(this->emitter_, other.emitter_);
    }
  private:
    Observation *observation_;
    const char *title_;
    EmitterState *emitter_;
  };

  class MenuRoot : public StdCompositionBoundaryNodeBase<MenuRootProps>
  {
  public:
    explicit MenuRoot(const MenuRootProps &p)
        : StdCompositionBoundaryNodeBase<MenuRootProps>(p)
    {
      this->state(this->enabled_, true);
    }
    virtual void declareBindings(BindingToken &token)
    {
      token.action(this->clicked_, this, &MenuRoot::clicked);
    }
    virtual void composeNode(NodeComposition &c)
    {
      if (this->props.emitter())
      {
        LOKA_VERIFY(c.menuBar(bar(this->props.title(), this->props.emitter())));
        return;
      }
      MenuBarDefinition offered;
      offered << (Menu("File") << MenuItem(this->props.title()).shortcut('R').enabled(this->enabled_.state()).onClick(&this->clicked_));
      LOKA_VERIFY(c.menuBar(offered));
    }
    virtual void detachNode(NodeComposition &)
    {
      if (!this->props.observation())
        return;
      Observation &observation = *this->props.observation();
      ++observation.detaches;
      Win32Window &window = *observation.window;
      LOKA_VERIFY(GetMenu(window.hwnd()) != NULL);
      LOKA_VERIFY(!window.menuAttachment().dispatch(observation.command));
      if (observation.ownerAtShutdown)
      {
        // Virtual dispatch through the base borrow witnesses that Win32App is
        // still alive. Without its retireComponents(), App's default returns
        // false here. No active window means this continuation projects nothing.
        LOKA_VERIFY(observation.ownerAtShutdown->handleMenuCommand(-1, &window));
      }
    }
    void enabled(bool value)
    {
      StateTrackerGuard guard(this->tracker());
      this->enabled_.set(value);
    }
  private:
    void clicked() { ++this->props.observation()->calls; }
    NodeState<bool> enabled_;
    EmitterState clicked_;
  };

  WindowProps sceneProps(Observation &observation)
  {
    WindowProps result = props();
    result.scene(new Scene(Boundary<MenuRoot>(MenuRootProps(&observation))));
    return result;
  }

  struct Mounted
  {
    Observation observation;
    NullPlatformContext context;
    MenuApp app;
    Win32Window window;
    Mounted() : window(&this->context, sceneProps(this->observation))
    {
      this->observation.window = &this->window;
      this->window.setApp(&this->app);
      show(this->window, true);
      LOKA_VERIFY(this->window.scene()->menuBar());
      LOKA_VERIFY(this->window.menuAttachment().project(this->window.scene()->menuBar(), this->window.scene()) == Win32MenuAttachment::PROJECT_APPLIED);
      this->observation.command = firstCommand(this->window);
    }
    ~Mounted() { show(this->window, false); }
    MenuRoot &root()
    {
      return *static_cast<MenuRoot *>(loka::dsl::testing::SceneTestAccess::rootBoundary(*this->window.scene()));
    }
  };

  void count(void *data) { ++*static_cast<int *>(data); }
}

void testWin32MenuAttachmentProjectsOnceForEqualBar()
{
  NullPlatformContext context;
  Win32Window window(&context, props());
  show(window, true);
  Frame before;
  LOKA_VERIFY(window.queryNativeContentFrame(before));
  MenuBarDefinition first = bar("Open"), equal = bar("Open"), changed = bar("Save");
  LOKA_VERIFY(window.menuAttachment().project(&first, 0) == Win32MenuAttachment::PROJECT_APPLIED);
  HMENU installed = GetMenu(window.hwnd());
  LOKA_VERIFY(installed && IsMenu(installed));
  verifyFrame(window, before);
  LOKA_VERIFY(window.menuAttachment().project(&equal, 0) == Win32MenuAttachment::PROJECT_UNCHANGED);
  LOKA_VERIFY(GetMenu(window.hwnd()) == installed);
  LOKA_VERIFY(window.menuAttachment().project(&changed, 0) == Win32MenuAttachment::PROJECT_APPLIED);
  LOKA_VERIFY(GetMenu(window.hwnd()) != installed);
  LOKA_VERIFY(!IsMenu(installed));
  verifyFrame(window, before);
  LOKA_VERIFY(window.menuAttachment().project(0, 0) == Win32MenuAttachment::PROJECT_APPLIED);
  LOKA_VERIFY(!GetMenu(window.hwnd()));
  verifyFrame(window, before);
  // Completion re-offers every tail: absent over absent is unchanged.
  LOKA_VERIFY(window.menuAttachment().project(0, 0) == Win32MenuAttachment::PROJECT_UNCHANGED);
  // Equal offers after native recreation must rebuild, never reuse a dead HMENU.
  LOKA_VERIFY(window.menuAttachment().project(&changed, 0) == Win32MenuAttachment::PROJECT_APPLIED);
  show(window, false);
  show(window, true);
  LOKA_VERIFY(window.menuAttachment().project(&changed, 0) == Win32MenuAttachment::PROJECT_APPLIED);
  LOKA_VERIFY(GetMenu(window.hwnd()) && IsMenu(GetMenu(window.hwnd())));
  show(window, false);
}

void testWin32MenuAttachmentReleaseFromSourceDisconnects()
{
  Mounted f;
  Scene other((Boundary<MenuRoot>(MenuRootProps())));
  HMENU installed = GetMenu(f.window.hwnd());
  const UINT id = f.observation.command;
  HMENU popup = GetSubMenu(installed, 0);
  LOKA_VERIFY(GetMenuState(popup, id, MF_BYCOMMAND) == MF_ENABLED);
  f.window.menuAttachment().releaseFrom(&other);
  LOKA_VERIFY(f.window.menuAttachment().dispatch(id));
  LOKA_VERIFY(f.observation.calls == 1);
  f.root().enabled(false);
  const UINT disabled = GetMenuState(popup, id, MF_BYCOMMAND);
  LOKA_VERIFY(disabled != static_cast<UINT>(-1) && (disabled & MF_GRAYED));
  f.window.menuAttachment().releaseFrom(f.window.scene());
  LOKA_VERIFY(!f.window.menuAttachment().dispatch(id));
  f.root().enabled(true);
  LOKA_VERIFY(GetMenuState(popup, id, MF_BYCOMMAND) == disabled);
  LOKA_VERIFY(GetMenu(f.window.hwnd()) == installed && IsMenu(installed));
  LOKA_VERIFY(f.observation.calls == 1);
}

void testWin32SceneDetachReleasesMenu()
{
  Mounted f;
  HMENU installed = GetMenu(f.window.hwnd());
  LOKA_VERIFY(f.window.menuAttachment().dispatch(f.observation.command));
  LOKA_VERIFY(f.observation.calls == 1);
  loka::dsl::testing::SceneTestAccess::unmount(*f.window.scene());
  LOKA_VERIFY(f.observation.detaches == 1);
  LOKA_VERIFY(!f.window.menuAttachment().dispatch(f.observation.command));
  LOKA_VERIFY(GetMenu(f.window.hwnd()) == installed);
  {
    Mounted native;
    native.window.setApp(0); // External DestroyWindow must not queue this stack Window.
    const HWND hwnd = native.window.hwnd();
    const HMENU menu = GetMenu(hwnd);
    LOKA_VERIFY(DestroyWindow(hwnd));
    LOKA_VERIFY(native.observation.detaches == 1);
    LOKA_VERIFY(!native.window.menuAttachment().dispatch(native.observation.command));
    LOKA_VERIFY(!native.window.hwnd() && !IsMenu(menu));
  }
}

void testWin32TwoWindowsOwnTheirMenus()
{
  NullPlatformContext context;
  MenuApp app;
  int callsA = 0, callsB = 0;
  EmitterState emitterA, emitterB;
  emitterA.deferBind(&count, &callsA);
  emitterB.deferBind(&count, &callsB);
  // Each Scene owns its bar; each Window owns its native attachment.
  WindowProps propsA = props(), propsB = props();
  propsA.scene(new Scene(Boundary<MenuRoot>(MenuRootProps(0, "A", &emitterA))));
  propsB.scene(new Scene(Boundary<MenuRoot>(MenuRootProps(0, "B", &emitterB))));
  Win32Window a(&context, propsA), b(&context, propsB);
  a.setApp(&app);
  b.setApp(&app);
  show(a, true);
  show(b, true);
  // Exercise the production rail door: B's projection used to detach A.
  app.setActiveWindow(&a);
  const loka::app::MenuSources aSources = {0, a.scene()->menuBar(), a.scene()};
  app.projectMenu(&a, aSources);
  HMENU menuA = GetMenu(a.hwnd());
  LOKA_VERIFY(menuA);
  app.setActiveWindow(&b);
  const loka::app::MenuSources bSources = {0, b.scene()->menuBar(), b.scene()};
  app.projectMenu(&b, bSources);
  HMENU menuB = GetMenu(b.hwnd());
  LOKA_VERIFY(menuB && menuB != menuA);
  LOKA_VERIFY(GetMenu(a.hwnd()) == menuA && IsMenu(menuA));
  const UINT idA = firstCommand(a), idB = firstCommand(b);
  LOKA_VERIFY(idA == idB && "command ids are local to the receiving HWND");
  LOKA_VERIFY(PostMessageW(b.hwnd(), WM_COMMAND, MAKEWPARAM(idB, 0), 0));
  MSG message;
  LOKA_VERIFY(PeekMessageW(&message, b.hwnd(), WM_COMMAND, WM_COMMAND, PM_REMOVE));
  DispatchMessageW(&message);
  LOKA_VERIFY(callsA == 0 && callsB == 1);
  SendMessageW(a.hwnd(), WM_COMMAND, MAKEWPARAM(idA, 0), 0);
  LOKA_VERIFY(callsA == 1 && callsB == 1);
  show(b, false);
  LOKA_VERIFY(GetMenu(a.hwnd()) == menuA && IsMenu(menuA));
  show(a, false);
  emitterA.deferUnbind(&count, &callsA);
  emitterB.deferUnbind(&count, &callsB);
}

void testWin32AppShutdownReleasesMenuBeforeAttachmentDestruction()
{
  Observation observation;
  NullPlatformContext context;
  HMENU installed = NULL;
  HWND hwnd = NULL;
  {
    MenuApp app;
    Win32Window *window = new Win32Window(&context, sceneProps(observation));
    observation.window = window;
    observation.ownerAtShutdown = &app;
    window->setApp(&app);
    show(*window, true);
    app.own(window);
    LOKA_VERIFY(window->menuAttachment().project(window->scene()->menuBar(), window->scene()) == Win32MenuAttachment::PROJECT_APPLIED);
    observation.command = firstCommand(*window);
    LOKA_VERIFY(window->menuAttachment().dispatch(observation.command));
    LOKA_VERIFY(observation.calls == 1);
    installed = GetMenu(window->hwnd());
    hwnd = window->hwnd();
    app.setActiveWindow(0);
  }
  LOKA_VERIFY(observation.detaches == 1);
  LOKA_VERIFY(!IsMenu(installed));
  LOKA_VERIFY(!IsWindow(hwnd));
}

namespace
{
  // TranslateAccelerator reads the calling thread's keyboard state. Preserve
  // that state around synthetic MSGs without depending on physical key timing.
  class ControlKey
  {
  public:
    ControlKey()
    {
      LOKA_VERIFY(GetKeyboardState(this->saved_));
      BYTE state[256] = {0};
      state[VK_CONTROL] = 0x80;
      LOKA_VERIFY(SetKeyboardState(state));
    }
    ~ControlKey() { LOKA_VERIFY(SetKeyboardState(this->saved_)); }
    void release()
    {
      BYTE state[256] = {0};
      LOKA_VERIFY(SetKeyboardState(state));
    }
  private:
    BYTE saved_[256];
  };

  MSG keyMessage(HWND hwnd, WPARAM key)
  {
    MSG msg = {};
    msg.hwnd = hwnd;
    msg.message = WM_KEYDOWN;
    msg.wParam = key;
    msg.lParam = 1;
    return msg;
  }

  void pumpCommands(HWND hwnd)
  {
    // TranslateAccelerator sends WM_COMMAND synchronously; drain posted
    // commands too so an accidental duplicate cannot hide in the queue.
    MSG msg;
    while (PeekMessageW(&msg, hwnd, WM_COMMAND, WM_COMMAND, PM_REMOVE))
      DispatchMessageW(&msg);
  }

  MenuBarDefinition shortcutBar(const char *title, char key, EmitterState *emitter)
  {
    MenuBarDefinition result;
    result << (Menu("File") << MenuItem(title).shortcut(key).onClick(emitter));
    return result;
  }

  void verifyLabel(Win32Window &window, const wchar_t *expected)
  {
    wchar_t label[128];
    LOKA_VERIFY(GetMenuStringW(GetSubMenu(GetMenu(window.hwnd()), 0), 0,
                              label, 128, MF_BYPOSITION) > 0);
    LOKA_VERIFY(std::wcscmp(label, expected) == 0);
  }

  struct ShortcutFixture
  {
    NullPlatformContext context;
    MenuApp app;
    int calls;
    EmitterState emitter;
    Win32Window window;
    ShortcutFixture() : calls(0), window(&this->context, props())
    {
      this->emitter.deferBind(&count, &this->calls);
      this->window.setApp(&this->app);
      show(this->window, true);
    }
    ~ShortcutFixture()
    {
      show(this->window, false);
      this->emitter.deferUnbind(&count, &this->calls);
    }
    void project(char key)
    {
      MenuBarDefinition offered = shortcutBar("Save", key, &this->emitter);
      LOKA_VERIFY(this->window.menuAttachment().project(&offered, 0) == Win32MenuAttachment::PROJECT_APPLIED);
    }
    bool translate(WPARAM key)
    {
      MSG msg = keyMessage(this->window.hwnd(), key);
      const bool consumed = this->window.menuAttachment().translateAccelerator(msg);
      pumpCommands(this->window.hwnd());
      return consumed;
    }
  };
}

void testWin32MenuAcceleratorTranslatesDeclaredShortcut()
{
  ShortcutFixture f;
  f.project('s');
  verifyLabel(f.window, L"Save\tCtrl+S");
  ControlKey control;
  LOKA_VERIFY(f.translate('S'));
  LOKA_VERIFY(f.calls == 1);
  control.release();
  LOKA_VERIFY(!f.translate('S'));
  LOKA_VERIFY(f.calls == 1);
  ControlKey held;
  MenuBarDefinition nested;
  nested << (Menu("File")
      << (MenuItem("Parent").shortcut('P') << MenuItem("Digit").shortcut('7').onClick(&f.emitter))
      << MenuItem("Zero").shortcut(0)
      << MenuItem("Separator").separator().shortcut('X'));
  LOKA_VERIFY(f.window.menuAttachment().project(&nested, 0) == Win32MenuAttachment::PROJECT_APPLIED);
  verifyLabel(f.window, L"Parent");
  LOKA_VERIFY(!f.translate('P') && !f.translate('X') && !f.translate('S'));
  LOKA_VERIFY(f.translate('7'));
  LOKA_VERIFY(f.calls == 2);
  f.project(0);
  verifyLabel(f.window, L"Save");
  LOKA_VERIFY(!f.translate('7'));
}

void testWin32QuitHasNoDefaultAccelerator()
{
  ShortcutFixture f;
  MenuBarDefinition plain;
  plain << (Menu("File") << MenuItem("Exit").actionType(MENU_ACTION_QUIT_APP));
  LOKA_VERIFY(f.window.menuAttachment().project(&plain, 0) == Win32MenuAttachment::PROJECT_APPLIED);
  verifyLabel(f.window, L"Exit");
  ControlKey control;
  LOKA_VERIFY(!f.translate('Q'));
  LOKA_VERIFY(f.app.quits == 0);
  MenuBarDefinition explicitKey;
  explicitKey << (Menu("File") << MenuItem("Exit").actionType(MENU_ACTION_QUIT_APP).shortcut('Q'));
  LOKA_VERIFY(f.window.menuAttachment().project(&explicitKey, 0) == Win32MenuAttachment::PROJECT_APPLIED);
  verifyLabel(f.window, L"Exit\tCtrl+Q");
  LOKA_VERIFY(f.translate('Q'));
  LOKA_VERIFY(f.app.quits == 1);
}

void testWin32MenuAcceleratorInertAfterRelease()
{
  Mounted f;
  ControlKey control;
  MSG msg = keyMessage(f.window.hwnd(), 'R');
  LOKA_VERIFY(f.window.menuAttachment().translateAccelerator(msg));
  pumpCommands(f.window.hwnd());
  LOKA_VERIFY(f.observation.calls == 1);
  f.window.menuAttachment().releaseFrom(f.window.scene());
  LOKA_VERIFY(f.window.menuAttachment().translateAccelerator(msg));
  pumpCommands(f.window.hwnd());
  LOKA_VERIFY(f.observation.calls == 1);
}

void testWin32MenuAcceleratorSwapsWithProjection()
{
  ShortcutFixture f;
  f.project('S');
  ControlKey control;
  LOKA_VERIFY(f.translate('S'));
  f.project('N');
  LOKA_VERIFY(!f.translate('S'));
  LOKA_VERIFY(f.translate('N'));
  LOKA_VERIFY(f.calls == 2);
  const DWORD resources = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
  LOKA_VERIFY(resources > 0);
  for (int i = 0; i < 32; ++i)
    f.project(i % 2 ? 'N' : 'S');
  const DWORD afterSwaps = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
  LOKA_VERIFY(afterSwaps > 0 && afterSwaps <= resources);
  show(f.window, false);
  LOKA_VERIFY(!f.translate('N'));
  show(f.window, true);
  f.project('N');
  LOKA_VERIFY(f.translate('N'));
  LOKA_VERIFY(f.calls == 3);
  LOKA_VERIFY(f.window.menuAttachment().project(0, 0) == Win32MenuAttachment::PROJECT_APPLIED);
  LOKA_VERIFY(!f.translate('N'));
  // The external WM_DESTROY route also releases the accelerator table.
  f.project('S');
  f.window.setApp(0);
  LOKA_VERIFY(DestroyWindow(f.window.hwnd()));
  LOKA_VERIFY(!f.translate('S'));
}

void testWin32TwoWindowsOwnTheirAccelerators()
{
  ShortcutFixture a, b;
  a.project('S');
  b.project('S');
  ControlKey control;
  MSG msg = keyMessage(a.window.hwnd(), 'S');
  MenuApp::TranslateOrDispatch(msg);
  pumpCommands(a.window.hwnd());
  LOKA_VERIFY(a.calls == 1 && b.calls == 0);
  msg = keyMessage(b.window.hwnd(), 'S');
  MenuApp::TranslateOrDispatch(msg);
  pumpCommands(b.window.hwnd());
  LOKA_VERIFY(a.calls == 1 && b.calls == 1);
}

namespace
{
  struct EditObservation
  {
    WNDPROC previous;
    int keydowns;
    int dialogQueries;
    EditObservation() : previous(0), keydowns(0), dialogQueries(0) {}
  };

  LRESULT CALLBACK ObserveEdit(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
  {
    EditObservation *observation = reinterpret_cast<EditObservation *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_KEYDOWN)
      ++observation->keydowns;
    if (message == WM_GETDLGCODE)
      ++observation->dialogQueries;
    return CallWindowProcW(observation->previous, hwnd, message, wParam, lParam);
  }
}

void testWin32MenuAcceleratorPrecedesDialogAndDispatch()
{
  ShortcutFixture f;
  f.project('V');
  EditObservation observation;
  HWND edit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                               0, 0, 100, 24, f.window.hwnd(), NULL, GetModuleHandleW(NULL), NULL);
  HWND next = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                               0, 30, 100, 24, f.window.hwnd(), NULL, GetModuleHandleW(NULL), NULL);
  LOKA_VERIFY(edit && next);
  SetWindowLongPtrW(edit, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&observation));
  observation.previous = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(edit, GWLP_WNDPROC,
                                                    reinterpret_cast<LONG_PTR>(&ObserveEdit)));
  LOKA_VERIFY(observation.previous);
  LOKA_VERIFY(Win32Window::FromHwnd(f.window.hwnd()) == &f.window);
  // A foreign class with non-null userdata must never be cast to Win32Window.
  LOKA_VERIFY(!Win32Window::FromHwnd(edit));
  SetFocus(edit);
  ControlKey control;
  MSG msg = keyMessage(edit, 'V');
  MenuApp::TranslateOrDispatch(msg);
  pumpCommands(f.window.hwnd());
  LOKA_VERIFY(f.calls == 1);
  LOKA_VERIFY(observation.keydowns == 0 && observation.dialogQueries == 0);
  control.release();
  msg = keyMessage(edit, VK_F8);
  MenuApp::TranslateOrDispatch(msg);
  LOKA_VERIFY(observation.keydowns == 1);
  const int queries = observation.dialogQueries;
  msg = keyMessage(edit, VK_TAB);
  MenuApp::TranslateOrDispatch(msg);
  LOKA_VERIFY(observation.dialogQueries > queries);
  LOKA_VERIFY(observation.keydowns == 1);
  LOKA_VERIFY(GetFocus() == next);
  SetWindowLongPtrW(edit, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(observation.previous));
  SetWindowLongPtrW(edit, GWLP_USERDATA, 0);
  LOKA_VERIFY(DestroyWindow(next));
  LOKA_VERIFY(DestroyWindow(edit));
}

namespace
{
  class MenuTitleState : public MutableState<String>
  {
  public:
    explicit MenuTitleState(const char *value) : MutableState<String>(String::Literal(value)) {}
    size_t subscriptions() const { return this->deferredHandlers.size(); }
  };
}

void testWin32MenuTitleFollowsState()
{
  MenuTitleState state("Alpha");
  PushStateTracker tracker;
  tracker.addState(&state);
  ShortcutFixture f;
  MenuBarDefinition offered;
  offered << (Menu("File") << MenuItem("Alpha").text(&state).shortcut('S').onClick(&f.emitter));
  LOKA_VERIFY(f.window.menuAttachment().project(&offered, 0) == Win32MenuAttachment::PROJECT_APPLIED);
  verifyLabel(f.window, L"Alpha\tCtrl+S");
  const UINT command = firstCommand(f.window);
  ControlKey control;
  LOKA_VERIFY(f.translate('S'));
  LOKA_VERIFY(f.calls == 1);
  {
    StateTrackerGuard guard(&tracker);
    state.set(String::Literal("Beta"));
  }
  verifyLabel(f.window, L"Beta\tCtrl+S");
  LOKA_VERIFY(firstCommand(f.window) == command);
  LOKA_VERIFY(f.translate('S'));
  LOKA_VERIFY(f.calls == 2);
  // A fresh projection must read the current State, not the fallback title.
  LOKA_VERIFY(f.window.menuAttachment().project(0, 0) == Win32MenuAttachment::PROJECT_APPLIED);
  LOKA_VERIFY(f.window.menuAttachment().project(&offered, 0) == Win32MenuAttachment::PROJECT_APPLIED);
  verifyLabel(f.window, L"Beta\tCtrl+S");
}

void testWin32MenuPopupTitleFollowsState()
{
  MenuTitleState state("Alpha");
  PushStateTracker tracker;
  tracker.addState(&state);
  ShortcutFixture f;
  MenuBarDefinition offered;
  offered << (Menu("File") << (MenuItem("Fallback").text(&state).shortcut('P')
      << MenuItem("Child").shortcut('S').onClick(&f.emitter)));
  LOKA_VERIFY(f.window.menuAttachment().project(&offered, 0) == Win32MenuAttachment::PROJECT_APPLIED);
  verifyLabel(f.window, L"Alpha");
  const HMENU menu = GetSubMenu(GetMenu(f.window.hwnd()), 0);
  const HMENU child = GetSubMenu(menu, 0);
  LOKA_VERIFY(child);
  {
    StateTrackerGuard guard(&tracker);
    state.set(String::Literal("Beta"));
  }
  verifyLabel(f.window, L"Beta");
  LOKA_VERIFY(GetSubMenu(menu, 0) == child);
  ControlKey control;
  LOKA_VERIFY(!f.translate('P'));
  LOKA_VERIFY(f.translate('S'));
  LOKA_VERIFY(f.calls == 1);
}

void testWin32MenuTitleStateUnbindsOnRelease()
{
  MenuTitleState state("Alpha");
  PushStateTracker tracker;
  tracker.addState(&state);
  ShortcutFixture f;
  Scene source((Boundary<MenuRoot>(MenuRootProps())));
  MenuBarDefinition offered;
  offered << (Menu("File") << (MenuItem("Fallback").text(&state)
      << MenuItem("Fallback").text(&state).shortcut('S')));
  LOKA_VERIFY(f.window.menuAttachment().project(&offered, &source) == Win32MenuAttachment::PROJECT_APPLIED);
  const HMENU installed = GetMenu(f.window.hwnd());
  const HMENU child = GetSubMenu(GetSubMenu(installed, 0), 0);
  LOKA_VERIFY(state.subscriptions() == 2);
  {
    StateTrackerGuard guard(&tracker);
    state.set(String::Literal("Beta"));
  }
  verifyLabel(f.window, L"Beta");
  wchar_t label[128];
  LOKA_VERIFY(GetMenuStringW(child, 0, label, 128, MF_BYPOSITION) > 0);
  LOKA_VERIFY(std::wcscmp(label, L"Beta\tCtrl+S") == 0);
  // Bindings deliver inside set() today (the commit-time delivery is a
  // recorded framework follow-up), so release outside a transaction and pin
  // only that a later write reaches nothing.
  f.window.menuAttachment().releaseFrom(&source);
  LOKA_VERIFY(state.subscriptions() == 0);
  {
    StateTrackerGuard guard(&tracker);
    state.set(String::Literal("Gamma"));
  }
  LOKA_VERIFY(GetMenu(f.window.hwnd()) == installed && IsMenu(installed));
  verifyLabel(f.window, L"Beta");
  LOKA_VERIFY(GetMenuStringW(child, 0, label, 128, MF_BYPOSITION) > 0);
  LOKA_VERIFY(std::wcscmp(label, L"Beta\tCtrl+S") == 0);
}
