#include "MacDeferredDialogPresenter.hpp"

@implementation LokaMacDeferredDialogPresenter
#ifdef TEST_BUILD
- (NSTimer *)scheduledTimerForTesting
{
  return timer_;
}
#endif
- (id)initWithOwner:(MacDeferredDialogOwner *)owner
{
  self = [super init];
  if (self)
  {
    owner_ = owner;
    timer_ = nil;
  }
  return self;
}

- (void)dealloc
{
  [self cancelPresent];
  [super dealloc];
}

- (void)schedulePresent
{
  if (timer_)
  {
    return;
  }
  timer_ = [[NSTimer scheduledTimerWithTimeInterval:0.0
                                             target:self
                                           selector:@selector(onTimer:)
                                           userInfo:nil
                                            repeats:NO] retain];
}

- (void)cancelPresent
{
  if (!timer_)
  {
    return;
  }
  [timer_ invalidate];
  [timer_ release];
  timer_ = nil;
}

- (void)detachOwner
{
  owner_ = 0;
}

- (void)onTimer:(NSTimer *)timer
{
  if (timer != timer_)
  {
    return;
  }
  [timer_ release];
  timer_ = nil;
  if (owner_)
  {
    owner_->presentDeferred();
  }
}
@end
