#include "../Win32Ground.hpp"
#include "Win32AttributedTextContext.hpp"
#include "../Win32ScenePlatformController.hpp"
#include "../Win32InputDoor.hpp"
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
      node_(node)
{
  EnsureClassRegistered();
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
  assert(!this->hwnd() && !this->links_.table().valid() && "terminal delivery precedes context reclaim");
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
    ShowWindow(this->hwnd(), SW_SHOW);
}
void Win32AttributedTextContext::onNativeFact(loka::app::scene::NodeLifecycleFact,
                                              loka::app::scene::NodeLifecycleFact next)
{
  using namespace loka::app::scene;
  if (next == NODE_FACT_ATTACHED)
  {
    if (this->hwnd())
      ShowWindow(this->hwnd(), SW_SHOW);
    return;
  }
  this->links_.invalidatePress();
  this->presented_.invalidate();
  if (this->hwnd())
    ShowWindow(this->hwnd(), SW_HIDE);
  if (next == NODE_FACT_RETIRED)
  {
    this->clearMeasurement();
    this->textEnvironmentSubscription_.disconnectTextEnvironment();
    this->node_ = 0;
  }
}
void Win32AttributedTextContext::onPropsApplied()
{
  // Restore the old rectangle before clearMeasurement collapses the child.
  Win32ScenePlatformController::requestTransparentChildRepaint(this->hwnd());
  // The #965 input mark also forces rebuild on retained props application.
  this->clearMeasurement();
  if (this->hwnd())
    this->controller()->requestRelayout();
}
void Win32AttributedTextContext::clearMeasurement()
{
  this->links_.clear();
  this->presented_.invalidate();
  if (this->hwnd())
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
  if (state.inputs != loka::app::scene::NODE_DIRTY_NONE || !this->links_.table().reusable(constraint))
  {
    HDC dc = this->hwnd() ? GetDC(this->hwnd()) : 0;
    const bool built =
        dc && this->node_ && this->node_->props.text_
        && this->links_.rebuild(
            this->node_->props.text_->get(), this->node_->props.blockStyle_, constraint, dc, *this->controller());
    if (dc)
      ReleaseDC(this->hwnd(), dc);
    if (!built)
    {
      this->controller()->refuseTextMeasurement(this->node_, state);
      this->clearMeasurement();
    }
  }
  const int height = this->links_.table().height();
  const int width = state.width > 0 ? state.width : this->links_.table().width();
  state.height = Coordinate(height);
  this->relayout(state.x, state.y, Coordinate(width), state.height);
  Win32ScenePlatformController::requestTransparentChildRepaint(this->hwnd());
  return Coordinate(state.y + state.height);
}
void Win32AttributedTextContext::relayout(int x, int y, int width, int height)
{
  if (!this->hwnd())
    return;
  // Layout has completed the table for these absolute edges. Moving the
  // child revokes the old presentation before any synchronous native paint.
  this->presented_.invalidate();
  this->positionNativeWindow(this->hwnd(),
                             this->controller()->displayScale().projectFrame(loka::core::Frame(x, y, width, height)));
}
loka::app::scene::PaintAnswer
Win32AttributedTextContext::queryPaintDamage(const loka::app::scene::PaintQuery &query) const
{
  using namespace loka::app::scene;
  RECT rect;
  if (!this->hwnd())
    return PaintAnswer::refused(PAINT_REFUSED_NO_CONTEXT);
  if (query.placement != PLACEMENT_ELIGIBLE || query.scope != paintScope() || !GetClientRect(this->hwnd(), &rect)
      || IsRectEmpty(&rect))
    return PaintAnswer::refused(PAINT_REFUSED_PLACEMENT_UNSETTLED);
  if (!this->links_.table().valid() || !this->node_ || !this->node_->props.text_
      || this->node_->props.text_->get() != this->links_.table().value())
    return PaintAnswer::refused(PAINT_REFUSED_PROPS_UNRECONCILED);
  if (!this->presented_.isKnown())
    return PaintAnswer::refused(PAINT_REFUSED_HISTORY_UNKNOWN);
  PaintDamage damage = {query.scope, rect.left, rect.top, 0, 0, PAINT_COVERAGE_PAINT_ONLY};
  if (this->links_.table().value() != this->presented_.value().text)
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
  case WM_LBUTTONDOWN:
  case WM_LBUTTONUP:
    if (!self)
      break;
    Win32InputDoor::attributedTextButton(*self, msg, lParam);
    return 0;
  case WM_CAPTURECHANGED:
    if (!self)
      break;
    if (lParam != reinterpret_cast<LPARAM>(hwnd))
      self->links_.losePress();
    return 0;
  case WM_CANCELMODE:
    if (self)
      self->links_.losePress();
    return DefWindowProcW(hwnd, msg, wParam, lParam);
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
  if (!this->links_.table().valid() || !this->node_ || !this->node_->props.text_
      || this->links_.table().value() != this->node_->props.text_->get())
    return;
  if (this->links_.table().draw(dc, rect, this->node_->props.blockStyle_) && complete)
    this->presented_.commit(Win32PresentedText(this->links_.table().value(), rect), paintScope());
}
void RegisterWin32AttributedTextNodeHandler(loka::app::scene::PlatformNodeHandlerRegistry &registry)
{
  registry.registerHandler(&handler);
}

