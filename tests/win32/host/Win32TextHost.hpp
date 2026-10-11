#ifndef LOKA_TEST_WIN32_TEXT_HOST_HPP
#define LOKA_TEST_WIN32_TEXT_HOST_HPP
// Replace platform neighbors; compile the real contexts, table and environment delivery.
#define LOKA_WIN32_SCENE_PLATFORM_CONTROLLER_HPP
#include "windows.h"
#include "app/style/Style.hpp"
#include "app/layout/TextShaping.hpp"
#include "app/scene/projection/PlatformController.hpp"
#include "support/MeasurementRetryQueue.hpp"
#include "platform/Win32DisplayScale.hpp"
#include "platform/Win32DisplayFont.hpp"
#include "Win32TextEnvironment.hpp"
#include "core/resource/Image.hpp"
#include "app/RectSurface.hpp"
class Win32ScenePlatformController : public loka::app::scene::IPlatformController
{
public:
  MeasurementRetryQueue relayoutRetries;
  virtual void requestRelayout() { this->relayoutRetries.request(); }

  Win32ScenePlatformController()
      : rootHwnd_(0),
        small_(),
        large_()
  {
    this->small_.ascent = 10;
    this->small_.descent = 2;
    this->small_.leading = 1;
    this->small_.advance = 4;
    this->large_.ascent = 20;
    this->large_.descent = 5;
    this->large_.leading = 2;
    this->large_.advance = 8;
  }
  HFONT textFont(const loka::app::TextStyle &style) const
  {
    if (this->displayFont_.get())
      return this->displayFont_.find(style);
    return style.hasItalic_ && style.italic_ ? &this->large_ : &this->small_;
  }
  const loka::win32::Win32DisplayScale &displayScale() const
  {
    return this->displayScale_;
  }
  HFONT displayFont() const
  {
    return this->displayFont_.get() ? this->displayFont_.get() : &this->small_;
  }
  loka::app::TextShaping textShaping() const
  {
    return loka::app::PER_RUN;
  }
  HWND projectionParentHwnd() const
  {
    return this->rootHwnd_;
  }

  void onChange(loka::app::scene::Node *, loka::app::scene::NodeDirtyFlags, bool) {}
  void synchronize() {}
  bool hasPendingSync() const
  {
    return false;
  }
  void destroy() {}
  static void requestDirtyRect(HWND, const RECT *, BOOL) {}
  static void redrawDirtySubtreeNow(HWND, const RECT *, BOOL) {}
  void cancelRectSurfaceExtent(loka::app::RectSurfaceNode *) {}
  static void requestDirtySubtree(HWND, const RECT *, BOOL) {}
  // Acceptance twin of the production controller door. This measurement host
  // has no native repaint queue; pixel delivery is pinned on Win32.
  static bool requestTransparentChildRepaint(HWND child)
  {
    RECT rect;
    return child && GetParent(child) && GetWindowRect(child, &rect);
  }
  enum
  {
    NATIVE_PAINT_RECT_SURFACE
  };
  static void noteNativePaint(HWND, int, bool) {}
  void updateDisplayScale(const loka::win32::Win32DisplayScale &);
  void ensureDisplayFont();
  void applyDisplayFontToNativeSubtree(const loka::win32::Win32DisplayFont &);
  void positionNativeWindow(HWND, const loka::win32::NativeRect &);
  void resizeNativeWindow(HWND h, const loka::win32::NativeRect &r)
  {
    this->positionNativeWindow(h, r);
  }
  HWND createNativeChildWindow(
      DWORD, LPCWSTR, LPCWSTR, DWORD, const loka::win32::NativeRect &, HWND, HMENU, HINSTANCE, void *);
  void queueNativeRetirement(HWND);
  bool captureWindowClientBitmap(HWND, loka::core::resource::Image &out) const
  {
    out = loka::core::resource::Image();
    return false;
  }
  Win32TextEnvironment textEnvironment_;
  HWND rootHwnd_;
  loka::app::RailMetrics railMetrics_;
  loka::win32::Win32DisplayScale displayScale_;
  loka::win32::Win32DisplayFont displayFont_;

private:
  mutable HostFont small_, large_;
};
#endif
