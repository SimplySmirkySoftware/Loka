#ifndef LOKA_WIN32_DISPLAY_SCALE_HPP
#define LOKA_WIN32_DISPLAY_SCALE_HPP

#include <windows.h>
#include "core/Frame.hpp"
#include "app/layout/RailMetrics.hpp"

namespace loka
{
  namespace win32
  {
    class Win32DisplayScale;

    /** Completed device-pixel geometry. Only the projection policy can mint it.
        Integer storage is the Win32 alignment invariant; macOS must instead
        align points with round(pt * backingScale) / backingScale. */
    struct NativeRect
    {
      const RECT r;

    private:
      explicit NativeRect(const RECT &value)
          : r(value)
      {
      }
      friend class Win32DisplayScale;
    };

    /** Device-pixel length, never implicitly constructible from an int. */
    struct NativeLength
    {
      const int px;

    private:
      explicit NativeLength(int value)
          : px(value)
      {
      }
      friend class Win32DisplayScale;
    };

    /** Device-pixel edge, never implicitly constructible from an int. */
    struct NativeEdge
    {
      const int px;

    private:
      explicit NativeEdge(int value)
          : px(value)
      {
      }
      friend class Win32DisplayScale;
    };

    /** Immutable conversion between Loka logical display units and one
        Win32 window's device-pixel coordinate space. */
    class Win32DisplayScale
    {
    public:
      explicit Win32DisplayScale(UINT dpi = 96,
                                 const loka::app::RailMetrics &metrics = loka::app::RailMetrics());

      /** Snapshot of the rail facts, independent of actual display DPI. */
      const loka::app::RailMetrics &railMetrics() const { return this->metrics_; }

      static bool queryForWindow(HWND hwnd, Win32DisplayScale &out);
      static Win32DisplayScale forWindow(HWND hwnd);
      static Win32DisplayScale forSystem();

      UINT dpi() const
      {
        return this->dpi_;
      }
      int percent() const;
      /** Absolute edge: nearest integer pixel, ties away from zero (MulDiv).
          Extents must be differences of projected absolute edges. */
      int projectEdge(int logicalCoordinate) const;
      /** Typed edge/extent adapters; endpoints are absolute lu coordinates. */
      NativeEdge nativeEdge(int lu) const
      {
        return NativeEdge(this->projectEdge(lu));
      }
      NativeLength nativeLength(int startLu, int endLu) const
      {
        return NativeLength(this->projectEdge(endLu) - this->projectEdge(startLu));
      }
      /** Client/viewport capacity rounds down; native measurements round up.
          At unit spaceScale and 144 dpi, a 301 px client admits 200 lu (the
          old nearest inverse admitted 201). At 96 dpi every integer is exact. */
      int capacityToLu(int px) const;
      /** Outward client allocation, paired with floor capacityToLu. Ordinary
          child extents remain differences of nearest absolute edges. */
      NativeLength clientLengthToNative(int lu) const;
      int measurementToLu(int px) const;
      /** Client ingress: diagnose short overflow and verify containment. */
      int clientCapacityToLu(int px) const;
      NativeRect projectFrame(const loka::core::Frame &lu) const;
      NativeRect damageToNative(const loka::core::Frame &lu) const;
      /** SCROLLINFO deliberately stores lu, including its positions. */
      int scrollOffsetToNative(int lu) const
      {
        return lu;
      }
      int scrollPositionToLu(int position) const
      {
        return position;
      }
      /** DPI only, no space scale: decoded sprite geometry. */
      NativeRect projectDeviceOnly(const loka::core::Frame &pixels) const;
      /** Already-native damage or desktop/chrome geometry, never lu. */
      static NativeRect fromDevicePixels(const RECT &pixels)
      {
        return NativeRect(pixels);
      }
      /** A font size in logical units (1 lu = 1 px at 96 dpi, like every layout number on this rail): height = lu × dpi/96 × fontScale, never spaceScale. Not points. */
      int fontHeightToNative(int logicalUnits) const;
      /** Compatibility for pre-policy callers outside the production rail.
          New callers must choose capacity, measurement, or absolute edges. */
      int unprojectEdge(int px) const
      {
        return this->intrinsicPixelsToLu(px);
      }
      int projectLength(int lu) const
      {
        return this->projectEdge(lu);
      }
      int unprojectLength(int px) const
      {
        return this->intrinsicPixelsToLu(px);
      }
      /** Nearest reservation of decoded intrinsic pixels in the layout seat.
          This converts only the reservation; source rectangles remain pixels. */
      int intrinsicPixelsToLu(int px) const;
      /** Converts a device-space length measured at sourceScale into this
          scale's device space. */
      int scaleLengthFrom(const Win32DisplayScale &sourceScale,
                          int sourceLength) const;
      void projectFrame(const loka::core::Frame &lu, RECT &out) const
      {
        out = this->projectFrame(lu).r;
      }
      /** Preserves the Win32 virtual-desktop origin while converting the
          content size back to logical display units. */
      loka::core::Frame windowContentFrameFromNative(
          const RECT &nativeWindowRect,
          int nativeClientWidth,
          int nativeClientHeight) const;
      bool adjustWindowRect(RECT &nativeClientRect,
                            DWORD style,
                            BOOL hasMenu,
                            DWORD exStyle) const;

      /** A system metric (SM_*) at this scale's DPI: GetSystemMetricsForDpi
          where the OS has it (Windows 10 1607+), the system-DPI metric
          otherwise. A non-client measure predicted from it, such as the
          scroll bar thickness, then matches what Windows adds to a
          per-monitor-DPI window on that monitor. */
      int systemMetric(int index) const;

      bool operator==(const Win32DisplayScale &rhs) const
      {
        return this->dpi_ == rhs.dpi_ && this->metrics_ == rhs.metrics_;
      }
      bool operator!=(const Win32DisplayScale &rhs) const
      {
        return !(*this == rhs);
      }

    private:
      bool spaceFactors(int &numerator, int &denominator) const;

      UINT dpi_;
      loka::app::RailMetrics metrics_;
    };
  } // namespace win32
} // namespace loka

#endif // LOKA_WIN32_DISPLAY_SCALE_HPP
