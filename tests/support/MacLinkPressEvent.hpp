#ifndef LOKA_TESTS_MAC_LINK_PRESS_EVENT_HPP
#define LOKA_TESTS_MAC_LINK_PRESS_EVENT_HPP

#include "MacObjCCompat.hpp"
#include <mach/mach_time.h>

@interface NSView (LokaLinkPressGeometry)
- (NSLayoutManager *)lokaLayoutManager;
- (NSTextContainer *)lokaTextContainer;
@end

namespace loka
{
  namespace testing
  {
#if MAC_OS_X_VERSION_MAX_ALLOWED >= 101200
    const NSEventType PRESS_DOWN = NSEventTypeLeftMouseDown;
    const NSEventType PRESS_UP = NSEventTypeLeftMouseUp;
#else
    const NSEventType PRESS_DOWN = NSLeftMouseDown;
    const NSEventType PRESS_UP = NSLeftMouseUp;
#endif

    /** Centre of the visible glyph for one character in this leaf's installed layout. */
    inline bool queryMacPressGlyph(NSView *leaf, NSUInteger character, NSPoint &out)
    {
      NSLayoutManager *manager = [leaf lokaLayoutManager];
      if (character >= [[manager textStorage] length])
        return false;
      const NSRange range = NSMakeRange(character, 1);
      NSUInteger count = 0;
      NSRectArray rects = [manager rectArrayForCharacterRange:range
                                 withinSelectedCharacterRange:NSMakeRange(NSNotFound, 0)
                                              inTextContainer:[leaf lokaTextContainer]
                                                    rectCount:&count];
      if (!count)
        return false;
      const NSRect rect = rects[0];
      NSRange glyph = [manager glyphRangeForCharacterRange:range actualCharacterRange:NULL];
      const NSRect visible =
          NSIntersectionRect(rect, [manager lineFragmentUsedRectForGlyphAtIndex:glyph.location effectiveRange:NULL]);
      if (NSIsEmptyRect(visible))
        return false;
      out = NSMakePoint(NSMidX(visible), NSMidY(visible));
      return true;
    }

    /** One autoreleased mouse event stamped on the same clock as physical input. */
    inline NSEvent *macPressEvent(
        NSWindow *window, NSEventType type, NSPoint point, NSInteger number, NSUInteger flags, NSInteger clicks)
    {
      mach_timebase_info_data_t scale;
      if (mach_timebase_info(&scale) != KERN_SUCCESS)
        return nil;
      NSTimeInterval timestamp = mach_absolute_time() * static_cast<double>(scale.numer) / scale.denom / 1.0e9;
      return [NSEvent mouseEventWithType:type
                                location:point
                           modifierFlags:flags
                               timestamp:timestamp
                            windowNumber:[window windowNumber]
                                 context:nil
                             eventNumber:number
                              clickCount:clicks
                                pressure:type == PRESS_UP ? 0 : 1];
    }
  } // namespace testing
} // namespace loka
#endif
