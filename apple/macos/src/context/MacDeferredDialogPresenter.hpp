#ifndef LOKA_MAC_DEFERRED_DIALOG_PRESENTER_HPP
#define LOKA_MAC_DEFERRED_DIALOG_PRESENTER_HPP

/** Borrowed presentation door; detachOwner ends the native-to-context edge. */
class MacDeferredDialogOwner
{
public:
  virtual void presentDeferred() = 0;
protected:
  virtual ~MacDeferredDialogOwner() {}
};

#ifdef __OBJC__
#import <Foundation/Foundation.h>

/** Shared retained one-shot timer for file and confirm dialogs. Contexts cancel
    on detach, sever the owner on retirement, then queue this native object. */
@interface LokaMacDeferredDialogPresenter : NSObject
{
@private
  MacDeferredDialogOwner *owner_;
  NSTimer *timer_;
}
- (id)initWithOwner:(MacDeferredDialogOwner *)owner;
- (void)schedulePresent;
- (void)cancelPresent;
- (void)detachOwner;
- (void)onTimer:(NSTimer *)timer;
#ifdef TEST_BUILD
@property(nonatomic, assign, readonly) NSTimer *scheduledTimerForTesting;
#endif
@end

#endif
#endif
