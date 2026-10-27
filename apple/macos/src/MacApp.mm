#include "MacApp.hpp"
#include "core/Operation.hpp"
#include "MacWindow.hpp"
#include "MacScenePlatformController.hpp"
#include "MacObjCCompat.hpp"
#include <AppKit/AppKit.h>
#include <ApplicationServices/ApplicationServices.h>
#include <mach/mach_time.h>
#include "app/core/AppComponent.hpp"

/** Process-lifetime registration; the attached App is only borrowed. */
@interface LokaQuitEventRouter : NSObject
{
  MacApp *owner_;
}
@property(nonatomic, assign) MacApp *owner;
@end

@implementation LokaQuitEventRouter
@synthesize owner = owner_;
- (void)applicationWillFinishLaunching:(NSNotification *)notification
{
  (void)notification;
  [[NSAppleEventManager sharedAppleEventManager] setEventHandler:self
                                                     andSelector:@selector(handleQuitEvent:withReplyEvent:)
                                                   forEventClass:kCoreEventClass
                                                      andEventID:kAEQuitApplication];
}

- (void)handleQuitEvent:(NSAppleEventDescriptor *)event withReplyEvent:(NSAppleEventDescriptor *)reply
{
  (void)event;
  if (!self.owner || !self.owner->holdQuitEvent())
  {
    [reply setParamDescriptor:[NSAppleEventDescriptor descriptorWithInt32:userCanceledErr] forKeyword:keyErrorNumber];
    return;
  }
  self.owner->requestQuitAtCompletion();
}

- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)sender
{
  (void)sender;
  if (self.owner)
    self.owner->requestQuitAtCompletion();
  return NSTerminateCancel;
}
@end

namespace
{
  // Removing an Apple Event handler does not restore AppKit's handler.
  // Keep both registration and delegate alive across sequential Apps.
  LokaQuitEventRouter *quitEventRouter = nil;
} // namespace

@interface LokaFlushTarget : NSObject
{
  MacApp *owner_;
}
@property(nonatomic, assign) MacApp *owner;
@end

@implementation LokaFlushTarget
@synthesize owner = owner_;
- (void)handleFlush:(id)sender
{
  (void)sender;
  if (self.owner)
  {
    self.owner->flushInvalidationsTick();
  }
}
@end

namespace
{
  static const double kFlushTimerIntervalSeconds = 1.0 / 60.0;

  static bool IsEventTrackingRunLoopMode()
  {
    NSString *mode = [[NSRunLoop currentRunLoop] currentMode];
    if (!mode)
    {
      return false;
    }
    // NSEventTrackingRunLoopMode is an exported constant, not a macro: an
    // #ifdef on it is always false and silently disables this guard.
    return [mode isEqualToString:NSEventTrackingRunLoopMode] ? true : false;
  }
} // namespace

MacApp::MacApp(AppConfigurable *config)
    : App(config),
      menuAttachment_(*this),
      flushTarget_(0),
      flushTimer_(0),
      lastIdleTick_(0),
      idleTimebase_()
{
  idleTimebase_.numer = 0;
  idleTimebase_.denom = 0;
}

MacApp::~MacApp()
{
  if ([quitEventRouter owner] == this)
    [quitEventRouter setOwner:0];
  this->quitEventReply_.decline();
  this->retireComponents();
  this->stopInvalidationFlushTimer();
}

bool MacApp::windowAdopted(Window *window)
{
  MacWindow *macWindow = window ? window->asMacWindow() : 0;
  if (macWindow)
    macWindow->setApp(this);
  return true;
}

void MacApp::run()
{
  ProcessSerialNumber psn = {0, kCurrentProcess};
  TransformProcessType(&psn, kProcessTransformToForegroundApplication);
#if !defined(MAC_OS_X_VERSION_MAX_ALLOWED) || (MAC_OS_X_VERSION_MAX_ALLOWED < 1090)
  SetFrontProcess(&psn);
#endif

  [NSApplication sharedApplication];
  this->attachQuitEventRouter();
  if ([NSApp respondsToSelector:@selector(setActivationPolicy:)])
  {
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
  }

  App::run();
  if (this->windowlessAfterLaunch())
    return;
  mach_timebase_info(&idleTimebase_);
  lastIdleTick_ = mach_absolute_time();

  startInvalidationFlushTimer();
  [NSApp activateIgnoringOtherApps:YES];
  {
    loka::core::Operation::Regime regime;
    [NSApp run];
  }
  stopInvalidationFlushTimer();
}

void MacApp::attachQuitEventRouter()
{
  if (!quitEventRouter)
    quitEventRouter = [[LokaQuitEventRouter alloc] init];
  [NSApp setDelegate:(id)quitEventRouter];
  [quitEventRouter setOwner:this];
}

