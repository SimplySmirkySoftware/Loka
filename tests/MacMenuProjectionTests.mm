#include "MacMenuProjectionTests.hpp"
#include "support/TestVerify.hpp"
#include "MacApp.hpp"
#include "MacWindow.hpp"
#include "MacScenePlatformController.hpp"
#include "testing/MacWindowTestAccess.hpp"
#include "app/nodes/boundary/StdComposition.hpp"
#include "platform/null/NullPlatformContext.hpp"
#include "testing/scene/SceneTestFlow.hpp"
#include "core/util/StateTrackerGuard.hpp"
#include <AppKit/AppKit.h>
#include <objc/runtime.h>
#include <new>

// Test-only association observes the real target's destruction without adding
// an injected target or lifetime getter to the production attachment.
static NSMenu *gWitnessedMenu = nil;
@interface LokaMenuTargetReleaseWitness : NSObject
{
  bool *released_;
  bool *detached_;
}
- (id)initWithReleased:(bool *)released detached:(bool *)detached;
@end
@implementation LokaMenuTargetReleaseWitness
- (id)initWithReleased:(bool *)released detached:(bool *)detached
{
  self = [super init];
  if (self)
  {
    released_ = released;
    detached_ = detached;
  }
  return self;
}
- (void)dealloc
{
  *released_ = true;
  // AppKit substitutes its own application menu once the process has been
  // activated, so "detached" means our bar is no longer installed, not nil.
  *detached_ = [NSApp mainMenu] != gWitnessedMenu;
  [super dealloc];
}
@end

namespace
{
  using namespace loka::app;
  using namespace loka::app::scene;
  using namespace loka::core;
  typedef loka::dsl::testing::SceneTestAccess SceneAccess;

  struct CocoaHost
  {
    NSAutoreleasePool *pool;
    CocoaHost() : pool([[NSAutoreleasePool alloc] init])
    {
      [NSApplication sharedApplication];
      [NSApp setMainMenu:nil];
    }
    ~CocoaHost() { [this->pool drain]; }
  };

  MenuBarDefinition bar(bool checked = false)
  {
    MenuBarDefinition result;
    MenuDefinition menu = Menu("File") << MenuItem("Run").attr(MenuItemAttr().checked(checked));
    result << menu;
    return result;
  }

  MenuSources sources(const MenuBarDefinition *bar, const Scene *scene = 0)
  {
    const MenuSources result = {scene ? 0 : bar, scene ? bar : 0, scene};
    return result;
  }

  NSMenuItem *commandItem()
  {
    NSMenu *menu = [NSApp mainMenu];
    LOKA_VERIFY(menu && [menu numberOfItems] > 0);
    NSMenu *sub = [[menu itemAtIndex:0] submenu];
    LOKA_VERIFY(sub && [sub numberOfItems] > 0);
    NSMenuItem *item = [sub itemAtIndex:0];
    // The fixture nests its command to exercise recursive stripping too.
    if ([item submenu])
      item = [[item submenu] itemAtIndex:0];
    return item;
  }

  struct Observer
  {
    Observer() : attachment(0), item(nil), tag(0), calls(0), detaches(0), disconnectInAction(false) {}
    MacMenuAttachment *attachment;
    NSMenuItem *item;
    int tag;
    int calls;
    int detaches;
    bool disconnectInAction;
  };
  Observer *observer = 0;

  class Root : public BoundaryNodeFor<Root>
  {
  public:
    explicit Root(const BoundaryPropsFor<Root> &props) : BoundaryNodeFor<Root>(props)
    {
      this->state(this->enabled_, true);
      this->state(this->checked_, false);
    }
    virtual void declareBindings(BindingToken &token)
    {
      token.action(this->clicked_, this, &Root::clicked);
    }
    virtual void composeNode(NodeComposition &c)
    {
      MenuBarDefinition offered;
      offered << (Menu("File") << (MenuItem("Nested") << MenuItem("Run")
          .enabled(this->enabled_.state())
          .attr(MenuItemAttr().checked(this->checked_.state())).onClick(&this->clicked_)));
      LOKA_VERIFY(c.menuBar(offered));
    }
    virtual void detachNode(NodeComposition &)
    {
      ++observer->detaches;
      LOKA_VERIFY([observer->item target] == nil);
      LOKA_VERIFY([observer->item action] == NULL);
      LOKA_VERIFY([observer->item tag] == 0);
      LOKA_VERIFY(!observer->attachment->dispatch(observer->tag));
    }
    void toggle()
    {
      StateTrackerGuard guard(this->tracker());
      this->enabled_.set(!this->enabled_.get());
      this->checked_.set(!this->checked_.get());
    }
  private:
    void clicked()
    {
      ++observer->calls;
      if (observer->disconnectInAction)
        observer->attachment->disconnect();
    }
    NodeState<bool> enabled_;
    NodeState<bool> checked_;
    EmitterState clicked_;
  };