HitAnswer Win32AttributedTextContext::prepareLinkHit(POINT client, LinkHit &out)
{
  RECT seat;
  if (!this->node_ || !this->hwnd() || !GetClientRect(this->hwnd(), &seat))
    return HIT_REFUSED;
  HDC dc = GetDC(this->hwnd());
  if (!dc)
    return HIT_REFUSED;
  LinkHit local = LinkHit();
  HitAnswer answer = this->links_.table().hitAt(dc, seat, this->node_->props.blockStyle_, client, local);
  if (!ReleaseDC(this->hwnd(), dc))
    return HIT_REFUSED;
  if (answer == HIT_LINK)
    out = local;
  return answer;
}

bool Win32AttributedTextContext::queryEligibleRun(const LinkHit &hit, Win32PressedRun &out) const
{
  if (!loka::app::scene::IsAttachedCurrentContext(this) || !this->node_ || !this->node_->props.text_
      || !this->links_.table().valid() || !this->presented_.isKnown())
    return false;
  const Win32PresentedText &presented = this->presented_.value();
  const loka::app::AttributedString &current = this->node_->props.text_->get();
  if (!presented.text.hasSameSegmentValues(this->links_.table().value())
      || !this->links_.table().value().hasSameSegmentValues(current) || hit.seatWidth != presented.seatWidth
      || hit.seatHeight != presented.seatHeight)
    return false;
  loka::app::AttributedString::LinkRun run;
  // queryLinkRun success guarantees run.first <= firstSegment and run.last < segmentCount(), so run.last >= lastSegment
  // implies both bounds.
  if (!current.queryLinkRun(hit.firstSegment, run) || run.last < hit.lastSegment)
    return false;
  out = Win32PressedRun(run);
  return true;
}

namespace
{
  enum Win32CaptureRelease
  {
    CAPTURE_RELEASED,
    CAPTURE_NOT_HELD,
    CAPTURE_RELEASE_FAILED
  };
  Win32CaptureRelease releaseOwnedCapture(HWND hwnd)
  {
    if (GetCapture() != hwnd)
      return CAPTURE_NOT_HELD;
    return ReleaseCapture() ? CAPTURE_RELEASED : CAPTURE_RELEASE_FAILED;
  }
} // namespace
void Win32AttributedTextContext::handleLinkButton(UINT message, LPARAM lParam)
{
  Win32LinkPressProcedure p(this->links_);
  if (!p.admitted())
    return;
  const POINT point = {static_cast<short>(lParam & 0xffff), static_cast<short>((lParam >> 16) & 0xffff)};
  Win32PressedRun run;
  LinkHit hit = LinkHit();
  if (message == WM_LBUTTONDOWN)
  {
    p.takeCandidate(run);
    if (this->prepareLinkHit(point, hit) != HIT_LINK)
    {
      releaseOwnedCapture(this->hwnd());
      return;
    }
    if (!this->queryEligibleRun(hit, run) || p.invalidated())
    {
      releaseOwnedCapture(this->hwnd());
      return;
    }
    p.placeCandidate(run);
    SetCapture(this->hwnd());
    Win32PressedRun again;
    if (!p.invalidated() && p.candidateHolds(run) && GetCapture() == this->hwnd() && this->queryEligibleRun(hit, again)
        && again == run)
      return;
    p.takeCandidate(run);
    releaseOwnedCapture(this->hwnd());
    return;
  }
  if (!p.takeCandidate(run))
  {
    releaseOwnedCapture(this->hwnd());
    return;
  }
  const HitAnswer answer = this->prepareLinkHit(point, hit);
  switch (releaseOwnedCapture(this->hwnd()))
  {
  case CAPTURE_NOT_HELD:
  case CAPTURE_RELEASE_FAILED:
    return;
  case CAPTURE_RELEASED:
    break;
  }
  Win32PressedRun now;
  const bool deliver = !p.invalidated() && answer == HIT_LINK && this->queryEligibleRun(hit, now) && now == run;
  p.close();
  if (deliver)
    this->node_->writeActivatedLink(run.first());
}
