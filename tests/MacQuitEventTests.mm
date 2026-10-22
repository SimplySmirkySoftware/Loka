#include "MacQuitEventTests.hpp"
#include "support/TestVerify.hpp"
#include "support/NativeCloseTestConfig.hpp"
#include "MacApp.hpp"
#include "MacWindow.hpp"
#include <AppKit/AppKit.h>
#include <ApplicationServices/ApplicationServices.h>
#include <objc/runtime.h>

/** Uses the same attachment and clock doors as run(), without entering NSApp's loop. */
class MacQuitEventTestAccess
{
public:
  static void attach(MacApp &app, bool clock)
  {
    app.attachQuitEventRouter();
    if (clock)
      app.startInvalidationFlushTimer();
  }
  static bool held(const MacApp &app)
  {
    return app.quitEventReply_.isHeld();
  }
  static bool pending(const MacApp &app)
  {
    return app.hasPendingWindowAdmission();
  }
};

namespace
{
  class QuitTestPool
  {
  public:
    QuitTestPool()
        : pool_([[NSAutoreleasePool alloc] init])
    {
    }
    ~QuitTestPool()
    {
      [this->pool_ drain];
    }

  private:
    NSAutoreleasePool *pool_;
    QuitTestPool(const QuitTestPool &);
    QuitTestPool &operator=(const QuitTestPool &);
  };

  struct ReplyObservation
  {
    ReplyObservation()
        : count(0),
          error(0)
    {
    }
    int count;
    int error;
  };
  ReplyObservation *observingReply = 0;
  typedef void (*ResumeImplementation)(id, SEL, NSAppleEventManagerSuspensionID);
  ResumeImplementation originalResume = 0;

  // Observe at the actual resume door, while the native reply still exists.
  // The real Foundation implementation always runs; no fake suspension IDs.
  void observeResume(id manager, SEL selector, NSAppleEventManagerSuspensionID suspension)
  {
    if (observingReply)
    {
      ++observingReply->count;
      observingReply->error =
          [[[manager replyAppleEventForSuspensionID:suspension] paramDescriptorForKeyword:keyErrorNumber] int32Value];
    }
    originalResume(manager, selector, suspension);
  }

  class ReplyObserver
  {
  public:
    ReplyObserver()
    {
      Method method = class_getInstanceMethod([NSAppleEventManager class], @selector(resumeWithSuspensionID:));
      originalResume = reinterpret_cast<ResumeImplementation>(
          method_setImplementation(method, reinterpret_cast<IMP>(&observeResume)));
      observingReply = &this->result;
    }
    ~ReplyObserver()
    {
      observingReply = 0;
      Method method = class_getInstanceMethod([NSAppleEventManager class], @selector(resumeWithSuspensionID:));
      method_setImplementation(method, reinterpret_cast<IMP>(originalResume));
    }
    ReplyObservation result;

  private:
    ReplyObserver(const ReplyObserver &);
    ReplyObserver &operator=(const ReplyObserver &);
  };

  class QuitApp : public MacApp
  {
  public:
    explicit QuitApp(AppConfigurable &config)
        : MacApp(&config)
    {
    }
  };

  class QuitFixture
  {
  public:
    explicit QuitFixture(bool clock = true)
        : config(context, true),
          app(config)
    {
      [NSApplication sharedApplication];
      LOKA_VERIFY(!this->config.roster.open(loka::app::DocumentFacts()).isNone());
      this->app.App::run();
      MacQuitEventTestAccess::attach(this->app, clock);
      // The host runner never runs NSApp. Send the launch notification through
      // its delegate to exercise the production installation callback.
      [[NSApp delegate]
          applicationWillFinishLaunching:[NSNotification
                                             notificationWithName:NSApplicationWillFinishLaunchingNotification
                                                           object:NSApp]];
    }
    NativeCloseTestContext<MacWindow> context;
    NativeCloseTestConfig config;
    QuitApp app;
  };

  /** Raw descriptor storage outlives the App that can resume its suspended reply. */
  class RawQuit
  {
  public:
    RawQuit()
    {
      AEAddressDesc target = {typeNull, 0};
      const ProcessSerialNumber process = {0, kCurrentProcess};
      LOKA_VERIFY(AECreateDesc(typeProcessSerialNumber, &process, sizeof(process), &target) == noErr);
      LOKA_VERIFY(
          AECreateAppleEvent(
              kCoreEventClass, kAEQuitApplication, &target, kAutoGenerateReturnID, kAnyTransactionID, &this->event_)
          == noErr);
      LOKA_VERIFY(AECreateAppleEvent(
                      kCoreEventClass, kAEAnswer, &target, kAutoGenerateReturnID, kAnyTransactionID, &this->reply_)
                  == noErr);
      AEDisposeDesc(&target);
    }
    void dispatch()
    {
#if __LP64__
      const SRefCon refCon = this;
#else
      const SRefCon refCon = 0;
#endif
      LOKA_VERIFY([[NSAppleEventManager sharedAppleEventManager] dispatchRawAppleEvent:&this->event_
                                                                          withRawReply:&this->reply_
                                                                         handlerRefCon:refCon]
                  == noErr);
    }
    ~RawQuit()
    {
      AEDisposeDesc(&this->reply_);
      AEDisposeDesc(&this->event_);
    }
    bool hasError() const
    {
      DescType type = typeNull;
      Size size = 0;
      return AEGetParamPtr(&this->reply_, keyErrorNumber, typeSInt32, &type, 0, 0, &size) != errAEDescNotFound;
    }
    int error() const
    {
      SInt32 value = 0;
      DescType type = typeNull;
      Size size = 0;
      LOKA_VERIFY(AEGetParamPtr(&this->reply_, keyErrorNumber, typeSInt32, &type, &value, sizeof(value), &size)
                  == noErr);
      return value;
    }

