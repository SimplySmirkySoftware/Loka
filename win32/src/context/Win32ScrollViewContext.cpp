#include "../Win32PaintGround.hpp"
#include "Win32InputDoor.hpp"
#include "Win32ScrollViewContext.hpp"

#include <cassert>
#include <cwchar>
#include <windowsx.h>

#include "../Win32ScenePlatformController.hpp"

namespace
{
  const wchar_t kScrollFrameClassName[] = L"LOKA_SCROLL_FRAME";
  const wchar_t kScrollViewClassName[] = L"LOKA_SCROLL_VIEW";
  // One line is 16 logical pixels, following the WPF/WinUI convention:
  // the default three system wheel lines move 48 pixels per notch. This is
  // a Win32 rail default, like AppKit's lineScroll, not a logical Props door;
  // a later per-window platform parameter (#720) may override it.
  const int kLineStepPixels = 16;
  // Vista identifiers are not exposed by every XP-target SDK.
  static const UINT kMouseHorizontalWheelMessage = 0x020E;
#ifdef SPI_GETWHEELSCROLLCHARS
  static const UINT kSpiGetWheelScrollChars = SPI_GETWHEELSCROLLCHARS;
#else
  static const UINT kSpiGetWheelScrollChars = 0x006C;
#endif
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
      viewport_(0),
      bar_(0),
      committedInsets_()
{
  EnsureClassRegistered();
  // Both levels participate in recursive dialog navigation. Only the frame
  // carries a context pointer; abort and terminal retirement revoke it once.
  // The children are created empty; relayout owns their geometry.
  const loka::win32::Win32DisplayScale &scale = this->controller()->displayScale();
  const RECT empty = {0, 0, 0, 0};
  this->createNativeChildWindow(WS_EX_CONTROLPARENT,
                                kScrollFrameClassName,
                                L"",
                                WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
                                scale.projectFrame(loka::core::Frame(x, y, width, height)),
                                parent,
                                0,
                                GetModuleHandleW(NULL),
                                this);
  if (!this->hwnd())
    return;
  this->viewport_ = this->createSubordinateChildWindow(WS_EX_CONTROLPARENT,
                                                       kScrollViewClassName,
                                                       L"",
                                                       WS_CHILD | WS_VISIBLE,
                                                       scale.fromDevicePixels(empty),
                                                       this->hwnd(),
                                                       0,
                                                       GetModuleHandleW(NULL),
                                                       0);
  if (this->viewportHwnd())
    this->bar_ = this->createSubordinateChildWindow(0,
                                                    L"SCROLLBAR",
                                                    L"",
                                                    WS_CHILD | SBS_VERT,
                                                    scale.fromDevicePixels(empty),
                                                    this->hwnd(),
                                                    0,
                                                    GetModuleHandleW(NULL),
                                                    0);
  if (!this->viewportHwnd() || !this->barHwnd())
  {
    this->retireWindow();
    return;
  }
  this->relayout(x, y, width, height);
}

Win32ScrollViewContext::~Win32ScrollViewContext()
{
  assert(!this->hwnd() && "terminal fact delivery must queue the ScrollView HWND before context reclaim");
}

/** The frame and viewport have no paint-only State input: offset is LAYOUT, delivered
    by the native layout pass. During a settled paint-only visit it owes no
    client damage; descendant HWND obligations are enumerated separately. */
loka::app::scene::PaintAnswer Win32ScrollViewContext::queryPaintDamage(const loka::app::scene::PaintQuery &query) const
{
  using namespace loka::app::scene;
  if (!this->hwnd())
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

void Win32ScrollViewContext::onNativeFact(loka::app::scene::NodeLifecycleFact previous,
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
      this->node_ = 0;
    }
  }
}

bool Win32ScrollViewContext::isValid() const
{
  return this->hwnd() != 0;
}

void Win32ScrollViewContext::applyAttachedPresentation()
{
  if (this->hwnd())
  {
    ShowWindow(this->hwnd(), SW_SHOW);
  }
}

