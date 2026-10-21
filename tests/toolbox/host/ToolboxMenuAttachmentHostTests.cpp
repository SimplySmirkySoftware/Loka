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
  std::vector<std::string> deskAccessories, resourceMenuItems;
  std::vector<ItemCmdCall> itemCmdCalls, enabledCalls, checkedCalls;
  void (*afterMenuSet)() = 0;
  unsigned beeps = 0;
  unsigned menuDraws = 0, menuClears = 0, menuDisposes = 0, menuValueWrites = 0;
  int menuColorEntries = 0;
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
    toolbox_host::enabledCalls.clear();
    toolbox_host::checkedCalls.clear();
    toolbox_host::itemMarkCalls.clear();
    toolbox_host::menuInsertions.clear();
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
      LOKA_VERIFY(!observer->attachment->dispatch(129, 1));
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
  LOKA_VERIFY(toolbox_host::menuTitles.size() == 3 && toolbox_host::menuSets.size() == 2);
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
  LOKA_VERIFY(toolbox_host::menuDisposes == 3 && toolbox_host::menuTitles.size() == 3);
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
  LOKA_VERIFY(toolbox_host::menuTitles.size() == 2 && toolbox_host::menuDisposes == 2);
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
  LOKA_VERIFY(toolbox_host::menuTitles.size() == 3 && toolbox_host::menuDisposes == 3);
  LOKA_VERIFY(toolbox_host::menuSets.size() == 2 && toolbox_host::menuSets[0] == "Save");
  LOKA_VERIFY(toolbox_host::installedMenus[2]->items[0] == "Copy");
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
  LOKA_VERIFY(f.app.menuAttachment().dispatch(129, 1));
  LOKA_VERIFY(f.observation.calls == 1);
  const int before = f.app.redraws;
  f.root()->disable();
  LOKA_VERIFY(f.app.redraws == before + 1); // Positive control: live State reaches the thunk.
  clearCalls();
  f.app.menuAttachment().releaseFrom(f.window.scene());
  noCalls();
  LOKA_VERIFY(!f.app.menuAttachment().dispatch(129, 1));
  LOKA_VERIFY(toolbox_host::installedMenus.size() == 2);
  f.root()->disable();
  noCalls();
  MenuBarDefinition replacement = bar();
  LOKA_VERIFY(f.app.menuAttachment().project(sources(&replacement, 0)));
  LOKA_VERIFY(toolbox_host::menuDisposes == 2);
  f.app.menuAttachment().disconnect();
}
void testToolboxSceneDetachReleasesMenu()
{
  Mounted f;
  // A local copy makes overlay address reuse deterministic, including under
  // ASan. Its endpoints still belong to the mounted Scene composition.
  MenuBarDefinition reused(*f.window.scene()->menuBar());
  LOKA_VERIFY(f.app.menuAttachment().project(sources(&reused, f.window.scene())));
  LOKA_VERIFY(f.app.menuAttachment().dispatch(129, 1));
  clearCalls();
  f.window.unmount();
  LOKA_VERIFY(f.observation.detaches == 1 && f.observation.calls == 1);
  LOKA_VERIFY(!f.app.menuAttachment().dispatch(129, 1));
  noCalls();
  reused.~MenuBarDefinition();
  f.window.scene()->mount(&f.controller);
  LOKA_VERIFY(f.window.scene()->menuBar());
  new (&reused) MenuBarDefinition(*f.window.scene()->menuBar());
  LOKA_VERIFY(f.app.menuAttachment().project(sources(&reused, f.window.scene())));
  LOKA_VERIFY(toolbox_host::menuDisposes == 2 && toolbox_host::menuTitles.size() == 2);
  LOKA_VERIFY(f.app.menuAttachment().dispatch(129, 1));
  LOKA_VERIFY(f.observation.calls == 2);
  f.window.unmount();
}
void testToolboxMenuDispatchAfterDisconnectIsDropped()
{
  Mounted f;
  LOKA_VERIFY(f.app.menuAttachment().dispatch(129, 1));
  LOKA_VERIFY(f.observation.calls == 1);
  f.app.menuAttachment().disconnect();
  LOKA_VERIFY(!f.app.menuAttachment().dispatch(129, 1));
  LOKA_VERIFY(f.observation.calls == 1);
  clearCalls();
  LOKA_VERIFY(f.app.menuAttachment().project(sources(f.window.scene()->menuBar(), f.window.scene())));
  LOKA_VERIFY(toolbox_host::menuDisposes == 2 && toolbox_host::menuTitles.size() == 2);
}
void testToolboxMenuDispatchMayDisconnectWhileRunning()
{
  Mounted f;
  f.observation.disconnectInAction = true;
  LOKA_VERIFY(f.app.menuAttachment().dispatch(129, 1));
  LOKA_VERIFY(f.observation.calls == 1);
  LOKA_VERIFY(!f.app.menuAttachment().dispatch(129, 1));
}
void testToolboxMenuAttachmentMergeRefusalPreservesRecord()
{
  ToolboxApp app;
  const MenuBarDefinition first = checkedBar(false), second = checkedBar(true);
  LOKA_VERIFY(app.menuAttachment().project(sources(&first, 0)));
  MenuHandle installed = toolbox_host::installedMenus[1];
  loka::app::testing::failMenuBarDefinitionClones(1);
  clearCalls();
  LOKA_VERIFY(!app.menuAttachment().project(sources(&second, 0)));
  noCalls();
  LOKA_VERIFY(toolbox_host::installedMenus[1] == installed);
  LOKA_VERIFY(!app.menuAttachment().project(sources(&first, 0)));
  noCalls(); // Refusal preserved both native truth and the previous record.
  LOKA_VERIFY(app.menuAttachment().project(sources(&second, 0)));
  LOKA_VERIFY(toolbox_host::menuTitles.size() == 2 && toolbox_host::menuDisposes == 2);
  clearCalls();
  LOKA_VERIFY(!app.menuAttachment().project(sources(&second, 0)));
  noCalls();
  loka::app::testing::failMenuItemDefinitionClones(1);
  LOKA_VERIFY(!app.menuAttachment().project(sources(&first, 0)));
  noCalls();
  LOKA_VERIFY(!app.menuAttachment().project(sources(&second, 0)));
  noCalls();
  LOKA_VERIFY(app.menuAttachment().project(sources(&first, 0)));
  LOKA_VERIFY(toolbox_host::menuDisposes == 2 && toolbox_host::menuTitles.size() == 2);
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
  LOKA_VERIFY(app.menuAttachment().dispatch(129, 1));
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
  LOKA_VERIFY(toolbox_host::menuDisposes == 3 && toolbox_host::menuTitles.size() == 3);
  clearCalls();
  LOKA_VERIFY(app.menuAttachment().project(sources(0, 0)));
  LOKA_VERIFY(toolbox_host::menuDisposes == 3 && toolbox_host::menuClears == 1 && toolbox_host::menuDraws == 1);
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
  LOKA_VERIFY(toolbox_host::menuTitles.size() == 3 && toolbox_host::menuDraws == 1);
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
      LOKA_VERIFY(app.menuAttachment().dispatch(129, 1));
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
    LOKA_VERIFY(toolbox_host::menuDisposes == 2);
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
  LOKA_VERIFY(toolbox_host::installedMenus.size() == 3);
  LOKA_VERIFY(toolbox_host::installedMenus[1]->items[0] == "Run");
  LOKA_VERIFY(toolbox_host::installedMenus[2]->items[0] == "Copy");
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
  MenuHandle installed = toolbox_host::installedMenus[1];
  clearCalls();
  loka::app::testing::failMenuBarDefinitionClones(1);
  app.projectMenu(0, replacement);
  loka::app::testing::allowMenuBarDefinitionClones();
  noCalls();
  LOKA_VERIFY(toolbox_host::installedMenus[1] == installed);
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
  LOKA_VERIFY(toolbox_host::menuInitializations == 0);
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
  LOKA_VERIFY(toolbox_host::menuTitles.size() == 3);
  // InitMenus draws an empty menu bar itself, past the App's draw gate, so a
  // projection must not call it: the only screen write is the deferred draw.
  LOKA_VERIFY(toolbox_host::menuInitializations == 0);
  LOKA_VERIFY(toolbox_host::menuDraws == 0 && app.drawOwed());
  app.phase(ACTIVATION_FOREGROUND);
  LOKA_VERIFY(toolbox_host::menuDraws == 1 && !app.drawOwed());
  app.phase(ACTIVATION_FOREGROUND);
  LOKA_VERIFY(toolbox_host::menuDraws == 1);
  app.phase(ACTIVATION_BACKGROUND);
  clearCalls();
  LOKA_VERIFY(app.menuAttachment().project(sources(&partial, 0)));
  LOKA_VERIFY(toolbox_host::menuTitles.size() == 3 && toolbox_host::menuSets.size() == 2);
  LOKA_VERIFY(toolbox_host::menuDraws == 0 && app.drawOwed());
  app.phase(ACTIVATION_FOREGROUND);
  LOKA_VERIFY(toolbox_host::menuDraws == 1 && !app.drawOwed());
  app.phase(ACTIVATION_BACKGROUND);
  clearCalls();
  LOKA_VERIFY(app.menuAttachment().project(sources(0, 0)));
  LOKA_VERIFY(toolbox_host::installedMenus.empty());
  LOKA_VERIFY(toolbox_host::menuInitializations == 0);
  LOKA_VERIFY(toolbox_host::menuDraws == 0 && app.drawOwed());
  app.phase(ACTIVATION_FOREGROUND);
  LOKA_VERIFY(toolbox_host::menuDraws == 1 && !app.drawOwed());
  clearCalls();
  LOKA_VERIFY(app.menuAttachment().project(sources(&first, 0)));
  LOKA_VERIFY(toolbox_host::menuDraws == 1 && !app.drawOwed());
}
void testToolboxMenuProjectionKeepsMenuColors()
{
  // InitMenus loaded 'mctb' 0 into the color table; ClearMenuBar deletes its
  // entries. A projection must keep the table it found (bot P2 on #1526).
  ToolboxApp app;
  MenuBarDefinition first = bar(), second = bar("Save");
  toolbox_host::menuColorEntries = 3;
  LOKA_VERIFY(app.menuAttachment().project(sources(&first, 0)));
  LOKA_VERIFY(toolbox_host::menuColorEntries == 3);
  LOKA_VERIFY(app.menuAttachment().project(sources(&second, 0)));
  LOKA_VERIFY(toolbox_host::menuColorEntries == 3);
  LOKA_VERIFY(app.menuAttachment().project(sources(0, 0)));
  LOKA_VERIFY(toolbox_host::menuColorEntries == 3);
  toolbox_host::menuColorEntries = 0;
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
  LOKA_VERIFY(toolbox_host::itemCmdCalls[0].menu == toolbox_host::installedMenus[1]);
  LOKA_VERIFY(toolbox_host::itemCmdCalls[0].item == 3);
  LOKA_VERIFY(toolbox_host::itemCmdCalls[0].cmd == 'S');
  LOKA_VERIFY(toolbox_host::itemCmdCalls[1].menu == toolbox_host::installedMenus[1]);
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
    LOKA_VERIFY(toolbox_host::itemCmdCalls[i].menu == toolbox_host::installedMenus[1]);
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
  LOKA_VERIFY(toolbox_host::installedMenus.size() == 4);
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
    // File is menu 129 (after Apple): its item 2 is Close, while the submenu's
    // item 2 is Second. Choosing Close must not run Second.
    LOKA_VERIFY(app.menuAttachment().dispatch(129, 2));
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
  LOKA_VERIFY(toolbox_host::installedMenus.size() == 3);
  LOKA_VERIFY(toolbox_host::itemCmdCalls.size() == 2);
  LOKA_VERIFY(toolbox_host::itemCmdCalls[0].menu == toolbox_host::installedMenus[1]);
  LOKA_VERIFY(toolbox_host::itemCmdCalls[0].item == 1);
  LOKA_VERIFY(toolbox_host::itemCmdCalls[0].cmd == 'C');
  LOKA_VERIFY(toolbox_host::itemCmdCalls[1].menu == toolbox_host::installedMenus[2]);
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
  LOKA_VERIFY(toolbox_host::menuTitles.size() == 3 && toolbox_host::menuDisposes == 3);
  LOKA_VERIFY(toolbox_host::menuClears == 1);
  LOKA_VERIFY(toolbox_host::menuSets.size() == 2 && toolbox_host::menuSets[0] == "Save");
  LOKA_VERIFY(toolbox_host::installedMenus.size() == 3);
  LOKA_VERIFY(toolbox_host::itemCmdCalls.size() == 2);
  LOKA_VERIFY(toolbox_host::itemCmdCalls[0].menu == toolbox_host::installedMenus[1]);
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
  LOKA_VERIFY(toolbox_host::installedMenus[1]->items[0] == "Alpha");
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
  LOKA_VERIFY(toolbox_host::menuTitles.size() == 3 && toolbox_host::menuDisposes == 3);
  clearCalls();
  LOKA_VERIFY(app.menuAttachment().project(sources(&changed, 0)));
  LOKA_VERIFY(toolbox_host::menuSets.size() == 2 && toolbox_host::menuSets[0] == "Alpha");
  LOKA_VERIFY(toolbox_host::menuTitles.size() == 3 && toolbox_host::menuDisposes == 3);
  clearCalls();
  setTitle(tracker, first, "Old");
  noCalls();
  setTitle(tracker, second, "New");
  LOKA_VERIFY(toolbox_host::menuSets.size() == 1 && toolbox_host::installedMenus[1]->items[0] == "New");
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
  LOKA_VERIFY(toolbox_host::installedMenus[2]->items[0] == "More");
  clearCalls();
  setTitle(tracker, title, "Other");
  LOKA_VERIFY(toolbox_host::menuSets.size() == 1 && toolbox_host::menuSets[0] == "Other");
  LOKA_VERIFY(toolbox_host::itemCmdCalls.empty());
  app.menuAttachment().disconnect();
  clearCalls();
  setTitle(tracker, title, "Released");
  noCalls();
}
namespace
{
  void disposedSince(size_t first)
  {
    for (size_t i = first; i < toolbox_host::menuAllocations.size(); ++i)
      LOKA_VERIFY(toolbox_host::menuAllocations[i].disposals == 1);
  }
  void failedCandidate(bool shared)
  {
    MutableState<String> oldTitle(String::Literal("Old")), newTitle(String::Literal("New"));
    PushStateTracker tracker;
    tracker.addState(&oldTitle);
    tracker.addState(&newTitle);
    ToolboxApp app;
    struct Count
    {
      static void run(void *data)
      {
        ++*static_cast<unsigned *>(data);
      }
    };
    unsigned clicks = 0;
    EmitterState emitter;
    emitter.deferBind(&Count::run, &clicks);
    MenuBarDefinition a, b = titleBar(shared ? &oldTitle : &newTitle);
    a << (Menu("File") << MenuItem("Old").text(&oldTitle).onClick(&emitter));
    LOKA_VERIFY(app.menuAttachment().project(sources(&a, 0)));
    MenuHandle old = toolbox_host::installedMenus[1];
    size_t first = toolbox_host::menuAllocations.size();
    clearCalls();
    toolbox_host::testing::failMenuCreationCall(3);
    LOKA_VERIFY(!app.menuAttachment().project(sources(&b, 0)));
    LOKA_VERIFY(toolbox_host::menuClears == 0 && toolbox_host::menuInsertions.empty());
    LOKA_VERIFY(toolbox_host::installedMenus[1] == old);
    disposedSince(first);
    LOKA_VERIFY(app.menuAttachment().dispatch(129, 1));
    LOKA_VERIFY(clicks == 1);
    clearCalls();
    setTitle(tracker, newTitle, "Candidate gone");
    noCalls();
    setTitle(tracker, oldTitle, "Still live");
    LOKA_VERIFY(toolbox_host::menuSets.size() == 1 && old->items[0] == "Still live");
    clearCalls();
    LOKA_VERIFY(!app.menuAttachment().project(sources(&a, 0)));
    noCalls();
    LOKA_VERIFY(app.menuAttachment().project(sources(&b, 0)));
    clearCalls();
    LOKA_VERIFY(!app.menuAttachment().project(sources(&b, 0)));
    noCalls();
    emitter.deferUnbind(&Count::run, &clicks);
  }
  void refusedItem(unsigned kind)
  {
    ToolboxApp app;
    MenuBarDefinition a = bar(), b;
    if (kind == 0) b << (Menu("File") << MenuItem("Before") << MenuSeparator() << MenuItem("After"));
    if (kind == 1)
      b << (Menu("File") << MenuItem("Leaf"));
    if (kind == 2)
      b << (AppMenu() << MenuItem("About").actionType(MENU_ACTION_ABOUT_APP));
    if (kind == 3)
      b << (Menu("File") << (MenuItem("More") << MenuItem("Child")));
    LOKA_VERIFY(app.menuAttachment().project(sources(&a, 0)));
    clearCalls();
    size_t first = toolbox_host::menuAllocations.size();
    if (kind == 0) toolbox_host::testing::failItemInserts(1);
    if (kind == 1 || kind == 3)
      toolbox_host::testing::failPlaceholderAppends(1);
    if (kind == 2)
      toolbox_host::testing::failItemInserts(1);
    LOKA_VERIFY(!app.menuAttachment().project(sources(&b, 0)));
    LOKA_VERIFY(toolbox_host::menuClears == 0 && toolbox_host::menuInsertions.empty());
    if (kind == 3) LOKA_VERIFY(toolbox_host::menuTitles.size() == 2);
    disposedSince(first);
    clearCalls();
    LOKA_VERIFY(!app.menuAttachment().project(sources(&a, 0)));
    noCalls();
    LOKA_VERIFY(app.menuAttachment().project(sources(&b, 0)));
  }
} // namespace
void testToolboxCandidateFailurePreservesInstalled()
{
  failedCandidate(false);
}
void testToolboxCandidateUnbindKeepsSharedState()
{
  failedCandidate(true);
}
void testToolboxSeparatorRefusal()
{
  refusedItem(0);
}
void testToolboxPlaceholderRefusal()
{
  refusedItem(1);
}
void testToolboxAboutRefusal()
{
  refusedItem(2);
}
void testToolboxOpenerRefusal()
{
  refusedItem(3);
}
void testToolboxMenuCreationRefusals()
{
  ToolboxApp app;
  MenuBarDefinition apple, nested;
  apple << (AppMenu() << MenuItem("About").actionType(MENU_ACTION_ABOUT_APP));
  nested << (Menu("File") << (MenuItem("More") << (MenuItem("Deeper") << MenuItem("Child"))));
  clearCalls();
  toolbox_host::testing::failMenuCreations(2);
  LOKA_VERIFY(!app.menuAttachment().project(sources(&apple, 0)));
  LOKA_VERIFY(!app.menuAttachment().project(sources(&apple, 0)));
  LOKA_VERIFY(toolbox_host::menuClears == 0);
  LOKA_VERIFY(app.menuAttachment().project(sources(&apple, 0)));
  for (unsigned call = 1; call <= 4; ++call)
  {
    clearCalls();
    size_t first = toolbox_host::menuAllocations.size();
    toolbox_host::testing::failMenuCreationCall(call);
    LOKA_VERIFY(!app.menuAttachment().project(sources(&nested, 0)));
    LOKA_VERIFY(toolbox_host::menuClears == 0 && toolbox_host::menuInsertions.empty());
    disposedSince(first);
  }
  LOKA_VERIFY(app.menuAttachment().project(sources(&nested, 0)));
}
void testToolboxGraphPrunesExactlyOnce()
{
  size_t first = toolbox_host::menuAllocations.size();
  {
    ToolboxApp app;
    MenuBarDefinition empty;
    empty << AppMenu() << Menu("Empty")
          << (Menu("Nested") << (MenuItem("More")
                                 << (MenuItem("Deeper") << MenuItem("Hidden").attr(MenuItemAttr().visible(false)))));
    clearCalls();
    LOKA_VERIFY(app.menuAttachment().project(sources(&empty, 0)));
    LOKA_VERIFY(toolbox_host::installedMenus.size() == 1 && toolbox_host::installedMenus[0]->id == 128);
    LOKA_VERIFY(toolbox_host::menuAllocations.size() == first + 5);
    disposedSince(first + 1);
    MenuBarDefinition two;
    two << (AppMenu() << MenuItem("About").actionType(MENU_ACTION_ABOUT_APP))
        << (AppMenu() << MenuItem("Other About").actionType(MENU_ACTION_ABOUT_APP));
    LOKA_VERIFY(app.menuAttachment().project(sources(&two, 0)));
    LOKA_VERIFY(toolbox_host::installedMenus.size() == 1);
    LOKA_VERIFY(toolbox_host::menuAllocations.size() == first + 6);
  }
  disposedSince(first);
}
void testToolboxEmptyAbsenceClearsRecord()
{
  ToolboxApp app;
  MenuBarDefinition empty;
  empty << Menu("Empty");
  LOKA_VERIFY(app.menuAttachment().project(sources(&empty, 0)));
  clearCalls();
  LOKA_VERIFY(app.menuAttachment().project(sources(0, 0)));
  clearCalls();
  LOKA_VERIFY(!app.menuAttachment().project(sources(0, 0)));
  noCalls();
  LOKA_VERIFY(app.menuAttachment().project(sources(&empty, 0)));
  LOKA_VERIFY(toolbox_host::menuTitles.size() == 2);
}
void testToolboxSubmenuLimitAndInsertionOrder()
{
  MutableState<String> title(String::Literal("Bound"));
  PushStateTracker tracker;
  tracker.addState(&title);
  ToolboxApp app;
  MenuBarDefinition limit, overflow;
  // Apple takes 128; 105 ordinary menus take 129..233; parent 234, child 235.
  limit << (AppMenu() << MenuItem("About").actionType(MENU_ACTION_ABOUT_APP));
  overflow << (AppMenu() << MenuItem("About").actionType(MENU_ACTION_ABOUT_APP));
  for (unsigned i = 0; i < 105; ++i)
  {
    limit << (Menu("Filler") << MenuItem("Leaf").text(&title));
    overflow << (Menu("Filler") << MenuItem("Leaf").text(&title));
  }
  limit << (Menu("Parent") << (MenuItem("More") << MenuItem("Child")));
  overflow << (Menu("Parent") << (MenuItem("More") << (MenuItem("Too deep") << MenuItem("Child"))));
  clearCalls();
  LOKA_VERIFY(app.menuAttachment().project(sources(&limit, 0)));
  LOKA_VERIFY(toolbox_host::menuInsertions.size() == 108);
  for (size_t i = 0; i < 106; ++i)
  {
    LOKA_VERIFY(toolbox_host::menuInsertions[i].handle->id == 128 + static_cast<short>(i));
    LOKA_VERIFY(toolbox_host::menuInsertions[i].mode == 0);
  }
  LOKA_VERIFY(toolbox_host::menuInsertions[106].handle->id == 235);
  LOKA_VERIFY(toolbox_host::menuInsertions[106].mode == kInsertHierarchicalMenu);
  LOKA_VERIFY(toolbox_host::menuInsertions[107].handle->id == 234);
  LOKA_VERIFY(toolbox_host::menuInsertions[107].mode == 0);
  LOKA_VERIFY(toolbox_host::itemMarkCalls.size() == 1);
  LOKA_VERIFY(toolbox_host::itemMarkCalls[0].menu == toolbox_host::installedMenus[107]);
  LOKA_VERIFY(toolbox_host::itemMarkCalls[0].item == 1 && toolbox_host::itemMarkCalls[0].cmd == 235);
  size_t first = toolbox_host::menuAllocations.size();
  clearCalls();
  LOKA_VERIFY(!app.menuAttachment().project(sources(&overflow, 0)));
  LOKA_VERIFY(toolbox_host::menuClears == 0 && toolbox_host::menuInsertions.empty());
  LOKA_VERIFY(toolbox_host::menuTitles.size() == 108); // Never construct ID 236.
  disposedSince(first);
  clearCalls();
  setTitle(tracker, title, "Still bound");
  LOKA_VERIFY(toolbox_host::menuSets.size() == 105);
}

