#include "MacWindowAdmissionTests.hpp"
#include "support/TestVerify.hpp"
#include "support/NativeCloseTestConfig.hpp"

#include <AppKit/AppKit.h>
#include <cstdio>

#include "MacWindow.hpp"
#include "MacApp.hpp"
#include "app/core/App.hpp"
#include "app/nodes/boundary/StdComposition.hpp"
#include "app/scene/Scene.hpp"
#include "platform/null/NullPlatformContext.hpp"
#include "testing/MacWindowTestAccess.hpp"
#include "testing/app/AppTestAccess.hpp"
#include "testing/scene/SceneTestFlow.hpp"

namespace
{
  typedef loka::dsl::testing::MacWindowTestAccess NativeAccess;
  typedef loka::dsl::testing::SceneTestAccess SceneAccess;
  typedef loka::app::testing::AppTestAccess AppAccess;

  class AdmissionApp : public MacApp
  {
  public:
    AdmissionApp() : MacApp(0), closes(0) {}
    virtual void quit() {}
    void install(MacWindow *window)
    {
      this->group_ = new AppComponentGroup(std::vector<AppComponent *>(1, window));
      window->setApp(this);
    }
    int closes;
  protected:
    virtual void windowClosed(Window *window)
    {
      ++this->closes;
      App::windowClosed(window);
    }
  };

  enum VisibilityCommand { KEEP_VISIBILITY, HIDE, HIDE_THEN_SHOW };

  /** Stack-owned observations survive both root generations and App reclamation. */
  struct AdmissionProbe
  {
    AdmissionProbe() : command(KEEP_VISIBILITY), callbacks(0), mounts(0), unmounts(0) {}
    VisibilityCommand command;
    int callbacks;
    int mounts;
    int unmounts;
  };

  class AdmissionRoot;
  struct AdmissionTag {};
  struct AdmissionProps : loka::app::scene::NodePropsBase<AdmissionProps>
  {
    typedef AdmissionRoot NodeType;
    typedef AdmissionTag TypeTag;
    explicit AdmissionProps(AdmissionProbe *value) : probe(value) {}
    bool operator<(const loka::app::scene::PropsBase &rhs) const
    {
      if (rhs.propsTypeId() != this->propsTypeId())
        return this->propsTypeId() < rhs.propsTypeId();
      return this->probe < static_cast<const AdmissionProps &>(rhs).probe;
    }
    AdmissionProbe *probe;
  };

  class AdmissionRoot : public loka::app::scene::StdCompositionBoundaryNodeBase<AdmissionProps>
  {
    typedef loka::app::scene::StdCompositionBoundaryNodeBase<AdmissionProps> Base;
  public:
    typedef AdmissionTag TypeTag;
    explicit AdmissionRoot(const AdmissionProps &props) : Base(props) {}
    virtual void composeNode(loka::app::scene::NodeComposition &) {}
    virtual void attachNode(loka::app::scene::NodeComposition &)
    {
      ++this->props.probe->mounts;
    }
    virtual void detachNode(loka::app::scene::NodeComposition &)
    {
      ++this->props.probe->unmounts;
    }
    virtual void applyPendingUpdate(const loka::app::scene::PlatformApplyPlan &plan)
    {
      Base::applyPendingUpdate(plan);
      AdmissionProbe &probe = *this->props.probe;
      const VisibilityCommand command = probe.command;
      if (command == KEEP_VISIBILITY)
        return;
      probe.command = KEEP_VISIBILITY;
      ++probe.callbacks;
      LOKA_VERIFY(this->scene()->isRunInProgress());
      MacWindow *window = this->scene()->getWindow()->asMacWindow();
      LOKA_VERIFY(window != 0);
      void *native = NativeAccess::nativeWindow(*window);
      LOKA_VERIFY(native != 0);
      const int unmounts = probe.unmounts;
      {
        loka::core::StateTrackerGuard guard(window->getTracker());
        window->visibilityState().set(false);
      }
      // Check after notification delivery too, not just inside the transaction.
      LOKA_VERIFY(NativeAccess::nativeWindow(*window) == native);
      LOKA_VERIFY(probe.unmounts == unmounts);
      if (command == HIDE_THEN_SHOW)
      {
        loka::core::StateTrackerGuard guard(window->getTracker());
        window->visibilityState().set(true);
      }
      LOKA_VERIFY(NativeAccess::nativeWindow(*window) == native);
      LOKA_VERIFY(probe.unmounts == unmounts);
    }
  };
  typedef loka::app::scene::BoundaryDefinition<AdmissionProps, AdmissionRoot> AdmissionDefinition;

