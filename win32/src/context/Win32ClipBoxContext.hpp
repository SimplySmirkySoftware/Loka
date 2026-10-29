#ifndef LOKA_WIN32_CLIP_BOX_CONTEXT_HPP
#define LOKA_WIN32_CLIP_BOX_CONTEXT_HPP

#include "Win32RetirableContext.hpp"

/** Node-owned clip parent; the base transfers its HWND to the controller clock
    at retirement. Only the last placed logical height survives layout visits. */
class Win32ClipBoxContext : public Win32RetirableContext, public loka::app::scene::IClipBoxContext
{
public:
  explicit Win32ClipBoxContext(Win32ScenePlatformController *controller);
  virtual loka::app::scene::IClipBoxContext *asClipBoxContext()
  {
    return this;
  }
  /** Layout calls once per admitted visit; at most one allocation, no row walk. */
  bool ensure(HWND parent, const loka::win32::NativeRect &seat);
  /** One native placement per completed visit, even for an equal rectangle. */
  void place(const loka::win32::NativeRect &rect, int logicalHeight);
  /** Refusal asks once per visit; false before the first placement. No row walk. */
  bool queryPlacedHeight(int &out) const;
  /** Installation and successful creation read the owner's fact once. */
  void readLifecycleFactOnAttach();

protected:
  /** One show/hide per delivered fact; the base then queues terminal retirement. */
  virtual void onNativeFact(loka::app::scene::NodeLifecycleFact previous, loka::app::scene::NodeLifecycleFact next);

private:
  static void EnsureClassRegistered();
  static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
  int placedHeight_;
  Win32ClipBoxContext(const Win32ClipBoxContext &);
  Win32ClipBoxContext &operator=(const Win32ClipBoxContext &);
};

#ifdef TEST_BUILD
namespace loka
{
  namespace testing
  {
    void failWin32ClipBoxContexts(unsigned count);
    bool declineWin32ClipBoxContext();
  } // namespace testing
} // namespace loka
#endif
#endif
