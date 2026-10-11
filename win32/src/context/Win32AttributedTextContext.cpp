#include "../Win32Ground.hpp"
#include "Win32AttributedTextContext.hpp"
#include "../Win32ScenePlatformController.hpp"
#include "app/scene/projection/RetainedNodeHandler.hpp"
#include "app/layout/FallbackControlMetrics.hpp"
#include <climits>

namespace
{
  const wchar_t kAttributedTextClassName[] = L"LOKA_ATTRIBUTED_TEXT";
  short Coordinate(int value)
  {
    return static_cast<short>(value > SHRT_MAX ? SHRT_MAX : value < SHRT_MIN ? SHRT_MIN : value);
  }
  class Win32AttributedTextNodeHandler : public loka::app::scene::RetainedNodeHandler<Win32AttributedTextNodeHandler,
                                                                                      loka::app::AttributedTextNode,
                                                                                      Win32AttributedTextContext>
  {
  public:
    static loka::app::AttributedTextNode *cast(loka::app::scene::Node *node)
    {
      return node ? node->asAttributedTextNode() : 0;
    }
    static Win32AttributedTextContext *create(loka::app::AttributedTextNode *node,
                                              loka::app::scene::IPlatformController *controller,
                                              const loka::app::scene::LayoutState &state)
    {
      Win32ScenePlatformController *win32 = static_cast<Win32ScenePlatformController *>(controller);
      Win32AttributedTextContext *context = new Win32AttributedTextContext(
          win32, win32->projectionParentHwnd(), state.x, state.y, state.width, state.height, node);
      if (context && !context->paintHwnd())
      {
        delete context;
        return 0;
      }
      return context;
    }
  };
  Win32AttributedTextNodeHandler handler;
} // namespace
Win32AttributedTextContext::Win32AttributedTextContext(Win32ScenePlatformController *controller,
                                                       HWND parent,
                                                       int x,
                                                       int y,
                                                       int width,
                                                       int height,
                                                       loka::app::AttributedTextNode *node)
    : Win32RetirableContext(controller),
      textEnvironmentSubscription_(controller->textEnvironment_, *this),
      node_(node),
      hwnd_(0)
{
  EnsureClassRegistered();
  this->hwnd_ =
      this->createNativeChildWindow(0,
                                    kAttributedTextClassName,
                                    L"",
                                    WS_CHILD | WS_VISIBLE,
                                    controller->displayScale().projectFrame(loka::core::Frame(x, y, width, height)),
                                    parent,
                                    0,
                                    GetModuleHandleW(NULL),
                                    this);
}
Win32AttributedTextContext::~Win32AttributedTextContext()
{
  assert(!this->hwnd_ && !this->table_.valid() && "terminal delivery precedes context reclaim");
}
void *Win32AttributedTextContext::operator new(std::size_t size) throw()
{
  return loka::core::LokaAllocRaw(size, loka::core::LokaAllocationSite("Win32AttributedText", "Context"));
}
void Win32AttributedTextContext::operator delete(void *storage) throw()
{
  loka::core::LokaFreeRaw(storage, loka::core::LokaAllocationSite("Win32AttributedText", "Context"));
}
void Win32AttributedTextContext::readLifecycleFactOnAttach()
{
  if (this->node_ && this->node_->lifecycleFact() == loka::app::scene::NODE_FACT_ATTACHED)
    ShowWindow(this->hwnd_, SW_SHOW);
}
void Win32AttributedTextContext::onFactChanged(loka::app::scene::NodeLifecycleFact,
                                               loka::app::scene::NodeLifecycleFact next)
{
  using namespace loka::app::scene;
  if (next == NODE_FACT_ATTACHED)
  {
    if (this->hwnd_)
      ShowWindow(this->hwnd_, SW_SHOW);
    return;
  }
  this->presented_.invalidate();
  if (this->hwnd_)
    ShowWindow(this->hwnd_, SW_HIDE);
  if (next == NODE_FACT_RETIRED)
  {
    this->clearMeasurement();
    this->textEnvironmentSubscription_.disconnectTextEnvironment();
    this->retireWindow(this->hwnd_);
    this->node_ = 0;
  }
}
void Win32AttributedTextContext::onPropsApplied()
{
  // Restore the old rectangle before clearMeasurement collapses the child.
  Win32ScenePlatformController::requestTransparentChildRepaint(this->hwnd_);
  // The #965 input mark also forces rebuild on retained props application.
  this->clearMeasurement();
  if (this->hwnd_)
    this->controller()->requestRelayout();
}
void Win32AttributedTextContext::clearMeasurement()
{
  this->table_.clear();
  this->presented_.invalidate();
  if (this->hwnd_)
    this->relayout(0, 0, 0, 0);
}
void Win32AttributedTextContext::onTextEnvironmentChanged()
{
  this->clearMeasurement();
}
short Win32AttributedTextContext::layout(loka::app::scene::IPlatformController *, loka::app::scene::LayoutState &state)
{
  assert(this->controller()->textShaping() == loka::app::PER_RUN);
  this->presented_.invalidate();
  const loka::win32::Win32DisplayScale &scale = this->controller()->displayScale();
  const int constraint = scale.nativeLength(state.x, state.x + state.width).px;
  if (state.inputs != loka::app::scene::NODE_DIRTY_NONE || !this->table_.reusable(constraint))
  {
    HDC dc = this->hwnd_ ? GetDC(this->hwnd_) : 0;
    const bool built = dc && this->node_ && this->node_->props.text_
                       && this->table_.build(this->node_->props.text_->get(),
                                             this->node_->props.blockStyle_, constraint, dc, *this->controller());
    if (dc)
      ReleaseDC(this->hwnd_, dc);
    if (!built)
    {
      this->controller()->refuseTextMeasurement(this->node_, state);
      this->clearMeasurement();
    }
  }
  const int height = this->table_.height();
  const int width = state.width > 0 ? state.width : this->table_.width();
  state.height = Coordinate(height);
  this->relayout(state.x, state.y, Coordinate(width), state.height);
  Win32ScenePlatformController::requestTransparentChildRepaint(this->hwnd_);
  return Coordinate(state.y + state.height + loka::app::layout::FallbackControlMetrics::kVerticalSpacing);
}
void Win32AttributedTextContext::relayout(int x, int y, int width, int height)
{
  if (!this->hwnd_)
    return;
  // Layout has completed the table for these absolute edges. Moving the
  // child revokes the old presentation before any synchronous native paint.
  this->presented_.invalidate();
  this->positionNativeWindow(this->hwnd_,
                             this->controller()->displayScale().projectFrame(loka::core::Frame(x, y, width, height)));
}
loka::app::scene::PaintAnswer
Win32AttributedTextContext::queryPaintDamage(const loka::app::scene::PaintQuery &query) const
{
  using namespace loka::app::scene;
  RECT rect;
  if (!this->hwnd_)
    return PaintAnswer::refused(PAINT_REFUSED_NO_CONTEXT);
  if (query.placement != PLACEMENT_ELIGIBLE || query.scope != paintScope() || !GetClientRect(this->hwnd_, &rect)
      || IsRectEmpty(&rect))
    return PaintAnswer::refused(PAINT_REFUSED_PLACEMENT_UNSETTLED);
  if (!this->table_.valid() || !this->node_ || !this->node_->props.text_
      || this->node_->props.text_->get() != this->table_.value())
    return PaintAnswer::refused(PAINT_REFUSED_PROPS_UNRECONCILED);
  if (!this->presented_.isKnown())
    return PaintAnswer::refused(PAINT_REFUSED_HISTORY_UNKNOWN);
  PaintDamage damage = {query.scope, rect.left, rect.top, 0, 0, PAINT_COVERAGE_PAINT_ONLY};
  if (this->table_.value() != this->presented_.value())
  {
    damage.width = rect.right - rect.left;
    damage.height = rect.bottom - rect.top;
  }
  return PaintAnswer::exact(damage);
}
void Win32AttributedTextContext::EnsureClassRegistered()
{
  // RegisterClassW is idempotent for this process-owned class; failure to
  // register/create is handled by the installation wall, without a flag.
  WNDCLASSW wc;
  ZeroMemory(&wc, sizeof(wc));
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = Win32AttributedTextContext::WndProc;
  wc.hInstance = GetModuleHandleW(NULL);
  wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
  wc.lpszClassName = kAttributedTextClassName;
  RegisterClassW(&wc);
}
LRESULT CALLBACK Win32AttributedTextContext::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  Win32AttributedTextContext *self =
      reinterpret_cast<Win32AttributedTextContext *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (msg == WM_NCCREATE)
  {
    self = static_cast<Win32AttributedTextContext *>(reinterpret_cast<CREATESTRUCTW *>(lParam)->lpCreateParams);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
  }
  switch (msg)
  {
  case WM_SETFONT:
    // The controller broadcasts before swapping/deleting its font table.
    // Do not rebuild here: the replacement is not yet installed.
    if (self)
      self->onTextEnvironmentChanged();
    return 0;
  case WM_SIZE:
    if (self)
      self->presented_.invalidate();
    break;
  case WM_ERASEBKGND:
    Win32ScenePlatformController::noteNativePaint(hwnd, Win32ScenePlatformController::NATIVE_PAINT_RECT_SURFACE, true);
    return 1;
  case WM_PAINT:
  {
    Win32ScenePlatformController::noteNativePaint(hwnd, Win32ScenePlatformController::NATIVE_PAINT_RECT_SURFACE, false);
    PAINTSTRUCT paint;
    HDC dc = BeginPaint(hwnd, &paint);
    if (self)
    {
      RECT rect;
      GetClientRect(hwnd, &rect);
      self->draw(dc, rect);
    }
    EndPaint(hwnd, &paint);
    return 0;
  }
  default:
    break;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}