void Win32ScrollViewContext::applyDetachedPresentation()
{
  if (this->hwnd())
  {
    ShowWindow(this->hwnd(), SW_HIDE);
  }
}

void Win32ScrollViewContext::relayout(int x, int y, int width, int height)
{
  if (!this->hwnd())
    return;
  const loka::core::Frame seat(x, y, width, height);
  const loka::win32::Win32DisplayScale &scale = this->controller()->displayScale();
  const loka::win32::NativeRect native = scale.projectFrame(seat);
  this->positionNativeWindow(this->hwnd(), native);
  const int clientWidth = native.r.right - native.r.left - scale.systemMetric(SM_CXVSCROLL);
  const RECT viewport = {0, 0, clientWidth > 0 ? clientWidth : 0, native.r.bottom - native.r.top};
  this->positionNativeWindow(this->viewportHwnd(), scale.fromDevicePixels(viewport));
  this->placeVerticalBar(seat);
}

void Win32ScrollViewContext::commitVerticalBarInsets(const loka::app::ScrollViewContentInsets &insets,
                                                     const loka::core::Frame &seatLu)
{
  this->committedInsets_ = insets;
  this->placeVerticalBar(seatLu);
}

void Win32ScrollViewContext::placeVerticalBar(const loka::core::Frame &seatLu)
{
  if (!this->barHwnd())
    return;
  const loka::win32::Win32DisplayScale &scale = this->controller()->displayScale();
  const RECT seat = scale.projectFrame(seatLu).r;
  const int width = seat.right - seat.left;
  const int height = seat.bottom - seat.top;
  const int cx = scale.systemMetric(SM_CXVSCROLL);
  const int column = width > cx ? width - cx : 0;
  const bool horizontal = (GetWindowLongPtrW(this->viewportHwnd(), GWL_STYLE) & WS_HSCROLL) != 0;
  const int availableBottom = horizontal ? height - scale.systemMetric(SM_CYHSCROLL) : height;
  const RECT topBand =
      scale.projectFrame(loka::core::Frame(seatLu.x, seatLu.y, seatLu.width, this->committedInsets_.top())).r;
  const RECT bottomBand =
      scale
          .projectFrame(loka::core::Frame(seatLu.x,
                                          seatLu.y + seatLu.height - this->committedInsets_.bottom(),
                                          seatLu.width,
                                          this->committedInsets_.bottom()))
          .r;
  const int top = topBand.bottom - topBand.top;
  const int bottomEdge = height - (bottomBand.bottom - bottomBand.top);
  const int bottom = bottomEdge < availableBottom ? bottomEdge : availableBottom;
  if (bottom <= top || column >= width)
    ShowWindow(this->barHwnd(), SW_HIDE);
  else
  {
    const RECT bar = {column, top, width, bottom};
    this->positionNativeWindow(this->barHwnd(), scale.fromDevicePixels(bar));
    ShowWindow(this->barHwnd(), SW_SHOWNA);
  }
  const RECT damage = {column, 0, width, height};
  this->controller()->requestDirtySubtree(this->hwnd(), &damage, TRUE);
}

HWND Win32ScrollViewContext::nativeBar(int &axis) const
{
  if (axis == SB_VERT)
  {
    axis = SB_CTL;
    return this->barHwnd();
  }
  return this->viewportHwnd();
}

