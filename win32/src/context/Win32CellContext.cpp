#include "../Win32PaintGround.hpp"
#include "Win32InputDoor.hpp"
#include "Win32CellContext.hpp"
#include <cassert>
#include "../Win32ScenePlatformController.hpp"
#include "app/layout/FallbackControlMetrics.hpp"
#include "app/scene/projection/RetainedNodeHandler.hpp"
#include "app/nodes/controls/Cell.hpp"
#include "core/State.hpp"
#include "platform/Win32String.hpp"

namespace
{
  const wchar_t kCellClassName[] = L"LOKA_CELL";

  class Win32CellNodeHandler
      : public loka::app::scene::RetainedNodeHandler<Win32CellNodeHandler,
                                                     loka::app::CellNode,
                                                     Win32CellContext>
  {
  public:
    static loka::app::CellNode *cast(loka::app::scene::Node *node)
    {
      return node ? node->asCellNode() : 0;
    }

    static Win32CellContext *create(loka::app::CellNode *cell,
                                    loka::app::scene::IPlatformController *controller,
                                    const loka::app::scene::LayoutState &state)
    {
      Win32ScenePlatformController *win32 = static_cast<Win32ScenePlatformController *>(controller);
      return new Win32CellContext(
          win32, win32->projectionParentHwnd(), state.x, state.y, state.width, state.height, cell);
    }

    static void refresh(Win32CellContext *ctx, const loka::app::scene::LayoutState &state)
    {
      ctx->relayout(state.x, state.y, state.width, state.height);
    }
  };

  Win32CellNodeHandler gWin32CellNodeHandler;
} // namespace

Win32CellContext::Win32CellContext(Win32ScenePlatformController *controller,
                                   HWND parent,
                                   int x,
                                   int y,
                                   int width,
                                   int height,
                                   loka::app::CellNode *node)
    : Win32RetirableContext(controller),
      node_(node),
      hwnd_(0),
      textState_(0),
      text_()
{
  EnsureClassRegistered();
  hwnd_ = this->createNativeChildWindow(
      0,
      kCellClassName,
      L"",
      WS_CHILD | WS_VISIBLE,
      this->controller()->displayScale().projectFrame(loka::core::Frame(x, y, width, height)),
      parent,
      0,
      GetModuleHandleW(NULL),
      this);
  bindText();
}

Win32CellContext::~Win32CellContext()
{
  assert(!hwnd_ && "terminal fact delivery must queue the HWND before context reclaim");
}

/** The text observer has redrawn this HWND subtree with erase=true. */
loka::app::scene::PaintAnswer Win32CellContext::queryPaintDamage(const loka::app::scene::PaintQuery &query) const
{
  using namespace loka::app::scene;
  if (!this->hwnd_)
    return PaintAnswer::refused(PAINT_REFUSED_NO_CONTEXT);
  if (query.placement != PLACEMENT_ELIGIBLE)
    return PaintAnswer::refused(PAINT_REFUSED_PLACEMENT_UNSETTLED);
  if (!this->node_ || this->node_->props.text_ != this->textState_)
    return PaintAnswer::refused(PAINT_REFUSED_PROPS_UNRECONCILED);
  return PaintAnswer::nativeScheduled();
}

void Win32CellContext::readLifecycleFactOnAttach()
{
  if (this->node_ && this->node_->lifecycleFact() == loka::app::scene::NODE_FACT_ATTACHED)
  {
    this->applyAttachedPresentation();
  }
}

void Win32CellContext::onFactChanged(loka::app::scene::NodeLifecycleFact previous,
                                     loka::app::scene::NodeLifecycleFact next)
{
  (void)previous;
  if (next == loka::app::scene::NODE_FACT_ATTACHED)
  {
    this->applyAttachedPresentation();
  }
  else
  {
    // DETACHED_RETAINED hides; terminal RETIRED keeps the same policy
    // (hide before the ritual destroys the native pair).
    this->applyDetachedPresentation();
    if (next == loka::app::scene::NODE_FACT_RETIRED)
    {
      this->unbindText();
      this->retireWindow(this->hwnd_);
      this->node_ = 0;
    }
  }
}

void Win32CellContext::applyAttachedPresentation()
{
  if (hwnd_)
  {
    ShowWindow(hwnd_, SW_SHOW);
  }
}

void Win32CellContext::applyDetachedPresentation()
{
  if (hwnd_)
  {
    ShowWindow(hwnd_, SW_HIDE);
  }
}

short Win32CellContext::layout(loka::app::scene::IPlatformController *, loka::app::scene::LayoutState &state)
{
  const short requestedHeight = state.height;
  const int cellHeight = requestedHeight > 0 ? requestedHeight
                                             : loka::app::layout::FallbackControlMetrics::kCellHeight;
  this->relayout(state.x, state.y, state.width, cellHeight);
  state.height = static_cast<short>(cellHeight);
  short result = static_cast<short>(state.y + cellHeight);
  if (requestedHeight <= 0)
  {
    result = static_cast<short>(result + loka::app::layout::FallbackControlMetrics::kVerticalSpacing);
  }
  return result;
}

