#include "Win32PaintGround.hpp"
#include "Win32InputDoor.hpp"
#include "Win32Window.hpp"
#include "Win32App.hpp"
#include <windows.h>
#include <string>
#include "app/core/Window.hpp"
#include "core/StateTracker.hpp"

#include "app/scene/Scene.hpp"
#include "Win32ScenePlatformController.hpp"
#include "context/Win32OpenFileDialogContext.hpp"
#include "context/Win32ScrollViewContext.hpp"
#include "context/Win32EditTextContext.hpp"
#include "context/Win32TextEditorContext.hpp"
#include "context/Win32FocusParticipant.hpp"
#include "core/String.hpp"
#include "platform/Win32String.hpp"
#include "platform/Win32DisplayScale.hpp"

namespace
{
  static bool g_classRegistered = false;
  static const wchar_t *kWndClassName = L"DevWndClass";
  static const DWORD kWindowStyle = WS_OVERLAPPEDWINDOW;
  static const DWORD kWindowExStyle = WS_EX_CONTROLPARENT;
  // Windows SDK headers hide this message when the XP compatibility preset
  // sets _WIN32_WINNT=0x0501, although newer Windows can still send it to the
  // same binary. Keep the wire value local to the capability-aware rail.
  static const UINT kWindowDpiChangedMessage = 0x02E0;

  /** One policy for window geometry and scene admission, including bootstrap
      before an HWND exists. Native chrome and font queries stay DPI-only. */
  loka::win32::Win32DisplayScale WindowProjection(HWND hwnd)
  {
    return loka::win32::Win32DisplayScale(
        loka::win32::Win32DisplayScale::forWindow(hwnd).dpi(),
        loka::win32::DefaultRailMetrics());
  }

  bool CalculateOuterSizeForClient(const loka::win32::NativeLength &clientWidth,
                                   const loka::win32::NativeLength &clientHeight,
                                   DWORD style,
                                   DWORD exStyle,
                                   BOOL hasMenu,
                                   const loka::win32::Win32DisplayScale &scale,
                                   int &outerWidth,
                                   int &outerHeight)
  {
    RECT rect = {0, 0, clientWidth.px, clientHeight.px};
    if (!scale.adjustWindowRect(rect, style, hasMenu, exStyle))
    {
      return false;
    }
    outerWidth = rect.right - rect.left;
    outerHeight = rect.bottom - rect.top;
    return true;
  }

  BOOL MoveNativeWindow(HWND hwnd, const loka::win32::NativeRect &geometry)
  {
    return MoveWindow(hwnd,
                      geometry.r.left,
                      geometry.r.top,
                      geometry.r.right - geometry.r.left,
                      geometry.r.bottom - geometry.r.top,
                      TRUE);
  }

  void PositionNativeWindow(HWND hwnd, const loka::win32::NativeRect &geometry)
  {
    SetWindowPos(hwnd,
                 NULL,
                 geometry.r.left,
                 geometry.r.top,
                 geometry.r.right - geometry.r.left,
                 geometry.r.bottom - geometry.r.top,
                 SWP_NOZORDER | SWP_NOACTIVATE);
  }

  HWND CreateNativeWindow(const loka::win32::NativeRect &geometry, void *owner)
  {
    return CreateWindowExW(kWindowExStyle,
                           kWndClassName,
                           L"",
                           kWindowStyle,
                           geometry.r.left,
                           geometry.r.top,
                           geometry.r.right - geometry.r.left,
                           geometry.r.bottom - geometry.r.top,
                           NULL,
                           NULL,
                           GetModuleHandle(NULL),
                           owner);
  }