namespace
{
  MenuHandle nativeMenu(short id)
  {
    for (size_t i = 0; i < toolbox_host::installedMenus.size(); ++i)
      if (toolbox_host::installedMenus[i]->id == id) return toolbox_host::installedMenus[i];
    return 0;
  }
  short nativeKey(MenuHandle menu, short item)
  {
    for (size_t i = toolbox_host::itemCmdCalls.size(); i > 0; --i)
      if (toolbox_host::itemCmdCalls[i - 1].menu == menu && toolbox_host::itemCmdCalls[i - 1].item == item)
        return toolbox_host::itemCmdCalls[i - 1].cmd;
    return 0;
  }
} // namespace
void testToolboxAppQuitCreatesFileFirst()
{
  ToolboxApp app;
  MenuBarDefinition offered;
  offered << (AppMenu() << MenuItem("About").actionType(MENU_ACTION_ABOUT_APP) << MenuSeparator()
                        << MenuItem("Quit").actionType(MENU_ACTION_QUIT_APP))
          << (Menu("Game") << MenuItem("Play"));
  clearCalls();
  LOKA_VERIFY(app.menuAttachment().project(sources(&offered, 0)));
  LOKA_VERIFY(nativeMenu(128) && nativeMenu(129) && nativeMenu(130));
  LOKA_VERIFY(toolbox_host::menuTitles.size() == 3 && toolbox_host::menuTitles[0] == std::string(1, 0x14));
  LOKA_VERIFY(toolbox_host::menuTitles[1] == "File" && toolbox_host::menuTitles[2] == "Game");
  LOKA_VERIFY(nativeMenu(128)->items[0] == "About" && nativeMenu(128)->items.back() == "Desk accessory");
  LOKA_VERIFY(nativeMenu(129)->items.size() == 1 && nativeMenu(129)->items[0] == "Quit");
  LOKA_VERIFY(nativeKey(nativeMenu(129), 1) == 'Q');
  LOKA_VERIFY(app.menuAttachment().dispatch(129, 1) && app.quits == 1);
}
void testToolboxAppQuitReplacesOrdinaryLeaves()
{
  MutableState<String> hiddenTitle(String::Literal("Old quit"));
  PushStateTracker tracker;
  tracker.addState(&hiddenTitle);
  ToolboxApp app;
  MenuBarDefinition offered;
  offered << (AppMenu() << MenuItem("About").actionType(MENU_ACTION_ABOUT_APP) << MenuSeparator()
                        << MenuItem("Leave").actionType(MENU_ACTION_QUIT_APP).shortcut('x'))
          << (Menu("File") << MenuItem("Open") << MenuSeparator()
                           << MenuItem("Ordinary").text(&hiddenTitle).actionType(MENU_ACTION_QUIT_APP).shortcut('z')
                           << MenuSeparator() << MenuItem("Save") << MenuSeparator())
          << (Menu("File") << MenuItem("Second"));
  clearCalls();
  LOKA_VERIFY(app.menuAttachment().project(sources(&offered, 0)));
  MenuHandle file = nativeMenu(129);
  LOKA_VERIFY(file && file->items.size() == 5);
  LOKA_VERIFY(file->items[0] == "Open" && file->items[1] == "-" && file->items[2] == "Save" && file->items[3] == "-" &&
              file->items[4] == "Leave");
  LOKA_VERIFY(nativeKey(file, 5) == 'X' && toolbox_host::itemCmdCalls.size() == 1);
  LOKA_VERIFY(nativeMenu(130)->items.size() == 1);
  LOKA_VERIFY(!app.menuAttachment().dispatch(129, 6));
  clearCalls();
  setTitle(tracker, hiddenTitle, "Must not bind");
  noCalls();
  LOKA_VERIFY(app.menuAttachment().dispatch(129, 5) && app.quits == 1);
}
void testToolboxInvisibleAppQuitKeepsOrdinary()
{
  ToolboxApp app;
  MenuBarDefinition offered;
  offered << (AppMenu() << MenuItem("Hidden").actionType(MENU_ACTION_QUIT_APP).attr(MenuItemAttr().visible(false)))
          << (Menu("File") << MenuItem("Ordinary").actionType(MENU_ACTION_QUIT_APP));
  clearCalls();
  LOKA_VERIFY(app.menuAttachment().project(sources(&offered, 0)));
  LOKA_VERIFY(nativeMenu(129) && nativeMenu(129)->items.size() == 1 && nativeMenu(129)->items[0] == "Ordinary");
  LOKA_VERIFY(nativeKey(nativeMenu(129), 1) == 'Q');
  LOKA_VERIFY(app.menuAttachment().dispatch(129, 1) && app.quits == 1);
}
void testToolboxAppTitlesFallbackAndKeys()
{
  MutableState<String> about(String::Literal("")), quit(String::Literal(""));
  PushStateTracker tracker;
  tracker.addState(&about);
  tracker.addState(&quit);
  ToolboxApp app;
  MenuBarDefinition offered;
  offered << (AppMenu() << MenuItem("Constant ignored").text(&about).actionType(MENU_ACTION_ABOUT_APP).shortcut('a')
                        << MenuItem("Constant ignored").text(&quit).actionType(MENU_ACTION_QUIT_APP).shortcut('l'));
  clearCalls();
  LOKA_VERIFY(app.menuAttachment().project(sources(&offered, 0)));
  MenuHandle apple = nativeMenu(128), file = nativeMenu(129);
  LOKA_VERIFY(apple && file && apple->items[0] == "About" && file->items[0] == "Quit");
  LOKA_VERIFY(nativeKey(apple, 1) == 'A' && nativeKey(file, 1) == 'L');
  setTitle(tracker, about, "My app");
  setTitle(tracker, quit, "Leave");
  LOKA_VERIFY(apple->items[0] == "My app" && file->items[0] == "Leave");
  setTitle(tracker, about, "");
  setTitle(tracker, quit, "");
  LOKA_VERIFY(apple->items[0] == "About" && file->items[0] == "Quit");
  LOKA_VERIFY(nativeMenu(128) == apple && nativeMenu(129) == file);
  LOKA_VERIFY(toolbox_host::itemCmdCalls.size() == 2 && nativeKey(apple, 1) == 'A' && nativeKey(file, 1) == 'L');
}
void testToolboxQuitParentsAndSeparators()
{
  ToolboxApp app;
  MenuBarDefinition offered;
  offered << (AppMenu() << MenuItem("").actionType(MENU_ACTION_QUIT_APP))
          << (Menu("File") << MenuSeparator() << MenuSeparator()
                           << (MenuItem("Parent").actionType(MENU_ACTION_QUIT_APP)
                               << MenuSeparator() << MenuItem("Removed").actionType(MENU_ACTION_QUIT_APP)
                               << MenuItem("Kept") << MenuSeparator())
                           << MenuSeparator() << MenuSeparator()
                           << (MenuItem("Empty")
                               << MenuSeparator() << MenuItem("Quit").actionType(MENU_ACTION_QUIT_APP))
                           << MenuItem("Colors").actionType(MENU_ACTION_SHOW_COLOR_PICKER)
                           << MenuItem("Hidden").attr(MenuItemAttr().visible(false)) << MenuSeparator()
                           << MenuItem("Tail") << MenuSeparator());
  clearCalls();
  LOKA_VERIFY(app.menuAttachment().project(sources(&offered, 0)));
  MenuHandle file = nativeMenu(129), sub = nativeMenu(130);
  LOKA_VERIFY(file && sub && file->items.size() == 5 && sub->items.size() == 1);
  LOKA_VERIFY(file->items[0] == "Parent" && file->items[1] == "-" && file->items[2] == "Tail" &&
              file->items[3] == "-" && file->items[4] == "Quit");
  LOKA_VERIFY(sub->items[0] == "Kept" && nativeKey(file, 1) == hMenuCmd);
  LOKA_VERIFY(!app.menuAttachment().dispatch(129, 1) && !app.menuAttachment().dispatch(130, 2));
}
void testToolboxAppleAlwaysAndDeskAccessories()
{
  ToolboxApp app;
  MenuBarDefinition offered = bar();
  clearCalls();
  LOKA_VERIFY(app.menuAttachment().project(sources(&offered, 0)));
  LOKA_VERIFY(nativeMenu(128) && nativeMenu(129) && nativeMenu(130));
  LOKA_VERIFY(toolbox_host::menuInsertions[0].handle == nativeMenu(128));
  LOKA_VERIFY(nativeMenu(128)->items.size() == 2 && nativeMenu(128)->items[0] == "-" &&
              nativeMenu(128)->items[1] == "Desk accessory");
  LOKA_VERIFY(!app.menuAttachment().dispatch(128, 2));
  app.handleMenuSelection(128, 2);
  LOKA_VERIFY(toolbox_host::deskAccessories.back() == "Desk accessory");
}
void testToolboxAppQuitRefusals()
{
  for (unsigned kind = 0; kind < 3; ++kind)
  {
    ToolboxApp app;
    MenuBarDefinition old = bar(), offered;
    offered << (AppMenu() << MenuItem("Quit").actionType(MENU_ACTION_QUIT_APP));
    if (kind == 2) offered << (Menu("File") << MenuItem("Open"));
    LOKA_VERIFY(app.menuAttachment().project(sources(&old, 0)));
    MenuHandle previous = nativeMenu(129);
    clearCalls();
    size_t first = toolbox_host::menuAllocations.size();
    if (kind == 0) toolbox_host::testing::failMenuCreationCall(2);
    if (kind == 1) toolbox_host::testing::failPlaceholderAppends(1);
    if (kind == 2) toolbox_host::testing::failSeparatorAppends(1);
    LOKA_VERIFY(!app.menuAttachment().project(sources(&offered, 0)));
    LOKA_VERIFY(toolbox_host::menuClears == 0 && toolbox_host::menuInsertions.empty() && nativeMenu(129) == previous);
    disposedSince(first);
    clearCalls();
    LOKA_VERIFY(!app.menuAttachment().project(sources(&old, 0)));
    noCalls();
    LOKA_VERIFY(app.menuAttachment().project(sources(&offered, 0)));
  }
}