  WindowProps sceneProps()
  {
    WindowProps props;
    props.frame(50, 50, 320, 240).visible(true).scene(new Scene(Boundary<Root>()));
    return props;
  }

  class TestApp : public MacApp
  {
  public:
    TestApp() : MacApp(0) {}
    using MacApp::projectMenu;
    virtual void quit() {}
    void own(MacWindow *window)
    {
      this->group_ = new AppComponentGroup(std::vector<AppComponent *>(1, window));
      window->setApp(this);
    }
    void retire() { this->retireComponents(); }
  };

  struct Mounted
  {
    Observer observation;
    NullPlatformContext platform;
    TestApp app;
    MacWindow *window;
    Mounted()
    {
      observer = &this->observation;
      this->observation.attachment = &this->app.menuAttachment();
      this->window = new MacWindow(&this->platform, sceneProps());
      this->app.own(this->window);
      LOKA_VERIFY(this->window->scene()->menuBar());
      LOKA_VERIFY(this->app.menuAttachment().project(sources(this->window->scene()->menuBar(), this->window->scene())));
      this->observation.item = [commandItem() retain];
      this->observation.tag = static_cast<int>([this->observation.item tag]);
      LOKA_VERIFY(this->observation.tag != 0);
    }
    ~Mounted()
    {
      this->app.retire();
      [this->observation.item release];
      observer = 0;
    }
    Root *root()
    {
      return static_cast<Root *>(SceneAccess::rootBoundary(*this->window->scene()));
    }
  };
}

void testMacMenuAttachmentProjectsOnceForIdentity()
{
  CocoaHost host;
  MenuBarDefinition first = bar(), equal = bar();
  TestApp app;
  const MenuSources firstSources = {&first, 0, 0};
  LOKA_VERIFY(app.menuAttachment().project(firstSources));
  NSMenu *installed = [[NSApp mainMenu] retain];
  loka::app::testing::failMenuBarDefinitionClones(1);
  LOKA_VERIFY(!app.menuAttachment().project(firstSources));
  LOKA_VERIFY([NSApp mainMenu] == installed);
  OwnedDef<MenuBarDefinition> refused(first.clone());
  LOKA_VERIFY(!refused.isSet()); // Suppression did not consume the clone failure.
  loka::app::testing::allowMenuBarDefinitionClones();
  const MenuSources equalSources = {&equal, 0, 0};
  LOKA_VERIFY(app.menuAttachment().project(equalSources));
  LOKA_VERIFY([NSApp mainMenu] != installed);
  [installed release];
}

void testMacMenuDoorSkipsMergeForUnchangedSources()
{
  CocoaHost host;
  MenuBarDefinition offered = bar();
  TestApp app;
  const MenuSources offer = {&offered, 0, 0};
  app.projectMenu(0, offer);
  NSMenu *installed = [[NSApp mainMenu] retain];
  loka::app::testing::failMenuBarDefinitionClones(1);
  app.projectMenu(0, offer);
  LOKA_VERIFY([NSApp mainMenu] == installed);
  OwnedDef<MenuBarDefinition> refused(offered.clone());
  LOKA_VERIFY(!refused.isSet());
  loka::app::testing::allowMenuBarDefinitionClones();
  [installed release];
}

