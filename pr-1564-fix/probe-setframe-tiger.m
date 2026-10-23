// Tiger probe 2 (#1564): does setFrame: (0-height -> full -> 0-height) mark the
// superview dirty? Opaque and non-opaque child, child with its own subview.
#import <Cocoa/Cocoa.h>
static int gParent = 0, gChild = 0, gGrand = 0;
@interface P : NSView @end
@implementation P
- (BOOL)isOpaque { return YES; }
- (void)drawRect:(NSRect)r { gParent++; [[NSColor whiteColor] set]; NSRectFill([self bounds]); }
@end
@interface C : NSView @end
@implementation C
- (BOOL)isOpaque { return NO; }
- (void)drawRect:(NSRect)r { gChild++; [[NSColor redColor] set]; NSRectFill([self bounds]); }
@end
@interface G : NSView @end
@implementation G
- (BOOL)isOpaque { return YES; }
- (void)drawRect:(NSRect)r { gGrand++; [[NSColor blueColor] set]; NSRectFill([self bounds]); }
@end
static void settle(NSWindow *w, const char *label)
{
  [w displayIfNeeded];
  [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
  [w flushWindow];
  printf("%-40s parent=%d child=%d grand=%d\n", label, gParent, gChild, gGrand);
  gParent = gChild = gGrand = 0;
}
int main()
{
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  [NSApplication sharedApplication];
  NSWindow *w = [[NSWindow alloc] initWithContentRect:NSMakeRect(60,60,300,200) styleMask:NSTitledWindowMask backing:NSBackingStoreBuffered defer:NO];
  P *p = [[P alloc] initWithFrame:NSMakeRect(0,0,300,200)];
  [w setContentView:p];
  C *c = [[C alloc] initWithFrame:NSMakeRect(20,20,260,0)];
  G *g = [[G alloc] initWithFrame:NSMakeRect(10,10,50,50)];
  [c addSubview:g];
  [p addSubview:c];
  [w orderFront:nil];
  settle(w, "orderFront (child height 0)");
  [c setFrame:NSMakeRect(20,20,260,160)];
  settle(w, "setFrame 0 -> 160");
  [c setFrame:NSMakeRect(20,20,260,0)];
  settle(w, "setFrame 160 -> 0");
  [c setFrame:NSMakeRect(20,20,260,160)];
  [c setNeedsDisplay:YES];
  settle(w, "control: grow + child setNeedsDisplay");
  [c setFrame:NSMakeRect(20,20,260,0)];
  [p setNeedsDisplayInRect:NSMakeRect(20,20,260,160)];
  settle(w, "control: shrink + parent old rect");
  [c setFrame:NSMakeRect(20,20,260,100)];
  settle(w, "setFrame 0 -> 100 (grand clipped)");
  [c setFrame:NSMakeRect(40,20,260,100)];
  settle(w, "setFrame move x 20 -> 40");
  [pool release];
  return 0;
}
