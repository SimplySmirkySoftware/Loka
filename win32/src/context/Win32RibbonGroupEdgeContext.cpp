#include "../Win32PaintGround.hpp"
#include "Win32RibbonGroupEdgeContext.hpp"
#include <cassert>
#include "../Win32ScenePlatformController.hpp"
#include "app/scene/projection/RetainedNodeHandler.hpp"
#include "app/nodes/controls/Ribbon.hpp"

namespace
{
  const wchar_t kEdgeClassName[] = L"LOKA_RIBBON_GROUP_EDGE";

  class Win32RibbonGroupEdgeNodeHandler : public loka::app::scene::RetainedNodeHandler<Win32RibbonGroupEdgeNodeHandler,
                                                                                       loka::app::RibbonGroupEdgeNode,
                                                                                       Win32RibbonGroupEdgeContext>
  {
  public:
    static loka::app::RibbonGroupEdgeNode *cast(loka::app::scene::Node *node)
    {
      return node ? node->asRibbonGroupEdgeNode() : 0;
    }

    static Win32RibbonGroupEdgeContext *create(loka::app::RibbonGroupEdgeNode *edge,
                                               loka::app::scene::IPlatformController *controller,
                                               const loka::app::scene::LayoutState &state)
    {
      Win32ScenePlatformController *win32 = static_cast<Win32ScenePlatformController *>(controller);
      return new Win32RibbonGroupEdgeContext(
          win32, win32->projectionParentHwnd(), state.x, state.y, state.width, state.height, edge);
    }

    static void refresh(Win32RibbonGroupEdgeContext *ctx, const loka::app::scene::LayoutState &state)
    {
      ctx->relayout(state.x, state.y, state.width, state.height);
    }
  };

  Win32RibbonGroupEdgeNodeHandler gWin32RibbonGroupEdgeNodeHandler;
} // namespace

Win32RibbonGroupEdgeContext::Win32RibbonGroupEdgeContext(Win32ScenePlatformController *controller,
                                                         HWND parent,
                                                         int x,
                                                         int y,
                                                         int width,
                                                         int height,
                                                         loka::app::RibbonGroupEdgeNode *node)
    : Win32RetirableContext(controller),
      node_(node)
{
  EnsureClassRegistered();
  this->createNativeChildWindow(
      0,
      kEdgeClassName,
      L"",
      WS_CHILD | WS_VISIBLE,
      this->controller()->displayScale().projectFrame(loka::core::Frame(x, y, width, height)),
      parent,
      0,
      GetModuleHandleW(NULL),
      this);
}

Win32RibbonGroupEdgeContext::~Win32RibbonGroupEdgeContext()
{
  assert(!this->hwnd() && "terminal fact delivery must queue the HWND before context reclaim");
}

/** Layout queues this fully painted child HWND through the native redraw path. */
loka::app::scene::PaintAnswer
Win32RibbonGroupEdgeContext::queryPaintDamage(const loka::app::scene::PaintQuery &query) const
{
  using namespace loka::app::scene;
  if (!this->hwnd())
    return PaintAnswer::refused(PAINT_REFUSED_NO_CONTEXT);
  if (query.placement != PLACEMENT_ELIGIBLE)
    return PaintAnswer::refused(PAINT_REFUSED_PLACEMENT_UNSETTLED);
  return PaintAnswer::nativeScheduled();
}

void Win32RibbonGroupEdgeContext::readLifecycleFactOnAttach()
{
  if (this->node_ && this->node_->lifecycleFact() == loka::app::scene::NODE_FACT_ATTACHED)
  {
    this->applyAttachedPresentation();
  }
}

