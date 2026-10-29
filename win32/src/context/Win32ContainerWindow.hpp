#ifndef LOKA_WIN32_CONTAINER_WINDOW_HPP
#define LOKA_WIN32_CONTAINER_WINDOW_HPP

#include "../Win32PaintGround.hpp"
#include "../Win32ScenePlatformController.hpp"

namespace loka
{
  namespace win32
  {
    /** The messages every container child window (a ZStack unit, a ScrollView
        viewport) answers alike. It paints the window ground beneath its
        transparent static children (#1199, so no WS_CLIPCHILDREN), and it
        routes control commands to the controller: a control parented to a
        container notifies the container, not the root window, so the command
        goes through the same controller door Win32Window::WndProc uses.
        Returns true and sets result for a message it answered; otherwise
        returns false, leaves result untouched, and the caller handles its own
        input before falling through to DefWindowProcW. The root window
        answers the same three paint messages in Win32Window::WndProc itself,
        because it also records native paint for the paint probe. */
    inline bool Win32AnswerContainerMessage(
        HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, Win32ScenePlatformController *controller, LRESULT &result)
    {
      switch (msg)
      {
      case WM_CTLCOLORSTATIC:
        result = reinterpret_cast<LRESULT>(
            Win32StaticTextColors(reinterpret_cast<HDC>(wParam), reinterpret_cast<HWND>(lParam)));
        return true;
      // Paint-only subtree requests must restore the ground even without
      // RDW_ERASE, and erasure must not be a separate clearing pass.
      case WM_ERASEBKGND:
        result = 1;
        return true;
      case WM_PAINT:
      {
        PAINTSTRUCT paint;
        HDC dc = BeginPaint(hwnd, &paint);
        Win32ScenePlatformController::paintWindowGround(dc, paint.rcPaint);
        EndPaint(hwnd, &paint);
        result = 0;
        return true;
      }
      case WM_COMMAND:
        if (controller && controller->handleCommand(wParam, lParam))
        {
          result = 0;
          return true;
        }
        return false;
      default:
        return false;
      }
    }
  } // namespace win32
} // namespace loka

#endif
