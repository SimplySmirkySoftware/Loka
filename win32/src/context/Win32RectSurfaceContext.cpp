#include "../Win32PaintGround.hpp"
#include "Win32RectSurfaceContext.hpp"
#include <cassert>
#include "../Win32ScenePlatformController.hpp"
#include "app/RectSurface.hpp"

namespace
{
  const wchar_t kRectSurfaceClassName[] = L"LOKA_RECT_SURFACE";
}

Win32RectSurfaceContext::Win32RectSurfaceContext(Win32ScenePlatformController *controller,
                                                 HWND parent,
                                                 int x,
                                                 int y,
                                                 int width,
                                                 int height,
                                                 loka::app::RectSurfaceNode *node)
    : Win32RetirableContext(controller),
      node_(node),
      hwnd_(0),
      modelState_(0)
{
  EnsureClassRegistered();
  hwnd_ = this->createNativeChildWindow(
      0,
      kRectSurfaceClassName,
      L"",
      WS_CHILD | WS_VISIBLE,
      this->controller()->displayScale().projectFrame(loka::core::Frame(x, y, width, height)),
      parent,
      0,
      GetModuleHandleW(NULL),
      this);
  // A context without a native window is discarded by the controller; it
  // must not have bound anything.
  if (hwnd_)
  {
    bindModel();
  }
}

Win32RectSurfaceContext::~Win32RectSurfaceContext()
{
  assert(!hwnd_ && "terminal fact delivery must queue the HWND before context reclaim");
}

/** A clearing surface owes its own HWND client rect, without erase or children.
    A non-clearing surface refuses: its WM_ERASEBKGND does not restore
    ground, so an own-HWND erase request could not discharge that obligation. */
loka::app::scene::PaintAnswer Win32RectSurfaceContext::queryPaintDamage(const loka::app::scene::PaintQuery &query) const
{
  using namespace loka::app::scene;
  RECT rect;
  if (!this->hwnd_)
    return PaintAnswer::refused(PAINT_REFUSED_NO_CONTEXT);
  if (query.placement != PLACEMENT_ELIGIBLE || query.scope != paintScope() || !GetClientRect(this->hwnd_, &rect))
    return PaintAnswer::refused(PAINT_REFUSED_PLACEMENT_UNSETTLED);
  if (!this->node_ || !this->modelState_ || this->node_->props.model_ != this->modelState_)
    return PaintAnswer::refused(PAINT_REFUSED_PROPS_UNRECONCILED);
  if (!this->presented_.isKnown())
    return PaintAnswer::refused(PAINT_REFUSED_HISTORY_UNKNOWN);
  PaintDamage damage = {query.scope, rect.left, rect.top, 0, 0, PAINT_COVERAGE_PAINT_ONLY};
  if (this->modelState_->get() != this->presented_.value())
  {
    damage.width = rect.right - rect.left;
    damage.height = rect.bottom - rect.top;
  }
  return PaintAnswer::exact(damage);
}

void Win32RectSurfaceContext::readLifecycleFactOnAttach()
{
  if (this->node_ && this->node_->lifecycleFact() == loka::app::scene::NODE_FACT_ATTACHED)
  {
    this->applyAttachedPresentation();
  }
}

void Win32RectSurfaceContext::onFactChanged(loka::app::scene::NodeLifecycleFact previous,
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
    // (hide before the ritual destroys the native pair). Either way the
    // surface is no longer placed: its pending seat rows go first, while
    // the back-pointers are still intact.
    if (this->controller())
    {
      this->controller()->cancelRectSurfaceExtent(this->node_);
    }
    this->presented_.invalidate();
    this->applyDetachedPresentation();
    if (next == loka::app::scene::NODE_FACT_RETIRED)
    {
      this->unbindModel();
      this->retireWindow(this->hwnd_);
      this->node_ = 0;
    }
  }
}

void Win32RectSurfaceContext::onPropsApplied()
{
  this->presented_.invalidate();
  if (this->node_ && this->node_->props.model_ != this->modelState_)
  {
    this->unbindModel();
    this->bindModel();
  }
}

void Win32RectSurfaceContext::applyAttachedPresentation()
{
  if (hwnd_)
  {
    ShowWindow(hwnd_, SW_SHOW);
  }
}

void Win32RectSurfaceContext::applyDetachedPresentation()
{
  if (hwnd_)
  {
    ShowWindow(hwnd_, SW_HIDE);
  }
}

void Win32RectSurfaceContext::relayout(int x, int y, int width, int height)
{
  if (!hwnd_)
  {
    return;
  }
  this->presented_.invalidate();
  this->positionNativeWindow(this->hwnd_,
                             this->controller()->displayScale().projectFrame(loka::core::Frame(x, y, width, height)));
  HWND parent = 0;
  RECT rect;
  if (this->queryBoundsInParent(parent, rect))
  {
    Win32ScenePlatformController::redrawDirtySubtreeNow(parent, &rect, TRUE);
  }
}

void Win32RectSurfaceContext::EnsureClassRegistered()
{
  static bool registered = false;
  if (registered)
  {
    return;
  }
  WNDCLASSW wc;
  ZeroMemory(&wc, sizeof(wc));
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = Win32RectSurfaceContext::WndProc;
  wc.hInstance = GetModuleHandleW(NULL);
  wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
  wc.hbrBackground = NULL;
  wc.lpszClassName = kRectSurfaceClassName;
  RegisterClassW(&wc);
  registered = true;
}

