#include "Win32TextEditorAccess.hpp"
#include "support/TextBufferFixture.hpp"
#include "support/Headless.hpp"
#include "support/LifecycleFactTestAccess.hpp"
#include "context/Win32FocusParticipant.hpp"
#include "app/nodes/nestable/RowColumn.hpp"
#include "app/nodes/nestable/Show.hpp"
#include <cstring>
#include "Win32MenuAttachmentTests.hpp"
#include <cstdio>
#include <cwchar>
#include <functional>
#include <new>
#include "core/util/OwnedDef.hpp"
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

  // Mirror the Toolbox fixture: standalone bars are bases, Scene bars overlays.
  MenuSources sources(const MenuBarDefinition *bar, const Scene *scene)
  {
    const MenuSources result = {scene ? 0 : bar, scene ? bar : 0, scene};
    return result;
  }

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
      LOKA_VERIFY(this->window.menuAttachment().project(sources(this->window.scene()->menuBar(), this->window.scene())) == Win32MenuAttachment::PROJECT_APPLIED);
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

void testWin32MenuAttachmentProjectsOncePerIdentity()
{
  NullPlatformContext context;
  Win32Window window(&context, props());
  show(window, true);
  Frame before;
  LOKA_VERIFY(window.queryNativeContentFrame(before));
  MenuBarDefinition first = bar("Open"), equal = bar("Open"), changed = bar("Save");
  LOKA_VERIFY(window.menuAttachment().project(sources(&first, 0)) == Win32MenuAttachment::PROJECT_APPLIED);
  HMENU installed = GetMenu(window.hwnd());
  LOKA_VERIFY(installed && IsMenu(installed));
  verifyFrame(window, before);
  loka::app::testing::failMenuBarDefinitionClones(1);
  LOKA_VERIFY(window.menuAttachment().project(sources(&first, 0)) == Win32MenuAttachment::PROJECT_UNCHANGED);
  LOKA_VERIFY(GetMenu(window.hwnd()) == installed);
  // A direct clone consumes the failure left armed by the unchanged offer.
  OwnedDef<MenuBarDefinition> refused(first.clone());
  LOKA_VERIFY(!refused.isSet());
  OwnedDef<MenuBarDefinition> accepted(first.clone());
  LOKA_VERIFY(accepted.isSet());
  // Equal structure at a distinct address now applies, unlike the old snapshot.
  LOKA_VERIFY(window.menuAttachment().project(sources(&equal, 0)) == Win32MenuAttachment::PROJECT_APPLIED);
  LOKA_VERIFY(GetMenu(window.hwnd()) != installed);
  installed = GetMenu(window.hwnd());
  LOKA_VERIFY(window.menuAttachment().project(sources(&changed, 0)) == Win32MenuAttachment::PROJECT_APPLIED);
  LOKA_VERIFY(GetMenu(window.hwnd()) != installed);
  LOKA_VERIFY(!IsMenu(installed));
  verifyFrame(window, before);
  LOKA_VERIFY(window.menuAttachment().project(sources(0, 0)) == Win32MenuAttachment::PROJECT_APPLIED);
  LOKA_VERIFY(!GetMenu(window.hwnd()));
  verifyFrame(window, before);
  // Completion re-offers every tail: absent over absent is unchanged.
  LOKA_VERIFY(window.menuAttachment().project(sources(0, 0)) == Win32MenuAttachment::PROJECT_UNCHANGED);
  // Equal offers after native recreation must rebuild, never reuse a dead HMENU.
  LOKA_VERIFY(window.menuAttachment().project(sources(&changed, 0)) == Win32MenuAttachment::PROJECT_APPLIED);
  show(window, false);
  show(window, true);
  LOKA_VERIFY(window.menuAttachment().project(sources(&changed, 0)) == Win32MenuAttachment::PROJECT_APPLIED);
  LOKA_VERIFY(GetMenu(window.hwnd()) && IsMenu(GetMenu(window.hwnd())));
  show(window, false);
}

void testWin32MenuAttachmentRetriesRefusedOffer()
{
  NullPlatformContext context;
  Win32Window window(&context, props());
  show(window, true);
  MenuBarDefinition first = bar("Open"), second = bar("Save");
  LOKA_VERIFY(window.menuAttachment().project(sources(&first, 0)) == Win32MenuAttachment::PROJECT_APPLIED);
  const HMENU installed = GetMenu(window.hwnd());
  Frame before;
  LOKA_VERIFY(window.queryNativeContentFrame(before));
  loka::app::testing::failMenuBarDefinitionClones(1);
  LOKA_VERIFY(window.menuAttachment().project(sources(&second, 0)) == Win32MenuAttachment::PROJECT_REFUSED);
  LOKA_VERIFY(GetMenu(window.hwnd()) == installed && IsMenu(installed));
  verifyFrame(window, before);
  loka::app::testing::allowMenuBarDefinitionClones();
  // Refusal kept the old record as well as the installed native menu.
  LOKA_VERIFY(window.menuAttachment().project(sources(&first, 0)) == Win32MenuAttachment::PROJECT_UNCHANGED);
  LOKA_VERIFY(window.menuAttachment().project(sources(&second, 0)) == Win32MenuAttachment::PROJECT_APPLIED);
  LOKA_VERIFY(GetMenu(window.hwnd()) != installed);
  LOKA_VERIFY(window.menuAttachment().project(sources(&second, 0)) == Win32MenuAttachment::PROJECT_UNCHANGED);
  show(window, false);
}

