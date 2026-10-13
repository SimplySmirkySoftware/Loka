#include "MacMenuProjectionTests.hpp"
#include "support/TestVerify.hpp"
#include "MacApp.hpp"
#include "MacWindow.hpp"
#include "testing/MacWindowTestAccess.hpp"
#include "app/nodes/boundary/StdComposition.hpp"
#include "platform/null/NullPlatformContext.hpp"
#include "testing/scene/SceneTestFlow.hpp"
#include "core/util/StateTrackerGuard.hpp"
#include <AppKit/AppKit.h>
#include <objc/runtime.h>

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
      LOKA_VERIFY(this->app.menuAttachment().project(this->window->scene()->menuBar(), this->window->scene()));
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

void testMacMenuAttachmentProjectsOnceForEqualBar()
{
  CocoaHost host;
  TestApp app;
  MenuBarDefinition first = bar(), equal = bar(), changed = bar(true);
  LOKA_VERIFY(app.menuAttachment().project(&first, 0));
  NSMenu *installed = [[NSApp mainMenu] retain];
  LOKA_VERIFY(!app.menuAttachment().project(&equal, 0));
  LOKA_VERIFY([NSApp mainMenu] == installed);
  // An item-level change must replace the installed projection.
  LOKA_VERIFY(app.menuAttachment().project(&changed, 0));
  LOKA_VERIFY([NSApp mainMenu] != installed);
  [installed release];
  installed = [[NSApp mainMenu] retain];
  Scene other((Boundary<Root>()));
  LOKA_VERIFY(app.menuAttachment().project(&changed, &other));
  LOKA_VERIFY([NSApp mainMenu] != installed);
  [installed release];
  app.menuAttachment().releaseFrom(&other);
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
  app.projectMenu(0, changedSources);
  LOKA_VERIFY([NSApp mainMenu] != installed);
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
  LOKA_VERIFY(o.attachment->project(fixture.window->scene()->menuBar(), fixture.window->scene()));
  LOKA_VERIFY([o.item target] == nil && [o.item action] == NULL && [o.item tag] == 0);
  LOKA_VERIFY(o.attachment->dispatch(static_cast<int>([commandItem() tag])));
  LOKA_VERIFY(o.calls == 3);
}

void testMacSceneDetachReleasesMenu()
{
  CocoaHost host;
  Mounted fixture;
  LOKA_VERIFY(fixture.observation.attachment->dispatch(fixture.observation.tag));
  LOKA_VERIFY(fixture.observation.calls == 1);
  SceneAccess::unmount(*fixture.window->scene());
  LOKA_VERIFY(fixture.observation.detaches == 1);
  LOKA_VERIFY(!fixture.observation.attachment->dispatch(fixture.observation.tag));
  LOKA_VERIFY(fixture.observation.calls == 1);
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
    LOKA_VERIFY(app.menuAttachment().project(window->scene()->menuBar(), window->scene()));
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
    LOKA_VERIFY(app.menuAttachment().project(&offered, 0));
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

void testMacMenuAttachmentCloneRefusalClearsBaseline()
{
  CocoaHost host;
  TestApp app;
  MenuBarDefinition first = bar(), changed = bar(true);
  LOKA_VERIFY(app.menuAttachment().project(&first, 0));
  loka::app::testing::failMenuBarDefinitionClones(1);
  LOKA_VERIFY(app.menuAttachment().project(&changed, 0));
  loka::app::testing::allowMenuBarDefinitionClones();
  NSMenu *installed = [[NSApp mainMenu] retain];
  LOKA_VERIFY(app.menuAttachment().project(&changed, 0));
  LOKA_VERIFY([NSApp mainMenu] != installed);
  [installed release];
  LOKA_VERIFY(!app.menuAttachment().project(&changed, 0));
  NSMenu *before = [[NSApp mainMenu] retain];
  LOKA_VERIFY(app.menuAttachment().project(0, 0));
  LOKA_VERIFY([NSApp mainMenu] != before);
  [before release];
  // Completion re-offers every tick: absent over absent is unchanged.
  LOKA_VERIFY(!app.menuAttachment().project(0, 0));
}

void testMacMenuAttachmentDispatchMayDisconnect()
{
  CocoaHost host;
  Mounted fixture;
  fixture.observation.disconnectInAction = true;
  LOKA_VERIFY(fixture.observation.attachment->dispatch(fixture.observation.tag));
  LOKA_VERIFY(fixture.observation.calls == 1);
  LOKA_VERIFY(!fixture.observation.attachment->dispatch(fixture.observation.tag));
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
    LOKA_VERIFY(app.menuAttachment().project(&offered, 0));
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
  LOKA_VERIFY(app.menuAttachment().project(&offered, 0));
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
  LOKA_VERIFY(app.menuAttachment().project(&offered, &source));
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
