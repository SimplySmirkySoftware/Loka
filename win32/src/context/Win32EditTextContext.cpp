#include "Win32EditTextContext.hpp"
#include "Win32FocusParticipant.hpp"
#include <cassert>
#include "../Win32ScenePlatformController.hpp"
#include "app/layout/FallbackControlMetrics.hpp"
#include "app/scene/boundary/Boundary.hpp"
#include "app/scene/projection/RetainedNodeHandler.hpp"
#include <vector>
#include "app/nodes/controls/EditText.hpp"
#include "core/State.hpp"
#include "Win32EditTextBridge.hpp"

namespace
{
  class Win32EditTextNodeHandler
      : public loka::app::scene::RetainedNodeHandler<Win32EditTextNodeHandler,
                                                     loka::app::EditTextNode,
                                                     Win32EditTextContext>
  {
  public:
    static loka::app::EditTextNode *cast(loka::app::scene::Node *node)
    {
      return node ? node->asEditTextNode() : 0;
    }

    static Win32EditTextContext *create(loka::app::EditTextNode *edit,
                                        loka::app::scene::IPlatformController *controller,
                                        const loka::app::scene::LayoutState &state)
    {
      Win32ScenePlatformController *win32 = static_cast<Win32ScenePlatformController *>(controller);
      return new Win32EditTextContext(
          win32, win32->projectionParentHwnd(), state.x, state.y, state.width, state.height, edit);
    }

    static void refresh(Win32EditTextContext *ctx, const loka::app::scene::LayoutState &state)
    {
      ctx->relayout(state.x, state.y, state.width, state.height);
    }
  };

  Win32EditTextNodeHandler gWin32EditTextNodeHandler;
} // namespace

Win32EditTextContext::Win32EditTextContext(Win32ScenePlatformController *controller,
                                           HWND parent,
                                           int x,
                                           int y,
                                           int width,
                                           int height,
                                           loka::app::EditTextNode *node)
    : Win32RetirableContext(controller),
      node_(node),
      textState_(0),
      applyingFromState_(false),
      updatingFromControl_(false),
      textDelivery_(loka::app::scene::PaintAnswer::refused(loka::app::scene::PAINT_REFUSED_HISTORY_UNKNOWN))
{
  this->createNativeChildWindow(
      loka::win32::EditTextControlExStyle(),
      L"EDIT",
      L"",
      loka::win32::EditTextControlStyle(),
      this->controller()->displayScale().projectFrame(loka::core::Frame(x, y, width, height)),
      parent,
      NULL,
      GetModuleHandleW(NULL),
      NULL);
  if (this->hwnd())
  {
    SetWindowLongPtr(this->hwnd(), GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
  }
  bindText();
}

Win32EditTextContext::~Win32EditTextContext()
{
  assert(!this->hwnd() && "terminal fact delivery must queue the HWND before context reclaim");
}

/** EDIT owns the repaint caused by its submitted WM_SETTEXT. Native echoes
    and equal text need no additional damage; no framework erase/child request. */
loka::app::scene::PaintAnswer Win32EditTextContext::queryPaintDamage(const loka::app::scene::PaintQuery &query) const
{
  using namespace loka::app::scene;
  if (!this->hwnd())
    return PaintAnswer::refused(PAINT_REFUSED_NO_CONTEXT);
  if (query.placement != PLACEMENT_ELIGIBLE)
    return PaintAnswer::refused(PAINT_REFUSED_PLACEMENT_UNSETTLED);
  if (!this->node_ || this->node_->props.text_.state() != this->textState_)
    return PaintAnswer::refused(PAINT_REFUSED_PROPS_UNRECONCILED);
  PaintAnswer answer = this->textDelivery_;
  if (answer.kind == PAINT_ANSWER_EXACT)
    answer.damage.scope = query.scope;
  return answer;
}

void Win32EditTextContext::readLifecycleFactOnAttach()
{
  if (this->node_ && this->node_->lifecycleFact() == loka::app::scene::NODE_FACT_ATTACHED)
  {
    this->applyAttachedPresentation();
  }
}

void Win32EditTextContext::onNativeFact(loka::app::scene::NodeLifecycleFact previous,
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
    this->textDelivery_ = loka::app::scene::PaintAnswer::refused(loka::app::scene::PAINT_REFUSED_HISTORY_UNKNOWN);
    this->applyDetachedPresentation();
    if (next == loka::app::scene::NODE_FACT_RETIRED)
    {
      this->unbindText();
      this->node_ = 0;
    }
  }
}