int Win32ScrollViewContext::setScrollMetrics(int bar, int content, int viewport, int offset)
{
  if (content < 0)
  {
    content = 0;
  }
  if (viewport < 0)
  {
    viewport = 0;
  }
  int maximum = content - viewport;
  if (maximum < 0)
  {
    maximum = 0;
  }
  const int clamped = this->clampOffset(offset, maximum);
  if (!this->hwnd())
  {
    return clamped;
  }

  SCROLLINFO info;
  ZeroMemory(&info, sizeof(info));
  info.cbSize = sizeof(info);
  // SIF_DISABLENOSCROLL keeps the vertical scrollbar present (disabled) when
  // content fits, so the viewport client width never changes between layout
  // passes and the width read before measurement stays true after it. The
  // horizontal bar omits it: SetScrollInfo then shows the bar only while the
  // range exists, so every viewport whose content fits keeps today's pixels.
  info.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
  if (bar == SB_VERT)
    info.fMask |= SIF_DISABLENOSCROLL;
  info.nMin = 0;
  info.nMax = content > 0 ? content - 1 : 0;
  info.nPage = viewport > 0 ? static_cast<UINT>(viewport) : 0;
  info.nPos = this->controller()->displayScale().scrollOffsetToNative(clamped);
  HWND target = this->nativeBar(bar);
  SetScrollInfo(target, bar, &info, TRUE);
  return clamped;
}

/** Immutable selection of read-only or moving axes for one settled publication. */
class Win32ScrollViewContext::SettledPosition
{
public:
  static SettledPosition settled(int bar)
  {
    return SettledPosition(bar == SB_HORZ, bar == SB_VERT, false, 0, 0);
  }
  static SettledPosition moved(int bar, int value)
  {
    return SettledPosition(bar == SB_HORZ, bar == SB_VERT, true, value, value);
  }
  static SettledPosition requested(const loka::core::Frame &target, const loka::core::Frame &current)
  {
    return SettledPosition(target.x != current.x, target.y != current.y, true, target.x, target.y);
  }
  bool chooses(int bar) const
  {
    return bar == SB_HORZ ? this->x_ : this->y_;
  }
  bool sets(int bar) const
  {
    return this->move_ && this->chooses(bar);
  }
  int value(int bar) const
  {
    return bar == SB_HORZ ? this->target_.x : this->target_.y;
  }

private:
  SettledPosition(bool x, bool y, bool move, int targetX, int targetY)
      : x_(x),
        y_(y),
        move_(move),
        target_(targetX, targetY, 0, 0)
  {
  }
  const bool x_, y_, move_;
  const loka::core::Frame target_;
};

bool Win32ScrollViewContext::handleScroll(int bar, int command, int thumbPosition)
{
#ifdef LOKA_LIFECYCLE_AUDIT
  assert(this->controller()->borrowPhase().open());
#endif
  SCROLLINFO info;
  if (!this->readScrollInfo(bar, info))
  {
    return false;
  }

  const int maximum = this->maximumOffset(info);
  int next = this->node_ && this->node_->props.viewport_.isValid()
                 ? (bar == SB_HORZ ? this->node_->requestedOrigin().x : this->node_->requestedOrigin().y)
                 : this->controller()->displayScale().scrollPositionToLu(info.nPos);
  next = this->clampOffset(next, maximum);
  int page = info.nPage > static_cast<UINT>(maximum)
                 ? maximum
                 : static_cast<int>(info.nPage);
  switch (command)
  {
  case SB_LINEUP:
    next = next < kLineStepPixels ? 0 : next - kLineStepPixels;
    break;
  case SB_LINEDOWN:
    next = maximum - next < kLineStepPixels ? maximum : next + kLineStepPixels;
    break;
  case SB_PAGEUP:
    next = next < page ? 0 : next - page;
    break;
  case SB_PAGEDOWN:
    next = next > maximum - page ? maximum : next + page;
    break;
  case SB_THUMBTRACK:
    this->setVisualPosition(
        bar, this->clampOffset(this->controller()->displayScale().scrollPositionToLu(thumbPosition), maximum));
    return true;
  case SB_THUMBPOSITION:
    next = this->clampOffset(this->controller()->displayScale().scrollPositionToLu(thumbPosition), maximum);
    break;
  case SB_ENDSCROLL:
    return this->commitSettledPosition(SettledPosition::settled(bar));
  default:
    return false;
  }
  // Every settled step moves the native position with the fact: a click's
  // SB_ENDSCROLL arrives before layout and republishes that position.
  return this->commitSettledPosition(SettledPosition::moved(bar, next));
}