void testMacMenuDoorSkipsProjectionDuringTracking()
{
  CocoaHost host;
  TestApp app;
  MenuBarDefinition first = bar(), changed = bar(true);
  const loka::app::MenuSources firstSources = {&first, 0, 0};
  app.projectMenu(0, firstSources);
  NSMenu *installed = [[NSApp mainMenu] retain];
  // Run the loop in the tracking mode AppKit uses while a menu is open; the
  // door must leave the tracked graph alone and the next completion re-offers.
  __block bool ran = false;
  TestApp *door = &app;
  MenuBarDefinition *offer = &changed;
  CFRunLoopPerformBlock(CFRunLoopGetCurrent(), (CFStringRef)NSEventTrackingRunLoopMode, ^{
    ran = true;
    const loka::app::MenuSources sources = {offer, 0, 0};
    door->projectMenu(0, sources);
  });
  [[NSRunLoop currentRunLoop] runMode:NSEventTrackingRunLoopMode beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.2]];
  LOKA_VERIFY(ran);
  LOKA_VERIFY([NSApp mainMenu] == installed);
  const loka::app::MenuSources changedSources = {&changed, 0, 0};
  // The identical offer skipped above was not committed: this completion applies it.
  app.projectMenu(0, changedSources);
  LOKA_VERIFY([NSApp mainMenu] != installed);
  [installed release];
  installed = [[NSApp mainMenu] retain];
  app.projectMenu(0, changedSources);
  LOKA_VERIFY([NSApp mainMenu] == installed);
  [installed release];
}
void testMacMenuAttachmentDisconnectStripsTargetAndAction()
{
  CocoaHost host;
  Mounted fixture;
  Observer &o = fixture.observation;
  NSMenu *installed = [NSApp mainMenu];
  id target = [o.item target];
  SEL action = [o.item action];
  LOKA_VERIFY(target != nil && action != NULL);
  LOKA_VERIFY(o.attachment->dispatch(o.tag));
  LOKA_VERIFY(o.calls == 1);
  LOKA_VERIFY([o.item isEnabled] && [o.item state] == 0);
  fixture.root()->toggle();
  LOKA_VERIFY(![o.item isEnabled] && [o.item state] != 0);
  Scene other((Boundary<Root>()));
  o.attachment->releaseFrom(&other);
  LOKA_VERIFY([o.item target] == target && [o.item action] == action);
  LOKA_VERIFY([o.item tag] == o.tag);
  fixture.root()->toggle();
  LOKA_VERIFY([o.item isEnabled] && [o.item state] == 0);
  LOKA_VERIFY(o.attachment->dispatch(o.tag));
  LOKA_VERIFY(o.calls == 2);

  o.attachment->releaseFrom(fixture.window->scene());
  LOKA_VERIFY([NSApp mainMenu] == installed);
  LOKA_VERIFY([o.item target] == nil && [o.item action] == NULL && [o.item tag] == 0);
  LOKA_VERIFY(!o.attachment->dispatch(o.tag));
  LOKA_VERIFY(o.calls == 2);
  fixture.root()->toggle();
  LOKA_VERIFY([o.item isEnabled] && [o.item state] == 0);
  // Reinstall uses the same numeric IDs, but the tracking-retained old item
  // must stay inert even after replacement commands become dispatchable.
  LOKA_VERIFY(o.attachment->project(sources(fixture.window->scene()->menuBar(), fixture.window->scene())));
  LOKA_VERIFY([o.item target] == nil && [o.item action] == NULL && [o.item tag] == 0);
  LOKA_VERIFY(o.attachment->dispatch(static_cast<int>([commandItem() tag])));
  LOKA_VERIFY(o.calls == 3);
}

void testMacSceneDetachReleasesMenu()
{
  CocoaHost host;
  Mounted fixture;
  Observer &o = fixture.observation;
  // Mirror the Win32 pin: reuse the overlay address across fresh compositions.
  MenuBarDefinition reused(*fixture.window->scene()->menuBar());
  const MenuSources offer = {0, &reused, fixture.window->scene()};
  LOKA_VERIFY(o.attachment->project(offer));
  [o.item release];
  o.item = [commandItem() retain];
  o.tag = static_cast<int>([o.item tag]);
  NSMenu *installed = [[NSApp mainMenu] retain];
  LOKA_VERIFY(o.attachment->dispatch(o.tag));
  LOKA_VERIFY(o.calls == 1);
  SceneAccess::unmount(*fixture.window->scene());
  LOKA_VERIFY(o.detaches == 1);
  LOKA_VERIFY(!o.attachment->dispatch(o.tag));
  LOKA_VERIFY([NSApp mainMenu] == installed);
  reused.~MenuBarDefinition();
  MacScenePlatformController *controller = MacScenePlatformController::findForRootView(
      loka::dsl::testing::MacWindowTestAccess::contentView(*fixture.window));
  LOKA_VERIFY(controller != 0);
  LOKA_VERIFY(fixture.window->scene()->mount(controller));
  LOKA_VERIFY(fixture.window->scene()->menuBar());
  new (&reused) MenuBarDefinition(*fixture.window->scene()->menuBar());
  LOKA_VERIFY(o.attachment->project(offer));
  LOKA_VERIFY([NSApp mainMenu] != installed);
  [installed release];
  [o.item release];
  o.item = [commandItem() retain];
  o.tag = static_cast<int>([o.item tag]);
  LOKA_VERIFY(o.attachment->dispatch(o.tag));
  LOKA_VERIFY(o.calls == 2);
  SceneAccess::unmount(*fixture.window->scene());
  LOKA_VERIFY(o.detaches == 2);
  LOKA_VERIFY(!o.attachment->dispatch(o.tag));
}