  void verifyReusableHideReleasesDelegate()
  {
    NullPlatformContext context;
    AdmissionProbe probe;
    AdmissionApp app;
    WindowProps props;
    props.visible(false);
    props.scene(new loka::app::scene::Scene(AdmissionDefinition(AdmissionProps(&probe))));
    MacWindow *window = new MacWindow(&context, props);
    LOKA_VERIFY(NativeAccess::nativeWindow(*window) == 0);
    LOKA_VERIFY(probe.mounts == 0);
    app.install(window);
    // Observe the framework-owned delegate without extending its lifetime.
    // AppKit may retain the window and view beyond this autorelease pool.
    // NSPointerFunctionsWeakMemory arrived in the 10.8 SDK; an older SDK
    // cannot observe the delegate without retaining it, so only that
    // observation is skipped.
#if defined(MAC_OS_X_VERSION_MAX_ALLOWED) && MAC_OS_X_VERSION_MAX_ALLOWED >= 1080
    NSHashTable *delegates = [[NSHashTable alloc]
        initWithOptions:NSPointerFunctionsWeakMemory | NSPointerFunctionsObjectPointerPersonality
               capacity:1];
#else
    NSHashTable *delegates = nil;
    std::printf("[skip] delegate release observation: the SDK has no NSPointerFunctionsWeakMemory (10.8+)\n");
#endif
    const int cycles = 8;
    for (int cycle = 0; cycle < cycles; ++cycle)
    {
      NSAutoreleasePool *cyclePool = [[NSAutoreleasePool alloc] init];
      {
        loka::core::StateTrackerGuard guard(window->getTracker());
        window->visibilityState().set(true);
      }
      LOKA_VERIFY(NativeAccess::nativeWindow(*window) == 0);
      LOKA_VERIFY(probe.mounts == cycle);
      AppAccess::flushWindowInvalidations(app);
      NSWindow *native = (NSWindow *)NativeAccess::nativeWindow(*window);
      LOKA_VERIFY(native != nil);
      LOKA_VERIFY(![native isReleasedWhenClosed]);
      [delegates addObject:[native delegate]];
      if (delegates)
        LOKA_VERIFY([[delegates allObjects] count] == 1);
      // Make activation deterministic even when the test runner is not frontmost.
      window->handleWindowDidBecomeKey();
      LOKA_VERIFY(app.activeWindow() == window);
      {
        loka::core::StateTrackerGuard guard(window->getTracker());
        window->visibilityState().set(false);
      }
      AppAccess::flushWindowInvalidations(app);
      LOKA_VERIFY(NativeAccess::nativeWindow(*window) == 0);
      LOKA_VERIFY(NativeAccess::contentView(*window) == 0);
      LOKA_VERIFY(app.activeWindow() == 0);
      LOKA_VERIFY(AppAccess::pendingWindowCloseCount(app) == 0);
      LOKA_VERIFY(app.closes == 0);
      LOKA_VERIFY(probe.mounts == cycle + 1 && probe.unmounts == cycle + 1);
      [cyclePool drain];
      if (delegates)
        LOKA_VERIFY([[delegates allObjects] count] == 0);
    }
    [delegates release];
  }
}