void testWin32MenuAttachmentReleaseThenAbsentRemovesMenu()
{
  Mounted f;
  f.window.menuAttachment().releaseFrom(f.window.scene());
  LOKA_VERIFY(GetMenu(f.window.hwnd()));
  LOKA_VERIFY(f.window.menuAttachment().project(sources(0, 0)) == Win32MenuAttachment::PROJECT_APPLIED);
  LOKA_VERIFY(GetMenu(f.window.hwnd()) == NULL);
  LOKA_VERIFY(f.window.menuAttachment().project(sources(0, 0)) == Win32MenuAttachment::PROJECT_UNCHANGED);
  // MenuRoot's detach observation expects a native bar; restore before teardown.
  LOKA_VERIFY(f.window.menuAttachment().project(sources(f.window.scene()->menuBar(), f.window.scene())) == Win32MenuAttachment::PROJECT_APPLIED);
}

void testWin32MenuAttachmentAbsentClearsEmptyProjection()
{
  NullPlatformContext context;
  Win32Window window(&context, props());
  show(window, true);
  MenuBarDefinition empty;
  LOKA_VERIFY(window.menuAttachment().project(sources(&empty, 0)) == Win32MenuAttachment::PROJECT_APPLIED);
  LOKA_VERIFY(GetMenu(window.hwnd()) == NULL);
  LOKA_VERIFY(window.menuAttachment().project(sources(0, 0)) == Win32MenuAttachment::PROJECT_UNCHANGED);
  LOKA_VERIFY(window.menuAttachment().project(sources(&empty, 0)) == Win32MenuAttachment::PROJECT_APPLIED);
  show(window, false);
}

void testWin32MenuAttachmentDisconnectClearsIdentity()
{
  Mounted f;
  const MenuSources offer = sources(f.window.scene()->menuBar(), f.window.scene());
  f.window.menuAttachment().disconnect();
  LOKA_VERIFY(!f.window.menuAttachment().dispatch(f.observation.command));
  LOKA_VERIFY(f.window.menuAttachment().project(offer) == Win32MenuAttachment::PROJECT_APPLIED);
  LOKA_VERIFY(f.window.menuAttachment().dispatch(f.observation.command));
  LOKA_VERIFY(f.observation.calls == 1);
}

void testWin32MenuAttachmentAppDetachClearsIdentity()
{
  NullPlatformContext context;
  MenuApp app;
  Win32Window window(&context, props());
  window.setApp(&app);
  show(window, true);
  MenuBarDefinition base = bar("Open");
  LOKA_VERIFY(window.menuAttachment().project(sources(&base, 0)) == Win32MenuAttachment::PROJECT_APPLIED);
  const HMENU installed = GetMenu(window.hwnd());
  window.setApp(0);
  LOKA_VERIFY(GetMenu(window.hwnd()) == installed && IsMenu(installed));
  LOKA_VERIFY(window.menuAttachment().project(sources(&base, 0)) == Win32MenuAttachment::PROJECT_APPLIED);
  show(window, false);
}