void Win32AttributedTextContext::draw(HDC dc, const RECT &rect)
{
  this->presented_.invalidate();
  if (!dc || IsRectEmpty(&rect))
    return;
  RECT clip;
  const bool complete = GetClipBox(dc, &clip) == SIMPLEREGION && EqualRect(&clip, &rect);
  // Transparent like Text: the parent repaint supplies ground. Only a complete,
  // successful foreground paint certifies presentation; partial paints do not.
  if (!this->table_.valid() || !this->node_ || !this->node_->props.text_
      || this->table_.value() != this->node_->props.text_->get())
    return;
  // Like Text's WM_CTLCOLORSTATIC path, pair transparent text with WINDOW.
  // The table uses this color for both runs and ellipsis; restore the caller's DC.
  const COLORREF previousText = SetTextColor(
      dc, GetSysColor(loka::win32::Win32TextRoleColor(loka::app::SURFACE_GROUND_WINDOW)));
  if (this->table_.draw(dc, rect, this->node_->props.blockStyle_) && complete)
    this->presented_.commit(this->table_.value(), paintScope());
  SetTextColor(dc, previousText);
}
void RegisterWin32AttributedTextNodeHandler(loka::app::scene::PlatformNodeHandlerRegistry &registry)
{
  registry.registerHandler(&handler);
}