void testMacWindowVisibilityAdmissionAndDelegateClose()
{
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  [NSApplication sharedApplication];
  verifyReusableHideReleasesDelegate();
  {
    NullPlatformContext context;
    AdmissionProbe probe;
    AdmissionApp app;
    WindowProps props;
    props.scene(new loka::app::scene::Scene(AdmissionDefinition(AdmissionProps(&probe))));
    MacWindow *window = new MacWindow(&context, props);
    // The initial seed mounts before App installation or the first idle tick.
    LOKA_VERIFY(NativeAccess::nativeWindow(*window) != 0);
    LOKA_VERIFY(SceneAccess::rootNode(*window->scene()) != 0);
    LOKA_VERIFY(probe.mounts == 1 && probe.unmounts == 0);
    app.install(window);
    AppAccess::flushWindowInvalidations(app);
    loka::app::scene::Scene *scene = window->scene();
    NSWindow *first = (NSWindow *)NativeAccess::nativeWindow(*window);
    LOKA_VERIFY(first != nil);
    // Keep the old allocation alive until the identity comparison: a fresh
    // NSWindow must not accidentally reuse the old object's address.
    [first retain];
    LOKA_VERIFY(probe.mounts == 1);
    LOKA_VERIFY(probe.unmounts == 0);

    probe.command = HIDE;
    scene->requestInvalidate(loka::app::scene::NODE_DIRTY_LAYOUT);
    AppAccess::flushWindowInvalidations(app);
    LOKA_VERIFY(probe.callbacks == 1);
    LOKA_VERIFY(!window->visibilityState().get());
    LOKA_VERIFY(NativeAccess::nativeWindow(*window) == first);
    LOKA_VERIFY(probe.unmounts == 0);

    AppAccess::flushWindowInvalidations(app);
    LOKA_VERIFY(NativeAccess::nativeWindow(*window) == 0);
    LOKA_VERIFY(SceneAccess::rootNode(*scene) == 0);
    LOKA_VERIFY(SceneAccess::platformController(*scene) == 0);
    LOKA_VERIFY(probe.unmounts == 1);
    LOKA_VERIFY(AppAccess::pendingWindowCloseCount(app) == 0);
    LOKA_VERIFY(app.closes == 0);
    LOKA_VERIFY([first delegate] == nil);
    AppAccess::flushWindowInvalidations(app);
    LOKA_VERIFY(probe.unmounts == 1);

    {
      loka::core::StateTrackerGuard guard(window->getTracker());
      window->visibilityState().set(true);
    }
    LOKA_VERIFY(NativeAccess::nativeWindow(*window) == 0);
    AppAccess::flushWindowInvalidations(app);
    NSWindow *second = (NSWindow *)NativeAccess::nativeWindow(*window);
    LOKA_VERIFY(second != nil && second != first);
    [first release];
    LOKA_VERIFY(window->scene() == scene);
    LOKA_VERIFY(SceneAccess::rootNode(*scene) != 0);
    LOKA_VERIFY(SceneAccess::platformController(*scene) != 0);
    LOKA_VERIFY(probe.mounts == 2);
    LOKA_VERIFY(probe.unmounts == 1);

    probe.command = HIDE_THEN_SHOW;
    scene->requestInvalidate(loka::app::scene::NODE_DIRTY_LAYOUT);
    AppAccess::flushWindowInvalidations(app);
    LOKA_VERIFY(probe.callbacks == 2);
    LOKA_VERIFY(window->visibilityState().get());
    AppAccess::flushWindowInvalidations(app);
    LOKA_VERIFY(NativeAccess::nativeWindow(*window) == second);
    LOKA_VERIFY(probe.mounts == 2 && probe.unmounts == 1);
    LOKA_VERIFY(AppAccess::pendingWindowCloseCount(app) == 0);

    // Deliver through the real Objective-C delegate, before App reclamation.
    [second retain];
    id delegate = [second delegate];
    LOKA_VERIFY(delegate != nil);
    NSNotification *notification = [NSNotification notificationWithName:NSWindowWillCloseNotification object:second];
    [delegate windowWillClose:notification];
    LOKA_VERIFY(NativeAccess::nativeWindow(*window) == 0);
    LOKA_VERIFY(NativeAccess::contentView(*window) == 0);
    LOKA_VERIFY([second delegate] == nil);
    LOKA_VERIFY(SceneAccess::rootNode(*scene) != 0);
    LOKA_VERIFY(SceneAccess::platformController(*scene) != 0);
    LOKA_VERIFY(probe.unmounts == 1 && app.closes == 0);
    LOKA_VERIFY(AppAccess::pendingWindowCloseCount(app) == 1);
    // A late delivery through the saved delegate cannot enqueue another close.
    [delegate windowWillClose:notification];
    LOKA_VERIFY(AppAccess::pendingWindowCloseCount(app) == 1);
    [second close];
    AppAccess::flushWindowInvalidations(app);
    // Window and Scene are now deleted: only stack-owned observations remain.
    LOKA_VERIFY(app.closes == 1);
    LOKA_VERIFY(probe.unmounts == 2); // One per mounted generation.
    LOKA_VERIFY(AppAccess::pendingWindowCloseCount(app) == 0);
    AppAccess::flushWindowInvalidations(app);
    LOKA_VERIFY(app.closes == 1 && probe.unmounts == 2);
    [second release];
  }
  [pool drain];
}

