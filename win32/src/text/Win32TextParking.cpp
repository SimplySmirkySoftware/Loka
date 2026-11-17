#include "Win32TextParking.hpp"
#include "Win32TextStore.hpp"
LRESULT CALLBACK Win32TextParking::procedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
  if (message == WM_COMMAND)
  {
    if (lParam && HIWORD(wParam) == EN_CHANGE)
    {
      Win32TextStore *row =
          reinterpret_cast<Win32TextStore *>(GetWindowLongPtrW(reinterpret_cast<HWND>(lParam), GWLP_USERDATA));
      if (row)
        row->noteEdit();
    }
    return 0;
  }
  return DefWindowProcW(window, message, wParam, lParam);
}
bool Win32TextParking::ensure()
{
  if (this->window_)
    return true;
#ifdef TEST_BUILD
  if (loka::win32::testing::refuseTextParkingCreation())
    return false;
#endif
  WNDCLASSW wc = {};
  wc.lpfnWndProc = &Win32TextParking::procedure;
  wc.hInstance = GetModuleHandleW(0);
  wc.lpszClassName = L"LOKA_TEXT_PARKING";
  if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
    return false;
  this->window_ = CreateWindowExW(0, wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, 0, wc.hInstance, 0);
  return this->window_ != 0;
}
void Win32TextParking::destroy()
{
  this->ledger_.retireAll();
  this->drain();
  if (this->window_)
  {
    DestroyWindow(this->window_);
    this->window_ = 0;
  }
}
