#include "../Win32PaintGround.hpp"
#include "Win32InputDoor.hpp"
#include "Win32ScrollViewContext.hpp"

#include <cassert>
#include <cwchar>
#include <windowsx.h>

#include "../Win32ScenePlatformController.hpp"

namespace
{
  const wchar_t kScrollViewClassName[] = L"LOKA_SCROLL_VIEW";
  // One line is 16 logical pixels, following the WPF/WinUI convention:
  // the default three system wheel lines move 48 pixels per notch. This is
  // a Win32 rail default, like AppKit's lineScroll, not a logical Props door;
  // a later per-window platform parameter (#720) may override it.
  const int kLineStepPixels = 16;
}

Win32ScrollViewContext::Win32ScrollViewContext(Win32ScenePlatformController *controller,
                                               HWND parent,
                                               int x,
                                               int y,
                                               int width,
                                               int height,
                                               loka::app::ScrollViewNode *node)
    : Win32RetirableContext(controller),
      node_(node),
      hwnd_(0)
{
  EnsureClassRegistered();
  // WS_EX_CONTROLPARENT: Win32App routes Tab through IsDialogMessage on the
  // root, and the dialog manager only recurses into children of windows
  // carrying this style (the root window sets it too, Win32Window.cpp) —
  // without it every control inside a ScrollView is skipped by keyboard
  // navigation.
  // #1199: like the root, paint ground beneath transparent children. Native
  // parentage still clips those children to this viewport's client area (#544).
  this->hwnd_ = this->createNativeChildWindow(
      WS_EX_CONTROLPARENT,
      kScrollViewClassName,
      L"",
      WS_CHILD | WS_VISIBLE | WS_VSCROLL,
      this->controller()->displayScale().projectFrame(loka::core::Frame(x, y, width, height)),
      parent,
      0,
      GetModuleHandleW(NULL),
      this);
}

Win32ScrollViewContext::~Win32ScrollViewContext()
{
  assert(!this->hwnd_ && "terminal fact delivery must queue the ScrollView HWND before context reclaim");
}

/** The viewport has no paint-only State input: offset is LAYOUT, delivered
    by the native layout pass. During a settled paint-only visit it owes no
    client damage; descendant HWND obligations are enumerated separately. */
loka::app::scene::PaintAnswer Win32ScrollViewContext::queryPaintDamage(const loka::app::scene::PaintQuery &query) const
{
  using namespace loka::app::scene;
  if (!this->hwnd_)
    return PaintAnswer::refused(PAINT_REFUSED_NO_CONTEXT);
  if (query.placement != PLACEMENT_ELIGIBLE)
    return PaintAnswer::refused(PAINT_REFUSED_PLACEMENT_UNSETTLED);
  const PaintDamage empty = {query.scope, 0, 0, 0, 0, PAINT_COVERAGE_PAINT_ONLY};
  return PaintAnswer::exact(empty);
}

void Win32ScrollViewContext::readLifecycleFactOnAttach()
{
  if (this->node_ && this->node_->lifecycleFact() == loka::app::scene::NODE_FACT_ATTACHED)
  {
    this->applyAttachedPresentation();
  }
}

void Win32ScrollViewContext::onFactChanged(loka::app::scene::NodeLifecycleFact previous,
                                           loka::app::scene::NodeLifecycleFact next)
{
  (void)previous;
  if (next == loka::app::scene::NODE_FACT_ATTACHED)
  {
    this->applyAttachedPresentation();
  }
  else
  {
    this->applyDetachedPresentation();
    if (next == loka::app::scene::NODE_FACT_RETIRED)
    {
      this->retireWindow(this->hwnd_);
      this->node_ = 0;
    }
  }
}

bool Win32ScrollViewContext::isValid() const
{
  return this->hwnd_ != 0;
}

HWND Win32ScrollViewContext::hwnd() const
{
  return this->hwnd_;
}

void Win32ScrollViewContext::applyAttachedPresentation()
{
  if (this->hwnd_)
  {
    ShowWindow(this->hwnd_, SW_SHOW);
  }
}

void Win32ScrollViewContext::applyDetachedPresentation()
{
  if (this->hwnd_)
  {
    ShowWindow(this->hwnd_, SW_HIDE);
  }
}

void Win32ScrollViewContext::relayout(int x, int y, int width, int height)
{
  if (this->hwnd_)
  {
    this->positionNativeWindow(this->hwnd_,
                               this->controller()->displayScale().projectFrame(loka::core::Frame(x, y, width, height)));
  }
}