LRESULT CALLBACK Win32RectSurfaceContext::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  Win32RectSurfaceContext *self =
      static_cast<Win32RectSurfaceContext *>(reinterpret_cast<void *>(GetWindowLongPtr(hwnd, GWLP_USERDATA)));
  if (msg == WM_NCCREATE)
  {
    CREATESTRUCTW *create = reinterpret_cast<CREATESTRUCTW *>(lParam);
    self = static_cast<Win32RectSurfaceContext *>(create->lpCreateParams);
    SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
  }

  switch (msg)
  {
  case WM_SIZE:
    if (self)
    {
      self->presented_.invalidate();
    }
    break;
  case WM_ERASEBKGND:
    Win32ScenePlatformController::noteNativePaint(hwnd, Win32ScenePlatformController::NATIVE_PAINT_RECT_SURFACE, true);
    return 1;
  case WM_PAINT:
  {
    Win32ScenePlatformController::noteNativePaint(hwnd, Win32ScenePlatformController::NATIVE_PAINT_RECT_SURFACE, false);
    PAINTSTRUCT paint;
    HDC hdc = BeginPaint(hwnd, &paint);
    if (self)
    {
      RECT rect;
      GetClientRect(hwnd, &rect);
      self->draw(hdc, rect);
    }
    EndPaint(hwnd, &paint);
    return 0;
  }
  default:
    break;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void Win32RectSurfaceContext::bindModel()
{
  if (!node_)
  {
    return;
  }
  modelState_ = node_->props.model_;
  if (modelState_)
  {
    modelState_->bind(&Win32RectSurfaceContext::ModelChangedThunk, this, false);
  }
  this->applyModel();
}

void Win32RectSurfaceContext::unbindModel()
{
  if (modelState_)
  {
    modelState_->unbind(&Win32RectSurfaceContext::ModelChangedThunk, this);
    modelState_ = 0;
  }
}

void Win32RectSurfaceContext::applyModel()
{
  if (this->hwnd_)
  {
    Win32ScenePlatformController::requestDirtyRect(this->hwnd_, NULL, FALSE);
    return;
  }
  HWND parent = 0;
  RECT rect;
  if (this->queryBoundsInParent(parent, rect))
  {
    Win32ScenePlatformController::requestDirtySubtree(parent, &rect, TRUE);
  }
}

bool Win32RectSurfaceContext::queryBoundsInParent(HWND &parent, RECT &rect) const
{
  parent = this->hwnd_ ? GetParent(this->hwnd_) : 0;
  if (!parent || !GetWindowRect(this->hwnd_, &rect))
  {
    return false;
  }
  MapWindowPoints(NULL, parent, reinterpret_cast<POINT *>(&rect), 2);
  return true;
}

void Win32RectSurfaceContext::ModelChangedThunk(void *userData)
{
  Win32RectSurfaceContext *self = static_cast<Win32RectSurfaceContext *>(userData);
  if (self)
  {
    self->applyModel();
  }
}

void Win32RectSurfaceContext::draw(HDC hdc, const RECT &rect)
{
  this->presented_.invalidate();
  const int width = static_cast<int>(rect.right - rect.left);
  const int height = static_cast<int>(rect.bottom - rect.top);
  if (!hdc || width <= 0 || height <= 0)
  {
    return;
  }
  // A partial/complex native clip cannot establish a whole-client fact.
  RECT clip;
  const bool complete = GetClipBox(hdc, &clip) == SIMPLEREGION && EqualRect(&clip, &rect);
  const loka::app::RectSurfaceModel model =
      this->modelState_ ? this->modelState_->get() : loka::app::RectSurfaceModel();
  HDC memoryDC = CreateCompatibleDC(hdc);
  HBITMAP bitmap = memoryDC ? CreateCompatibleBitmap(hdc, width, height) : NULL;
  HGDIOBJ previous = bitmap ? SelectObject(memoryDC, bitmap) : NULL;
  const bool buffered = previous && previous != HGDI_ERROR;
  HDC target = buffered ? memoryDC : hdc;
  const loka::app::SurfaceGround ground = loka::app::GroundForKind(loka::app::scene::NODE_KIND_RECT_SURFACE);
  bool painted = loka::win32::Win32PaintGround(target, ground, rect);
  if (this->node_ && this->modelState_)
  {
    HBRUSH spriteBrush = GetSysColorBrush(loka::win32::Win32TextRoleColor(ground));
    for (short i = 0; i < model.rectCount; ++i)
    {
      RECT spriteRect;
      const loka::core::Frame logicalRect(model.rects[i].x,
                                          model.rects[i].y,
                                          model.rects[i].width,
                                          model.rects[i].height);
      // DPI only, no space scale: these are decoded sprite pixels.
      spriteRect = this->controller()->displayScale().projectDeviceOnly(logicalRect).r;
      if (!FillRect(target, &spriteRect, spriteBrush))
        painted = false;
    }
  }
  if (target == memoryDC)
  {
    if (!BitBlt(hdc, 0, 0, width, height, memoryDC, 0, 0, SRCCOPY))
      painted = false;
  }
  if (complete && painted && this->node_ && this->modelState_)
  {
    this->presented_.commit(model, paintScope());
  }
  if (buffered)
  {
    SelectObject(memoryDC, previous);
  }
  if (bitmap)
  {
    DeleteObject(bitmap);
  }
  if (memoryDC)
  {
    DeleteDC(memoryDC);
  }
}