  private:
    AppleEvent event_;
    AppleEvent reply_;
    RawQuit(const RawQuit &);
    RawQuit &operator=(const RawQuit &);
  };
} // namespace

void testMacQuitEventHoldsReplyAndReservesQuit()
{
  QuitTestPool pool;
  RawQuit event;
  QuitFixture fixture;
  event.dispatch();
  LOKA_VERIFY(MacQuitEventTestAccess::held(fixture.app));
  LOKA_VERIFY(!event.hasError());
  LOKA_VERIFY(fixture.config.roster.closeAsked().get().id.isNone());
  fixture.app.flushInvalidationsTick();
  LOKA_VERIFY(!fixture.config.roster.closeAsked().get().id.isNone());
}

void testMacQuitEventSuccessBeforeQuitReturns()
{
  QuitTestPool pool;
  ReplyObserver observer;
  {
    RawQuit event;
    QuitFixture fixture;
    event.dispatch();
    fixture.app.flushInvalidationsTick();
    const loka::app::CloseAsk ask = fixture.config.roster.closeAsked().get();
    LOKA_VERIFY(!ask.id.isNone());
    LOKA_VERIFY(fixture.config.roster.close(ask.id) == loka::core::EDIT_OK);
    fixture.app.flushInvalidationsTick();
    LOKA_VERIFY(observer.result.count == 1 && observer.result.error == 0);
    LOKA_VERIFY(!MacQuitEventTestAccess::held(fixture.app));
  }
  LOKA_VERIFY(observer.result.count == 1);
}

void testMacQuitEventDeclinesWhenAttemptEnds()
{
  QuitTestPool pool;
  ReplyObserver observer;
  RawQuit event;
  QuitFixture fixture;
  event.dispatch();
  fixture.app.flushInvalidationsTick();
  fixture.config.roster.declineClose(fixture.config.roster.closeAsked().get());
  fixture.app.flushInvalidationsTick();
  LOKA_VERIFY(observer.result.count == 1 && observer.result.error == userCanceledErr);
  LOKA_VERIFY(!MacQuitEventTestAccess::held(fixture.app));
}

void testMacQuitEventDestructionDeclines()
{
  QuitTestPool pool;
  ReplyObserver observer;
  {
    RawQuit event;
    QuitFixture fixture;
    event.dispatch();
    LOKA_VERIFY(MacQuitEventTestAccess::held(fixture.app));
  }
  LOKA_VERIFY(observer.result.count == 1 && observer.result.error == userCanceledErr);
}

void testMacQuitEventSecondEventRefused()
{
  QuitTestPool pool;
  ReplyObserver observer;
  RawQuit first;
  RawQuit second;
  QuitFixture fixture;
  first.dispatch();
  second.dispatch();
  LOKA_VERIFY(second.error() == userCanceledErr);
  LOKA_VERIFY(MacQuitEventTestAccess::held(fixture.app));
  LOKA_VERIFY(observer.result.count == 0);
}

void testMacQuitEventWithoutTimerRefused()
{
  QuitTestPool pool;
  RawQuit event;
  QuitFixture fixture(false);
  event.dispatch();
  LOKA_VERIFY(event.error() == userCanceledErr);
  LOKA_VERIFY(!MacQuitEventTestAccess::held(fixture.app));
}

void testMacQuitEventWithoutAppRefused()
{
  QuitTestPool pool;
  {
    QuitFixture fixture;
  }
  RawQuit event;
  event.dispatch();
  LOKA_VERIFY(event.error() == userCanceledErr);
}

void testMacQuitEventSequentialAppsAttach()
{
  QuitTestPool pool;
  {
    QuitFixture first;
  }
  // No second launch notification: registration must survive the first App.
  NativeCloseTestContext<MacWindow> context;
  NativeCloseTestConfig config(context, true);
  LOKA_VERIFY(!config.roster.open(loka::app::DocumentFacts()).isNone());
  RawQuit event;
  QuitApp second(config);
  second.App::run();
  MacQuitEventTestAccess::attach(second, true);
  event.dispatch();
  LOKA_VERIFY(MacQuitEventTestAccess::held(second));
}

void testMacQuitEventTerminateFallbackCancelsAndReserves()
{
  QuitTestPool pool;
  QuitFixture fixture;
  id delegate = [NSApp delegate];
  LOKA_VERIFY([delegate respondsToSelector:@selector(applicationShouldTerminate:)]);
  LOKA_VERIFY([delegate applicationShouldTerminate:NSApp] == NSTerminateCancel);
  LOKA_VERIFY(MacQuitEventTestAccess::pending(fixture.app));
  LOKA_VERIFY(fixture.config.roster.closeAsked().get().id.isNone());
  fixture.app.flushInvalidationsTick();
  LOKA_VERIFY(!fixture.config.roster.closeAsked().get().id.isNone());
}

void testMacQuitEventQuitTwiceRepliesOnce()
{
  QuitTestPool pool;
  ReplyObserver observer;
  {
    RawQuit event;
    QuitFixture fixture;
    event.dispatch();
    fixture.app.quit();
    LOKA_VERIFY(observer.result.count == 1 && observer.result.error == 0);
    fixture.app.quit();
    LOKA_VERIFY(observer.result.count == 1);
  }
  LOKA_VERIFY(observer.result.count == 1);
}
