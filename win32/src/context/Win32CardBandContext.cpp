#include "../Win32CardPainter.hpp"
#include "Win32CardBandContext.hpp"
#include <cassert>
#include "../Win32ScenePlatformController.hpp"
#include "app/scene/projection/RetainedNodeHandler.hpp"
#include "app/nodes/controls/Card.hpp"

namespace
{
  const wchar_t kBandClassName[] = L"LOKA_CARD_BAND";

  class Win32CardBandNodeHandler : public loka::app::scene::RetainedNodeHandler<Win32CardBandNodeHandler,
                                                                                loka::app::CardBandNode,
                                                                                Win32CardBandContext>
  {
  public:
    static loka::app::CardBandNode *cast(loka::app::scene::Node *node)
    {
      return node ? node->asCardBandNode() : 0;
    }

    static Win32CardBandContext *create(loka::app::CardBandNode *band,
                                        loka::app::scene::IPlatformController *controller,
                                        const loka::app::scene::LayoutState &state)
    {
      Win32ScenePlatformController *win32 = static_cast<Win32ScenePlatformController *>(controller);
      return new Win32CardBandContext(
          win32, win32->projectionParentHwnd(), state.x, state.y, state.width, state.height, band);
    }

    static void refresh(Win32CardBandContext *ctx, const loka::app::scene::LayoutState &state)
    {
      ctx->relayout(state.x, state.y, state.width, state.height);
    }
  };

  Win32CardBandNodeHandler gWin32CardBandNodeHandler;
} // namespace

Win32CardBandContext::Win32CardBandContext(Win32ScenePlatformController *controller,
                                           HWND parent,
                                           int x,
                                           int y,
                                           int width,
                                           int height,
                                           loka::app::CardBandNode *node)
    : Win32RetirableContext(controller),
      node_(node),
      frame_(x, y, width, height)
{
  EnsureClassRegistered();
  this->createNativeChildWindow(0,
                                kBandClassName,
                                L"",
                                WS_CHILD | WS_VISIBLE,
                                this->controller()->displayScale().projectFrame(loka::core::Frame(x, y, width, height)),
                                parent,
                                0,
                                GetModuleHandleW(NULL),
                                this);
}

Win32CardBandContext::~Win32CardBandContext()
{
  assert(!this->hwnd() && "terminal fact delivery must queue the HWND before context reclaim");
}

/** The parent repaint path schedules this transparent native leaf. */
loka::app::scene::PaintAnswer Win32CardBandContext::queryPaintDamage(const loka::app::scene::PaintQuery &query) const
{
  using namespace loka::app::scene;
  if (!this->hwnd())
    return PaintAnswer::refused(PAINT_REFUSED_NO_CONTEXT);
  if (query.placement != PLACEMENT_ELIGIBLE)
    return PaintAnswer::refused(PAINT_REFUSED_PLACEMENT_UNSETTLED);
  return PaintAnswer::nativeScheduled();
}

void Win32CardBandContext::readLifecycleFactOnAttach()
{
  if (this->node_ && this->node_->lifecycleFact() == loka::app::scene::NODE_FACT_ATTACHED)
  {
    this->applyAttachedPresentation();
  }
}

void Win32CardBandContext::onNativeFact(loka::app::scene::NodeLifecycleFact previous,
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
      this->node_ = 0;
    }
  }
}

void Win32CardBandContext::applyAttachedPresentation()
{
  if (this->hwnd())
  {
    ShowWindow(this->hwnd(), SW_SHOW);
    this->onPropsApplied();
  }
}

void Win32CardBandContext::applyDetachedPresentation()
{
  if (this->hwnd())
  {
    this->onPropsApplied();
    ShowWindow(this->hwnd(), SW_HIDE);
  }
}

short Win32CardBandContext::layout(loka::app::scene::IPlatformController *, loka::app::scene::LayoutState &state)
{
  this->relayout(state.x, state.y, state.width, state.height);
  return static_cast<short>(state.y + state.height);
}

void Win32CardBandContext::relayout(int x, int y, int width, int height)
{
  if (!this->hwnd())
  {
    return;
  }
  Win32ScenePlatformController::requestTransparentChildRepaint(this->hwnd());
  this->frame_ = loka::core::Frame(x, y, width, height);
  this->positionNativeWindow(this->hwnd(),
                             this->controller()->displayScale().projectFrame(loka::core::Frame(x, y, width, height)));
  Win32ScenePlatformController::requestTransparentChildRepaint(this->hwnd());
}

void Win32CardBandContext::EnsureClassRegistered()
{
  static bool registered = false;
  if (registered)
  {
    return;
  }
  WNDCLASSW wc;
  ZeroMemory(&wc, sizeof(wc));
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = Win32CardBandContext::WndProc;
  wc.hInstance = GetModuleHandleW(NULL);
  wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
  wc.hbrBackground = NULL;
  wc.lpszClassName = kBandClassName;
  RegisterClassW(&wc);
  registered = true;
}

LRESULT CALLBACK Win32CardBandContext::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  Win32CardBandContext *self =
      static_cast<Win32CardBandContext *>(reinterpret_cast<void *>(GetWindowLongPtr(hwnd, GWLP_USERDATA)));
  if (msg == WM_NCCREATE)
  {
    CREATESTRUCTW *cs = reinterpret_cast<CREATESTRUCTW *>(lParam);
    self = static_cast<Win32CardBandContext *>(cs->lpCreateParams);
    SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
  }
  switch (msg)
  {
  case WM_ERASEBKGND:
    return 1;
  case WM_PAINT:
  {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    if (self)
    {
      RECT rect;
      GetClientRect(hwnd, &rect);
      self->drawBand(hdc, rect);
    }
    EndPaint(hwnd, &ps);
    return 0;
  }
  default:
    break;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

/** Transparent leaf twin of ImageView: the parent supplies every ground pixel. */
void Win32CardBandContext::onPropsApplied()
{
  if (this->hwnd())
    Win32ScenePlatformController::requestTransparentChildRepaint(this->hwnd());
}

void Win32CardBandContext::drawBand(HDC hdc, const RECT &rect)
{
  if (!this->node_ || rect.right <= rect.left || rect.bottom <= rect.top || !this->frame_.hasSize())
    return;
  const loka::win32::Win32DisplayScale &scale = this->controller()->displayScale();
  POINT origin = {0, 0};
  MapWindowPoints(this->hwnd(), GetAncestor(this->hwnd(), GA_ROOT), &origin, 1);
  loka::win32::PaintCardBandInk(hdc, this->frame_, this->node_->props, scale, origin.x, origin.y);
}

void RegisterWin32CardBandNodeHandler(loka::app::scene::PlatformNodeHandlerRegistry &registry)
{
  registry.registerHandler(&gWin32CardBandNodeHandler);
}
