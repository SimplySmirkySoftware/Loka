#include "Win32ClipBoxContext.hpp"
#include "Win32ContainerWindow.hpp"
#include <cassert>
#include <climits>

Win32ClipBoxContext::Win32ClipBoxContext(Win32ScenePlatformController *controller)
    : Win32RetirableContext(controller),
      placedHeight_(INT_MIN)
{
}

bool Win32ClipBoxContext::ensure(HWND parent, const loka::win32::NativeRect &seat)
{
  if (this->hwnd())
    return true;
  EnsureClassRegistered();
  if (!this->createNativeChildWindow(WS_EX_CONTROLPARENT,
                                     L"LOKA_CLIP_BOX",
                                     L"",
                                     WS_CHILD | WS_VISIBLE,
                                     seat,
                                     parent,
                                     0,
                                     GetModuleHandleW(NULL),
                                     this))
    return false;
  this->readLifecycleFactOnAttach();
  return true;
}

void Win32ClipBoxContext::place(const loka::win32::NativeRect &rect, int logicalHeight)
{
  // A difference of short layout coordinates cannot equal the absent sentinel.
  assert(logicalHeight != INT_MIN);
  this->positionNativeWindow(this->hwnd(), rect);
  this->placedHeight_ = logicalHeight;
}

bool Win32ClipBoxContext::queryPlacedHeight(int &out) const
{
  if (this->placedHeight_ == INT_MIN)
    return false;
  out = this->placedHeight_;
  return true;
}

void Win32ClipBoxContext::readLifecycleFactOnAttach()
{
  if (this->owner())
    this->onNativeFact(this->owner()->lifecycleFact(), this->owner()->lifecycleFact());
}

void Win32ClipBoxContext::onNativeFact(loka::app::scene::NodeLifecycleFact, loka::app::scene::NodeLifecycleFact next)
{
  if (this->hwnd())
    ShowWindow(this->hwnd(), next == loka::app::scene::NODE_FACT_ATTACHED ? SW_SHOW : SW_HIDE);
}

// Deliberate class-family twin of Win32ZStackContext::Unit.
void Win32ClipBoxContext::EnsureClassRegistered()
{
  WNDCLASSW wc;
  ZeroMemory(&wc, sizeof(wc));
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = WndProc;
  wc.hInstance = GetModuleHandleW(NULL);
  wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
  wc.hbrBackground = NULL;
  wc.lpszClassName = L"LOKA_CLIP_BOX";
  RegisterClassW(&wc);
}

LRESULT CALLBACK Win32ClipBoxContext::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  Win32ClipBoxContext *self = reinterpret_cast<Win32ClipBoxContext *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (msg == WM_NCCREATE)
  {
    CREATESTRUCTW *create = reinterpret_cast<CREATESTRUCTW *>(lParam);
    self = static_cast<Win32ClipBoxContext *>(create->lpCreateParams);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
  }
  if (msg == WM_NCHITTEST)
    return HTTRANSPARENT;
  LRESULT result;
  if (loka::win32::Win32AnswerContainerMessage(hwnd, msg, wParam, lParam, self ? self->controller() : 0, result))
    return result;
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}
