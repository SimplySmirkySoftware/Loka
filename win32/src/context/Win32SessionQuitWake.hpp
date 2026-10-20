#ifndef LOKA_WIN32_SESSION_QUIT_WAKE_HPP
#define LOKA_WIN32_SESSION_QUIT_WAKE_HPP

#include <windows.h>

// XP SDK targets may hide these flags; newer Windows can send them to the same binary.
#ifndef ENDSESSION_CRITICAL
#define ENDSESSION_CRITICAL 0x40000000
#endif
#ifndef ENDSESSION_CLOSEAPP
#define ENDSESSION_CLOSEAPP 0x00000001
#endif

namespace loka
{
  namespace win32
  {
    /** Pointer-free wake only; the App reservation is delivered at admission. */
    inline UINT SessionQuitWakeMessage()
    {
      return WM_APP + 42;
    }
  } // namespace win32
} // namespace loka

#endif