void testToolboxPlacedQuitKeepsLivePayload()
{
  MutableState<bool> enabled(false), checked(true), visible(true);
  PushStateTracker tracker;
  tracker.addState(&enabled);
  tracker.addState(&checked);
  tracker.addState(&visible);
  ToolboxApp app;
  MenuBarDefinition offered;
  offered << (AppMenu() << MenuItem("")
                               .actionType(MENU_ACTION_QUIT_APP)
                               .enabled(&enabled)
                               .attr(MenuItemAttr().checked(&checked).visible(&visible)))
          << (Menu("File") << MenuItem("Open"));
  clearCalls();
  LOKA_VERIFY(app.menuAttachment().project(sources(&offered, 0)));
  MenuHandle file = nativeMenu(129);
  LOKA_VERIFY(file && file->items.size() == 3 && file->items[2] == "Quit");
  LOKA_VERIFY(toolbox_host::enabledCalls.size() == 1 && toolbox_host::enabledCalls[0].menu == file &&
              toolbox_host::enabledCalls[0].item == 3 && toolbox_host::enabledCalls[0].cmd == 0);
  LOKA_VERIFY(toolbox_host::checkedCalls.back().menu == file && toolbox_host::checkedCalls.back().item == 3 &&
              toolbox_host::checkedCalls.back().cmd == 1);
  clearCalls();
  {
    StateTrackerGuard guard(&tracker);
    enabled.set(true);
    checked.set(false);
    visible.set(false);
  }
  LOKA_VERIFY(toolbox_host::enabledCalls.size() == 1 && toolbox_host::enabledCalls[0].item == 3 &&
              toolbox_host::enabledCalls[0].cmd == 1);
  LOKA_VERIFY(toolbox_host::checkedCalls.size() == 1 && toolbox_host::checkedCalls[0].item == 3 &&
              toolbox_host::checkedCalls[0].cmd == 0);
  LOKA_VERIFY(nativeMenu(129) == file && file->items.size() == 3); // Visibility is a build snapshot.
  app.menuAttachment().disconnect();
  clearCalls();
  {
    StateTrackerGuard guard(&tracker);
    enabled.set(false);
    checked.set(true);
  }
  noCalls();
}
void testToolboxAppParentsStayUnplaced()
{
  ToolboxApp app;
  MenuBarDefinition offered;
  offered << (AppMenu() << (MenuItem("About parent").actionType(MENU_ACTION_ABOUT_APP) << MenuItem("Child"))
                        << (MenuItem("Quit parent").actionType(MENU_ACTION_QUIT_APP) << MenuItem("Child"))
                        << MenuItem("Other") << MenuSeparator()
                        << MenuItem("Colors").actionType(MENU_ACTION_SHOW_COLOR_PICKER))
          << (Menu("File") << MenuItem("Ordinary quit").actionType(MENU_ACTION_QUIT_APP));
  clearCalls();
  LOKA_VERIFY(app.menuAttachment().project(sources(&offered, 0)));
  LOKA_VERIFY(nativeMenu(128) && nativeMenu(128)->items.back() == "Desk accessory");
  LOKA_VERIFY(!app.menuAttachment().dispatch(128, 1));
  LOKA_VERIFY(nativeMenu(129) && nativeMenu(129)->items.size() == 1 && nativeMenu(129)->items[0] == "Ordinary quit");
}
void testToolboxExactFileAndEmptyTitles()
{
  ToolboxApp app;
  MenuBarDefinition offered;
  offered << (Menu("file") << MenuItem("Lower"))
          << (AppMenu() << MenuItem("").actionType(MENU_ACTION_ABOUT_APP)
                        << MenuItem("").actionType(MENU_ACTION_QUIT_APP));
  clearCalls();
  LOKA_VERIFY(app.menuAttachment().project(sources(&offered, 0)));
  LOKA_VERIFY(nativeMenu(128) && nativeMenu(128)->items[0] == "About");
  LOKA_VERIFY(nativeMenu(129) && nativeMenu(129)->items.size() == 1 && nativeMenu(129)->items[0] == "Quit");
  LOKA_VERIFY(toolbox_host::menuTitles[1] == "File" && nativeMenu(130)->items[0] == "Lower");
  LOKA_VERIFY(nativeKey(nativeMenu(128), 1) == 0);
}
void testToolboxSeparatorBeforeOpenerAndTailCollapse()
{
  ToolboxApp app;
  MenuBarDefinition offered;
  offered << (Menu("File") << MenuSeparator() << MenuItem("First") << MenuSeparator()
                           << (MenuItem("More") << MenuItem("Child")) << MenuSeparator()
                           << (MenuItem("Empty") << MenuItem("Colors").actionType(MENU_ACTION_SHOW_COLOR_PICKER))
                           << MenuSeparator() << MenuSeparator());
  clearCalls();
  LOKA_VERIFY(app.menuAttachment().project(sources(&offered, 0)));
  LOKA_VERIFY(nativeMenu(129) && nativeMenu(129)->items.size() == 3);
  LOKA_VERIFY(nativeMenu(129)->items[0] == "First" && nativeMenu(129)->items[1] == "-" &&
              nativeMenu(129)->items[2] == "More");
  LOKA_VERIFY(nativeKey(nativeMenu(129), 3) == hMenuCmd);
  LOKA_VERIFY(!app.menuAttachment().dispatch(129, 2) && !app.menuAttachment().dispatch(129, 3));
}