void Win32EditTextContext::applyAttachedPresentation()
{
  if (this->hwnd())
  {
    // A refused property leaves the native control unmarked; reads report none.
    Win32FocusParticipant::attach(this->hwnd(), this);
    ShowWindow(this->hwnd(), SW_SHOW);
  }
}

void Win32EditTextContext::applyDetachedPresentation()
{
  if (this->hwnd())
  {
    Win32FocusParticipant::detach(this->hwnd());
    ShowWindow(this->hwnd(), SW_HIDE);
  }
}

bool Win32EditTextContext::handleCommand(WPARAM wParam, LPARAM)
{
#ifdef LOKA_LIFECYCLE_AUDIT
  assert(this->controller()->borrowPhase().open());
#endif
  WORD code = HIWORD(wParam);
  if (code == EN_CHANGE)
  {
    if (!applyingFromState_)
    {
      syncStateFromControl();
    }
    return true;
  }
  return false;
}

short Win32EditTextContext::layout(loka::app::scene::IPlatformController *, loka::app::scene::LayoutState &state)
{
  this->relayout(state.x, state.y, state.width, loka::app::layout::FallbackControlMetrics::kEditTextHeight);
  state.height = static_cast<short>(loka::app::layout::FallbackControlMetrics::kEditTextHeight);
  return static_cast<short>(state.y + loka::app::layout::FallbackControlMetrics::kEditTextHeight);
}

void Win32EditTextContext::relayout(int x, int y, int width, int height)
{
  if (!this->hwnd())
  {
    return;
  }
  this->positionNativeWindow(this->hwnd(),
                             this->controller()->displayScale().projectFrame(loka::core::Frame(x, y, width, height)));
}

void Win32EditTextContext::bindText()
{
  if (!node_)
  {
    return;
  }
  textState_ = node_->props.text_.state();
  textSeat_ = node_->props.text_;
  if (textState_)
  {
    textState_->bind(&Win32EditTextContext::TextChangedThunk, this, true);
  }
}

void Win32EditTextContext::unbindText()
{
  if (textState_)
  {
    textState_->unbind(&Win32EditTextContext::TextChangedThunk, this);
    textState_ = 0;
    textSeat_ = loka::app::scene::WriteSeat<loka::core::String>();
  }
}

void Win32EditTextContext::applyText()
{
  using namespace loka::app::scene;
  this->textDelivery_ = PaintAnswer::refused(PAINT_REFUSED_PROPS_UNRECONCILED);
  const PaintDamage empty = {paintScope(), 0, 0, 0, 0, PAINT_COVERAGE_PAINT_ONLY};
  if (!this->hwnd() || !textState_)
  {
    return;
  }
  if (updatingFromControl_)
  {
    this->textDelivery_ = PaintAnswer::exact(empty);
    return;
  }
  std::wstring currentText;
  loka::win32::ReadEditTextWide(this->hwnd(), currentText);
  std::wstring desired;
  if (!loka::win32::MaterializeWideString(textState_->get(), desired))
  {
    desired.clear();
  }
  if (currentText == desired)
  {
    this->textDelivery_ = PaintAnswer::exact(empty);
    return;
  }
  DWORD selStart = 0;
  DWORD selEnd = 0;
  SendMessageW(this->hwnd(), EM_GETSEL, reinterpret_cast<WPARAM>(&selStart), reinterpret_cast<LPARAM>(&selEnd));
  applyingFromState_ = true;
  const bool submitted = SetWindowTextW(this->hwnd(), desired.c_str()) != FALSE;
  SendMessageW(this->hwnd(), EM_SETSEL, selStart, selEnd);
  applyingFromState_ = false;
  if (submitted)
    this->textDelivery_ = PaintAnswer::nativeScheduled();
}

void Win32EditTextContext::syncStateFromControl()
{
  if (!textState_ || !this->hwnd())
  {
    return;
  }
  updatingFromControl_ = true;
  textSeat_.set(loka::win32::ReadEditTextString(this->hwnd()), true);
  updatingFromControl_ = false;
}

void Win32EditTextContext::TextChangedThunk(void *userData)
{
  Win32EditTextContext *self = static_cast<Win32EditTextContext *>(userData);
  if (self)
  {
    self->applyText();
  }
}

void RegisterWin32EditTextNodeHandler(loka::app::scene::PlatformNodeHandlerRegistry &registry)
{
  registry.registerHandler(&gWin32EditTextNodeHandler);
}