bool MacApp::holdQuitEvent()
{
  if (!this->flushTimer_)
    return false;
  return this->quitEventReply_.hold();
}

void MacApp::quit()
{
  this->quitEventReply_.accept();
  stopInvalidationFlushTimer();
  // Avoid terminate: here. On Leopard/PPC and earlier AppKit shutdown paths,
  // terminate: may synchronously drain autorelease pools inside AppKit while
  // menu/window-close stacks are still unwinding, which crashes in objc_msgSend
  // or releaseAllPools. Stopping the run loop lets App::run() return and keeps
  // cleanup in our normal C++ object lifetime instead.
  [NSApp stop:nil];
#if defined(MAC_OS_X_VERSION_MAX_ALLOWED) && (MAC_OS_X_VERSION_MAX_ALLOWED >= 101200)
  const NSEventType wakeEventType = NSEventTypeApplicationDefined;
#else
  const NSEventType wakeEventType = NSApplicationDefined;
#endif
  NSEvent *event = [NSEvent otherEventWithType:wakeEventType
                                      location:NSMakePoint(0.0, 0.0)
                                 modifierFlags:0
                                     timestamp:0.0
                                  windowNumber:0
                                       context:nil
                                       subtype:0
                                         data1:0
                                         data2:0];
  if (event)
  {
    // stop: only takes effect after NSApplication dispatches a real event.
    // Put the wake event first so timer-driven quit cannot wait behind later work.
    [NSApp postEvent:event atStart:YES];
  }
}

void MacApp::flushInvalidationsTick()
{
#ifdef LOKA_LIFECYCLE_AUDIT
  const bool entryActive = loka::core::Operation::hasActive();
#endif
  {
    loka::core::Operation turn;
    const unsigned long long now = mach_absolute_time();
    double elapsedSeconds = 0.0;
    if (lastIdleTick_ != 0 && idleTimebase_.denom != 0)
    {
      const unsigned long long elapsed = now - lastIdleTick_;
      const double nanos = static_cast<double>(elapsed) * static_cast<double>(idleTimebase_.numer)
                           / static_cast<double>(idleTimebase_.denom);
      elapsedSeconds = nanos * 1.0e-9;
    }
    lastIdleTick_ = now;
    double dispatchElapsedSeconds = 0.0;
    if (this->consumeIdle(elapsedSeconds, dispatchElapsedSeconds))
    {
      this->handleIdle(dispatchElapsedSeconds);
    }
    turn.settle();
    this->admitAndApplyWindows();
    MacScenePlatformController::flushPendingRelayouts();
    this->reconcileFocus();
    turn.close();
    // Native dealloc chains may autorelease, including on direct tick entry.
    NSAutoreleasePool *retirementPool = [[NSAutoreleasePool alloc] init];
    this->reclaimWindows();
    [retirementPool drain];
    if (this->quitEventReply_.isHeld() && !this->hasQuitAttempt())
      this->quitEventReply_.decline();
  }
#ifdef LOKA_LIFECYCLE_AUDIT
  // A runModal timer joins the outer apply turn; only an outermost timer returns idle.
  assert(loka::core::Operation::hasActive() == entryActive);
#endif
}

void MacApp::startInvalidationFlushTimer()
{
  if (flushTimer_)
  {
    return;
  }
  LokaFlushTarget *target = [[LokaFlushTarget alloc] init];
  [target setOwner:this];
  NSTimer *timer = [NSTimer timerWithTimeInterval:kFlushTimerIntervalSeconds
                                           target:target
                                         selector:@selector(handleFlush:)
                                         userInfo:nil
                                          repeats:YES];
  if (!timer)
  {
    [target release];
    return;
  }
  NSRunLoop *runLoop = [NSRunLoop currentRunLoop];
  [runLoop addTimer:timer forMode:NSDefaultRunLoopMode];
  [runLoop addTimer:timer forMode:NSRunLoopCommonModes];
  flushTarget_ = target;
  flushTimer_ = timer;
}

void MacApp::stopInvalidationFlushTimer()
{
  if (flushTimer_)
  {
    [(NSTimer *)flushTimer_ invalidate];
    flushTimer_ = 0;
  }
  if (flushTarget_)
  {
    [(id)flushTarget_ release];
    flushTarget_ = 0;
  }
}

void MacApp::dispatchNativeMenuCommand(int commandId, void *nativeItem)
{
  this->menuAttachment_.dispatch(commandId, nativeItem);
}

bool MacApp::validateNativeMenuCommand(void *nativeItem)
{
  return this->menuAttachment_.validate((NSMenuItem *)nativeItem);
}

void MacApp::projectMenu(Window *window, const loka::app::MenuSources &sources)
{
  if (window != this->activeWindow() || IsEventTrackingRunLoopMode())
    return;
  this->menuAttachment_.project(sources);
}