bool Win32ScrollViewContext::readScrollInfo(int bar, SCROLLINFO &info) const
{
  if (!this->hwnd())
  {
    return false;
  }
  ZeroMemory(&info, sizeof(info));
  info.cbSize = sizeof(info);
  info.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
  HWND target = this->nativeBar(bar);
#ifdef TEST_BUILD
  if (loka::win32::testing::refuseScrollInfoRead())
    return false;
#endif
  return GetScrollInfo(target, bar, &info) != FALSE;
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

void Win32ScrollViewContext::setVisualPosition(int bar, int value)
{
  if (!this->hwnd())
  {
    return;
  }
  SCROLLINFO info;
  ZeroMemory(&info, sizeof(info));
  info.cbSize = sizeof(info);
  info.fMask = SIF_POS;
  info.nPos = this->controller()->displayScale().scrollOffsetToNative(value);
  HWND target = this->nativeBar(bar);
  SetScrollInfo(target, bar, &info, TRUE);
}

bool Win32ScrollViewContext::commitSettledPosition(const SettledPosition &selection)
{
  const int bars[] = {SB_HORZ, SB_VERT};
  SCROLLINFO info[2];
  for (int i = 0; i != 2; ++i)
    if (selection.chooses(bars[i]) && !this->readScrollInfo(bars[i], info[i]))
      return false;
  for (int i = 0; i != 2; ++i)
    if (selection.sets(bars[i]))
      this->setVisualPosition(bars[i], selection.value(bars[i]));
  for (int i = 0; i != 2; ++i)
    if (selection.sets(bars[i]) && !this->readScrollInfo(bars[i], info[i]))
      return false;
  if (this->node_ && (selection.chooses(SB_HORZ) || selection.chooses(SB_VERT)))
  {
    const loka::core::Frame current = this->node_->requestedOrigin();
    const loka::win32::Win32DisplayScale &scale = this->controller()->displayScale();
    this->node_->publishOrigin(selection.chooses(SB_HORZ) ? scale.scrollPositionToLu(info[0].nPos) : current.x,
                               selection.chooses(SB_VERT) ? scale.scrollPositionToLu(info[1].nPos) : current.y);
  }
  return true;
}

bool Win32ScrollViewContext::applyScrollRequest(const loka::core::Frame &target)
{
  const loka::core::Frame current = this->node_ ? this->node_->requestedOrigin() : loka::core::Frame();
  return this->commitSettledPosition(SettledPosition::requested(target, current));
}

bool Win32ScrollViewContext::forwardMouseWheel(HWND root, UINT msg, WPARAM wParam, LPARAM lParam)
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
    if (GetClassNameW(target, className, sizeof(className) / sizeof(className[0]))
        && std::wcscmp(className, kScrollFrameClassName) == 0 && GetWindowLongPtrW(target, GWLP_USERDATA))
    {
      // One forwarding site: Win32Window's context routing calls here for
      // root-directed wheels (including default propagation from controls
      // outside a viewport). Children inside a viewport propagate directly
      // to its WndProc; no per-control wheel handlers are needed.
      SendMessageW(target, msg, wParam, lParam);
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
  wc.lpfnWndProc = Win32ScrollViewContext::FrameWndProc;
  wc.lpszClassName = kScrollFrameClassName;
  RegisterClassW(&wc);
  registered = true;
}

Win32ScrollViewContext *Win32ScrollViewContext::FromFrame(HWND hwnd)
{
  return reinterpret_cast<Win32ScrollViewContext *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

Win32ScrollViewContext *Win32ScrollViewContext::FromViewport(HWND hwnd)
{
  return FromFrame(GetParent(hwnd));
}

LRESULT CALLBACK Win32ScrollViewContext::FrameWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  Win32ScrollViewContext *self = FromFrame(hwnd);
  if (msg == WM_NCCREATE)
  {
    CREATESTRUCTW *create = reinterpret_cast<CREATESTRUCTW *>(lParam);
    self = static_cast<Win32ScrollViewContext *>(create->lpCreateParams);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
  }
  if (msg == WM_ERASEBKGND)
    return 1;
  if (msg == WM_PAINT)
  {
    PAINTSTRUCT paint;
    HDC dc = BeginPaint(hwnd, &paint);
    Win32ScenePlatformController::paintWindowGround(dc, paint.rcPaint);
    if (self && self->viewportHwnd() && (GetWindowLongPtrW(self->viewportHwnd(), GWL_STYLE) & WS_HSCROLL))
    {
      RECT corner;
      GetClientRect(hwnd, &corner);
      const loka::win32::Win32DisplayScale &scale = self->controller()->displayScale();
      const int cx = scale.systemMetric(SM_CXVSCROLL);
      const int cy = scale.systemMetric(SM_CYHSCROLL);
      corner.left = corner.right > cx ? corner.right - cx : 0;
      corner.top = corner.bottom > cy ? corner.bottom - cy : 0;
      FillRect(dc, &corner, GetSysColorBrush(COLOR_BTNFACE));
    }
    EndPaint(hwnd, &paint);
    return 0;
  }
  if (msg == WM_VSCROLL && self && reinterpret_cast<HWND>(lParam) == self->barHwnd() && lParam
      && Win32InputDoor::scroll(*self, SB_VERT, LOWORD(wParam), HIWORD(wParam)))
    return 0;
  if ((msg == WM_MOUSEWHEEL || msg == kMouseHorizontalWheelMessage) && self)
  {
    Win32InputDoor::mouseWheel(*self, msg == kMouseHorizontalWheelMessage ? SB_HORZ : SB_VERT, wParam);
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// Deliberate twin of Win32ZStackContext::Unit::WndProc, with viewport input.
LRESULT CALLBACK Win32ScrollViewContext::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  Win32ScrollViewContext *self = FromViewport(hwnd);

  if (msg == WM_CTLCOLORSTATIC)
    return reinterpret_cast<LRESULT>(
        loka::win32::Win32StaticTextColors(reinterpret_cast<HDC>(wParam), reinterpret_cast<HWND>(lParam)));

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

  if (msg == WM_HSCROLL && lParam == 0 && self
      && Win32InputDoor::scroll(*self, SB_HORZ, LOWORD(wParam), HIWORD(wParam)))
  {
    return 0;
  }
  if ((msg == WM_MOUSEWHEEL || msg == kMouseHorizontalWheelMessage) && self)
  {
    Win32InputDoor::mouseWheel(*self, msg == kMouseHorizontalWheelMessage ? SB_HORZ : SB_VERT, wParam);
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

void Win32ScrollViewContext::handleMouseWheel(int bar, WPARAM wParam)
{
#ifdef LOKA_LIFECYCLE_AUDIT
  assert(this->controller()->borrowPhase().open());
#endif
  UINT lines = 3;
  if (!SystemParametersInfoW(bar == SB_HORZ ? kSpiGetWheelScrollChars : SPI_GETWHEELSCROLLLINES, 0, &lines, 0))
  {
    lines = 3;
  }
  // Whole detents only: this context has no gesture lifetime in which to
  // own a sub-detent remainder. Keep all offset writes in the line/page path.
  const int detents = GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA;
  const bool page = lines == WHEEL_PAGESCROLL;
  // Positive horizontal delta scrolls RIGHT; positive vertical delta scrolls UP.
  // The horizontal command aliases have the same values as the vertical ones.
  const bool backwards = bar == SB_HORZ ? detents < 0 : detents > 0;
  const int command = page ? (backwards ? SB_PAGEUP : SB_PAGEDOWN) : (backwards ? SB_LINEUP : SB_LINEDOWN);
  const UINT steps = page ? 1 : lines;
  const int count = detents < 0 ? -detents : detents;
  for (int detent = 0; detent < count; ++detent)
  {
    for (UINT step = 0; step < steps; ++step)
    {
      this->handleScroll(bar, command, 0);
    }
  }
}
