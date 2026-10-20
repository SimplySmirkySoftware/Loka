#include "ToolboxMenuHost.hpp"
#include "support/TestVerify.hpp"
#include "support/StandaloneMountTestSupport.hpp"
#include "app/nodes/boundary/StdComposition.hpp"
#include "app/core/AppConfigurable.hpp"
#include "testing/scene/SceneTestFlow.hpp"
#include "core/util/StateTrackerGuard.hpp"
#include "Script.h"
#include "Sound.h"
#include <cstdio>
#include <cstring>
#include <new>

namespace toolbox_host
{
  std::vector<std::string> menuTitles, menuAppends, menuInserts, menuSets, disposedMenuItems;
  std::vector<MenuHandle> installedMenus;
  std::vector<ItemCmdCall> itemCmdCalls;
  void (*afterMenuSet)() = 0;
  unsigned beeps = 0;
  unsigned menuDraws = 0, menuClears = 0, menuDisposes = 0, menuValueWrites = 0;
}
long GetScriptManagerVariable(short) { return smRoman; }
using namespace loka::app;
using namespace loka::app::scene;
using namespace loka::core;
namespace
{
  MenuSources sources(const MenuBarDefinition *bar, const Scene *scene)
  {
    const MenuSources result = {scene ? 0 : bar, scene ? bar : 0, scene};
    return result;
  }
  void clearCalls()
  {
    toolbox_host::itemCmdCalls.clear();
    toolbox_host::beeps = 0;
    toolbox_host::menuInitializations = 0;
    toolbox_host::menuTitles.clear();
    toolbox_host::menuAppends.clear();
    toolbox_host::menuInserts.clear();
    toolbox_host::menuSets.clear();
    toolbox_host::menuDraws = toolbox_host::menuClears = toolbox_host::menuDisposes = toolbox_host::menuValueWrites = 0;
  }
  void noCalls()
  {
    LOKA_VERIFY(toolbox_host::menuInitializations == 0 && toolbox_host::beeps == 0);
    LOKA_VERIFY(toolbox_host::itemCmdCalls.empty());
    LOKA_VERIFY(toolbox_host::menuTitles.empty());
    LOKA_VERIFY(toolbox_host::menuAppends.empty());
    LOKA_VERIFY(toolbox_host::menuInserts.empty());
    LOKA_VERIFY(toolbox_host::menuSets.empty());
    LOKA_VERIFY(toolbox_host::menuDraws == 0 && toolbox_host::menuClears == 0);
    LOKA_VERIFY(toolbox_host::menuDisposes == 0 && toolbox_host::menuValueWrites == 0);
  }
  MenuBarDefinition bar(const char *item = "Open")
  {
    MenuBarDefinition result;
    result << (Menu("File") << MenuItem(item));
    result << (Menu("Edit") << MenuItem("Copy"));
    return result;
  }
  MenuBarDefinition checkedBar(bool checked)
  {
    MenuBarDefinition result;
    result << (Menu("View") << MenuItem("Actual").attr(MenuItemAttr().checked(checked)));
    return result;
  }
  struct Observer
  {
    Observer() : attachment(0), calls(0), detaches(0), disconnectInAction(false) {}
    ToolboxMenuAttachment *attachment;
    int calls, detaches;
    bool disconnectInAction;
  };
  Observer *observer = 0;
  class Root : public BoundaryNodeFor<Root>
  {
  public:
    explicit Root(const BoundaryPropsFor<Root> &props) : BoundaryNodeFor<Root>(props)
    { this->state(this->enabled_, true); }
    virtual void declareBindings(BindingToken &token) { token.action(this->clicked_, this, &Root::clicked); }
    virtual void composeNode(NodeComposition &c)
    {
      MenuBarDefinition offered;
      offered << (Menu("File") << MenuItem("Run").enabled(this->enabled_.state()).onClick(&this->clicked_));
      LOKA_VERIFY(c.menuBar(offered));
    }
    virtual void detachNode(NodeComposition &)
    {
      ++observer->detaches;
      LOKA_VERIFY(!observer->attachment->dispatch(128, 1));
    }
    void disable() { StateTrackerGuard guard(this->tracker()); this->enabled_.set(!this->enabled_.get()); }
  private:
    void clicked()
    {
      ++observer->calls;
      if (observer->disconnectInAction)
        observer->attachment->disconnect();
    }
    NodeState<bool> enabled_;
    EmitterState clicked_;
  };
  struct Mounted
  {
    Observer observation;
    loka::testing::StandaloneMountTestPlatformContext platform;
    ToolboxApp app;
    ToolboxWindow window;
    ToolboxScenePlatformController controller;
    Mounted() : window(&platform, props(), &app), controller(&window)
    {
      observer = &this->observation;
      this->observation.attachment = &this->app.menuAttachment();
      this->window.scene()->mount(&this->controller);
      LOKA_VERIFY(this->window.scene()->menuBar());
      LOKA_VERIFY(this->app.menuAttachment().project(sources(this->window.scene()->menuBar(), this->window.scene())));
    }
    ~Mounted() { this->window.unmount(); }
    static WindowProps props()
    {
      WindowProps result;
      result.scene(new Scene(Boundary<Root>()));
      return result;
    }
    Root *root()
    {
      return static_cast<Root *>(loka::dsl::testing::SceneTestAccess::rootBoundary(*this->window.scene()));
    }
  };
}

