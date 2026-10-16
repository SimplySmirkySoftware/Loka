#ifndef LOKA_MAC_NATIVE_GEOMETRY_HPP
#define LOKA_MAC_NATIVE_GEOMETRY_HPP

#include "MacProjection.hpp"

namespace loka
{
  namespace macos
  {
    /** Typed AppKit frame write; callers must project layout before entry. */
    inline void SetMacFrame(NSView *view, const MacRect &frame)
    {
      [view setFrame:frame.r];
    }

    /** Width is projected; AppKit's unbounded measurement height stays native. */
    inline NSRect MacMeasurementBounds(const MacLength &width)
    {
      return NSMakeRect(0, 0, width.pt, CGFLOAT_MAX);
    }

    /** One-line natural measurement: neither axis is bounded, so nothing is projected. */
    inline NSRect MacUnboundedMeasurementBounds()
    {
      return NSMakeRect(0, 0, CGFLOAT_MAX, CGFLOAT_MAX);
    }

    /** Native document width includes at least the clip width; height is projected layout. */
    inline void SetMacDocumentSize(NSScrollView *scrollView, CGFloat width, const MacLength &height)
    {
      [[scrollView documentView] setFrame:NSMakeRect(0, 0, width, height.pt)];
    }

    /** Clip translation is applied by AppKit, never repeated for child frames. */
    inline void ScrollMacDocument(NSView *documentView, const MacEdge &x, const MacEdge &y)
    {
      [documentView scrollPoint:NSMakePoint(x.pt, y.pt)];
    }
  }
}
#endif
