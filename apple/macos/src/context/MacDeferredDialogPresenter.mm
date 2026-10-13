#include "MacDeferredDialogPresenter.hpp"

// Library/core Objective-C: internal state goes through private accessors
// (AGENTS.md, macOS Objective-C policy); the ivars are touched only by the
// synthesized properties.
@interface LokaMacDeferredDialogPresenter ()
@property(nonatomic, assign) MacDeferredDialogOwner *owner;
@property(nonatomic, retain) NSTimer *timer;
@end

@implementation LokaMacDeferredDialogPresenter
@synthesize owner = owner_;
@synthesize timer = timer_;
#ifdef TEST_BUILD
- (NSTimer *)scheduledTimerForTesting
{
  return self.timer;
}
#endif
- (id)initWithOwner:(MacDeferredDialogOwner *)owner
{
  self = [super init];
  if (self)
  {
    self.owner = owner;
    self.timer = nil;
  }
  return self;
}

- (void)dealloc
{
  [self cancelPresent];
  [super dealloc];
}

- (BOOL)schedulePresent
{
  if (self.timer)
  {
    return YES;
  }
  // The retain property keeps the scheduled timer; a refused allocation leaves
  // nil and the caller presents synchronously instead of waiting forever.
  self.timer = [NSTimer scheduledTimerWithTimeInterval:0.0
                                                target:self
                                              selector:@selector(onTimer:)
                                              userInfo:nil
                                               repeats:NO];
  return self.timer != nil;
}

- (void)cancelPresent
{
  if (!self.timer)
  {
    return;
  }
  [self.timer invalidate];
  self.timer = nil;
}

- (void)detachOwner
{
  self.owner = 0;
}

- (void)onTimer:(NSTimer *)timer
{
  if (timer != self.timer)
  {
    return;
  }
  self.timer = nil;
  if (self.owner)
  {
    self.owner->presentDeferred();
  }
}
@end