int Win32ScrollViewContext::setScrollMetrics(int contentHeight,
                                             int viewportHeight,
                                             int offset)
{
  if (contentHeight < 0)
  {
    contentHeight = 0;
  }
  if (viewportHeight < 0)
  {
    viewportHeight = 0;
  }
  int maximum = contentHeight - viewportHeight;
  if (maximum < 0)
  {
    maximum = 0;
  }
  const int clamped = this->clampOffset(offset, maximum);
  if (!this->hwnd_)
  {
    return clamped;
  }

  SCROLLINFO info;
  ZeroMemory(&info, sizeof(info));
  info.cbSize = sizeof(info);
  // SIF_DISABLENOSCROLL keeps the scrollbar present (disabled) when content
  // fits, so the viewport client width never changes between layout passes
  // and the width read before measurement stays true after it.
  info.fMask = SIF_RANGE | SIF_PAGE | SIF_POS | SIF_DISABLENOSCROLL;
  info.nMin = 0;
  info.nMax = contentHeight > 0 ? contentHeight - 1 : 0;
  info.nPage = viewportHeight > 0 ? static_cast<UINT>(viewportHeight) : 0;
  info.nPos = this->controller()->displayScale().scrollOffsetToNative(clamped);
  SetScrollInfo(this->hwnd_, SB_VERT, &info, TRUE);
  return clamped;
}

bool Win32ScrollViewContext::handleVerticalScroll(int command,
                                                  int thumbPosition)
{
#ifdef LOKA_LIFECYCLE_AUDIT
  assert(this->controller()->borrowPhase().open());
#endif
  SCROLLINFO info;
  if (!this->readScrollInfo(info))
  {
    return false;
  }

  const int maximum = this->maximumOffset(info);
  int next = this->node_ && this->node_->props.offset_.isValid()
                 ? this->node_->props.offset_.state()->get()
                 : this->controller()->displayScale().scrollPositionToLu(info.nPos);
  next = this->clampOffset(next, maximum);
  int page = info.nPage > static_cast<UINT>(maximum)
                 ? maximum
                 : static_cast<int>(info.nPage);
  switch (command)
  {
  case SB_LINEUP:
    next = next < kLineStepPixels ? 0 : next - kLineStepPixels;
    this->publishOffset(next);
    return true;
  case SB_LINEDOWN:
    next = maximum - next < kLineStepPixels ? maximum : next + kLineStepPixels;
    this->publishOffset(next);
    return true;
  case SB_PAGEUP:
    next = next < page ? 0 : next - page;
    this->publishOffset(next);
    return true;
  case SB_PAGEDOWN:
    next = next > maximum - page ? maximum : next + page;
    this->publishOffset(next);
    return true;
  case SB_THUMBTRACK:
    this->setVisualPosition(
        this->clampOffset(this->controller()->displayScale().scrollPositionToLu(thumbPosition), maximum));
    return true;
  case SB_THUMBPOSITION:
    next = this->clampOffset(this->controller()->displayScale().scrollPositionToLu(thumbPosition), maximum);
    this->setVisualPosition(next);
    this->publishOffset(next);
    return true;
  case SB_ENDSCROLL:
    this->publishOffset(this->clampOffset(this->controller()->displayScale().scrollPositionToLu(info.nPos), maximum));
    return true;
  default:
    return false;
  }
}

bool Win32ScrollViewContext::readScrollInfo(SCROLLINFO &info) const
{
  if (!this->hwnd_)
  {
    return false;
  }
  ZeroMemory(&info, sizeof(info));
  info.cbSize = sizeof(info);
  info.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
  return GetScrollInfo(this->hwnd_, SB_VERT, &info) != FALSE;
}

int Win32ScrollViewContext::maximumOffset(const SCROLLINFO &info) const
{
  int maximum = info.nMax;
  if (info.nPage > 0)
  {
    maximum -= static_cast<int>(info.nPage - 1);
  }
  if (maximum < info.nMin)
  {
    maximum = info.nMin;
  }
  return maximum;
}

int Win32ScrollViewContext::clampOffset(int value, int maximum) const
{
  if (value < 0)
  {
    return 0;
  }
  if (value > maximum)
  {
    return maximum;
  }
  return value;
}

void Win32ScrollViewContext::setVisualPosition(int value)
{
  if (!this->hwnd_)
  {
    return;
  }
  SCROLLINFO info;
  ZeroMemory(&info, sizeof(info));
  info.cbSize = sizeof(info);
  info.fMask = SIF_POS;
  info.nPos = this->controller()->displayScale().scrollOffsetToNative(value);
  SetScrollInfo(this->hwnd_, SB_VERT, &info, TRUE);
}