void Win32RibbonGroupEdgeContext::onNativeFact(loka::app::scene::NodeLifecycleFact previous,
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

void Win32RibbonGroupEdgeContext::applyAttachedPresentation()
{
  if (this->hwnd())
  {
    ShowWindow(this->hwnd(), SW_SHOW);
  }
}

void Win32RibbonGroupEdgeContext::applyDetachedPresentation()
{
  if (this->hwnd())
  {
    ShowWindow(this->hwnd(), SW_HIDE);
  }
}

short Win32RibbonGroupEdgeContext::layout(loka::app::scene::IPlatformController *, loka::app::scene::LayoutState &state)
{
  this->relayout(state.x, state.y, state.width, state.height);
  return static_cast<short>(state.y + state.height);
}

void Win32RibbonGroupEdgeContext::relayout(int x, int y, int width, int height)
{
  if (!this->hwnd())
  {
    return;
  }
  this->positionNativeWindow(this->hwnd(),
                             this->controller()->displayScale().projectFrame(loka::core::Frame(x, y, width, height)));
  Win32ScenePlatformController::redrawDirtySubtreeNow(this->hwnd(), NULL, TRUE);
}

void Win32RibbonGroupEdgeContext::EnsureClassRegistered()
{
  static bool registered = false;
  if (registered)
  {
    return;
  }
  WNDCLASSW wc;
  ZeroMemory(&wc, sizeof(wc));
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = Win32RibbonGroupEdgeContext::WndProc;
  wc.hInstance = GetModuleHandleW(NULL);
  wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
  wc.hbrBackground = NULL;
  wc.lpszClassName = kEdgeClassName;
  RegisterClassW(&wc);
  registered = true;
}

LRESULT CALLBACK Win32RibbonGroupEdgeContext::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  Win32RibbonGroupEdgeContext *self =
      static_cast<Win32RibbonGroupEdgeContext *>(reinterpret_cast<void *>(GetWindowLongPtr(hwnd, GWLP_USERDATA)));
  if (msg == WM_NCCREATE)
  {
    CREATESTRUCTW *cs = reinterpret_cast<CREATESTRUCTW *>(lParam);
    self = static_cast<Win32RibbonGroupEdgeContext *>(cs->lpCreateParams);
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
      self->drawEdge(hdc, rect);
    }
    EndPaint(hwnd, &ps);
    return 0;
  }
  default:
    break;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// Deliberate Cell twin: this leaf has no State observers or input door.
void Win32RibbonGroupEdgeContext::drawEdge(HDC hdc, const RECT &rect)
{
  if (!this->node_ || rect.right <= rect.left || rect.bottom <= rect.top)
    return;
  const COLORREF edge = GetSysColor(COLOR_3DSHADOW);
  if (this->node_->props.edge == loka::app::RIBBON_GROUP_EDGE_BOTTOM)
  {
    FillRect(hdc, &rect, GetSysColorBrush(COLOR_3DSHADOW));
    return;
  }
  int groundIndex;
  const bool answered = loka::win32::QueryWin32GroundColor(loka::app::SURFACE_GROUND_WINDOW, groundIndex);
  assert(answered);
  if (!answered)
    return;
  const COLORREF ground = GetSysColor(groundIndex);
  const int lastRow = rect.bottom - rect.top - 1;
  for (int row = 0; row <= lastRow; ++row)
  {
    // A one-row side is its bottom endpoint; empty strips were handled above.
    const COLORREF color = lastRow == 0 ? edge
                                        : RGB((GetRValue(ground) * (lastRow - row) + GetRValue(edge) * row) / lastRow,
                                              (GetGValue(ground) * (lastRow - row) + GetGValue(edge) * row) / lastRow,
                                              (GetBValue(ground) * (lastRow - row) + GetBValue(edge) * row) / lastRow);
    HBRUSH brush = CreateSolidBrush(color);
    if (brush)
    {
      const RECT strip = {rect.left, rect.top + row, rect.right, rect.top + row + 1};
      FillRect(hdc, &strip, brush);
      DeleteObject(brush);
    }
  }
}

void RegisterWin32RibbonGroupEdgeNodeHandler(loka::app::scene::PlatformNodeHandlerRegistry &registry)
{
  registry.registerHandler(&gWin32RibbonGroupEdgeNodeHandler);
}