void testWin32AppMenuReofferSkipsMerge()
{
  NullPlatformContext context;
  MenuApp app;
  Win32Window window(&context, props());
  window.setApp(&app);
  show(window, true);
  MenuBarDefinition base = bar("Open");
  const MenuSources offer = sources(&base, 0);
  app.projectMenu(&window, offer);
  const HMENU installed = GetMenu(window.hwnd());
  LOKA_VERIFY(installed);
  loka::app::testing::failMenuBarDefinitionClones(1);
  app.projectMenu(&window, offer);
  LOKA_VERIFY(GetMenu(window.hwnd()) == installed);
  OwnedDef<MenuBarDefinition> refused(base.clone());
  LOKA_VERIFY(!refused.isSet());
  OwnedDef<MenuBarDefinition> accepted(base.clone());
  LOKA_VERIFY(accepted.isSet());
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
  // Keep the same overlay address across two fresh Scene compositions.
  MenuBarDefinition reused(*f.window.scene()->menuBar());
  LOKA_VERIFY(f.window.menuAttachment().project(sources(&reused, f.window.scene())) == Win32MenuAttachment::PROJECT_APPLIED);
  HMENU installed = GetMenu(f.window.hwnd());
  LOKA_VERIFY(f.window.menuAttachment().dispatch(f.observation.command));
  LOKA_VERIFY(f.observation.calls == 1);
  loka::dsl::testing::SceneTestAccess::unmount(*f.window.scene());
  LOKA_VERIFY(f.observation.detaches == 1);
  LOKA_VERIFY(!f.window.menuAttachment().dispatch(f.observation.command));
  LOKA_VERIFY(GetMenu(f.window.hwnd()) == installed);
  reused.~MenuBarDefinition();
  const loka::win32::Win32DisplayScale scale(
      loka::win32::Win32DisplayScale::forWindow(f.window.hwnd()).dpi(),
      loka::win32::DefaultRailMetrics());
  Win32ScenePlatformController controller(f.window.hwnd(), scale, &f.context);
  LOKA_VERIFY(f.window.scene()->mount(&controller));
  LOKA_VERIFY(f.window.scene()->menuBar());
  new (&reused) MenuBarDefinition(*f.window.scene()->menuBar());
  LOKA_VERIFY(f.window.menuAttachment().project(sources(&reused, f.window.scene())) == Win32MenuAttachment::PROJECT_APPLIED);
  LOKA_VERIFY(GetMenu(f.window.hwnd()) != installed);
  LOKA_VERIFY(f.window.menuAttachment().dispatch(f.observation.command));
  LOKA_VERIFY(f.observation.calls == 2);
  loka::dsl::testing::SceneTestAccess::unmount(*f.window.scene());
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
    LOKA_VERIFY(window->menuAttachment().project(sources(window->scene()->menuBar(), window->scene())) == Win32MenuAttachment::PROJECT_APPLIED);
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
    OwnedDef<MenuBarDefinition> offered;
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
      OwnedDef<MenuBarDefinition> next(new MenuBarDefinition(shortcutBar("Save", key, &this->emitter)));
      LOKA_VERIFY(this->window.menuAttachment().project(sources(next.get(), 0)) == Win32MenuAttachment::PROJECT_APPLIED);
      this->offered.reset(next.take());
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
  LOKA_VERIFY(f.window.menuAttachment().project(sources(&nested, 0)) == Win32MenuAttachment::PROJECT_APPLIED);
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
  LOKA_VERIFY(f.window.menuAttachment().project(sources(&plain, 0)) == Win32MenuAttachment::PROJECT_APPLIED);
  verifyLabel(f.window, L"Exit");
  ControlKey control;
  LOKA_VERIFY(!f.translate('Q'));
  LOKA_VERIFY(f.app.quits == 0);
  MenuBarDefinition explicitKey;
  explicitKey << (Menu("File") << MenuItem("Exit").actionType(MENU_ACTION_QUIT_APP).shortcut('Q'));
  LOKA_VERIFY(f.window.menuAttachment().project(sources(&explicitKey, 0)) == Win32MenuAttachment::PROJECT_APPLIED);
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
  LOKA_VERIFY(f.window.menuAttachment().project(sources(0, 0)) == Win32MenuAttachment::PROJECT_APPLIED);
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
  LOKA_VERIFY(f.window.menuAttachment().project(sources(&offered, 0)) == Win32MenuAttachment::PROJECT_APPLIED);
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
  LOKA_VERIFY(f.window.menuAttachment().project(sources(0, 0)) == Win32MenuAttachment::PROJECT_APPLIED);
  LOKA_VERIFY(f.window.menuAttachment().project(sources(&offered, 0)) == Win32MenuAttachment::PROJECT_APPLIED);
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
  LOKA_VERIFY(f.window.menuAttachment().project(sources(&offered, 0)) == Win32MenuAttachment::PROJECT_APPLIED);
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
  LOKA_VERIFY(f.window.menuAttachment().project(sources(&offered, &source)) == Win32MenuAttachment::PROJECT_APPLIED);
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

namespace
{
  void verifyMenuLabel(HMENU menu, UINT position, const wchar_t *expected)
  {
    wchar_t label[128];
    LOKA_VERIFY(GetMenuStringW(menu, position, label, 128, MF_BYPOSITION) > 0);
    LOKA_VERIFY(std::wcscmp(label, expected) == 0);
  }
} // namespace

void testWin32AppQuitPlacement()
{
  ShortcutFixture f;
  MenuBarDefinition only;
  only << (AppMenu() << MenuItem("About").actionType(MENU_ACTION_ABOUT_APP) << MenuSeparator()
                     << MenuItem("Quit").actionType(MENU_ACTION_QUIT_APP));
  LOKA_VERIFY(f.window.menuAttachment().project(sources(&only, 0)) == Win32MenuAttachment::PROJECT_APPLIED);
  HMENU menu = GetMenu(f.window.hwnd());
  LOKA_VERIFY(GetMenuItemCount(menu) == 1);
  verifyMenuLabel(menu, 0, L"File");
  LOKA_VERIFY(GetMenuItemCount(GetSubMenu(menu, 0)) == 1);
  verifyLabel(f.window, L"Quit");
  ControlKey control;
  LOKA_VERIFY(!f.translate('Q'));
  LOKA_VERIFY(f.window.menuAttachment().dispatch(firstCommand(f.window)));
  LOKA_VERIFY(f.app.quits == 1);

  MenuBarDefinition view;
  view << (AppMenu() << MenuItem("Quit").actionType(MENU_ACTION_QUIT_APP)) << (Menu("View") << MenuItem("Zoom"));
  LOKA_VERIFY(f.window.menuAttachment().project(sources(&view, 0)) == Win32MenuAttachment::PROJECT_APPLIED);
  menu = GetMenu(f.window.hwnd());
  LOKA_VERIFY(GetMenuItemCount(menu) == 2);
  verifyMenuLabel(menu, 0, L"File");
  verifyMenuLabel(menu, 1, L"View");
}

void testWin32AppQuitSuppressesOrdinaryLeaves()
{
  MenuTitleState suppressed("Ordinary"), parent("Parent");
  ShortcutFixture f;
  MenuBarDefinition offered;
  offered << (AppMenu() << MenuItem("Quit").actionType(MENU_ACTION_QUIT_APP))
          << (Menu("File") << MenuItem("Open") << MenuSeparator()
                           << MenuItem("Ordinary").text(&suppressed).shortcut('Q').actionType(MENU_ACTION_QUIT_APP));
  LOKA_VERIFY(f.window.menuAttachment().project(sources(&offered, 0)) == Win32MenuAttachment::PROJECT_APPLIED);
  HMENU file = GetSubMenu(GetMenu(f.window.hwnd()), 0);
  LOKA_VERIFY(GetMenuItemCount(file) == 3);
  verifyMenuLabel(file, 0, L"Open");
  LOKA_VERIFY(GetMenuState(file, 1, MF_BYPOSITION) & MF_SEPARATOR);
  verifyMenuLabel(file, 2, L"Quit");
  LOKA_VERIFY(suppressed.subscriptions() == 0);
  LOKA_VERIFY(!f.window.menuAttachment().dispatch(1002));
  ControlKey control;
  LOKA_VERIFY(!f.translate('Q'));

  MenuBarDefinition nested;
  nested << (AppMenu() << MenuItem("Quit").actionType(MENU_ACTION_QUIT_APP))
         << (Menu("File") << MenuSeparator() << MenuSeparator() << MenuItem("Open") << MenuSeparator()
                          << (MenuItem("Empty") << MenuItem("Drop").actionType(MENU_ACTION_QUIT_APP))
                          << MenuItem("Color").actionType(MENU_ACTION_SHOW_COLOR_PICKER) << MenuSeparator()
                          << (MenuItem("Parent").text(&parent).actionType(MENU_ACTION_QUIT_APP)
                              << MenuSeparator()
                              << MenuItem("Drop").text(&suppressed).shortcut('Q').actionType(MENU_ACTION_QUIT_APP)
                              << MenuSeparator() << MenuItem("Child") << MenuSeparator())
                          << MenuSeparator() << MenuSeparator());
  LOKA_VERIFY(f.window.menuAttachment().project(sources(&nested, 0)) == Win32MenuAttachment::PROJECT_APPLIED);
  file = GetSubMenu(GetMenu(f.window.hwnd()), 0);
  LOKA_VERIFY(GetMenuItemCount(file) == 5);
  verifyMenuLabel(file, 0, L"Open");
  LOKA_VERIFY(GetMenuState(file, 1, MF_BYPOSITION) & MF_SEPARATOR);
  LOKA_VERIFY(GetMenuState(file, 3, MF_BYPOSITION) & MF_SEPARATOR);
  verifyMenuLabel(file, 4, L"Quit");
  HMENU child = GetSubMenu(file, 2);
  LOKA_VERIFY(child && GetMenuItemCount(child) == 1);
  verifyMenuLabel(child, 0, L"Child");
  parent.set(String::Literal("Renamed"));
  verifyMenuLabel(file, 2, L"Renamed");
  LOKA_VERIFY(GetSubMenu(file, 2) == child);
  LOKA_VERIFY(suppressed.subscriptions() == 0);
  LOKA_VERIFY(!f.translate('Q'));
  LOKA_VERIFY(!f.window.menuAttachment().dispatch(1003));
}

void testWin32AppQuitTitleFallbackAndPayload()
{
  MenuTitleState title("");
  MutableState<bool> enabled(false), checked(true);
  ShortcutFixture f;
  MenuBarDefinition offered;
  offered << (AppMenu() << MenuItem("Ignored")
                               .text(&title)
                               .shortcut('L')
                               .enabled(&enabled)
                               .attr(MenuItemAttr().checked(&checked))
                               .actionType(MENU_ACTION_QUIT_APP));
  LOKA_VERIFY(f.window.menuAttachment().project(sources(&offered, 0)) == Win32MenuAttachment::PROJECT_APPLIED);
  verifyLabel(f.window, L"Exit\tCtrl+L");
  const UINT id = firstCommand(f.window);
  HMENU file = GetSubMenu(GetMenu(f.window.hwnd()), 0);
  LOKA_VERIFY(GetMenuState(file, id, MF_BYCOMMAND) & MF_GRAYED);
  LOKA_VERIFY(GetMenuState(file, id, MF_BYCOMMAND) & MF_CHECKED);
  enabled.set(true);
  checked.set(false);
  LOKA_VERIFY(!(GetMenuState(file, id, MF_BYCOMMAND) & (MF_GRAYED | MF_CHECKED)));
  ControlKey control;
  LOKA_VERIFY(f.translate('L'));
  title.set(String::Literal("Leave"));
  verifyLabel(f.window, L"Leave\tCtrl+L");
  LOKA_VERIFY(f.translate('L'));
  title.set(String::Literal(""));
  verifyLabel(f.window, L"Exit\tCtrl+L");
  LOKA_VERIFY(firstCommand(f.window) == id);
  LOKA_VERIFY(f.translate('L'));
  LOKA_VERIFY(f.app.quits == 3);

  MenuBarDefinition about;
  about << (Menu("Help") << MenuItem("Ignored").text(&title).actionType(MENU_ACTION_ABOUT_APP));
  LOKA_VERIFY(f.window.menuAttachment().project(sources(&about, 0)) == Win32MenuAttachment::PROJECT_APPLIED);
  verifyLabel(f.window, L"About");
  title.set(String::Literal("Details"));
  verifyLabel(f.window, L"Details");
  title.set(String::Literal(""));
  verifyLabel(f.window, L"About");
}

void testWin32InvisibleAppQuitKeepsOrdinaryQuit()
{
  ShortcutFixture f;
  MenuBarDefinition offered;
  offered << (AppMenu() << MenuItem("App Quit").attr(MenuItemAttr().visible(false)).actionType(MENU_ACTION_QUIT_APP))
          << (Menu("File") << MenuItem("Ordinary Quit").shortcut('Q').actionType(MENU_ACTION_QUIT_APP));
  LOKA_VERIFY(f.window.menuAttachment().project(sources(&offered, 0)) == Win32MenuAttachment::PROJECT_APPLIED);
  verifyLabel(f.window, L"Ordinary Quit\tCtrl+Q");
  LOKA_VERIFY(GetMenuItemCount(GetSubMenu(GetMenu(f.window.hwnd()), 0)) == 1);
  ControlKey control;
  LOKA_VERIFY(f.translate('Q'));
  LOKA_VERIFY(f.app.quits == 1);
}

void testWin32AppQuitRefusalPreservesProjection()
{
  MenuTitleState title("Save");
  ShortcutFixture f;
  MenuBarDefinition first;
  first << (Menu("File") << MenuItem("Save").text(&title).shortcut('S').onClick(&f.emitter));
  LOKA_VERIFY(f.window.menuAttachment().project(sources(&first, 0)) == Win32MenuAttachment::PROJECT_APPLIED);
  HMENU installed = GetMenu(f.window.hwnd());
  MenuBarDefinition next;
  next << (AppMenu() << MenuItem("Quit").actionType(MENU_ACTION_QUIT_APP));
  // There is no native CreatePopupMenu/AppendMenuW injection seam. Exercise
  // the existing merge-refusal seam without introducing a production test API.
  loka::app::testing::failMenuBarDefinitionClones(1);
  LOKA_VERIFY(f.window.menuAttachment().project(sources(&next, 0)) == Win32MenuAttachment::PROJECT_REFUSED);
  loka::app::testing::allowMenuBarDefinitionClones();
  LOKA_VERIFY(GetMenu(f.window.hwnd()) == installed && IsMenu(installed));
  LOKA_VERIFY(f.window.menuAttachment().project(sources(&first, 0)) == Win32MenuAttachment::PROJECT_UNCHANGED);
  title.set(String::Literal("Retained"));
  verifyLabel(f.window, L"Retained\tCtrl+S");
  LOKA_VERIFY(title.subscriptions() == 1);
  ControlKey control;
  LOKA_VERIFY(f.translate('S') && f.calls == 1);
  LOKA_VERIFY(f.window.menuAttachment().project(sources(&next, 0)) == Win32MenuAttachment::PROJECT_APPLIED);
  LOKA_VERIFY(title.subscriptions() == 0);
  LOKA_VERIFY(!f.translate('S'));
  verifyLabel(f.window, L"Quit");
}

void testWin32EditMenuDefaultsAndFallback()
{
  ShortcutFixture f;
  MenuDefinition edit = EditMenu();
  for (MenuItemDefinition *item = edit.itemsHead(); item; item = item->nextInComposition)
    if (!item->isSeparator)
      item->onClick(&f.emitter);
  MenuBarDefinition offered;
  offered << edit;
  LOKA_VERIFY(f.window.menuAttachment().project(sources(&offered, 0)) == Win32MenuAttachment::PROJECT_APPLIED);
  HMENU menu = GetSubMenu(GetMenu(f.window.hwnd()), 0);
  LOKA_VERIFY(GetMenuItemCount(menu) == 6);
  const wchar_t *titles[] = {L"Undo", L"Cut", L"Copy", L"Paste", L"Select All"};
  const char keys[] = {'Z', 'X', 'C', 'V', 'A'};
  ControlKey control;
  for (unsigned i = 0; i < 5; ++i)
  {
    const int row = i == 0 ? 0 : static_cast<int>(i + 1);
    wchar_t title[32];
    LOKA_VERIFY(GetMenuStringW(menu, row, title, 32, MF_BYPOSITION) > 0);
    LOKA_VERIFY(std::wcscmp(title, titles[i]) == 0);
    LOKA_VERIFY(!f.translate(keys[i]));
    LOKA_VERIFY(f.window.menuAttachment().dispatch(GetMenuItemID(menu, row)));
    LOKA_VERIFY(f.calls == static_cast<int>(i + 1));
  }
}

namespace
{
  struct EditFacts : HeadlessStateOwner
  {
    NodeState<String> text;
    Reported<LineCursor> cursor;
    loka::testing::TextBufferFixture lines;
    EditFacts()
    {
      StateBatchBase::CreateImmediateState(this, this->cursor, LineCursor::None());
      LOKA_VERIFY(this->lines.attach(this->tracker()->asPushTracker(), 4) == ATTACH_OK);
      LOKA_VERIFY(this->lines.insert(0, String("first")) == EDIT_OK);
      LOKA_VERIFY(this->lines.insert(1, String("last")) == EDIT_OK);
    }
  };
  EditFacts *editFacts = 0;
  class EditRoot : public BoundaryNodeFor<EditRoot>
  {
  public:
    NodeState<bool> editorShown;
    explicit EditRoot(const BoundaryPropsFor<EditRoot> &p)
        : BoundaryNodeFor<EditRoot>(p)
    {
      this->state(editFacts->text, String("field"));
      this->state(this->editorShown, true);
    }
    virtual void composeNode(NodeComposition &c)
    {
      c.declare(Column() << EditText(EditTextProps(editFacts->text)) << EditText()
                         << (Show(*this->editorShown.state()).destroyOnDetach()
                             << TextEditor(TextEditorProps(editFacts->lines, editFacts->cursor))));
    }
  };
  struct EditFixture
  {
    EditFacts facts;
    NullPlatformContext platform;
    int calls;
    EmitterState emitter;
    MenuBarDefinition bar;
    Win32Window window;
    WindowAdmissionTestApp admission;
    static WindowProps props(EditFacts &facts)
    {
      editFacts = &facts;
      return WindowProps().frame(60, 60, 400, 300).visible(true).scene(new Scene(Boundary<EditRoot>()));
    }
    explicit EditFixture(bool pasteShortcut = false)
        : calls(0),
          window(&this->platform, props(this->facts)),
          admission(this->window)
    {
      this->emitter.deferBind(&count, &this->calls);
      this->admission.flush();
      MenuDefinition edit = EditMenu();
      for (MenuItemDefinition *item = edit.itemsHead(); item; item = item->nextInComposition)
        if (!item->isSeparator)
        {
          item->onClick(&this->emitter);
          if (pasteShortcut && item->action == MENU_ACTION_EDIT_PASTE)
            item->shortcut('V');
        }
      this->bar << edit;
      LOKA_VERIFY(this->window.menuAttachment().project(sources(&this->bar, 0))
                  == Win32MenuAttachment::PROJECT_APPLIED);
    }
    ~EditFixture()
    {
      this->emitter.deferUnbind(&count, &this->calls);
    }
    Node *field(unsigned index)
    {
      BoundaryNode *root = loka::dsl::testing::SceneTestAccess::rootBoundary(*this->window.scene());
      Node *node = root->childrenHead()->asNestable()->childrenHead();
      for (unsigned i = 0; i != index && node; ++i)
        node = node->nextInComposition;
      if (index == 2)
      {
        LOKA_VERIFY(node && node->asNestable());
        node = node->asNestable()->childrenHead(); // Show's selected-arm Fragment.
      }
      LOKA_VERIFY(node && node->getContext());
      return node;
    }
    HWND edit(unsigned index)
    {
      NodeContext *context = this->field(index)->getContext();
      return index == 2 ? static_cast<Win32TextEditorContext *>(context)->hwnd()
                        : static_cast<Win32EditTextContext *>(context)->hwnd();
    }
    bool focus(unsigned index)
    {
      SetActiveWindow(this->window.hwnd());
      SetFocus(this->edit(index));
      if (GetFocus() == this->edit(index))
        return true;
      std::puts("[skip] Edit menu: native focus refused; no-target checks still run.");
      return false;
    }
    void dispatch(EditVerb verb)
    {
      int row = 0;
      switch (verb)
      {
      case EDIT_UNDO:
        row = 0;
        break;
      case EDIT_CUT:
        row = 2;
        break;
      case EDIT_COPY:
        row = 3;
        break;
      case EDIT_PASTE:
        row = 4;
        break;
      case EDIT_SELECT_ALL:
        row = 5;
        break;
      }
      const UINT id = GetMenuItemID(GetSubMenu(GetMenu(this->window.hwnd()), 0), row);
      LOKA_VERIFY(this->window.menuAttachment().dispatch(id));
    }
  };
  void selectionIs(HWND hwnd, DWORD start, DWORD end)
  {
    DWORD actualStart = 0, actualEnd = 0;
    SendMessageW(hwnd, EM_GETSEL, reinterpret_cast<WPARAM>(&actualStart), reinterpret_cast<LPARAM>(&actualEnd));
    LOKA_VERIFY(actualStart == start && actualEnd == end);
  }
  bool clipboardIs(HWND owner, const wchar_t *expected)
  {
    if (!OpenClipboard(owner))
    {
      std::puts("[skip] Edit menu: clipboard read ownership refused; selection and model checks still run.");
      return false;
    }
    HANDLE data = GetClipboardData(CF_UNICODETEXT);
    LOKA_VERIFY(data);
    const wchar_t *text = static_cast<const wchar_t *>(GlobalLock(data));
    LOKA_VERIFY(text && std::wcscmp(text, expected) == 0);
    GlobalUnlock(data);
    LOKA_VERIFY(CloseClipboard());
    return true;
  }
  bool putClipboard(HWND owner, const wchar_t *text)
  {
    if (!OpenClipboard(owner))
    {
      std::puts("[skip] Edit menu: clipboard write ownership refused; paste and undo checks skipped.");
      return false;
    }
    const std::size_t bytes = (std::wcslen(text) + 1) * sizeof(wchar_t);
    HGLOBAL data = GlobalAlloc(GMEM_MOVEABLE, bytes);
    LOKA_VERIFY(data);
    void *target = GlobalLock(data);
    LOKA_VERIFY(target);
    std::memcpy(target, text, bytes);
    GlobalUnlock(data);
    LOKA_VERIFY(EmptyClipboard());
    if (!SetClipboardData(CF_UNICODETEXT, data))
    {
      GlobalFree(data);
      LOKA_VERIFY(CloseClipboard());
      std::puts("[skip] Edit menu: clipboard transfer refused; paste and undo checks skipped.");
      return false;
    }
    LOKA_VERIFY(CloseClipboard());
    return true;
  }
} // namespace

void testWin32EditMenuTextEditor()
{
  EditFixture f;
  if (!f.focus(2))
    return;
  const HWND hwnd = f.edit(2);
  f.dispatch(EDIT_SELECT_ALL);
  selectionIs(hwnd, 0, 11); // Native CRLF between the two model rows.
  LOKA_VERIFY(f.facts.cursor.state()->get() == LineCursor(f.facts.lines.at(1).id, 4));
  LOKA_VERIFY(f.window.handleEditCommand(EDIT_SELECT_ALL));
  LOKA_VERIFY(f.calls == 0);
  f.dispatch(EDIT_COPY);
  const bool clipboardAvailable = clipboardIs(f.window.hwnd(), L"first\r\nlast");
  if (clipboardAvailable)
  {
    const ListRevision before = f.facts.lines.revision().get();
    f.dispatch(EDIT_CUT);
    LOKA_VERIFY(f.facts.lines.size() == 1 && f.facts.lines.at(0).value.equals(String("")));
    LOKA_VERIFY(f.facts.lines.revision().get() != before);
  }
  else
    std::puts("[skip] Edit menu TextEditor: clipboard unavailable; Cut skipped, paste still attempted.");
  if (putClipboard(f.window.hwnd(), L"pasted"))
  {
    f.dispatch(EDIT_PASTE);
    LOKA_VERIFY(f.facts.lines.size() == 1 && f.facts.lines.at(0).value.equals(String("pasted")));
    f.dispatch(EDIT_UNDO);
    if (clipboardAvailable)
      LOKA_VERIFY(f.facts.lines.size() == 1 && f.facts.lines.at(0).value.equals(String("")));
    else
    {
      LOKA_VERIFY(f.facts.lines.size() == 2);
      LOKA_VERIFY(f.facts.lines.at(0).value.equals(String("first")));
      LOKA_VERIFY(f.facts.lines.at(1).value.equals(String("last")));
    }
  }
  LOKA_VERIFY(f.calls == 0);
}

void testWin32EditMenuEditText()
{
  EditFixture f;
  if (!f.focus(0))
    return;
  const HWND hwnd = f.edit(0);
  f.dispatch(EDIT_SELECT_ALL);
  selectionIs(hwnd, 0, 5);
  LOKA_VERIFY(f.window.handleEditCommand(EDIT_SELECT_ALL));
  f.dispatch(EDIT_COPY);
  const bool clipboardAvailable = clipboardIs(f.window.hwnd(), L"field");
  if (clipboardAvailable)
  {
    f.dispatch(EDIT_CUT);
    LOKA_VERIFY(f.facts.text.get().equals(String("")));
  }
  else
    std::puts("[skip] Edit menu EditText: clipboard unavailable; Cut skipped, paste still attempted.");
  if (putClipboard(f.window.hwnd(), L"pasted"))
  {
    f.dispatch(EDIT_PASTE);
    LOKA_VERIFY(f.facts.text.get().equals(String("pasted")));
    f.dispatch(EDIT_UNDO);
    LOKA_VERIFY(f.facts.text.get().equals(String(clipboardAvailable ? "" : "field")));
  }
  LOKA_VERIFY(f.calls == 0);
}

void testWin32EditMenuTargetWalls()
{
  EditFixture f;
  SetActiveWindow(f.window.hwnd());
  SetFocus(f.window.hwnd());
  // With activation refused there is still no marked target in this window.
  const EditVerb verbs[] = {EDIT_UNDO, EDIT_CUT, EDIT_COPY, EDIT_PASTE, EDIT_SELECT_ALL};
  for (unsigned i = 0; i != 5; ++i)
  {
    LOKA_VERIFY(!f.window.handleEditCommand(verbs[i]));
    f.dispatch(verbs[i]);
    LOKA_VERIFY(f.calls == static_cast<int>(i + 1));
  }
  EditFixture other;
  if (other.focus(0))
  {
    LOKA_VERIFY(!f.window.handleEditCommand(EDIT_SELECT_ALL));
    f.dispatch(EDIT_SELECT_ALL);
    LOKA_VERIFY(f.calls == 6);
  }
  if (!f.focus(1))
    return;
  Node *node = f.field(1);
  const HWND hidden = f.edit(1);
  NotifySubtreeNodeDetached(node);
  LifecycleFactTestAccess::DeliverFacts(node);
  LOKA_VERIFY(!Win32FocusParticipant::read(hidden));
  LOKA_VERIFY(!IsWindowVisible(hidden));
  SetFocus(hidden);
  LOKA_VERIFY(!f.window.handleEditCommand(EDIT_SELECT_ALL));
  const int before = f.calls;
  f.dispatch(EDIT_SELECT_ALL);
  LOKA_VERIFY(f.calls == before + 1);
  NotifySubtreeNodeAttached(node);
  LifecycleFactTestAccess::DeliverFacts(node);
}

void testWin32EditMenuSelectAllInput()
{
  typedef loka::testing::Win32TextEditorAccess EditorAccess;
  EditFixture f;
  if (!f.focus(2))
    return;
  const HWND hwnd = f.edit(2);
  Win32TextEditorContext &context = *static_cast<Win32TextEditorContext *>(f.field(2)->getContext());
  SendMessageW(hwnd, EM_SETSEL, 7, 7);
  SendMessageW(hwnd, WM_KEYDOWN, VK_END, 0);
  const LineCursor keyboard = f.facts.cursor.state()->get();
  const EditorResult keyboardStatus = EditorAccess::status(context);
  LOKA_VERIFY(keyboard == LineCursor(f.facts.lines.at(1).id, 4));
  SendMessageW(hwnd, EM_SETSEL, 0, 0);
  SendMessageW(hwnd, WM_KEYDOWN, VK_HOME, 0);
  LOKA_VERIFY(f.facts.cursor.state()->get() != keyboard);
  f.dispatch(EDIT_SELECT_ALL);
  LOKA_VERIFY(f.facts.cursor.state()->get() == keyboard);
  LOKA_VERIFY(EditorAccess::status(context) == keyboardStatus && keyboardStatus == EDITOR_OK);
  selectionIs(hwnd, 0, 11);
  SendMessageW(hwnd, EM_SETSEL, 1, 2);
  EditorAccess::status(context, EDITOR_UNAVAILABLE);
  LOKA_VERIFY(f.window.handleEditCommand(EDIT_SELECT_ALL));
  selectionIs(hwnd, 1, 2);
  f.dispatch(EDIT_SELECT_ALL);
  selectionIs(hwnd, 1, 2);
  LOKA_VERIFY(f.calls == 0);
  EditorAccess::status(context, EDITOR_OK);
}

void testWin32EditMenuPasteShortcut()
{
  EditFixture f(true);
  if (!f.focus(2) || !putClipboard(f.window.hwnd(), L"once"))
    return;
  SendMessageW(f.edit(2), EM_SETSEL, 0, -1);
  ControlKey control;
  MSG message = keyMessage(f.window.hwnd(), 'V');
  LOKA_VERIFY(f.window.menuAttachment().translateAccelerator(message));
  pumpCommands(f.window.hwnd());
  LOKA_VERIFY(f.facts.lines.size() == 1 && f.facts.lines.at(0).value.equals(String("once")));
  LOKA_VERIFY(f.calls == 0);
}

namespace
{
  struct EditReentry
  {
    EditFixture &fixture;
    const bool retire;
    unsigned calls;
    EditReentry(EditFixture &f, bool r)
        : fixture(f),
          retire(r),
          calls(0)
    {
    }
    static void moved(void *data)
    {
      EditReentry &self = *static_cast<EditReentry *>(data);
      ++self.calls;
      LOKA_VERIFY(self.fixture.window.scene()->isBorrowOpen());
      Node *node = self.fixture.field(2);
      const HWND hwnd = self.fixture.edit(2);
      if (self.retire)
      {
        // syncCaret publishes after EM_SETSEL returns, inside the input borrow.
        // Request structural removal through the Scene's ordinary Show seat.
        EditRoot &root =
            *static_cast<EditRoot *>(loka::dsl::testing::SceneTestAccess::rootBoundary(*self.fixture.window.scene()));
        root.editorShown.set(false);
        self.fixture.admission.flush();
        LOKA_VERIFY(self.fixture.window.scene()->isBorrowOpen());
        LOKA_VERIFY(self.fixture.field(2) == node && node->getContext());
        LOKA_VERIFY(IsWindow(hwnd) && Win32FocusParticipant::read(hwnd));
      }
      else
      {
        // The cursor subscriber runs in COMMIT. A nested Select All must be
        // handled without touching the native selection or invoking fallback.
        const LRESULT before = SendMessageW(hwnd, EM_GETSEL, 0, 0);
        LOKA_VERIFY(self.fixture.window.handleEditCommand(EDIT_SELECT_ALL));
        LOKA_VERIFY(SendMessageW(hwnd, EM_GETSEL, 0, 0) == before);
      }
    }
  };
} // namespace

void testWin32EditMenuSelectAllReentry()
{
  for (unsigned mode = 0; mode != 2; ++mode)
  {
    EditFixture f;
    if (!f.focus(2))
      continue;
    const HWND hwnd = f.edit(2);
    NodeContext *const context = f.field(2)->getContext();
    EditReentry probe(f, mode != 0);
    f.facts.cursor.state()->bind(&EditReentry::moved, &probe, false);
    f.dispatch(EDIT_SELECT_ALL);
    f.facts.cursor.state()->unbind(&EditReentry::moved, &probe);
    LOKA_VERIFY(probe.calls == 1 && f.calls == 0);
    LOKA_VERIFY(!f.window.scene()->isBorrowOpen());
    if (mode != 0)
    {
      // BorrowScope exit does not pump: the pending seat still owns its editor.
      LOKA_VERIFY(f.field(2)->getContext() == context);
      LOKA_VERIFY(IsWindow(hwnd) && Win32FocusParticipant::read(hwnd));
      f.admission.flush();
      // Only inspect the surviving tree and native handle after reclamation.
      Node *arm = f.field(1)->nextInComposition;
      LOKA_VERIFY(arm && arm->asNestable());
      LOKA_VERIFY(!arm->asNestable()->childrenHead());
      LOKA_VERIFY(!IsWindow(hwnd));
    }
  }
}