  // The declared frame is a request (#712): the outer rectangle, chrome
  // included, is placed inside the work area of the monitor it mostly covers.
  // Size shrinks first, then the right/bottom edges move in, then the
  // left/top edges so the title bar always stays reachable. Every path that
  // positions the top-level window goes through here: the frame observer and
  // the create path via applyNativeContentFrame, and the DPI-change handler
  // with the rectangle Windows suggests for the destination monitor.
  bool ClampOuterRectToWorkArea(RECT &outer)
  {
    MONITORINFO monitorInfo = {};
    monitorInfo.cbSize = sizeof(monitorInfo);
    if (!GetMonitorInfoW(MonitorFromRect(&outer, MONITOR_DEFAULTTONEAREST),
                         &monitorInfo))
    {
      return false;
    }
    const RECT &work = monitorInfo.rcWork;
    int width = outer.right - outer.left;
    int height = outer.bottom - outer.top;
    if (width > work.right - work.left)
    {
      width = work.right - work.left;
    }
    if (height > work.bottom - work.top)
    {
      height = work.bottom - work.top;
    }
    int x = outer.left;
    int y = outer.top;
    if (x + width > work.right)
    {
      x = work.right - width;
    }
    if (y + height > work.bottom)
    {
      y = work.bottom - height;
    }
    if (x < work.left)
    {
      x = work.left;
    }
    if (y < work.top)
    {
      y = work.top;
    }
    outer.left = x;
    outer.top = y;
    outer.right = x + width;
    outer.bottom = y + height;
    return true;
  }
} // namespace

Win32Window::Win32Window(PlatformContext *context, const WindowProps &props)
    : Window(context, props),
      hwnd_(NULL),
      app_(NULL),
      menuAttachment_(*this),
      scenePlatformController_(0)
{
  // Track logical states and project them into native Win32 state.
  this->observeNativeState(this->displayTitleState(), &Win32Window::TitleChangedThunk, this);
  this->observeNativeState(this->frameState(), &Win32Window::FrameChangedThunk, this);
}

Win32Window::~Win32Window()
{
  this->dialogResults().close();
  this->detachNativeStateObservers();
  if (this->hwnd_)
  {
    this->destroyNativeWindow();
  }
  else
  {
    this->teardownScene();
  }
}

bool Win32Window::queryNativeContentFrame(loka::core::Frame &out) const
{
  if (!this->hwnd_)
  {
    return false;
  }
  RECT windowRect;
  RECT clientRect;
  if (!GetWindowRect(this->hwnd_, &windowRect) || !GetClientRect(this->hwnd_, &clientRect))
  {
    return false;
  }
  out = WindowProjection(this->hwnd_)
            .windowContentFrameFromNative(windowRect,
                                          clientRect.right - clientRect.left,
                                          clientRect.bottom - clientRect.top);
  return true;
}

bool Win32Window::storeCurrentNativeContentFrame()
{
  loka::core::Frame frame;
  if (!this->queryNativeContentFrame(frame))
  {
    return false;
  }
  this->storeNativeFrame(frame);
  return true;
}

bool Win32Window::applyNativeContentFrame(const loka::core::Frame &frame)
{
  if (!this->hwnd_ || !frame.hasSize())
  {
    return false;
  }
  RECT windowRect;
  if (!GetWindowRect(this->hwnd_, &windowRect))
  {
    return false;
  }
  int outerWidth = 0;
  int outerHeight = 0;
  const DWORD style = static_cast<DWORD>(GetWindowLongPtrW(this->hwnd_, GWL_STYLE));
  const DWORD exStyle = static_cast<DWORD>(GetWindowLongPtrW(this->hwnd_, GWL_EXSTYLE));
  const loka::win32::Win32DisplayScale scale =
      WindowProjection(this->hwnd_);
  if (!CalculateOuterSizeForClient(scale.clientLengthToNative(frame.width),
                                   scale.clientLengthToNative(frame.height),
                                   style,
                                   exStyle,
                                   GetMenu(this->hwnd_) ? TRUE : FALSE,
                                   scale,
                                   outerWidth,
                                   outerHeight))
  {
    assert(false && "Win32 client size must convert to an outer window size");
    return false;
  }
  const int x = frame.x >= 0 ? frame.x : windowRect.left;
  const int y = frame.y >= 0 ? frame.y : windowRect.top;
  RECT outer = {x, y, x + outerWidth, y + outerHeight};
  if (!ClampOuterRectToWorkArea(outer))
  {
    return false;
  }
  if (EqualRect(&outer, &windowRect))
  {
    return true;
  }
  return MoveNativeWindow(this->hwnd_, scale.fromDevicePixels(outer)) != FALSE;
}