void testMacMenuReleaseThenAbsentRemovesMainMenu()
{
  CocoaHost host;
  Mounted fixture;
  MacMenuAttachment &attachment = *fixture.observation.attachment;
  NSMenu *installed = [[NSApp mainMenu] retain];
  attachment.releaseFrom(fixture.window->scene());
  LOKA_VERIFY([NSApp mainMenu] == installed);
  const MenuSources absent = {0, 0, 0};
  LOKA_VERIFY(attachment.project(absent));
  // AppKit can substitute its application menu; our installed graph must be gone.
  LOKA_VERIFY([NSApp mainMenu] != installed);
  LOKA_VERIFY(!attachment.project(absent));
  [installed release];
}

void testMacNativeCloseReleasesMenuBeforeReclaim()
{
  CocoaHost host;
  Mounted fixture;
  NSWindow *native = (NSWindow *)loka::dsl::testing::MacWindowTestAccess::nativeWindow(*fixture.window);
  LOKA_VERIFY(native != nil);
  LOKA_VERIFY(fixture.observation.attachment->dispatch(fixture.observation.tag));
  LOKA_VERIFY(fixture.observation.calls == 1);
  // AppKit delivers windowWillClose: to the Loka delegate. The close clears
  // the delegate route while the Scene teardown waits for reclamation, so the
  // close itself must release the borrowed States (#1242).
  [native close];
  LOKA_VERIFY(fixture.observation.detaches == 0);
  LOKA_VERIFY(!fixture.observation.attachment->dispatch(fixture.observation.tag));
  LOKA_VERIFY(fixture.observation.calls == 1);
  // ~Mounted reclaims the window; Root::detachNode verifies the stripped item.
}

namespace
{
  class ShutdownWindow : public MacWindow
  {
  public:
    explicit ShutdownWindow(PlatformContext *platform) : MacWindow(platform, sceneProps()) {}
    virtual ~ShutdownWindow()
    {
      // Fail before touching a dead attachment if MacApp omitted retireComponents.
      LOKA_VERIFY([NSApp mainMenu] != nil);
      LOKA_VERIFY([observer->item target] != nil);
    }
  };
}

void testMacAppShutdownReleasesMenuBeforeAttachmentDestruction()
{
  CocoaHost host;
  Observer observation;
  observer = &observation;
  NullPlatformContext platform;
  NSMenu *installed = nil;
  {
    TestApp app;
    observation.attachment = &app.menuAttachment();
    ShutdownWindow *window = new ShutdownWindow(&platform);
    app.own(window);
    LOKA_VERIFY(app.menuAttachment().project(sources(window->scene()->menuBar(), window->scene())));
    observation.item = [commandItem() retain];
    observation.tag = static_cast<int>([observation.item tag]);
    LOKA_VERIFY(app.menuAttachment().dispatch(observation.tag));
    LOKA_VERIFY(observation.calls == 1);
    installed = [[NSApp mainMenu] retain];
  }
  LOKA_VERIFY(observation.detaches == 1);
  // The attachment removes only its own bar; AppKit may now show its default.
  LOKA_VERIFY([NSApp mainMenu] != installed);
  [installed release];
  LOKA_VERIFY([observation.item target] == nil && [observation.item action] == NULL);
  [observation.item release];
  observer = 0;
}