void testToolboxMenuAttachmentProjectsOncePerIdentity()
{
  ToolboxApp app;
  MenuBarDefinition first = bar(), equal = bar();
  clearCalls();
  LOKA_VERIFY(app.menuAttachment().project(sources(&first, 0)));
  LOKA_VERIFY(toolbox_host::menuTitles.size() == 2 && toolbox_host::menuSets.size() == 2);
  clearCalls();
  loka::app::testing::failMenuBarDefinitionClones(1);
  LOKA_VERIFY(!app.menuAttachment().project(sources(&first, 0)));
  noCalls();
  // The unchanged offer left the failure armed: a direct clone consumes it.
  OwnedDef<MenuBarDefinition> refused(first.clone());
  LOKA_VERIFY(!refused.isSet());
  OwnedDef<MenuBarDefinition> accepted(first.clone());
  LOKA_VERIFY(accepted.isSet());
  LOKA_VERIFY(app.menuAttachment().project(sources(&equal, 0)));
  LOKA_VERIFY(toolbox_host::menuDisposes == 2 && toolbox_host::menuTitles.size() == 2);
  clearCalls();
  LOKA_VERIFY(!app.menuAttachment().project(sources(&equal, 0)));
  noCalls();
}
void testToolboxMenuAttachmentSeesItemChange()
{
  ToolboxApp app;
  const MenuBarDefinition first = checkedBar(false), second = checkedBar(true);
  LOKA_VERIFY(app.menuAttachment().project(sources(&first, 0)));
  LOKA_VERIFY(toolbox_host::menuSets.back() == "Actual");
  clearCalls();
  app.menuAttachment().project(sources(&second, 0));
  LOKA_VERIFY(toolbox_host::menuSets.size() == 1 && toolbox_host::menuSets[0] == "Actual");
  LOKA_VERIFY(toolbox_host::menuTitles.size() == 1 && toolbox_host::menuDisposes == 1);
  clearCalls();
  app.menuAttachment().project(sources(&second, 0));
  noCalls();
}
void testToolboxMenuAttachmentChangedOverlayRebuildsFully()
{
  ToolboxApp app;
  Scene source((Boundary<Root>()));
  MenuBarDefinition first = bar(), changed = bar("Save");
  LOKA_VERIFY(app.menuAttachment().project(sources(&first, &source)));
  clearCalls();
  LOKA_VERIFY(app.menuAttachment().project(sources(&changed, &source)));
  LOKA_VERIFY(toolbox_host::menuTitles.size() == 2 && toolbox_host::menuDisposes == 2);
  LOKA_VERIFY(toolbox_host::menuSets.size() == 2 && toolbox_host::menuSets[0] == "Save");
  LOKA_VERIFY(toolbox_host::installedMenus[1]->items[0] == "Copy");
  // Deliberate contract probe: mutation is not a new declaration identity.
  changed << (Menu("Help") << MenuItem("Help"));
  clearCalls();
  LOKA_VERIFY(!app.menuAttachment().project(sources(&changed, &source)));
  noCalls();
  app.menuAttachment().disconnect();
}
void testToolboxMenuAttachmentReleaseFromSourceDisconnects()
{
  Mounted f;
  Scene other((Boundary<Root>()));
  clearCalls();
  f.app.menuAttachment().releaseFrom(&other);
  LOKA_VERIFY(f.app.menuAttachment().dispatch(128, 1));
  LOKA_VERIFY(f.observation.calls == 1);
  const int before = f.app.redraws;
  f.root()->disable();
  LOKA_VERIFY(f.app.redraws == before + 1); // Positive control: live State reaches the thunk.
  clearCalls();
  f.app.menuAttachment().releaseFrom(f.window.scene());
  noCalls();
  LOKA_VERIFY(!f.app.menuAttachment().dispatch(128, 1));
  LOKA_VERIFY(toolbox_host::installedMenus.size() == 1);
  f.root()->disable();
  noCalls();
  MenuBarDefinition replacement = bar();
  LOKA_VERIFY(f.app.menuAttachment().project(sources(&replacement, 0)));
  LOKA_VERIFY(toolbox_host::menuDisposes == 1);
  f.app.menuAttachment().disconnect();
}
void testToolboxSceneDetachReleasesMenu()
{
  Mounted f;
  // A local copy makes overlay address reuse deterministic, including under
  // ASan. Its endpoints still belong to the mounted Scene composition.
  MenuBarDefinition reused(*f.window.scene()->menuBar());
  LOKA_VERIFY(f.app.menuAttachment().project(sources(&reused, f.window.scene())));
  LOKA_VERIFY(f.app.menuAttachment().dispatch(128, 1));
  clearCalls();
  f.window.unmount();
  LOKA_VERIFY(f.observation.detaches == 1 && f.observation.calls == 1);
  LOKA_VERIFY(!f.app.menuAttachment().dispatch(128, 1));
  noCalls();
  reused.~MenuBarDefinition();
  f.window.scene()->mount(&f.controller);
  LOKA_VERIFY(f.window.scene()->menuBar());
  new (&reused) MenuBarDefinition(*f.window.scene()->menuBar());
  LOKA_VERIFY(f.app.menuAttachment().project(sources(&reused, f.window.scene())));
  LOKA_VERIFY(toolbox_host::menuDisposes == 1 && toolbox_host::menuTitles.size() == 1);
  LOKA_VERIFY(f.app.menuAttachment().dispatch(128, 1));
  LOKA_VERIFY(f.observation.calls == 2);
  f.window.unmount();
}
void testToolboxMenuDispatchAfterDisconnectIsDropped()
{
  Mounted f;
  LOKA_VERIFY(f.app.menuAttachment().dispatch(128, 1));
  LOKA_VERIFY(f.observation.calls == 1);
  f.app.menuAttachment().disconnect();
  LOKA_VERIFY(!f.app.menuAttachment().dispatch(128, 1));
  LOKA_VERIFY(f.observation.calls == 1);
  clearCalls();
  LOKA_VERIFY(f.app.menuAttachment().project(sources(f.window.scene()->menuBar(), f.window.scene())));
  LOKA_VERIFY(toolbox_host::menuDisposes == 1 && toolbox_host::menuTitles.size() == 1);
}
void testToolboxMenuDispatchMayDisconnectWhileRunning()
{
  Mounted f;
  f.observation.disconnectInAction = true;
  LOKA_VERIFY(f.app.menuAttachment().dispatch(128, 1));
  LOKA_VERIFY(f.observation.calls == 1);
  LOKA_VERIFY(!f.app.menuAttachment().dispatch(128, 1));
}
void testToolboxMenuAttachmentMergeRefusalPreservesRecord()
{
  ToolboxApp app;
  const MenuBarDefinition first = checkedBar(false), second = checkedBar(true);
  LOKA_VERIFY(app.menuAttachment().project(sources(&first, 0)));
  MenuHandle installed = toolbox_host::installedMenus[0];
  loka::app::testing::failMenuBarDefinitionClones(1);
  clearCalls();
  LOKA_VERIFY(!app.menuAttachment().project(sources(&second, 0)));
  noCalls();
  LOKA_VERIFY(toolbox_host::installedMenus[0] == installed);
  LOKA_VERIFY(!app.menuAttachment().project(sources(&first, 0)));
  noCalls(); // Refusal preserved both native truth and the previous record.
  LOKA_VERIFY(app.menuAttachment().project(sources(&second, 0)));
  LOKA_VERIFY(toolbox_host::menuTitles.size() == 1 && toolbox_host::menuDisposes == 1);
  clearCalls();
  LOKA_VERIFY(!app.menuAttachment().project(sources(&second, 0)));
  noCalls();
  loka::app::testing::failMenuItemDefinitionClones(1);
  LOKA_VERIFY(!app.menuAttachment().project(sources(&first, 0)));
  noCalls();
  LOKA_VERIFY(!app.menuAttachment().project(sources(&second, 0)));
  noCalls();
  LOKA_VERIFY(app.menuAttachment().project(sources(&first, 0)));
  LOKA_VERIFY(toolbox_host::menuDisposes == 1 && toolbox_host::menuTitles.size() == 1);
  clearCalls();
  LOKA_VERIFY(!app.menuAttachment().project(sources(&first, 0)));
  noCalls();
}

