#ifndef LOKA_WIN32_DIALOG_RESULT_WAKE_HPP
#define LOKA_WIN32_DIALOG_RESULT_WAKE_HPP

#include "../Win32Window.hpp"

namespace loka
{
  namespace win32
  {
    /** One pointer-free wake family for the Window's typed dialog ledger. */
    inline UINT DialogResultWakeMessage() { return WM_APP + 41; }

    /** Seal before waking. Delivery remains exclusively at App admission. */
    template <class Port, class Result>
    void QueueDeferredDialogResult(Port &port, const Result &result)
    {
      Window *window = port.seal(result);
      Win32Window *win32 = window ? window->asWin32Window() : 0;
      if (win32 && win32->hwnd())
        PostMessageW(win32->hwnd(), DialogResultWakeMessage(), 0, 0);
      // Failed posts and missing HWNDs leave the same pending work for App admission.
    }
  } // namespace win32
} // namespace loka

#endif
