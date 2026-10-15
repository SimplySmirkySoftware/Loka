#ifndef LOKA_WIN32_RETIRABLE_CONTEXT_HPP
#define LOKA_WIN32_RETIRABLE_CONTEXT_HPP

#include <windows.h>
#include "platform/Win32DisplayScale.hpp"
#include "app/scene/projection/NativeNodeContext.hpp"

class Win32ScenePlatformController;

/** Platform-context base that transfers an HWND to its controller at the
    terminal lifecycle fact. The context dies synchronously; the HWND does
    not cross its native destruction line until the App safe point. */
class Win32RetirableContext : public loka::app::scene::NativeNodeContext
{
public:
  explicit Win32RetirableContext(Win32ScenePlatformController *controller);
  virtual ~Win32RetirableContext();
  /** The one native child, or zero before creation completes and after retirement. */
  HWND hwnd() const
  {
    return this->hwnd_;
  }
  virtual bool acceptsInstallation() const
  {
    return this->hwnd() != 0;
  }
  /** Subclasses override onNativeFact, never this terminal sequencing door.
      The hook runs before HWND retirement; retirement follows its local scopes. */
  virtual void onFactChanged(loka::app::scene::NodeLifecycleFact previous,
                             loka::app::scene::NodeLifecycleFact next);
  /** EXACT damage uses device pixels in this HWND's client coordinates.
      Only contexts that return non-empty EXACT need to expose a target. */
  virtual HWND paintHwnd() const
  {
    return 0;
  }
  /** A visit-local coordinate convention, not a retained HWND identity.
      Each answer record's context supplies its own native destination. */
  static loka::app::scene::PaintScope paintScope()
  {
    const loka::app::scene::PaintScope scope = {1, 0, 0, 0, 0, 0, 0};
    return scope;
  }

protected:
  virtual void onNativeFact(loka::app::scene::NodeLifecycleFact previous,
                            loka::app::scene::NodeLifecycleFact next);
  /** Also used for constructor aborts before installation (TextEditor). */
  void retireWindow();
  void positionNativeWindow(HWND hwnd, const loka::win32::NativeRect &geometry);
  void resizeNativeWindow(HWND hwnd, const loka::win32::NativeRect &geometry);
  /** Called from the derived constructor body with its concrete create parameter.
      Store only after synchronous native creation messages have returned. */
  HWND createNativeChildWindow(DWORD exStyle,
                               LPCWSTR className,
                               LPCWSTR windowName,
                               DWORD style,
                               const loka::win32::NativeRect &geometry,
                               HWND parent,
                               HMENU menu,
                               HINSTANCE instance,
                               void *createParameter);

  Win32ScenePlatformController *controller() const
  {
    return this->controller_;
  }

private:
  Win32ScenePlatformController *controller_;
  HWND hwnd_;
};

#endif // LOKA_WIN32_RETIRABLE_CONTEXT_HPP
