#include "MacImageViewTests.hpp"
#include "MacObjCCompat.hpp"
#include "support/TestVerify.hpp"
#include <cstring>
#include <objc/runtime.h>
#include <cstdlib>

@interface NSObject (LokaImageViewTestMessages)
- (void)setImage:(NSImage *)image;
@end

@interface LokaImageViewCustomRepDelegate : NSObject
- (BOOL)drawImage:(NSCustomImageRep *)rep;
@end
@implementation LokaImageViewCustomRepDelegate
- (BOOL)drawImage:(NSCustomImageRep *)rep
{
  (void)rep;
  return YES;
}
@end

namespace
{
  NSBitmapImageRep *bitmap(NSString *space)
  {
    NSBitmapImageRep *rep = [[[NSBitmapImageRep alloc] initWithBitmapDataPlanes:NULL
                                                                     pixelsWide:4
                                                                     pixelsHigh:4
                                                                  bitsPerSample:8
                                                                samplesPerPixel:4
                                                                       hasAlpha:YES
                                                                       isPlanar:NO
                                                                 colorSpaceName:space
                                                                    bytesPerRow:16
                                                                   bitsPerPixel:32] autorelease];
    LOKA_VERIFY(rep != nil && [rep bitmapData] != NULL);
    std::memset([rep bitmapData], 0, 64);
    return rep;
  }

  NSView *imageView()
  {
    Class cls = NSClassFromString(@"LokaImageView");
    LOKA_VERIFY(cls != Nil);
    NSView *view = [[[cls alloc] initWithFrame:NSMakeRect(0, 0, 4, 4)] autorelease];
    LOKA_VERIFY(view != nil);
    return view;
  }

  NSImage *drawnImage(NSView *view)
  {
    // Read the view's private slot through the runtime (KVC does not map
    // "image" to the trailing-underscore ivar) instead of adding a getter.
    Ivar ivar = class_getInstanceVariable([view class], "image_");
    LOKA_VERIFY(ivar != 0);
    return (NSImage *)object_getIvar(view, ivar);
  }
} // namespace

void testMacImageViewConvertsBitmapToDeviceRGB()
{
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  [NSApplication sharedApplication];
  NSBitmapImageRep *source = bitmap(NSCalibratedRGBColorSpace);
  // Black/white endpoints avoid device-dependent midtone conversion; the
  // asymmetric pattern also detects orientation and source-size mistakes.
  for (int y = 0; y < 4; ++y)
    for (int x = 0; x < 4; ++x)
    {
      unsigned char *pixel = [source bitmapData] + y * 16 + x * 4;
      pixel[0] = pixel[1] = pixel[2] = (x == 0 || y == 3) ? 255 : 0;
      pixel[3] = 255;
    }
  [source bitmapData][(1 * 4 + 2) * 4 + 3] = 0;
  NSImage *input = [[[NSImage alloc] initWithSize:NSMakeSize(2, 2)] autorelease];
  [source setSize:NSMakeSize(2, 2)];
  [input addRepresentation:source];
  NSView *view = imageView();
  NSGraphicsContext *previous = [NSGraphicsContext currentContext];
  [view setImage:input];
  LOKA_VERIFY([NSGraphicsContext currentContext] == previous);
  NSImage *drawn = drawnImage(view);
  LOKA_VERIFY(drawn != nil && drawn != input);
  LOKA_VERIFY(NSEqualSizes([drawn size], [input size]));
  LOKA_VERIFY([[drawn representations] count] == 1);
  NSBitmapImageRep *converted = (NSBitmapImageRep *)[[drawn representations] objectAtIndex:0];
  LOKA_VERIFY([converted isKindOfClass:[NSBitmapImageRep class]]);
  LOKA_VERIFY([[converted colorSpaceName] isEqualToString:NSDeviceRGBColorSpace]);
  LOKA_VERIFY([converted pixelsWide] == 4 && [converted pixelsHigh] == 4);
  LOKA_VERIFY([converted bitsPerSample] == 8 && [converted samplesPerPixel] == 4);
  LOKA_VERIFY([converted hasAlpha] && ![converted isPlanar]);
  LOKA_VERIFY([[source colorSpaceName] isEqualToString:NSCalibratedRGBColorSpace]);

  NSBitmapImageRep *output = bitmap(NSDeviceRGBColorSpace);
  NSGraphicsContext *context = [NSGraphicsContext graphicsContextWithBitmapImageRep:output];
  LOKA_VERIFY(context != nil);
  [NSGraphicsContext saveGraphicsState];
  [NSGraphicsContext setCurrentContext:context];
  [view drawRect:[view bounds]];
  [NSGraphicsContext restoreGraphicsState];
  LOKA_VERIFY([NSGraphicsContext currentContext] == previous);
  for (int i = 0; i < 64; ++i)
    LOKA_VERIFY(std::abs(static_cast<int>([output bitmapData][i]) - static_cast<int>([source bitmapData][i])) <= 2);
  [pool drain];
}

void testMacImageViewPreservesNonBitmapImage()
{
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  [NSApplication sharedApplication];
  LokaImageViewCustomRepDelegate *delegate = [[[LokaImageViewCustomRepDelegate alloc] init] autorelease];
  NSCustomImageRep *custom = [[[NSCustomImageRep alloc] initWithDrawSelector:@selector(drawImage:)
                                                                    delegate:delegate] autorelease];
  LOKA_VERIFY(custom != nil);
  [custom setSize:NSMakeSize(2, 2)];
  NSImage *input = [[[NSImage alloc] initWithSize:NSMakeSize(2, 2)] autorelease];
  [input addRepresentation:custom];
  NSView *view = imageView();
  [view setImage:input];
  LOKA_VERIFY(drawnImage(view) == input);

  // A bitmap first must not hide a later vector/custom representation.
  NSImage *mixed = [[[NSImage alloc] initWithSize:NSMakeSize(2, 2)] autorelease];
  [mixed addRepresentation:bitmap(NSCalibratedRGBColorSpace)];
  [mixed addRepresentation:custom];
  [view setImage:mixed];
  LOKA_VERIFY(drawnImage(view) == mixed);
  [pool drain];
}