void testMacMenuProjectionDetachesMainMenuBeforeReleasingTarget()
{
  CocoaHost host;
  bool targetReleased = false;
  bool detachedBeforeTargetRelease = false;
  static char witnessKey;
  {
    TestApp app;
    MenuBarDefinition offered = bar();
    LOKA_VERIFY(app.menuAttachment().project(sources(&offered, 0)));
    {
      // Reading a weak `target` without ARC retains and autoreleases it; drain
      // that +1 before the attachment dies so the witness sees the real release.
      NSAutoreleasePool *reading = [[NSAutoreleasePool alloc] init];
      id target = [commandItem() target];
      LOKA_VERIFY(target != nil);
      LokaMenuTargetReleaseWitness *witness = [[LokaMenuTargetReleaseWitness alloc]
          initWithReleased:&targetReleased detached:&detachedBeforeTargetRelease];
      objc_setAssociatedObject(target, &witnessKey, witness, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
      [witness release];
      [reading drain];
    }
    gWitnessedMenu = [NSApp mainMenu];
    LOKA_VERIFY(!targetReleased && gWitnessedMenu != nil);
  }
  LOKA_VERIFY([NSApp mainMenu] != gWitnessedMenu);
  gWitnessedMenu = nil;
  LOKA_VERIFY(targetReleased);
  LOKA_VERIFY(detachedBeforeTargetRelease);
}

void testMacMenuAttachmentMergeRefusalPreservesProjection()
{
  CocoaHost host;
  MenuBarDefinition first = bar(), changed = bar(true);
  TestApp app;
  const MenuSources firstSources = {&first, 0, 0};
  const MenuSources changedSources = {&changed, 0, 0};
  LOKA_VERIFY(app.menuAttachment().project(firstSources));
  NSMenu *installed = [[NSApp mainMenu] retain];
  loka::app::testing::failMenuBarDefinitionClones(1);
  LOKA_VERIFY(!app.menuAttachment().project(changedSources));
  loka::app::testing::allowMenuBarDefinitionClones();
  LOKA_VERIFY([NSApp mainMenu] == installed);
  LOKA_VERIFY(!app.menuAttachment().project(firstSources)); // Old record survives.
  LOKA_VERIFY(app.menuAttachment().project(changedSources)); // Identical retry applies.
  LOKA_VERIFY([NSApp mainMenu] != installed);
  [installed release];
  LOKA_VERIFY(!app.menuAttachment().project(changedSources));
  NSMenu *before = [[NSApp mainMenu] retain];
  const MenuSources absent = {0, 0, 0};
  LOKA_VERIFY(app.menuAttachment().project(absent));
  LOKA_VERIFY([NSApp mainMenu] != before);
  [before release];
  LOKA_VERIFY(!app.menuAttachment().project(absent));
}

void testMacMenuAttachmentDispatchMayDisconnect()
{
  CocoaHost host;
  Mounted fixture;
  fixture.observation.disconnectInAction = true;
  LOKA_VERIFY(fixture.observation.attachment->dispatch(fixture.observation.tag));
  LOKA_VERIFY(fixture.observation.calls == 1);
  LOKA_VERIFY(!fixture.observation.attachment->dispatch(fixture.observation.tag));
  const MenuSources offer = {0, fixture.window->scene()->menuBar(), fixture.window->scene()};
  LOKA_VERIFY(fixture.observation.attachment->project(offer));
  LOKA_VERIFY(fixture.observation.attachment->dispatch(static_cast<int>([commandItem() tag])));
  LOKA_VERIFY(fixture.observation.calls == 2);
}

void testMacMenuTitleFollowsState()
{
  CocoaHost host;
  // The same build door must cover ordinary leaves and application commands.
  const MenuActionType actions[] = {MENU_ACTION_NONE, MENU_ACTION_ABOUT_APP, MENU_ACTION_QUIT_APP};
  for (size_t i = 0; i < sizeof(actions) / sizeof(actions[0]); ++i)
  {
    MutableState<String> state(String::Literal("Alpha"));
    PushStateTracker tracker;
    tracker.addState(&state);
    TestApp app;
    MenuDefinition menu = i == 0 ? Menu("File") : AppMenu();
    MenuItemDefinition command = MenuItem("Fallback").text(&state).actionType(actions[i]);
    if (i == 0)
      command.shortcut('a');
    menu << command;
    MenuBarDefinition offered;
    offered << menu;
    LOKA_VERIFY(app.menuAttachment().project(sources(&offered, 0)));
    NSMenuItem *item = commandItem();
    LOKA_VERIFY([[item title] isEqualToString:@"Alpha"]);
    NSString *shortcut = i == 0 ? @"a" : (i == 2 ? @"q" : @"");
    LOKA_VERIFY([[item keyEquivalent] isEqualToString:shortcut]);
    {
      StateTrackerGuard guard(&tracker);
      state.set(String::Literal("Beta"));
    }
    LOKA_VERIFY(commandItem() == item);
    LOKA_VERIFY([[item title] isEqualToString:@"Beta"]);
    LOKA_VERIFY([[item keyEquivalent] isEqualToString:shortcut]);
  }
}

void testMacMenuSubmenuTitleFollowsState()
{
  CocoaHost host;
  MutableState<String> state(String::Literal("Alpha"));
  PushStateTracker tracker;
  tracker.addState(&state);
  TestApp app;
  MenuBarDefinition offered;
  offered << (Menu("File") << (MenuItem("Fallback").text(&state) << MenuItem("Child")));
  LOKA_VERIFY(app.menuAttachment().project(sources(&offered, 0)));
  NSMenu *menu = [[[NSApp mainMenu] itemAtIndex:0] submenu];
  LOKA_VERIFY(menu && [menu numberOfItems] == 1);
  NSMenuItem *item = [menu itemAtIndex:0];
  NSMenu *submenu = [item submenu];
  LOKA_VERIFY(submenu != nil);
  LOKA_VERIFY([[item title] isEqualToString:@"Alpha"]);
  LOKA_VERIFY([[submenu title] isEqualToString:@"Alpha"]);
  {
    StateTrackerGuard guard(&tracker);
    state.set(String::Literal("Beta"));
  }
  LOKA_VERIFY([[item title] isEqualToString:@"Beta"]);
  LOKA_VERIFY([[submenu title] isEqualToString:@"Beta"]);
}

void testMacMenuTitleStateUnbindsOnRelease()
{
  CocoaHost host;
  MutableState<String> state(String::Literal("Alpha"));
  PushStateTracker tracker;
  tracker.addState(&state);
  Scene source((Boundary<Root>()));
  TestApp app;
  MenuBarDefinition offered;
  offered << (Menu("File") << MenuItem("Fallback").text(&state));
  LOKA_VERIFY(app.menuAttachment().project(sources(&offered, &source)));
  NSMenuItem *item = [commandItem() retain];
  {
    StateTrackerGuard guard(&tracker);
    state.set(String::Literal("Beta"));
  }
  LOKA_VERIFY([[item title] isEqualToString:@"Beta"]);
  app.menuAttachment().releaseFrom(&source);
  {
    StateTrackerGuard guard(&tracker);
    state.set(String::Literal("Gamma"));
  }
  LOKA_VERIFY([[item title] isEqualToString:@"Beta"]);
  [item release];
}

namespace
{
  void verifyMenuRows(NSMenu *menu, const char *const *titles, NSInteger count)
  {
    LOKA_VERIFY(menu && [menu numberOfItems] == count);
    for (NSInteger i = 0; i < count; ++i)
    {
      NSMenuItem *item = [menu itemAtIndex:i];
      if (!titles[i])
        LOKA_VERIFY([item isSeparatorItem]);
      else
      {
        LOKA_VERIFY(![item isSeparatorItem]);
        LOKA_VERIFY([[item title] isEqualToString:[NSString stringWithUTF8String:titles[i]]]);
      }
    }
  }
} // namespace

void testMacMenuAppFirstAndSections()
{
  CocoaHost host;
  for (int variant = 0; variant < 6; ++variant)
  {
    TestApp app;
    MenuDefinition application = AppMenu();
    application << MenuSeparator() << MenuSeparator();
    // Deliberately declare Quit first and About last: roles determine sections.
    application << MenuItem("Quit").actionType(MENU_ACTION_QUIT_APP) << MenuSeparator();
    if (variant != 2)
      application << MenuItem("Prefs") << MenuSeparator() << MenuSeparator();
    application << MenuItem("About").actionType(MENU_ACTION_ABOUT_APP).attr(MenuItemAttr().visible(variant != 1))
                << MenuSeparator();
    if (variant == 3)
    {
      application = AppMenu();
      application << MenuSeparator() << MenuItem("Hidden").attr(MenuItemAttr().visible(false));
    }
    if (variant == 4)
      application = AppMenu() << MenuItem("About").actionType(MENU_ACTION_ABOUT_APP) << MenuSeparator()
                              << MenuItem("Prefs") << MenuSeparator()
                              << MenuItem("Quit").actionType(MENU_ACTION_QUIT_APP);
    // No declared separators: the section separators are the rail's own.
    if (variant == 5)
      application = AppMenu() << MenuItem("About").actionType(MENU_ACTION_ABOUT_APP) << MenuItem("Prefs")
                              << MenuItem("Quit").actionType(MENU_ACTION_QUIT_APP);
    MenuBarDefinition offered;
    offered << (Menu("File") << MenuItem("Open")) << (Menu("View") << MenuItem("Zoom")) << application;
    LOKA_VERIFY(app.menuAttachment().project(sources(&offered)));
    NSMenu *main = [NSApp mainMenu];
    LOKA_VERIFY([main numberOfItems] == (variant == 3 ? 2 : 3));
    NSInteger fileIndex = variant == 3 ? 0 : 1;
    LOKA_VERIFY([[[main itemAtIndex:fileIndex] title] isEqualToString:@"File"]);
    LOKA_VERIFY([[[main itemAtIndex:fileIndex + 1] title] isEqualToString:@"View"]);
    if (variant == 3)
      continue;
    NSMenu *menu = [[main itemAtIndex:0] submenu];
    const char *all[] = {"About", 0, "Prefs", 0, "Quit"};
    const char *hidden[] = {"Prefs", 0, "Quit"};
    const char *pair[] = {"About", 0, "Quit"};
    verifyMenuRows(menu,
                   variant == 0 || variant >= 4 ? all : (variant == 1 ? hidden : pair),
                   variant == 0 || variant >= 4 ? 5 : 3);
  }
}

void testMacMenuQuitSuppressionAndSeparators()
{
  CocoaHost host;
  // Visible (including disabled) app Quit suppresses leaves; absent/invisible
  // app Quit and absent app container preserve ordinary Quit commands.
  for (int variant = 0; variant < 5; ++variant)
  {
    TestApp app;
    MenuBarDefinition offered;
    MenuDefinition application = AppMenu();
    application << MenuItem("About").actionType(MENU_ACTION_ABOUT_APP);
    if (variant != 1)
      application << MenuItem("Quit")
                         .actionType(MENU_ACTION_QUIT_APP)
                         .enabled(variant != 3)
                         .attr(MenuItemAttr().visible(variant != 2));
    if (variant != 4)
      offered << application;
    offered << (Menu("File") << MenuSeparator() << MenuItem("Open") << MenuSeparator()
                             << MenuItem("Quit").actionType(MENU_ACTION_QUIT_APP) << MenuSeparator());
    offered << (Menu("Nested") << MenuSeparator()
                               << (MenuItem("Quit parent").actionType(MENU_ACTION_QUIT_APP)
                                   << MenuSeparator() << MenuItem("Keep") << MenuSeparator()
                                   << MenuItem("Nested Quit").actionType(MENU_ACTION_QUIT_APP) << MenuSeparator())
                               << MenuSeparator() << MenuSeparator()
                               << (MenuItem("Empty") << MenuItem("Hidden").attr(MenuItemAttr().visible(false)))
                               << MenuSeparator() << MenuItem("Last") << MenuSeparator());
    LOKA_VERIFY(app.menuAttachment().project(sources(&offered)));
    NSMenu *main = [NSApp mainMenu];
    const bool suppressed = variant == 0 || variant == 3;
    const NSInteger fileIndex = variant == 4 ? 0 : 1;
    LOKA_VERIFY([main numberOfItems] == fileIndex + 2);
    const char *openOnly[] = {"Open"};
    const char *withQuit[] = {"Open", 0, "Quit"};
    verifyMenuRows([[main itemAtIndex:fileIndex] submenu], suppressed ? openOnly : withQuit, suppressed ? 1 : 3);
    NSMenu *nested = [[main itemAtIndex:fileIndex + 1] submenu];
    const char *parents[] = {"Quit parent", 0, "Last"};
    verifyMenuRows(nested, parents, 3);
    const char *keepOnly[] = {"Keep"};
    const char *keepQuit[] = {"Keep", 0, "Nested Quit"};
    verifyMenuRows([[nested itemAtIndex:0] submenu], suppressed ? keepOnly : keepQuit, suppressed ? 1 : 3);
    LOKA_VERIFY([[nested itemAtIndex:0] tag] == 0);
    if (variant == 3)
      LOKA_VERIFY(![[[[main itemAtIndex:0] submenu] itemAtIndex:2] isEnabled]);
  }
}

void testMacMenuAppMiddlePreservesPayloads()
{
  CocoaHost host;
  TestApp app;
  MenuBarDefinition offered;
  offered << (AppMenu() << MenuItem("Quit").actionType(MENU_ACTION_QUIT_APP).shortcut('x') << MenuItem("Prefs")
                        << MenuSeparator() << MenuSeparator()
                        << MenuItem("Colors").actionType(MENU_ACTION_SHOW_COLOR_PICKER)
                        << (MenuItem("About parent").actionType(MENU_ACTION_ABOUT_APP) << MenuItem("Child"))
                        << (MenuItem("Quit parent").actionType(MENU_ACTION_QUIT_APP)
                            << MenuItem("Child Quit").actionType(MENU_ACTION_QUIT_APP))
                        << MenuItem("About").actionType(MENU_ACTION_ABOUT_APP).shortcut('a'));
  LOKA_VERIFY(app.menuAttachment().project(sources(&offered)));
  NSMenu *menu = [[[NSApp mainMenu] itemAtIndex:0] submenu];
  const char *rows[] = {"About", 0, "Prefs", 0, "Colors", "About parent", "Quit parent", 0, "Quit"};
  verifyMenuRows(menu, rows, 9);
  LOKA_VERIFY([[[menu itemAtIndex:0] keyEquivalent] isEqualToString:@"a"]);
  LOKA_VERIFY([[[menu itemAtIndex:8] keyEquivalent] isEqualToString:@"x"]);
  const char *child[] = {"Child"};
  const char *childQuit[] = {"Child Quit"};
  verifyMenuRows([[menu itemAtIndex:5] submenu], child, 1);
  verifyMenuRows([[menu itemAtIndex:6] submenu], childQuit, 1);
}

void testMacMenuEmptyActionTitleFollowsState()
{
  CocoaHost host;
  const MenuActionType actions[] = {MENU_ACTION_NONE, MENU_ACTION_ABOUT_APP, MENU_ACTION_QUIT_APP};
  const char *words[] = {"Menu Item", "About", "Quit"};
  for (int i = 0; i < 3; ++i)
  {
    MutableState<String> title(String::Literal(""));
    MutableState<bool> enabled(false), checked(true);
    PushStateTracker tracker;
    tracker.addState(&title);
    tracker.addState(&enabled);
    tracker.addState(&checked);
    TestApp app;
    MenuBarDefinition offered;
    offered << (AppMenu() << MenuItem("Ignored constant")
                                 .text(&title)
                                 .actionType(actions[i])
                                 .enabled(&enabled)
                                 .attr(MenuItemAttr().checked(&checked)));
    LOKA_VERIFY(app.menuAttachment().project(sources(&offered)));
    NSMenuItem *item = commandItem();
    const NSInteger tag = [item tag];
    LOKA_VERIFY(tag != 0 && [item target] != nil && [item action] != NULL);
    NSString *key = i == 2 ? @"q" : @"";
    LOKA_VERIFY([[item title] isEqualToString:[NSString stringWithUTF8String:words[i]]]);
    LOKA_VERIFY([[item keyEquivalent] isEqualToString:key]);
    LOKA_VERIFY(![item isEnabled] && [item state] != 0);
    {
      StateTrackerGuard guard(&tracker);
      title.set(String::Literal("Mine"));
      enabled.set(true);
      checked.set(false);
    }
    LOKA_VERIFY(commandItem() == item && [item tag] == tag);
    LOKA_VERIFY([[item title] isEqualToString:@"Mine"]);
    LOKA_VERIFY([item isEnabled] && [item state] == 0);
    {
      StateTrackerGuard guard(&tracker);
      title.set(String::Literal(""));
    }
    LOKA_VERIFY(commandItem() == item && [item tag] == tag);
    LOKA_VERIFY([[item title] isEqualToString:[NSString stringWithUTF8String:words[i]]]);
    LOKA_VERIFY([[item keyEquivalent] isEqualToString:key]);
  }
}

namespace
{
  class MenuVisibilityRead : public State<bool>
  {
  public:
    explicit MenuVisibilityRead(bool visible)
        : State<bool>(visible),
          reads(0)
    {
    }
    virtual bool get() const
    {
      ++this->reads;
      return State<bool>::get();
    }
    mutable int reads;
  };
} // namespace

void testMacMenuVisibilityReadOnceAndConstantFallback()
{
  CocoaHost host;
  MenuVisibilityRead about(true), quit(true), hidden(false);
  TestApp app;
  MenuBarDefinition offered;
  offered << (AppMenu() << MenuItem("").actionType(MENU_ACTION_QUIT_APP).attr(MenuItemAttr().visible(&quit))
                        << MenuItem("Hidden").attr(MenuItemAttr().visible(&hidden))
                        << MenuItem("").actionType(MENU_ACTION_ABOUT_APP).attr(MenuItemAttr().visible(&about)));
  LOKA_VERIFY(app.menuAttachment().project(sources(&offered)));
  LOKA_VERIFY(about.reads == 1 && quit.reads == 1 && hidden.reads == 1);
  const char *rows[] = {"About", 0, "Quit"};
  verifyMenuRows([[[NSApp mainMenu] itemAtIndex:0] submenu], rows, 3);
}
