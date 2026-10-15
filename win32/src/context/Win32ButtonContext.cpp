#include "Win32ButtonContext.hpp"
#include <cassert>
#include "../Win32ScenePlatformController.hpp"
#include "app/layout/FallbackControlMetrics.hpp"
#include "app/scene/projection/RetainedNodeHandler.hpp"
#include "app/nodes/controls/Button.hpp"
#include "core/resource/Image.hpp"
#include "core/State.hpp"
#include "platform/Win32String.hpp"

namespace
{
  class Win32ButtonNodeHandler
      : public loka::app::scene::RetainedNodeHandler<Win32ButtonNodeHandler,
                                                     loka::app::ButtonNode,
                                                     Win32ButtonContext>
  {
  public:
    static loka::app::ButtonNode *cast(loka::app::scene::Node *node)
    {
      return node ? node->asButtonNode() : 0;
    }

    static Win32ButtonContext *create(loka::app::ButtonNode *button,
                                      loka::app::scene::IPlatformController *controller,
                                      const loka::app::scene::LayoutState &state)
    {
      Win32ScenePlatformController *win32 = static_cast<Win32ScenePlatformController *>(controller);
      return new Win32ButtonContext(
          win32, win32->projectionParentHwnd(), state.x, state.y, state.width, state.height, button);
    }

    static void refresh(Win32ButtonContext *ctx, const loka::app::scene::LayoutState &state)
    {
      ctx->relayout(state.x, state.y, state.width, state.height);
    }
  };

  Win32ButtonNodeHandler gWin32ButtonNodeHandler;

} // namespace

Win32ButtonContext::Win32ButtonContext(Win32ScenePlatformController *controller,
                                       HWND parent,
                                       int x,
                                       int y,
                                       int width,
                                       int height,
                                       loka::app::ButtonNode *node)
    : Win32RetirableContext(controller),
      node_(node),
      textState_(0),
      enabledState_(0)
{
  DWORD style = WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON;
  // Unicode window: keeps the label UTF-16 end to end (an ANSI window would
  // thunk SetWindowTextW through the system ACP and lose out-of-ACP chars).
  this->createNativeChildWindow(
      0,
      L"BUTTON",
      L"",
      style,
      this->controller()->displayScale().projectFrame(loka::core::Frame(x, y, width, height)),
      parent,
      reinterpret_cast<HMENU>(static_cast<INT_PTR>(1000)),
      GetModuleHandleW(NULL),
      NULL);
  if (this->hwnd())
  {
    SetWindowLongPtr(this->hwnd(), GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
  }
  bindText();
  bindEnabled();
}

Win32ButtonContext::~Win32ButtonContext()
{
  assert(!this->hwnd() && "terminal fact delivery must queue the HWND before context reclaim");
}

/** Text and enabled observers have requested this HWND, erase=true, children=false. */
loka::app::scene::PaintAnswer Win32ButtonContext::queryPaintDamage(const loka::app::scene::PaintQuery &query) const
{
  using namespace loka::app::scene;
  if (!this->hwnd())
    return PaintAnswer::refused(PAINT_REFUSED_NO_CONTEXT);
  if (query.placement != PLACEMENT_ELIGIBLE)
    return PaintAnswer::refused(PAINT_REFUSED_PLACEMENT_UNSETTLED);
  if (!this->node_ || this->node_->props.text_ != this->textState_
      || this->node_->props.enabled_ != this->enabledState_)
    return PaintAnswer::refused(PAINT_REFUSED_PROPS_UNRECONCILED);
  return PaintAnswer::nativeScheduled();
}

void Win32ButtonContext::readLifecycleFactOnAttach()
{
  if (this->node_ && this->node_->lifecycleFact() == loka::app::scene::NODE_FACT_ATTACHED)
  {
    this->applyAttachedPresentation();
  }
}

void Win32ButtonContext::onNativeFact(loka::app::scene::NodeLifecycleFact previous,
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
      this->unbindEnabled();
      this->node_ = 0;
    }
  }
}