void testToolboxMenuDispatchKeepsActionAcrossReplacement()
{
  ToolboxApp app;
  struct Replace
  {
    static void run(void *data)
    {
      ToolboxApp *app = static_cast<ToolboxApp *>(data);
      MenuBarDefinition replacement = bar("Replacement");
      LOKA_VERIFY(app->menuAttachment().project(sources(&replacement, 0)));
    }
  };
  EmitterState emitter;
  emitter.deferBind(&Replace::run, &app);
  MenuBarDefinition original;
  original << (Menu("File") << MenuItem("Replace").onClick(&emitter));
  LOKA_VERIFY(app.menuAttachment().project(sources(&original, 0)));
  clearCalls();
  LOKA_VERIFY(app.menuAttachment().dispatch(128, 1));
  LOKA_VERIFY(!toolbox_host::menuSets.empty() && toolbox_host::menuSets[0] == "Replacement");
  emitter.deferUnbind(&Replace::run, &app);
}
void testToolboxMenuAttachmentSwitchesSourceAndClearsNullBar()
{
  ToolboxApp app;
  Scene first((Boundary<Root>())), second((Boundary<Root>()));
  MenuBarDefinition offered = bar();
  LOKA_VERIFY(app.menuAttachment().project(sources(&offered, &first)));
  clearCalls();
  LOKA_VERIFY(app.menuAttachment().project(sources(&offered, &second)));
  LOKA_VERIFY(toolbox_host::menuDisposes == 2 && toolbox_host::menuTitles.size() == 2);
  clearCalls();
  LOKA_VERIFY(app.menuAttachment().project(sources(0, 0)));
  LOKA_VERIFY(toolbox_host::menuDisposes == 2 && toolbox_host::menuClears == 1 && toolbox_host::menuDraws == 1);
  LOKA_VERIFY(toolbox_host::installedMenus.empty());
  // Completion re-offers every turn: an absent bar over an empty projection
  // must neither clear nor draw again (codex review of N2a).
  clearCalls();
  const int redrawsBefore = app.redraws;
  LOKA_VERIFY(!app.menuAttachment().project(sources(0, 0)));
  LOKA_VERIFY(toolbox_host::menuClears == 0 && toolbox_host::menuDraws == 0 && app.redraws == redrawsBefore);
}
void testToolboxMenuAttachmentEmptyMenuRebuildUsesPreparedMerge()
{
  ToolboxApp app;
  MenuBarDefinition before, after;
  before << (Menu("File") << MenuItem("Open")) << Menu("Empty");
  after << (Menu("File") << MenuItem("Save")) << (Menu("Empty") << MenuItem("New"));
  LOKA_VERIFY(app.menuAttachment().project(sources(&before, 0)));
  clearCalls();
  // Arm refusal at the first native mutation: all cloning must be finished
  // before the full rebuild starts.
  toolbox_host::afterMenuSet = &loka::app::testing::failNextMenuBarDefinitionClone;
  LOKA_VERIFY(app.menuAttachment().project(sources(&after, 0)));
  toolbox_host::afterMenuSet = 0;
  loka::app::testing::allowMenuBarDefinitionClones();
  LOKA_VERIFY(toolbox_host::menuTitles.size() == 2 && toolbox_host::menuDraws == 1);
  clearCalls();
  LOKA_VERIFY(!app.menuAttachment().project(sources(&after, 0)));
  noCalls();
}
namespace
{
  class ShutdownApp : public ToolboxApp
  {
  public:
    void own(Window *window)
    {
      if (!this->group_)
        this->group_ = new AppComponentGroup(std::vector<AppComponent *>());
      this->group_->adopt(window);
    }
    void retireForTest() { this->retireComponents(); }
  };
  class ShutdownWindow : public ToolboxWindow
  {
  public:
    ShutdownWindow(PlatformContext *platform, ToolboxApp *app)
        : ToolboxWindow(platform, Mounted::props(), app), controller_(this)
    {
      this->scene()->mount(&this->controller_);
    }
    virtual ~ShutdownWindow()
    {
      // A positive native-handle witness makes the bad destructor order red
      // before invoking the production release door on a dead attachment.
      LOKA_VERIFY(toolbox_host::menuDisposes == 0);
      this->unmount();
      LOKA_VERIFY(observer->detaches == 1);
    }
  private:
    ToolboxScenePlatformController controller_;
  };
  void shutdownMenu(bool queued, bool retireTwice)
  {
    Observer observation;
    observer = &observation;
    loka::testing::StandaloneMountTestPlatformContext platform;
    {
      ShutdownApp app;
      observation.attachment = &app.menuAttachment();
      ShutdownWindow *window = new ShutdownWindow(&platform, &app);
      app.own(window);
      LOKA_VERIFY(app.menuAttachment().project(sources(window->scene()->menuBar(), window->scene())));
      LOKA_VERIFY(app.menuAttachment().dispatch(128, 1));
      LOKA_VERIFY(observation.calls == 1);
      if (queued)
        app.requestWindowClose(window);
      clearCalls();
      if (retireTwice)
      {
        app.retireForTest();
        LOKA_VERIFY(observation.detaches == 1);
        app.retireForTest();
        LOKA_VERIFY(observation.detaches == 1);
      }
    }
    LOKA_VERIFY(observation.detaches == 1);
    LOKA_VERIFY(toolbox_host::menuDisposes == 1);
    LOKA_VERIFY(toolbox_host::installedMenus.empty());
  }
}
void testToolboxAppShutdownReleasesMenuBeforeAttachmentDestruction()
{
  shutdownMenu(false, false);
}
void testToolboxAppShutdownDrainsPendingCloseBeforeAttachmentDestruction()
{
  shutdownMenu(true, false);
}
void testAppRetireComponentsIsIdempotent()
{
  shutdownMenu(false, true);
  shutdownMenu(true, true);
}
void testAppDestructorRetiresComponentsWithoutRailOwner()
{
  struct CountedComponent : AppComponent
  {
    explicit CountedComponent(int &deletions) : deletions_(deletions) {}
    virtual ~CountedComponent() { ++this->deletions_; }
    int &deletions_;
  };
  struct BaseOnlyApp : App
  {
    explicit BaseOnlyApp(AppComponent *component) : App(0)
    {
      this->group_ = new AppComponentGroup(std::vector<AppComponent *>());
      this->group_->adopt(component);
    }
    virtual void quit() {}
  };
  int deletions = 0;
  {
    BaseOnlyApp app(new CountedComponent(deletions));
  }
  LOKA_VERIFY(deletions == 1);
}
void testToolboxMenuDoorFiltersRowsAndForwardsScene()
{
  Mounted f;
  ToolboxWindow inactive(&f.platform, WindowProps(), &f.app);
  MenuBarDefinition other = bar("Other");
  f.app.setActiveWindow(&f.window);
  clearCalls();
  const MenuSources otherSources = {&other, 0, 0};
  f.app.projectMenu(&inactive, otherSources);
  f.app.projectMenu(0, otherSources);
  noCalls();
  // The door must forward the original identity without a bridge merge.
  const MenuSources sceneSources = {0, f.window.scene()->menuBar(), f.window.scene()};
  const int positions = f.window.positionPreserves;
  loka::app::testing::failMenuBarDefinitionClones(1);
  f.app.projectMenu(&f.window, sceneSources);
  noCalls();
  LOKA_VERIFY(f.window.positionPreserves == positions);
  OwnedDef<MenuBarDefinition> refused(other.clone());
  LOKA_VERIFY(!refused.isSet());
  OwnedDef<MenuBarDefinition> accepted(other.clone());
  LOKA_VERIFY(accepted.isSet());
  const MenuSources combined = {&other, f.window.scene()->menuBar(), f.window.scene()};
  f.app.projectMenu(&f.window, combined);
  LOKA_VERIFY(f.window.positionPreserves == positions + 1);
  LOKA_VERIFY(toolbox_host::installedMenus.size() == 2);
  LOKA_VERIFY(toolbox_host::installedMenus[0]->items[0] == "Run");
  LOKA_VERIFY(toolbox_host::installedMenus[1]->items[0] == "Copy");
  clearCalls();
  f.app.setActiveWindow(0);
  f.app.projectMenu(0, otherSources);
  LOKA_VERIFY(toolbox_host::menuDraws == 1);
  f.app.menuAttachment().disconnect();
}
void testToolboxMenuBridgeRefusalPreservesNativeAndRetries()
{
  ToolboxApp app;
  MenuBarDefinition first = bar("Initial"), second = bar("Replacement");
  const MenuSources initial = {&first, 0, 0}, replacement = {&second, 0, 0};
  app.projectMenu(0, initial);
  MenuHandle installed = toolbox_host::installedMenus[0];
  clearCalls();
  loka::app::testing::failMenuBarDefinitionClones(1);
  app.projectMenu(0, replacement);
  loka::app::testing::allowMenuBarDefinitionClones();
  noCalls();
  LOKA_VERIFY(toolbox_host::installedMenus[0] == installed);
  LOKA_VERIFY(installed->items[0] == "Initial");
  app.projectMenu(0, replacement);
  LOKA_VERIFY(toolbox_host::menuSets.size() == 2);
  LOKA_VERIFY(toolbox_host::menuSets[0] == "Replacement");
  clearCalls();
  app.projectMenu(0, replacement);
  noCalls();
}
void testToolboxMenuBridgeReleaseThenAbsentClearsNative()
{
  Mounted f;
  f.app.menuAttachment().releaseFrom(f.window.scene());
  LOKA_VERIFY(!toolbox_host::installedMenus.empty());
  f.app.setActiveWindow(0);
  clearCalls();
  f.app.menuAttachment().project(sources(0, 0));
  LOKA_VERIFY(toolbox_host::installedMenus.empty());
  LOKA_VERIFY(toolbox_host::menuClears == 1 && toolbox_host::menuDraws == 1);
  LOKA_VERIFY(toolbox_host::menuInitializations == 1);
  clearCalls();
  f.app.menuAttachment().project(sources(0, 0));
  noCalls();
}
void testToolboxMenuProjectionDefersBackgroundDraws()
{
  ToolboxApp app;
  MenuBarDefinition first = bar(), partial = bar("Save");
  app.phase(ACTIVATION_BACKGROUND);
  clearCalls();
  LOKA_VERIFY(app.menuAttachment().project(sources(&first, 0)));
  LOKA_VERIFY(toolbox_host::menuTitles.size() == 2);
  LOKA_VERIFY(toolbox_host::menuDraws == 0 && app.drawOwed());
  app.phase(ACTIVATION_FOREGROUND);
  LOKA_VERIFY(toolbox_host::menuDraws == 1 && !app.drawOwed());
  app.phase(ACTIVATION_FOREGROUND);
  LOKA_VERIFY(toolbox_host::menuDraws == 1);
  app.phase(ACTIVATION_BACKGROUND);
  clearCalls();
  LOKA_VERIFY(app.menuAttachment().project(sources(&partial, 0)));
  LOKA_VERIFY(toolbox_host::menuTitles.size() == 2 && toolbox_host::menuSets.size() == 2);
  LOKA_VERIFY(toolbox_host::menuDraws == 0 && app.drawOwed());
  app.phase(ACTIVATION_FOREGROUND);
  LOKA_VERIFY(toolbox_host::menuDraws == 1 && !app.drawOwed());
  app.phase(ACTIVATION_BACKGROUND);
  clearCalls();
  LOKA_VERIFY(app.menuAttachment().project(sources(0, 0)));
  LOKA_VERIFY(toolbox_host::installedMenus.empty());
  LOKA_VERIFY(toolbox_host::menuDraws == 0 && app.drawOwed());
  app.phase(ACTIVATION_FOREGROUND);
  LOKA_VERIFY(toolbox_host::menuDraws == 1 && !app.drawOwed());
  clearCalls();
  LOKA_VERIFY(app.menuAttachment().project(sources(&first, 0)));
  LOKA_VERIFY(toolbox_host::menuDraws == 1 && !app.drawOwed());
}
void testToolboxMenuShortcutProjectsItemCmd()
{
  ToolboxApp app;
  MenuBarDefinition menu;
  menu << (Menu("File") << MenuItem("Plain") << MenuSeparator()
                       << MenuItem("Save").shortcut('S')
                       << MenuItem("Lower").shortcut('a')
                       << MenuItem("Empty").shortcut(0)
                       // A control byte is a Classic marker, never a key (bot P2 on #1122).
                       << MenuItem("Marker").shortcut('\x1b'));
  clearCalls();
  LOKA_VERIFY(app.menuAttachment().project(sources(&menu, 0)));
  LOKA_VERIFY(toolbox_host::itemCmdCalls.size() == 2);
  LOKA_VERIFY(toolbox_host::itemCmdCalls[0].menu == toolbox_host::installedMenus[0]);
  LOKA_VERIFY(toolbox_host::itemCmdCalls[0].item == 3);
  LOKA_VERIFY(toolbox_host::itemCmdCalls[0].cmd == 'S');
  LOKA_VERIFY(toolbox_host::itemCmdCalls[1].menu == toolbox_host::installedMenus[0]);
  LOKA_VERIFY(toolbox_host::itemCmdCalls[1].item == 4);
  LOKA_VERIFY(toolbox_host::itemCmdCalls[1].cmd == 'A');
}
void testToolboxQuitDefaultsToCommandQ()
{
  ToolboxApp app;
  MenuBarDefinition menu;
  menu << (Menu("File") << MenuItem("Quit").actionType(MENU_ACTION_QUIT_APP)
                       << MenuItem("Override").actionType(MENU_ACTION_QUIT_APP).shortcut('X')
                       << MenuItem("Empty").actionType(MENU_ACTION_QUIT_APP).shortcut(0));
  clearCalls();
  LOKA_VERIFY(app.menuAttachment().project(sources(&menu, 0)));
  LOKA_VERIFY(toolbox_host::itemCmdCalls.size() == 3);
  for (std::size_t i = 0; i < 3; ++i)
  {
    LOKA_VERIFY(toolbox_host::itemCmdCalls[i].menu == toolbox_host::installedMenus[0]);
    LOKA_VERIFY(toolbox_host::itemCmdCalls[i].item == static_cast<short>(i + 1));
  }
  LOKA_VERIFY(toolbox_host::itemCmdCalls[0].cmd == 'Q');
  LOKA_VERIFY(toolbox_host::itemCmdCalls[1].cmd == 'X');
  LOKA_VERIFY(toolbox_host::itemCmdCalls[2].cmd == 'Q');
}
void testToolboxSubmenuIdDiffersFromItsParent()
{
  ToolboxApp app;
  MenuBarDefinition menu;
  menu << (Menu("File") << (MenuItem("More") << MenuItem("Child")) << MenuItem("Close"))
       << (Menu("Edit") << MenuItem("Copy"));
  clearCalls();
  LOKA_VERIFY(app.menuAttachment().project(sources(&menu, 0)));
  // The submenu, its parent and the next menu each carry their own ID:
  // MenuSelect and the command table both key on it.
  LOKA_VERIFY(toolbox_host::installedMenus.size() == 3);
  for (std::size_t i = 0; i < toolbox_host::installedMenus.size(); ++i)
    for (std::size_t j = i + 1; j < toolbox_host::installedMenus.size(); ++j)
      LOKA_VERIFY(toolbox_host::installedMenus[i]->id != toolbox_host::installedMenus[j]->id);
}
void testToolboxParentItemDoesNotDispatchItsSubmenuItem()
{
  struct Count
  {
    static void run(void *data)
    {
      ++*static_cast<int *>(data);
    }
  };
  int second = 0, close = 0;
  EmitterState secondEmitter, closeEmitter;
  secondEmitter.deferBind(&Count::run, &second);
  closeEmitter.deferBind(&Count::run, &close);
  {
    ToolboxApp app;
    MenuBarDefinition menu;
    menu << (Menu("File") << (MenuItem("More") << MenuItem("First") << MenuItem("Second").onClick(&secondEmitter))
                          << MenuItem("Close").onClick(&closeEmitter));
    LOKA_VERIFY(app.menuAttachment().project(sources(&menu, 0)));
    // File is menu 128 (no AppMenu): its item 2 is Close, while the submenu's
    // item 2 is Second. Choosing Close must not run Second.
    LOKA_VERIFY(app.menuAttachment().dispatch(128, 2));
    LOKA_VERIFY(close == 1);
    LOKA_VERIFY(second == 0);
  }
  secondEmitter.deferUnbind(&Count::run, &second);
  closeEmitter.deferUnbind(&Count::run, &close);
}
void testToolboxHierarchicalOpenerKeepsMarkerOverShortcut()
{
  ToolboxApp app;
  MenuBarDefinition menu;
  menu << (Menu("File") << (MenuItem("More").shortcut('K') << MenuItem("Child").shortcut('C')));
  clearCalls();
  LOKA_VERIFY(app.menuAttachment().project(sources(&menu, 0)));
  LOKA_VERIFY(toolbox_host::installedMenus.size() == 2);
  LOKA_VERIFY(toolbox_host::itemCmdCalls.size() == 2);
  LOKA_VERIFY(toolbox_host::itemCmdCalls[0].menu == toolbox_host::installedMenus[0]);
  LOKA_VERIFY(toolbox_host::itemCmdCalls[0].item == 1);
  LOKA_VERIFY(toolbox_host::itemCmdCalls[0].cmd == 'C');
  LOKA_VERIFY(toolbox_host::itemCmdCalls[1].menu == toolbox_host::installedMenus[1]);
  LOKA_VERIFY(toolbox_host::itemCmdCalls[1].item == 1);
  LOKA_VERIFY(toolbox_host::itemCmdCalls[1].cmd == hMenuCmd);
}
void testToolboxShortcutChangeReprojectsThroughFullRebuild()
{
  ToolboxApp app;
  MenuBarDefinition first, changed;
  first << (Menu("File") << MenuItem("Save").shortcut('S'))
        << (Menu("Help") << MenuItem("Help").shortcut('H'));
  changed << (Menu("File") << MenuItem("Save").shortcut('X'))
          << (Menu("Help") << MenuItem("Help").shortcut('H'));
  LOKA_VERIFY(app.menuAttachment().project(sources(&first, 0)));
  clearCalls();
  LOKA_VERIFY(app.menuAttachment().project(sources(&changed, 0)));
  LOKA_VERIFY(toolbox_host::menuTitles.size() == 2 && toolbox_host::menuDisposes == 2);
  LOKA_VERIFY(toolbox_host::menuClears == 1);
  LOKA_VERIFY(toolbox_host::menuSets.size() == 2 && toolbox_host::menuSets[0] == "Save");
  LOKA_VERIFY(toolbox_host::installedMenus.size() == 2);
  LOKA_VERIFY(toolbox_host::itemCmdCalls.size() == 2);
  LOKA_VERIFY(toolbox_host::itemCmdCalls[0].menu == toolbox_host::installedMenus[0]);
  LOKA_VERIFY(toolbox_host::itemCmdCalls[0].item == 1);
  LOKA_VERIFY(toolbox_host::itemCmdCalls[0].cmd == 'X');
  clearCalls();
  LOKA_VERIFY(!app.menuAttachment().project(sources(&changed, 0)));
  noCalls();
}
namespace
{
  MenuBarDefinition titleBar(State<String> *state)
  {
    MenuBarDefinition result;
    result << (Menu("File") << MenuItem("Fallback").text(state));
    result << (Menu("Edit") << MenuItem("Copy"));
    return result;
  }
  void setTitle(PushStateTracker &tracker, MutableState<String> &state, const char *value)
  {
    StateTrackerGuard guard(&tracker);
    state.set(String::Literal(value));
  }
}
void testToolboxMenuTitleFollowsState()
{
  MutableState<String> title(String::Literal("Alpha"));
  PushStateTracker tracker;
  tracker.addState(&title);
  ToolboxApp app;
  MenuBarDefinition offered = titleBar(&title);
  clearCalls();
  LOKA_VERIFY(app.menuAttachment().project(sources(&offered, 0)));
  LOKA_VERIFY(toolbox_host::installedMenus[0]->items[0] == "Alpha");
  const int before = app.redraws;
  clearCalls();
  {
    StateTrackerGuard guard(&tracker);
    title.set(String::Literal("Beta"));
  }
  LOKA_VERIFY(toolbox_host::menuSets.size() == 1 && toolbox_host::menuSets[0] == "Beta");
  LOKA_VERIFY(app.redraws == before + 1);
  clearCalls();
  LOKA_VERIFY(!app.menuAttachment().project(sources(&offered, 0)));
  noCalls(); // Live value is not definition identity.
}
void testToolboxMenuTitleStateUnbindsOnRelease()
{
  MutableState<String> title(String::Literal("Alpha"));
  PushStateTracker tracker;
  tracker.addState(&title);
  ToolboxApp app;
  Scene source((Boundary<Root>()));
  MenuBarDefinition offered = titleBar(&title);
  LOKA_VERIFY(app.menuAttachment().project(sources(&offered, &source)));
  clearCalls();
  setTitle(tracker, title, "Beta");
  LOKA_VERIFY(toolbox_host::menuSets.size() == 1);
  app.menuAttachment().releaseFrom(&source);
  const int before = app.redraws;
  clearCalls();
  setTitle(tracker, title, "Gamma");
  noCalls();
  LOKA_VERIFY(app.redraws == before);
}
void testToolboxMenuTitleBindingFollowsNewDefinition()
{
  MutableState<String> first(String::Literal("Alpha")), second(String::Literal("Alpha"));
  PushStateTracker tracker;
  tracker.addState(&first);
  tracker.addState(&second);
  ToolboxApp app;
  MenuBarDefinition offered = titleBar(&first), equal = titleBar(&first), changed = titleBar(&second);
  LOKA_VERIFY(app.menuAttachment().project(sources(&offered, 0)));
  clearCalls();
  LOKA_VERIFY(app.menuAttachment().project(sources(&equal, 0)));
  LOKA_VERIFY(toolbox_host::menuTitles.size() == 2 && toolbox_host::menuDisposes == 2);
  clearCalls();
  LOKA_VERIFY(app.menuAttachment().project(sources(&changed, 0)));
  LOKA_VERIFY(toolbox_host::menuSets.size() == 2 && toolbox_host::menuSets[0] == "Alpha");
  LOKA_VERIFY(toolbox_host::menuTitles.size() == 2 && toolbox_host::menuDisposes == 2);
  clearCalls();
  setTitle(tracker, first, "Old");
  noCalls();
  setTitle(tracker, second, "New");
  LOKA_VERIFY(toolbox_host::menuSets.size() == 1 && toolbox_host::installedMenus[0]->items[0] == "New");
}
void testToolboxAboutTitleFollowsStateAcrossRebuild()
{
  MutableState<String> first(String::Literal("About Alpha")), second(String::Literal("About Beta"));
  PushStateTracker tracker;
  tracker.addState(&first);
  tracker.addState(&second);
  ToolboxApp app;
  MenuBarDefinition offered, changed;
  offered << (AppMenu() << MenuItem("Fallback").text(&first).actionType(MENU_ACTION_ABOUT_APP));
  changed << (AppMenu() << MenuItem("Fallback").text(&second).actionType(MENU_ACTION_ABOUT_APP));
  LOKA_VERIFY(app.menuAttachment().project(sources(&offered, 0)));
  LOKA_VERIFY(toolbox_host::installedMenus[0]->items[0] == "About Alpha");
  LOKA_VERIFY(app.menuAttachment().project(sources(&changed, 0)));
  LOKA_VERIFY(toolbox_host::installedMenus[0]->items[0] == "About Beta");
  clearCalls();
  setTitle(tracker, first, "Old");
  noCalls();
  setTitle(tracker, second, "About Gamma");
  LOKA_VERIFY(toolbox_host::menuSets.size() == 1 && toolbox_host::menuSets[0] == "About Gamma");
  LOKA_VERIFY(app.menuAttachment().project(sources(0, 0)));
  clearCalls();
  setTitle(tracker, second, "Released");
  noCalls();
}
void testToolboxSubmenuTitleFollowsState()
{
  MutableState<String> title(String::Literal("More"));
  PushStateTracker tracker;
  tracker.addState(&title);
  ToolboxApp app;
  MenuBarDefinition offered;
  offered << (Menu("File") << (MenuItem("Fallback").text(&title) << MenuItem("Child")));
  LOKA_VERIFY(app.menuAttachment().project(sources(&offered, 0)));
  LOKA_VERIFY(toolbox_host::installedMenus[1]->items[0] == "More");
  clearCalls();
  setTitle(tracker, title, "Other");
  LOKA_VERIFY(toolbox_host::menuSets.size() == 1 && toolbox_host::menuSets[0] == "Other");
  LOKA_VERIFY(toolbox_host::itemCmdCalls.empty());
  app.menuAttachment().disconnect();
  clearCalls();
  setTitle(tracker, title, "Released");
  noCalls();
}
int main(int argc, char **argv)
{
  struct Test { const char *name; void (*run)(); };
#define PIN(name) {#name, &name}
  const Test tests[] = {
    PIN(testToolboxMenuBridgeRefusalPreservesNativeAndRetries),
    PIN(testToolboxMenuBridgeReleaseThenAbsentClearsNative),
    PIN(testToolboxMenuTitleFollowsState),
    PIN(testToolboxSubmenuTitleFollowsState),
    PIN(testToolboxMenuTitleStateUnbindsOnRelease),
    PIN(testToolboxMenuTitleBindingFollowsNewDefinition),
    PIN(testToolboxAboutTitleFollowsStateAcrossRebuild),
    PIN(testToolboxMenuDoorFiltersRowsAndForwardsScene),
    PIN(testToolboxMenuProjectionDefersBackgroundDraws),
    PIN(testToolboxShortcutChangeReprojectsThroughFullRebuild),
    PIN(testToolboxHierarchicalOpenerKeepsMarkerOverShortcut),
    PIN(testToolboxSubmenuIdDiffersFromItsParent),
    PIN(testToolboxParentItemDoesNotDispatchItsSubmenuItem),
    PIN(testToolboxQuitDefaultsToCommandQ),
    PIN(testToolboxMenuShortcutProjectsItemCmd),
    PIN(testToolboxMenuAttachmentProjectsOncePerIdentity),
    PIN(testToolboxMenuAttachmentSeesItemChange),
    PIN(testToolboxMenuAttachmentChangedOverlayRebuildsFully),
    PIN(testToolboxMenuAttachmentReleaseFromSourceDisconnects),
    PIN(testToolboxSceneDetachReleasesMenu),
    PIN(testToolboxMenuDispatchAfterDisconnectIsDropped),
    PIN(testToolboxMenuDispatchMayDisconnectWhileRunning),
    PIN(testToolboxMenuAttachmentMergeRefusalPreservesRecord),
    PIN(testToolboxMenuDispatchKeepsActionAcrossReplacement),
    PIN(testToolboxMenuAttachmentSwitchesSourceAndClearsNullBar),
    PIN(testToolboxMenuAttachmentEmptyMenuRebuildUsesPreparedMerge),
    PIN(testToolboxAppShutdownReleasesMenuBeforeAttachmentDestruction),
    PIN(testToolboxAppShutdownDrainsPendingCloseBeforeAttachmentDestruction),
    PIN(testAppRetireComponentsIsIdempotent),
    PIN(testAppDestructorRetiresComponentsWithoutRailOwner)
  };
#undef PIN
  for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); ++i)
    if (argc == 1 || std::strcmp(argv[1], tests[i].name) == 0)
    {
      tests[i].run();
      std::printf("PASS %s\n", tests[i].name);
    }
}