bool Win32Window::detachMenuForTeardown(HMENU expectedMenu)
{
  if (!this->hwnd_ || !expectedMenu || GetMenu(this->hwnd_) != expectedMenu)
  {
    return false;
  }
  loka::core::Frame contentFrame;
  if (!this->queryNativeContentFrame(contentFrame))
  {
    return false;
  }
  const DWORD style = static_cast<DWORD>(GetWindowLongPtrW(this->hwnd_, GWL_STYLE));
  const DWORD exStyle = static_cast<DWORD>(GetWindowLongPtrW(this->hwnd_, GWL_EXSTYLE));
  const loka::win32::Win32DisplayScale scale =
      WindowProjection(this->hwnd_);
  int outerWidth = 0;
  int outerHeight = 0;
  if (!CalculateOuterSizeForClient(scale.clientLengthToNative(contentFrame.width),
                                   scale.clientLengthToNative(contentFrame.height),
                                   style,
                                   exStyle,
                                   FALSE,
                                   scale,
                                   outerWidth,
                                   outerHeight))
  {
    return false;
  }

  const LONG_PTR userData = GetWindowLongPtrW(this->hwnd_, GWLP_USERDATA);
  SetWindowLongPtrW(this->hwnd_, GWLP_USERDATA, 0);
  const BOOL detached = SetMenu(this->hwnd_, NULL);
  if (detached)
  {
    const RECT outer = {contentFrame.x, contentFrame.y, contentFrame.x + outerWidth, contentFrame.y + outerHeight};
    MoveNativeWindow(this->hwnd_, scale.fromDevicePixels(outer));
  }
  SetWindowLongPtrW(this->hwnd_, GWLP_USERDATA, userData);
  return detached != FALSE;
}

void Win32Window::setApp(Win32App *app)
{
  app_ = app;
  if (app_ && hwnd_)
  {
    app_->setActiveWindow(this);
  }
}

// Deliberate counterpart of NullWindow's controller-identity comparison.
bool Win32Window::hasPendingNativeVisibility() const
{
  return this->visibility_->get() != (this->hwnd_ != NULL);
}

void Win32Window::applyNativeVisibility()
{
  if (!this->hasPendingNativeVisibility())
    return;
  if (this->visibility_->get())
  {
    this->createNativeWindow();
    if (this->hwnd_)
      this->onShow();
  }
  else
  {
    this->onHide();
    this->destroyNativeWindow();
  }
}

void Win32Window::TitleChangedThunk(void *userData)
{
  Win32Window *self = static_cast<Win32Window *>(userData);
  if (self && self->hwnd_)
  {
    std::wstring wide;
    if (loka::win32::MaterializeWideString(self->displayTitleState().get(), wide))
    {
      SetWindowTextW(self->hwnd_, wide.c_str());
    }
    else
    {
      SetWindowTextW(self->hwnd_, L"");
    }
  }
}

void Win32Window::FrameChangedThunk(void *userData)
{
  Win32Window *self = static_cast<Win32Window *>(userData);
  if (!self || !self->hwnd_)
  {
    return;
  }
  loka::core::Frame frame = self->frameState().get();
  self->applyNativeContentFrame(frame);
}