void Win32ButtonContext::applyAttachedPresentation()
{
  if (this->hwnd())
  {
    ShowWindow(this->hwnd(), SW_SHOW);
  }
}

void Win32ButtonContext::applyDetachedPresentation()
{
  if (this->hwnd())
  {
    ShowWindow(this->hwnd(), SW_HIDE);
  }
}

bool Win32ButtonContext::captureBitmap(loka::core::resource::Image &out) const
{
  return this->controller()->captureWindowClientBitmap(this->hwnd(), out);
}

short Win32ButtonContext::layout(loka::app::scene::IPlatformController *, loka::app::scene::LayoutState &state)
{
  // The height a Row aligns this button by, so the frame cannot disagree with it.
  const short height = static_cast<short>(this->controller()->rowLayoutMetrics().buttonHeight);
  this->relayout(state.x, state.y, state.width, height);
  state.height = height;
  return static_cast<short>(state.y + height);
}

bool Win32ButtonContext::handleCommand(WPARAM, LPARAM)
{
#ifdef LOKA_LIFECYCLE_AUDIT
  assert(this->controller()->borrowPhase().open());
#endif
  if (node_ && node_->props.onClick_)
  {
    node_->props.onClick_->emit();
    return true;
  }
  return false;
}

void Win32ButtonContext::relayout(int x, int y, int width, int height)
{
  if (!this->hwnd())
  {
    return;
  }
  this->positionNativeWindow(this->hwnd(),
                             this->controller()->displayScale().projectFrame(loka::core::Frame(x, y, width, height)));
}

void Win32ButtonContext::bindText()
{
  if (!node_)
    return;
  textState_ = static_cast<loka::core::State<loka::core::String> *>(node_->props.text_);
  if (textState_)
  {
    textState_->bind(&Win32ButtonContext::TextChangedThunk, this, true);
  }
}

void Win32ButtonContext::unbindText()
{
  if (textState_)
  {
    textState_->unbind(&Win32ButtonContext::TextChangedThunk, this);
    textState_ = 0;
  }
}

void Win32ButtonContext::bindEnabled()
{
  if (!node_)
    return;
  enabledState_ = static_cast<loka::core::State<bool> *>(node_->props.enabled_);
  if (enabledState_)
  {
    enabledState_->bind(&Win32ButtonContext::EnabledChangedThunk, this, true);
  }
}

void Win32ButtonContext::unbindEnabled()
{
  if (enabledState_)
  {
    enabledState_->unbind(&Win32ButtonContext::EnabledChangedThunk, this);
    enabledState_ = 0;
  }
}

void Win32ButtonContext::applyText()
{
  if (!this->hwnd() || !textState_)
  {
    return;
  }
  std::wstring wide;
  if (loka::win32::MaterializeWideString(textState_->get(), wide))
  {
    SetWindowTextW(this->hwnd(), wide.c_str());
  }
  Win32ScenePlatformController::requestDirtyRect(this->hwnd(), NULL, TRUE);
}

void Win32ButtonContext::applyEnabled()
{
  if (!this->hwnd() || !enabledState_)
  {
    return;
  }
  EnableWindow(this->hwnd(), enabledState_->get() ? TRUE : FALSE);
  Win32ScenePlatformController::requestDirtyRect(this->hwnd(), NULL, TRUE);
}

void Win32ButtonContext::TextChangedThunk(void *userData)
{
  Win32ButtonContext *self = static_cast<Win32ButtonContext *>(userData);
  if (self)
  {
    self->applyText();
  }
}

void Win32ButtonContext::EnabledChangedThunk(void *userData)
{
  Win32ButtonContext *self = static_cast<Win32ButtonContext *>(userData);
  if (self)
  {
    self->applyEnabled();
  }
}

void RegisterWin32ButtonNodeHandler(loka::app::scene::PlatformNodeHandlerRegistry &registry)
{
  registry.registerHandler(&gWin32ButtonNodeHandler);
}
