#ifndef LOKA_WIN32_OWNED_CHILD_WINDOW_HPP
#define LOKA_WIN32_OWNED_CHILD_WINDOW_HPP

#include <windows.h>
#include "platform/Win32DisplayScale.hpp"

class Win32ScenePlatformController;

/** One native child owned until synchronous retirement transfers it to the
    controller clock. Destruction only verifies that transfer already happened.
    Creation, placement and retirement are constant work per owner call. */
class Win32OwnedChildWindow
{
public:
  explicit Win32OwnedChildWindow(Win32ScenePlatformController *controller);
  ~Win32OwnedChildWindow();
  HWND hwnd() const
  {
    return this->hwnd_;
  }
  Win32ScenePlatformController *controller() const
  {
    return this->controller_;
  }
  void retireWindow();
  void raise();
  void positionNativeWindow(HWND hwnd, const loka::win32::NativeRect &geometry);
  void resizeNativeWindow(HWND hwnd, const loka::win32::NativeRect &geometry);
  HWND createNativeChildWindow(DWORD exStyle,
                               LPCWSTR className,
                               LPCWSTR windowName,
                               DWORD style,
                               const loka::win32::NativeRect &geometry,
                               HWND parent,
                               HMENU menu,
                               HINSTANCE instance,
                               void *createParameter);
  /** A child of the owned window, destroyed with it; never retired separately. */
  HWND createSubordinateChildWindow(DWORD exStyle,
                                    LPCWSTR className,
                                    LPCWSTR windowName,
                                    DWORD style,
                                    const loka::win32::NativeRect &geometry,
                                    HWND parent,
                                    HMENU menu,
                                    HINSTANCE instance,
                                    void *createParameter);

private:
  Win32ScenePlatformController *controller_;
  HWND hwnd_;
  Win32OwnedChildWindow(const Win32OwnedChildWindow &);
  Win32OwnedChildWindow &operator=(const Win32OwnedChildWindow &);
};
#endif
