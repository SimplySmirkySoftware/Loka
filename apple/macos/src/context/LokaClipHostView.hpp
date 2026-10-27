#ifndef LOKA_CLIP_HOST_VIEW_HPP
#define LOKA_CLIP_HOST_VIEW_HPP
#include "../MacObjCCompat.hpp"

/** Transparent projection parent. Children retain their parent's coordinates;
    MacClipBoxContext::place keeps the bounds origin on the frame origin. */
@interface LokaClipHostView : NSView
@end
#endif