int main(int argc, char **argv)
{
  toolbox_host::resourceMenuItems.push_back("-");
  toolbox_host::resourceMenuItems.push_back("Desk accessory");
  struct Test { const char *name; void (*run)(); };
#define PIN(name) {#name, &name}
  const Test tests[] = {
    PIN(testToolboxPlacedQuitKeepsLivePayload),
    PIN(testToolboxAppParentsStayUnplaced),
    PIN(testToolboxExactFileAndEmptyTitles),
    PIN(testToolboxSeparatorBeforeOpenerAndTailCollapse),

    PIN(testToolboxAppQuitCreatesFileFirst),
    PIN(testToolboxAppQuitReplacesOrdinaryLeaves),
    PIN(testToolboxInvisibleAppQuitKeepsOrdinary),
    PIN(testToolboxAppTitlesFallbackAndKeys),
    PIN(testToolboxQuitParentsAndSeparators),
    PIN(testToolboxAppleAlwaysAndDeskAccessories),
    PIN(testToolboxAppQuitRefusals),

    PIN(testToolboxCandidateFailurePreservesInstalled),
    PIN(testToolboxCandidateUnbindKeepsSharedState),
    PIN(testToolboxSeparatorRefusal),
    PIN(testToolboxPlaceholderRefusal),
    PIN(testToolboxAboutRefusal),
    PIN(testToolboxOpenerRefusal),
    PIN(testToolboxMenuCreationRefusals),
    PIN(testToolboxGraphPrunesExactlyOnce),
    PIN(testToolboxEmptyAbsenceClearsRecord),
    PIN(testToolboxSubmenuLimitAndInsertionOrder),
    PIN(testToolboxMenuBridgeRefusalPreservesNativeAndRetries),
    PIN(testToolboxMenuBridgeReleaseThenAbsentClearsNative),
    PIN(testToolboxMenuTitleFollowsState),
    PIN(testToolboxSubmenuTitleFollowsState),
    PIN(testToolboxMenuTitleStateUnbindsOnRelease),
    PIN(testToolboxMenuTitleBindingFollowsNewDefinition),
    PIN(testToolboxAboutTitleFollowsStateAcrossRebuild),
    PIN(testToolboxMenuDoorFiltersRowsAndForwardsScene),
    PIN(testToolboxMenuProjectionDefersBackgroundDraws),
    PIN(testToolboxMenuProjectionKeepsMenuColors),
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
