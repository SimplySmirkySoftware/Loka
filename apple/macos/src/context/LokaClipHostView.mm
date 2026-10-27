#include "LokaClipHostView.hpp"

// The selector exists from 10.9; older SDKs still compile the guarded send.
@interface NSView (LokaClipHostCompatibility)
- (void)setClipsToBounds:(BOOL)clips;
@end

@implementation LokaClipHostView
- (id)initWithFrame:(NSRect)frame
{
  self = [super initWithFrame:frame];
  if (self)
  {
    if ([self respondsToSelector:@selector(setClipsToBounds:)])
      [self setClipsToBounds:YES];
    [self setBoundsOrigin:frame.origin];
  }
  return self;
}
- (BOOL)isFlipped
{
  return YES;
}
- (BOOL)isOpaque
{
  return NO;
}
- (BOOL)wantsUpdateLayer
{
  return YES;
}
- (void)updateLayer
{
}
- (NSView *)hitTest:(NSPoint)point
{
  NSView *hit = [super hitTest:point];
  return hit == self ? nil : hit;
}
@end