void Win32CellContext::relayout(int x, int y, int width, int height)
{
  if (!hwnd_)
  {
    return;
  }
  this->positionNativeWindow(this->hwnd_,
                             this->controller()->displayScale().projectFrame(loka::core::Frame(x, y, width, height)));
  Win32ScenePlatformController::redrawDirtySubtreeNow(hwnd_, NULL, TRUE);
}

void Win32CellContext::EnsureClassRegistered()
{
  static bool registered = false;
  if (registered)
  {
    return;
  }
  WNDCLASSW wc;
  ZeroMemory(&wc, sizeof(wc));
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = Win32CellContext::WndProc;
  wc.hInstance = GetModuleHandleW(NULL);
  wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
  wc.hbrBackground = NULL;
  wc.lpszClassName = kCellClassName;
  RegisterClassW(&wc);
  registered = true;
}

LRESULT CALLBACK Win32CellContext::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  Win32CellContext *self =
      static_cast<Win32CellContext *>(reinterpret_cast<void *>(GetWindowLongPtr(hwnd, GWLP_USERDATA)));
  if (msg == WM_NCCREATE)
  {
    CREATESTRUCTW *cs = reinterpret_cast<CREATESTRUCTW *>(lParam);
    self = static_cast<Win32CellContext *>(cs->lpCreateParams);
    SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
  }
  switch (msg)
  {
  case WM_ERASEBKGND:
    Win32ScenePlatformController::noteNativePaint(hwnd, Win32ScenePlatformController::NATIVE_PAINT_CELL, true);
    return 1;
  case WM_PAINT:
  {
    Win32ScenePlatformController::noteNativePaint(hwnd, Win32ScenePlatformController::NATIVE_PAINT_CELL, false);
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    if (self)
    {
      RECT rect;
      GetClientRect(hwnd, &rect);
      self->drawCell(hdc, rect);
    }
    EndPaint(hwnd, &ps);
    return 0;
  }
  case WM_LBUTTONUP:
    if (self)
      Win32InputDoor::cellClick(*self);
    return 0;
  default:
    break;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void Win32CellContext::bindText()
{
  if (!node_)
  {
    return;
  }
  textState_ = static_cast<loka::core::State<loka::core::String> *>(node_->props.text_);
  if (textState_)
  {
    textState_->bind(&Win32CellContext::TextChangedThunk, this, true);
  }
}

void Win32CellContext::unbindText()
{
  if (textState_)
  {
    textState_->unbind(&Win32CellContext::TextChangedThunk, this);
    textState_ = 0;
  }
}

void Win32CellContext::applyText()
{
  if (!textState_)
  {
    return;
  }
  if (!loka::win32::MaterializeWideString(textState_->get(), text_))
  {
    text_.clear();
  }
  if (hwnd_)
  {
    Win32ScenePlatformController::redrawDirtySubtreeNow(hwnd_, NULL, TRUE);
  }
}

void Win32CellContext::drawCell(HDC hdc, const RECT &rect)
{
  const loka::app::SurfaceGround ground = loka::app::GroundForKind(loka::app::scene::NODE_KIND_CELL);
  loka::win32::Win32PaintGround(hdc, ground, rect);
  FrameRect(hdc, &rect, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
  if (!text_.empty())
  {
    HGDIOBJ previousFont = 0;
    if (this->controller() && this->controller()->displayFont())
    {
      previousFont = SelectObject(hdc, this->controller()->displayFont());
    }
    const int previousBackground = SetBkMode(hdc, TRANSPARENT);
    const COLORREF previousText = SetTextColor(hdc, GetSysColor(loka::win32::Win32TextRoleColor(ground)));
    RECT textRect = rect;
    DrawTextW(hdc,
              text_.c_str(),
              static_cast<int>(text_.size()),
              &textRect,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SetTextColor(hdc, previousText);
    SetBkMode(hdc, previousBackground);
    if (previousFont)
    {
      SelectObject(hdc, previousFont);
    }
  }
}

void Win32CellContext::TextChangedThunk(void *userData)
{
  Win32CellContext *self = static_cast<Win32CellContext *>(userData);
  if (self)
  {
    self->applyText();
  }
}

void RegisterWin32CellNodeHandler(loka::app::scene::PlatformNodeHandlerRegistry &registry)
{
  registry.registerHandler(&gWin32CellNodeHandler);
}

void Win32CellContext::handleClick()
{
#ifdef LOKA_LIFECYCLE_AUDIT
  assert(this->controller()->borrowPhase().open());
#endif
  if (this->node_ && this->node_->props.onClick_)
    this->node_->props.onClick_->emit();
}
