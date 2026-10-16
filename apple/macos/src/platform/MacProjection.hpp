#ifndef LOKA_MAC_PROJECTION_HPP
#define LOKA_MAC_PROJECTION_HPP

#include "../MacObjCCompat.hpp"
#include "app/layout/RailMetrics.hpp"
#include "core/Frame.hpp"

namespace loka
{
  namespace macos
  {
    class MacProjection;

    /** Completed native point geometry; only the projection policy mints it. */
    struct MacRect
    {
      const NSRect r;
    private:
      explicit MacRect(const NSRect &value) : r(value) {}
      friend class MacProjection;
    };

    /** Native point edge, distinct from logical coordinates. */
    struct MacEdge
    {
      const CGFloat pt;
    private:
      explicit MacEdge(CGFloat value) : pt(value) {}
      friend class MacProjection;
    };

    /** Native point extent, formed from projected endpoints. */
    struct MacLength
    {
      const CGFloat pt;
    private:
      explicit MacLength(CGFloat value) : pt(value) {}
      friend class MacProjection;
    };

    /** Scene-owned metrics. The root view is borrowed from the enclosing window;
        backing scale is queried live for alignment only, never space scaling.
        A null root is the bounded pre-window bootstrap policy. */
    class MacProjection
    {
    public:
      explicit MacProjection(void *rootView = 0,
                             const app::RailMetrics &metrics = app::RailMetrics());
      const app::RailMetrics &railMetrics() const { return this->metrics_; }
      /** Nearest point, ties away from zero, then nearest device pixel. */
      MacEdge projectEdge(int lu) const;
      MacLength projectLength(int startLu, int endLu) const;
      int capacityToLu(CGFloat pt) const;
      int clientCapacityToLu(CGFloat pt) const;
      int measurementToLu(CGFloat pt) const;
      /** Pure client capacity after horizontal chrome, converted only once. */
      int clientHeightBelowHorizontalScroller(const core::Frame &seatLu) const;
      int intrinsicPixelsToLu(int pixels) const;
      MacRect projectFrame(const core::Frame &lu) const;
      /** Allocate client capacity outward so the floor inverse preserves the
          declared lu size. Child frames still use nearest absolute edges. */
      MacRect projectClientSize(int widthLu, int heightLu) const;
      MacRect damageToNative(const core::Frame &lu) const;
      MacEdge scrollOffsetToNative(int lu) const;
      int scrollPositionToLu(CGFloat pt) const;
      /** DPI only, no space scale; macOS historically draws sprites raw. */
      MacRect projectDeviceOnly(const core::Frame &pixels) const;

    private:
      double backingScale() const;
      double spaceScale() const;
      void *const rootView_;
      const app::RailMetrics metrics_;
    };
  }
}
#endif
