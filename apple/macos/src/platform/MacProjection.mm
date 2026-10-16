#include "MacProjection.hpp"
#include <cassert>
#include <climits>
#include <cmath>

namespace
{
  double Nearest(double value)
  {
    return value < 0.0 ? std::ceil(value - 0.5) : std::floor(value + 0.5);
  }
}

namespace loka
{
  namespace macos
  {
    MacProjection::MacProjection(void *rootView, const app::RailMetrics &metrics)
        : rootView_(rootView), metrics_(metrics)
    {
      // RailMetrics already refuses ratios below one (falls back to unit).
      assert(metrics.spaceScale.valid());
    }

    double MacProjection::spaceScale() const
    {
      return static_cast<double>(this->metrics_.spaceScale.num) / this->metrics_.spaceScale.den;
    }

    double MacProjection::backingScale() const
    {
      NSWindow *window = [(NSView *)this->rootView_ window];
      if ([window respondsToSelector:@selector(backingScaleFactor)])
      {
        const double scale = [window backingScaleFactor];
        if (scale > 0.0)
          return scale;
      }
      return 1.0;
    }

    MacEdge MacProjection::projectEdge(int lu) const
    {
      const double backing = this->backingScale();
      const CGFloat pt = static_cast<CGFloat>(Nearest(Nearest(lu * this->spaceScale()) * backing) / backing);
      // At the unit ratio, integer lu on 1x/2x backing stays byte-identical.
      assert(!this->metrics_.spaceScale.isUnit() || backing != std::floor(backing) || pt == lu);
      return MacEdge(pt);
    }

    MacLength MacProjection::projectLength(int startLu, int endLu) const
    {
      return MacLength(this->projectEdge(endLu).pt - this->projectEdge(startLu).pt);
    }

    int MacProjection::capacityToLu(CGFloat pt) const
    {
      int capacity = static_cast<int>(std::floor(pt / this->spaceScale()));
      // Nearest edge alignment must not turn a floor capacity into overflow.
      if (this->projectEdge(capacity).pt > pt)
        --capacity;
      return capacity;
    }

    int MacProjection::clientCapacityToLu(CGFloat pt) const
    {
      const int capacity = this->capacityToLu(pt);
      assert(capacity >= SHRT_MIN && capacity <= SHRT_MAX && "macOS client capacity must fit short");
      assert(this->projectEdge(capacity).pt <= pt && "logical capacity must fit native client");
      return capacity;
    }

    int MacProjection::clientHeightBelowHorizontalScroller(const core::Frame &seatLu) const
    {
      CGFloat thickness = 0;
      if ([NSScroller respondsToSelector:@selector(preferredScrollerStyle)] &&
          [NSScroller respondsToSelector:@selector(scrollerWidthForControlSize:scrollerStyle:)])
      {
        // Overlay scrollers (style 1) take no space; legacy ones do.
        const NSInteger style = [NSScroller preferredScrollerStyle];
        thickness = style == 1 ? 0
                               : [NSScroller scrollerWidthForControlSize:LOKA_MAC_CONTROL_SIZE_REGULAR
                                                           scrollerStyle:[NSScroller preferredScrollerStyle]];
      }
      else
      {
        // Tiger through Snow Leopard: the one scroller width there is.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        thickness = [NSScroller scrollerWidth];
#pragma clang diagnostic pop
      }
      const CGFloat height = this->projectFrame(seatLu).r.size.height - thickness;
      return height > 0 ? this->clientCapacityToLu(height) : 0;
    }

    int MacProjection::measurementToLu(CGFloat pt) const
    {
      return static_cast<int>(std::ceil(pt / this->spaceScale()));
    }

    int MacProjection::intrinsicPixelsToLu(int pixels) const
    {
      return static_cast<int>(Nearest(pixels / this->spaceScale()));
    }

    MacRect MacProjection::projectFrame(const core::Frame &lu) const
    {
      const MacEdge left = this->projectEdge(lu.x);
      const MacEdge top = this->projectEdge(lu.y);
      const MacLength width = this->projectLength(lu.x, lu.x + lu.width);
      const MacLength height = this->projectLength(lu.y, lu.y + lu.height);
      return MacRect(NSMakeRect(left.pt, top.pt, width.pt, height.pt));
    }

    MacRect MacProjection::projectClientSize(int widthLu, int heightLu) const
    {
      // Window allocation is the outward dual of capacityToLu, not a child
      // edge. Round in whole points as AppKit window sizing does on all SDKs.
      return MacRect(NSMakeRect(0, 0, std::ceil(widthLu * this->spaceScale()),
                                std::ceil(heightLu * this->spaceScale())));
    }

    MacRect MacProjection::damageToNative(const core::Frame &lu) const
    {
      const double backing = this->backingScale();
      const double factor = this->spaceScale() * backing;
      const double left = std::floor(lu.x * factor) / backing;
      const double top = std::floor(lu.y * factor) / backing;
      const double right = std::ceil((static_cast<double>(lu.x) + lu.width) * factor) / backing;
      const double bottom = std::ceil((static_cast<double>(lu.y) + lu.height) * factor) / backing;
      return MacRect(NSMakeRect(left, top, right - left, bottom - top));
    }

    MacEdge MacProjection::scrollOffsetToNative(int lu) const
    {
      return this->projectEdge(lu);
    }

    int MacProjection::scrollPositionToLu(CGFloat pt) const
    {
      return static_cast<int>(Nearest(pt / this->spaceScale()));
    }

    MacRect MacProjection::projectDeviceOnly(const core::Frame &pixels) const
    {
      return MacRect(NSMakeRect(pixels.x, pixels.y, pixels.width, pixels.height));
    }
  }
}
