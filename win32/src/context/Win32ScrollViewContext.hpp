#ifndef LOKA_WIN32_SCROLL_VIEW_CONTEXT_HPP
#define LOKA_WIN32_SCROLL_VIEW_CONTEXT_HPP

#include <windows.h>

#include "Win32RetirableContext.hpp"
#include "app/nodes/nestable/ScrollView.hpp"

class Win32ScenePlatformController;

/** Owns a frame whose subordinate viewport clips content and whose sibling
    vertical scrollbar leaves the committed content inset rows clear. */
class Win32ScrollViewContext : public Win32RetirableContext
{
public:
  Win32ScrollViewContext(Win32ScenePlatformController *controller,
                         HWND parent,
                         int x,
                         int y,
                         int width,
                         int height,
                         loka::app::ScrollViewNode *node);
  virtual ~Win32ScrollViewContext();
  virtual loka::app::scene::PaintAnswer queryPaintDamage(const loka::app::scene::PaintQuery &query) const;

  void readLifecycleFactOnAttach();

  bool isValid() const;
  void relayout(int x, int y, int width, int height);
  /** Native projection parent; private children are valid only while the frame lives. */
  HWND viewportHwnd() const
  {
    return this->hwnd() ? this->viewport_ : 0;
  }
  /** Successful layout commits logical clearance, projected anew on every placement. */
  void commitVerticalBarInsets(const loka::app::ScrollViewContentInsets &insets, const loka::core::Frame &seatLu);

  /** Publishes native range/page/position state and returns the clamped
      logical offset represented by that state. */
  int setScrollMetrics(int bar, int content, int viewport, int offset);
  /** Shared range clamp for layout projection and native input. */
  int clampOffset(int value, int maximum) const;

  /** Routes a root-window wheel message to its nearest viewport at the
      message's screen position; refuses targets outside that root. */
  static bool forwardMouseWheel(HWND root, UINT msg, WPARAM wParam, LPARAM lParam);

  /** Request side of the same settled-position commit used by bar input.
      Only moving axes are set; a failed native read reports no fact. */
  bool applyScrollRequest(const loka::core::Frame &target);

protected:
  virtual void onNativeFact(loka::app::scene::NodeLifecycleFact previous, loka::app::scene::NodeLifecycleFact next);

private:
  friend class Win32InputDoor;
  void handleMouseWheel(int bar, WPARAM wParam);
  void applyAttachedPresentation();
  void applyDetachedPresentation();
  bool handleScroll(int bar, int command, int thumbPosition);
  bool readScrollInfo(int bar, SCROLLINFO &info) const;
  int maximumOffset(const SCROLLINFO &info) const;
  void setVisualPosition(int bar, int value);
  class SettledPosition;
  /** Shared input/request commit: pre-read, set, read back, then publish the pair once. */
  bool commitSettledPosition(const SettledPosition &selection);

  HWND barHwnd() const
  {
    return this->hwnd() ? this->bar_ : 0;
  }
  HWND nativeBar(int &axis) const;
  void placeVerticalBar(const loka::core::Frame &seatLu);
  static Win32ScrollViewContext *FromFrame(HWND hwnd);
  static Win32ScrollViewContext *FromViewport(HWND hwnd);
  static LRESULT CALLBACK FrameWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
  static void EnsureClassRegistered();
  static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

  loka::app::ScrollViewNode *node_;
  HWND viewport_;
  HWND bar_;
  loka::app::ScrollViewContentInsets committedInsets_;
};

#endif // LOKA_WIN32_SCROLL_VIEW_CONTEXT_HPP