namespace
{
  class CloseAskedMacApp : public MacApp
  {
  public:
    explicit CloseAskedMacApp(AppConfigurable &config) : MacApp(&config) {}
    virtual void quit() {}
    MacWindow *firstWindow()
    {
      LOKA_VERIFY(this->group_ != 0);
      LOKA_VERIFY(this->group_->getComponents().size() == 1);
      return this->group_->getComponents()[0]->asWindow()->asMacWindow();
    }
  };
}

void testMacPerformCloseAsksKeyedWindowWithoutClosing()
{
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  [NSApplication sharedApplication];
  {
    NativeCloseTestContext<MacWindow> context;
    NativeCloseTestConfig config(context, true);
    const loka::core::ItemId key = config.roster.open(loka::app::DocumentFacts());
    LOKA_VERIFY(!key.isNone());
    CloseAskedMacApp app(config);
    app.App::run();
    MacWindow *window = app.firstWindow();
    LOKA_VERIFY(window != 0);
    NSWindow *native = (NSWindow *)NativeAccess::nativeWindow(*window);
    LOKA_VERIFY(native != nil);
    LOKA_VERIFY(AppAccess::pendingWindowCloseCount(app) == 0);
    [native performClose:nil];
    LOKA_VERIFY(NativeAccess::nativeWindow(*window) == native);
    LOKA_VERIFY(AppAccess::pendingWindowCloseCount(app) == 0);
    LOKA_VERIFY(config.roster.closeAsked().get().id == key);
    LOKA_VERIFY([[native delegate] windowShouldClose:native] == NO);
  }
  [pool drain];
}

void testMacPerformCloseClosesUnkeyedWindowAsToday()
{
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  [NSApplication sharedApplication];
  {
    NativeCloseTestContext<MacWindow> context;
    NativeCloseTestConfig config(context, false);
    CloseAskedMacApp app(config);
    app.App::run();
    MacWindow *window = app.firstWindow();
    LOKA_VERIFY(window != 0);
    NSWindow *native = (NSWindow *)NativeAccess::nativeWindow(*window);
    LOKA_VERIFY(native != nil);
    LOKA_VERIFY(AppAccess::pendingWindowCloseCount(app) == 0);
    [native performClose:nil];
    LOKA_VERIFY(NativeAccess::nativeWindow(*window) == 0);
    LOKA_VERIFY(AppAccess::pendingWindowCloseCount(app) == 1);
  }
  [pool drain];
}