Win32Window *Win32Window::FromHwnd(HWND hwnd)
{
  wchar_t className[256];
  if (!hwnd || !GetClassNameW(hwnd, className, 256) || lstrcmpW(className, kWndClassName) != 0)
    return 0;
  return reinterpret_cast<Win32Window *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

LRESULT CALLBACK Win32Window::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  Win32Window *self = NULL;
  if (msg == WM_NCCREATE)
  {
    CREATESTRUCTW *cs = reinterpret_cast<CREATESTRUCTW *>(lParam);
    self = static_cast<Win32Window *>(cs->lpCreateParams);
    SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    self->hwnd_ = hwnd;
  }
  else
  {
    self = reinterpret_cast<Win32Window *>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
  }

  if (self)
  {
    if (Win32OpenFileDialogContext::handlePostedResultMessage(msg, wParam, lParam))
    {
      return 0;
    }
    switch (msg)
    {
    case WM_MOUSEWHEEL:
      if (Win32ScrollViewContext::forwardMouseWheel(hwnd, wParam, lParam))
      {
        return 0;
      }
      break;
    case WM_COMMAND:
      if (self->handleCommand(wParam, lParam))
      {
        return 0;
      }
      break;
    case WM_KEYDOWN:
      if (self->app_ && wParam == VK_SPACE)
      {
        if (self->app_->handleKeyPress(' '))
        {
          return 0;
        }
      }
      break;
    case WM_CHAR:
      if (self->app_)
      {
        if (wParam >= 0x20 && wParam <= 0x7E && wParam != ' ')
        {
          if (self->app_->handleKeyPress(static_cast<char>(wParam)))
          {
            return 0;
          }
        }
      }
      break;
    case WM_ACTIVATE:
      if (self->app_ && LOWORD(wParam) != WA_INACTIVE)
      {
        self->app_->setActiveWindow(static_cast<Window *>(self));
      }
      if ((LOWORD(wParam) == WA_ACTIVE || LOWORD(wParam) == WA_CLICKACTIVE)
          && !HIWORD(wParam))
      {
        loka::app::scene::NodeContext *context = self->publishedFocusContext();
        const HWND target = Win32FocusParticipant::target(context);
        if (target && Win32FocusParticipant::read(target) == context)
        {
          SetFocus(target);
          return 0;
        }
      }
      break;
    case WM_CTLCOLORSTATIC:
    {
      return reinterpret_cast<LRESULT>(
          loka::win32::Win32StaticTextColors(reinterpret_cast<HDC>(wParam)));
    }
    case WM_ERASEBKGND:
      Win32ScenePlatformController::noteNativePaint(hwnd, Win32ScenePlatformController::NATIVE_PAINT_ROOT, true);
      return 1;
    case WM_SIZE:
      if (self->scenePlatformController_)
        Win32InputDoor::windowSize(*self, wParam, lParam);
      else if (wParam != SIZE_MINIMIZED && self->hwnd_)
        self->storeCurrentNativeContentFrame();
      return 0;
    case kWindowDpiChangedMessage:
      if (self->scenePlatformController_)
        Win32InputDoor::windowDpi(*self, wParam, lParam);
      else
        self->handleNativeDpi(wParam, lParam);
      return 0;
    case WM_MOVE:
      if (self->hwnd_ && !IsIconic(self->hwnd_))
      {
        self->storeCurrentNativeContentFrame();
      }
      return 0;
    case WM_PAINT:
    {
      Win32ScenePlatformController::noteNativePaint(hwnd, Win32ScenePlatformController::NATIVE_PAINT_ROOT, false);
      PAINTSTRUCT ps;
      HDC hdc = BeginPaint(hwnd, &ps);
      // Clear the background with the native window color.
      Win32ScenePlatformController::paintWindowGround(hdc, ps.rcPaint);
      EndPaint(hwnd, &ps);
      break;
    }
    case WM_DESTROY:
      self->dialogResults().close();
      self->teardownScene();
      self->hwnd_ = NULL;
      self->menuAttachment_.resetForTeardown();
      self->onDestroy();
      if (self->app_)
      {
        self->app_->requestWindowClose(static_cast<Window *>(self));
      }
      break;
    default:
      return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void Win32Window::createNativeWindow()
{
  if (!g_classRegistered)
  {
    // Unicode window class: the title and other text messages stay UTF-16;
    // WndProc must pair with DefWindowProcW below.
    WNDCLASSW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc = Win32Window::WndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = kWndClassName;
    RegisterClassW(&wc);
    g_classRegistered = true;
  }
  const loka::core::Frame defaultFrame = Window::defaultFrame();
  const int clientWidth = this->hasSize() ? this->width() : defaultFrame.width;
  const int clientHeight = this->hasSize() ? this->height() : defaultFrame.height;
  const loka::win32::Win32DisplayScale initialScale =
      WindowProjection(NULL);
  int outerWidth = 0;
  int outerHeight = 0;
  if (!CalculateOuterSizeForClient(initialScale.clientLengthToNative(clientWidth),
                                   initialScale.clientLengthToNative(clientHeight),
                                   kWindowStyle,
                                   kWindowExStyle,
                                   FALSE,
                                   initialScale,
                                   outerWidth,
                                   outerHeight))
  {
    assert(false && "Win32 client size must convert to an outer window size");
    return;
  }
  const int x = this->hasPosition() ? this->positionX() : defaultFrame.x;
  const int y = this->hasPosition() ? this->positionY() : defaultFrame.y;
  const RECT outer = {x, y, x + outerWidth, y + outerHeight};
  HWND hwnd = CreateNativeWindow(initialScale.fromDevicePixels(outer), this);
  if (hwnd)
  {
    this->hwnd_ = hwnd;
    this->applyNativeContentFrame(
        loka::core::Frame(this->hasPosition() ? this->positionX() : defaultFrame.x,
                          this->hasPosition() ? this->positionY() : defaultFrame.y,
                          clientWidth,
                          clientHeight));
    this->storeCurrentNativeContentFrame();
    if (this->app_)
    {
      this->app_->setActiveWindow(this);
    }
    TitleChangedThunk(this);
    UpdateWindow(hwnd);
    this->dialogResults().open(*this);
    this->onCreate();
    this->mountScene();
  }
}

void Win32Window::destroyNativeWindow()
{
  this->dialogResults().close();
  if (this->hwnd_)
  {
    App *appToClear = this->app_ && this->app_->activeWindow() == this ? this->app_ : 0;
    teardownScene();
    SetWindowLongPtr(this->hwnd_, GWLP_USERDATA, 0);
    DestroyWindow(this->hwnd_);
    this->hwnd_ = NULL;
    this->menuAttachment_.resetForTeardown();
    if (appToClear)
    {
      appToClear->setActiveWindow(0);
    }
  }
}

void Win32Window::onCreate()
{
  // Native creation is entered from App admission; it reads the
  // application's intent and never writes it back (no forced re-notify).
  Window::onCreate();
}

void Win32Window::onShow()
{
  if (this->hwnd_)
    ShowWindow(this->hwnd_, SW_SHOW);
}

void Win32Window::onHide()
{
  // Native hide is handled by destroyNativeWindow().
}

void Win32Window::synchronizeScenePlatform()
{
  if (scenePlatformController_)
  {
    scenePlatformController_->synchronize();
  }
}

void Win32Window::drainNativeRetirements()
{
  if (this->scenePlatformController_)
  {
    this->scenePlatformController_->drainNativeRetirements();
  }
}

bool Win32Window::hasPendingScenePlatformSync() const
{
  return scenePlatformController_ ? scenePlatformController_->hasPendingSync() : false;
}

bool Win32Window::queryDisplayScalePercent(int &out) const
{
  if (!hwnd_)
  {
    return false;
  }
  loka::win32::Win32DisplayScale scale;
  if (!loka::win32::Win32DisplayScale::queryForWindow(this->hwnd_, scale))
  {
    return false;
  }
  out = scale.percent();
  return true;
}

bool Win32Window::queryDisplayDepth(int &out) const
{
  if (!hwnd_)
  {
    return false;
  }
  HDC dc = GetDC(hwnd_);
  if (!dc)
  {
    return false;
  }
  const int bitsPerPixel = GetDeviceCaps(dc, BITSPIXEL) * GetDeviceCaps(dc, PLANES);
  ReleaseDC(hwnd_, dc);
  if (bitsPerPixel <= 0)
  {
    return false;
  }
  out = bitsPerPixel;
  return true;
}

bool Win32Window::queryDisplayAppearance(DisplayAppearance &out) const
{
  // AppsUseLightTheme arrived with Windows 10. On an older system the value is
  // absent, and absent is the honest answer: those systems have no light/dark
  // distinction to report, so declining is not a failure path.
  HKEY key = 0;
  if (RegOpenKeyExW(HKEY_CURRENT_USER,
                    L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                    0,
                    KEY_QUERY_VALUE,
                    &key)
      != ERROR_SUCCESS)
  {
    return false;
  }
  DWORD value = 0;
  DWORD size = sizeof(value);
  DWORD type = 0;
  const LONG status = RegQueryValueExW(key, L"AppsUseLightTheme", 0, &type, reinterpret_cast<LPBYTE>(&value), &size);
  RegCloseKey(key);
  if (status != ERROR_SUCCESS || type != REG_DWORD)
  {
    return false;
  }
  out = value == 0 ? DISPLAY_APPEARANCE_DARK : DISPLAY_APPEARANCE_LIGHT;
  return true;
}

void Win32Window::mountScene()
{
  if (this->scene() && !this->scenePlatformController_)
    this->mountReplacementScene(this->scene());
}

bool Win32Window::mountReplacementScene(loka::app::scene::Scene *next)
{
  if (!this->hwnd_)
    return true;
  if (!this->scenePlatformController_)
    this->scenePlatformController_ = new Win32ScenePlatformController(
        this->hwnd_, WindowProjection(this->hwnd_), this->context());
  if (!this->scenePlatformController_)
    return false;
  return next->mount(this->scenePlatformController_);
}

void Win32Window::teardownScene()
{
  loka::app::scene::Scene *currentScene = this->scene();
  if (currentScene)
  {
    this->unmountSceneForTeardown(*currentScene);
  }
  if (scenePlatformController_)
  {
    delete scenePlatformController_;
    scenePlatformController_ = 0;
  }
}

bool Win32Window::handleCommand(WPARAM wParam, LPARAM lParam)
{
  if (lParam == 0)
  {
    int commandId = LOWORD(wParam);
    if (this->menuAttachment_.dispatch(commandId))
    {
      return true;
    }
  }
  if (!scenePlatformController_)
  {
    return false;
  }
  return scenePlatformController_->handleCommand(wParam, lParam);
}

void Win32Window::handleNativeSize(WPARAM wParam, LPARAM lParam)
{
#ifdef LOKA_LIFECYCLE_AUDIT
  assert(this->scenePlatformController_ && this->scenePlatformController_->borrowPhase().open());
#endif
  if (this->scenePlatformController_)
  {
    int width = LOWORD(lParam);
    int height = HIWORD(lParam);
    this->scenePlatformController_->relayoutNativeClientPixels(width, height);
  }
  if (wParam != SIZE_MINIMIZED && this->hwnd_)
  {
    this->storeCurrentNativeContentFrame();
  }
}

void Win32Window::handleNativeDpi(WPARAM wParam, LPARAM lParam)
{
#ifdef LOKA_LIFECYCLE_AUDIT
  assert(!this->scenePlatformController_ || this->scenePlatformController_->borrowPhase().open());
#endif
  const loka::win32::Win32DisplayScale nextScale(LOWORD(wParam));
  if (this->scenePlatformController_)
  {
    this->scenePlatformController_->updateDisplayScale(nextScale);
  }
  const RECT *suggested = reinterpret_cast<const RECT *>(lParam);
  if (suggested)
  {
    // Windows sizes the suggestion for the destination DPI but does not
    // keep it inside that monitor's work area; a frame fitted against
    // the source monitor's edge would otherwise land partly off-screen.
    RECT outer = *suggested;
    if (ClampOuterRectToWorkArea(outer))
    {
      PositionNativeWindow(this->hwnd_, nextScale.fromDevicePixels(outer));
    }
  }
  if (this->scenePlatformController_)
  {
    // SetWindowPos commonly reaches WM_SIZE and lays out once already, but
    // a size-preserving suggested rectangle need not. The projection pass
    // is idempotent, so finish from the current client pixels either way.
    RECT client;
    if (GetClientRect(this->hwnd_, &client))
    {
      this->scenePlatformController_->relayoutNativeClientPixels(
          client.right - client.left, client.bottom - client.top);
    }
  }
  this->storeCurrentNativeContentFrame();
}
