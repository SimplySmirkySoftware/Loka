#ifndef LOKA_CLIP_HOST_VIEW_HPP
#define LOKA_CLIP_HOST_VIEW_HPP
#include "../MacObjCCompat.hpp"

/** Transparent projection parent. Children retain their parent's coordinates. */
@interface LokaClipHostView : NSView
- (void)lokaPlace:(NSRect)rect;
@end
#endif