void Win32ScrollViewContext::publishOffset(int value)
{
  if (!this->node_ || !this->node_->props.offset_.isValid() ||
      this->node_->props.offset_.state()->get() == value)
  {
    return;
  }
  this->node_->props.offset_.set(value);
}

bool Win32ScrollViewContext::forwardMouseWheel(HWND root, WPARAM wParam, LPARAM lParam)
{
  const POINT point = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
  HWND target = WindowFromPoint(point);
  if (!target || GetAncestor(target, GA_ROOT) != root)
  {
    return false;
  }
  for (; target && target != root; target = GetParent(target))
  {
    wchar_t className[256];
    if (GetClassNameW(target, className, sizeof(className) / sizeof(className[0])) &&
        std::wcscmp(className, kScrollViewClassName) == 0 &&
        GetWindowLongPtr(target, GWLP_USERDATA))
    {
      // One forwarding site: Win32Window's context routing calls here for
      // root-directed wheels (including default propagation from controls
      // outside a viewport). Children inside a viewport propagate directly
      // to its WndProc; no per-control wheel handlers are needed.
      SendMessageW(target, WM_MOUSEWHEEL, wParam, lParam);
      return true;
    }
  }
  return false;
}

void Win32ScrollViewContext::EnsureClassRegistered()
{
  static bool registered = false;
  if (registered)
  {
    return;
  }
  WNDCLASSW wc;
  ZeroMemory(&wc, sizeof(wc));
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = Win32ScrollViewContext::WndProc;
  wc.hInstance = GetModuleHandleW(NULL);
  wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
  wc.hbrBackground = NULL;
  wc.lpszClassName = kScrollViewClassName;
  RegisterClassW(&wc);
  registered = true;
}

LRESULT CALLBACK Win32ScrollViewContext::WndProc(HWND hwnd,
                                                 UINT msg,
                                                 WPARAM wParam,
                                                 LPARAM lParam)
{
  Win32ScrollViewContext *self = static_cast<Win32ScrollViewContext *>(
      reinterpret_cast<void *>(GetWindowLongPtr(hwnd, GWLP_USERDATA)));
  if (msg == WM_NCCREATE)
  {
    CREATESTRUCTW *create = reinterpret_cast<CREATESTRUCTW *>(lParam);
    self = static_cast<Win32ScrollViewContext *>(create->lpCreateParams);
    SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
  }

  if (msg == WM_CTLCOLORSTATIC)
    return reinterpret_cast<LRESULT>(
        loka::win32::Win32StaticTextColors(reinterpret_cast<HDC>(wParam)));

  // Match Win32Window: paint-only subtree requests must restore the ground
  // even without RDW_ERASE, and erasure must not be a separate clearing pass.
  if (msg == WM_ERASEBKGND)
    return 1;
  if (msg == WM_PAINT)
  {
    PAINTSTRUCT paint;
    HDC dc = BeginPaint(hwnd, &paint);
    Win32ScenePlatformController::paintWindowGround(dc, paint.rcPaint);
    EndPaint(hwnd, &paint);
    return 0;
  }

  if (msg == WM_VSCROLL && self &&
      Win32InputDoor::verticalScroll(*self, LOWORD(wParam), HIWORD(wParam)))
  {
    return 0;
  }
  if (msg == WM_MOUSEWHEEL && self)
  {
    Win32InputDoor::mouseWheel(*self, wParam);
    return 0;
  }
  if (msg == WM_COMMAND && self && self->controller() &&
      self->controller()->handleCommand(wParam, lParam))
  {
    // Controls parented to the viewport notify it, not the root window;
    // forward through the same controller door Win32Window::WndProc uses so
    // a Button inside a ScrollView still reaches its Loka handler.
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void Win32ScrollViewContext::handleMouseWheel(WPARAM wParam)
{
#ifdef LOKA_LIFECYCLE_AUDIT
  assert(this->controller()->borrowPhase().open());
#endif
  UINT lines = 3;
  if (!SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0))
  {
    lines = 3;
  }
  // Whole detents only: this context has no gesture lifetime in which to
  // own a sub-detent remainder. Keep all offset writes in the line/page path.
  const int detents = GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA;
  const bool page = lines == WHEEL_PAGESCROLL;
  const int command = page ? (detents > 0 ? SB_PAGEUP : SB_PAGEDOWN)
                           : (detents > 0 ? SB_LINEUP : SB_LINEDOWN);
  const UINT steps = page ? 1 : lines;
  const int count = detents < 0 ? -detents : detents;
  for (int detent = 0; detent < count; ++detent)
  {
    for (UINT step = 0; step < steps; ++step)
    {
      this->handleVerticalScroll(command, 0);
    }
  }
}
