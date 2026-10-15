#include "Win32PopupMenuContext.hpp"
#include <cassert>
#include "../Win32ScenePlatformController.hpp"
#include "app/layout/FallbackControlMetrics.hpp"
#include "app/scene/boundary/Boundary.hpp"
#include "app/scene/projection/RetainedNodeHandler.hpp"
#include "platform/Win32String.hpp"
#include <string>
#include <tchar.h>

namespace
{
  class Win32PopupMenuNodeHandler
      : public loka::app::scene::RetainedNodeHandler<Win32PopupMenuNodeHandler,
                                                     loka::app::PopupMenuNode,
                                                     Win32PopupMenuContext>
  {
  public:
    static loka::app::PopupMenuNode *cast(loka::app::scene::Node *node)
    {
      return node ? node->asPopupMenuNode() : 0;
    }

    static Win32PopupMenuContext *create(loka::app::PopupMenuNode *popup,
                                         loka::app::scene::IPlatformController *controller,
                                         const loka::app::scene::LayoutState &state)
    {
      Win32ScenePlatformController *win32 = static_cast<Win32ScenePlatformController *>(controller);
      return new Win32PopupMenuContext(
          win32, win32->projectionParentHwnd(), state.x, state.y, state.width, state.height, popup);
    }

    static void refresh(Win32PopupMenuContext *ctx, const loka::app::scene::LayoutState &state)
    {
      ctx->relayout(state.x, state.y, state.width, state.height);
    }
  };

  Win32PopupMenuNodeHandler gWin32PopupMenuNodeHandler;
} // namespace

Win32PopupMenuContext::Win32PopupMenuContext(Win32ScenePlatformController *controller,
                                             HWND parent,
                                             int x,
                                             int y,
                                             int width,
                                             int height,
                                             loka::app::PopupMenuNode *node)
    : Win32RetirableContext(controller),
      node_(node),
      selectionState_(0),
      enabledState_(0),
      appliedItems_(),
      applyingFromState_(false),
      updatingFromControl_(false),
      baseHeight_(height),
      baseWidth_(width),
      controlDelivery_(loka::app::scene::PaintAnswer::refused(loka::app::scene::PAINT_REFUSED_HISTORY_UNKNOWN))
{
  // Unicode window: CB_ADDSTRING sent via SendMessageW would otherwise be
  // thunked through the system ACP by an ANSI combo box, losing out-of-ACP
  // characters.
  this->createNativeChildWindow(
      0,
      L"COMBOBOX",
      L"",
      WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
      this->controller()->displayScale().projectFrame(loka::core::Frame(x, y, width, height)),
      parent,
      0,
      GetModuleHandleW(NULL),
      NULL);
  if (this->hwnd())
  {
    SetWindowLongPtr(this->hwnd(), GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
  }

  applyItems();
  bindSelection();
  bindEnabled();
}

Win32PopupMenuContext::~Win32PopupMenuContext()
{
  assert(!this->hwnd() && "terminal fact delivery must queue the HWND before context reclaim");
}

/** COMBOBOX owns repaint after CB_SETCURSEL, EnableWindow, and item submission.
    Equal selection/enabled applies owe no new damage; no framework erase/child request. */
loka::app::scene::PaintAnswer Win32PopupMenuContext::queryPaintDamage(const loka::app::scene::PaintQuery &query) const
{
  using namespace loka::app::scene;
  if (!this->hwnd())
    return PaintAnswer::refused(PAINT_REFUSED_NO_CONTEXT);
  if (query.placement != PLACEMENT_ELIGIBLE)
    return PaintAnswer::refused(PAINT_REFUSED_PLACEMENT_UNSETTLED);
  if (!this->node_ || this->node_->props.selectedIndex_.state() != this->selectionState_
      || this->node_->props.enabled_ != this->enabledState_)
    return PaintAnswer::refused(PAINT_REFUSED_PROPS_UNRECONCILED);
  PaintAnswer answer = this->controlDelivery_;
  if (answer.kind == PAINT_ANSWER_EXACT)
    answer.damage.scope = query.scope;
  return answer;
}

void Win32PopupMenuContext::readLifecycleFactOnAttach()
{
  if (this->node_ && this->node_->lifecycleFact() == loka::app::scene::NODE_FACT_ATTACHED)
  {
    this->applyAttachedPresentation();
  }
}

void Win32PopupMenuContext::onNativeFact(loka::app::scene::NodeLifecycleFact previous,
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
    this->controlDelivery_ = loka::app::scene::PaintAnswer::refused(loka::app::scene::PAINT_REFUSED_HISTORY_UNKNOWN);
    this->applyDetachedPresentation();
    if (next == loka::app::scene::NODE_FACT_RETIRED)
    {
      this->unbindSelection();
      this->unbindEnabled();
      this->node_ = 0;
    }
  }
}

void Win32PopupMenuContext::applyAttachedPresentation()
{
  if (this->hwnd())
  {
    ShowWindow(this->hwnd(), SW_SHOW);
  }
}

void Win32PopupMenuContext::applyDetachedPresentation()
{
  if (this->hwnd())
  {
    ShowWindow(this->hwnd(), SW_HIDE);
  }
}

bool Win32PopupMenuContext::handleCommand(WPARAM, LPARAM)
{
#ifdef LOKA_LIFECYCLE_AUDIT
  assert(this->controller()->borrowPhase().open());
#endif
  if (!applyingFromState_)
  {
    syncStateFromControl();
  }
  return true;
}

short Win32PopupMenuContext::layout(loka::app::scene::IPlatformController *, loka::app::scene::LayoutState &state)
{
  this->relayout(state.x, state.y, state.width, loka::app::layout::FallbackControlMetrics::kPopupMenuHeight);
  state.height = static_cast<short>(loka::app::layout::FallbackControlMetrics::kPopupMenuHeight);
  return static_cast<short>(state.y + loka::app::layout::FallbackControlMetrics::kPopupMenuHeight
                            + loka::app::layout::FallbackControlMetrics::kVerticalSpacing);
}

void Win32PopupMenuContext::relayout(int x, int y, int width, int height)
{
  if (!this->hwnd())
  {
    return;
  }
  baseWidth_ = width;
  baseHeight_ = height;
  if (!this->itemsMatchApplied())
  {
    this->applyItems();
    this->applySelection();
  }
  this->positionNativeWindow(
      this->hwnd(), this->controller()->displayScale().projectFrame(loka::core::Frame(x, y, width, this->dropHeight())));
}

void Win32PopupMenuContext::bindSelection()
{
  if (!node_)
  {
    return;
  }
  selectionState_ = node_->props.selectedIndex_.state();
  selectionSeat_ = node_->props.selectedIndex_;
  if (selectionState_)
  {
    selectionState_->bind(&Win32PopupMenuContext::SelectionChangedThunk, this, true);
    applySelection();
  }
}

void Win32PopupMenuContext::unbindSelection()
{
  if (selectionState_)
  {
    selectionState_->unbind(&Win32PopupMenuContext::SelectionChangedThunk, this);
    selectionState_ = 0;
    selectionSeat_ = loka::app::scene::WriteSeat<int>();
  }
}

void Win32PopupMenuContext::bindEnabled()
{
  if (!node_)
  {
    return;
  }
  enabledState_ = static_cast<loka::core::State<bool> *>(node_->props.enabled_);
  if (enabledState_)
  {
    enabledState_->bind(&Win32PopupMenuContext::EnabledChangedThunk, this, true);
    applyEnabled();
  }
}

void Win32PopupMenuContext::unbindEnabled()
{
  if (enabledState_)
  {
    enabledState_->unbind(&Win32PopupMenuContext::EnabledChangedThunk, this);
    enabledState_ = 0;
  }
}

bool Win32PopupMenuContext::itemsMatchApplied() const
{
  const loka::Vector<loka::core::String> *items = node_ ? node_->props.items_ : 0;
  if (!items)
  {
    return this->appliedItems_.empty();
  }
  return loka::app::PopupMenuProps::compareItems(&this->appliedItems_, items) == 0;
}

void Win32PopupMenuContext::applyItems()
{
  using namespace loka::app::scene;
  this->controlDelivery_ = PaintAnswer::refused(PAINT_REFUSED_PROPS_UNRECONCILED);
  if (!this->hwnd() || !node_)
  {
    return;
  }
  const LRESULT previousCount = SendMessageW(this->hwnd(), CB_GETCOUNT, 0, 0);
  const PaintDamage empty = {paintScope(), 0, 0, 0, 0, PAINT_COVERAGE_PAINT_ONLY};
  SendMessageW(this->hwnd(), CB_RESETCONTENT, 0, 0);
  const loka::Vector<loka::core::String> *items = node_->props.items_;
  if (!items)
  {
    this->appliedItems_.clear();
    this->controlDelivery_ = previousCount == 0 ? PaintAnswer::exact(empty) : PaintAnswer::nativeScheduled();
    this->applyDropGeometry();
    return;
  }
  bool appliedAllItems = true;
  for (std::size_t i = 0; i < items->size(); ++i)
  {
    std::wstring wide;
    if (!loka::win32::MaterializeWideString((*items)[i], wide))
    {
      appliedAllItems = false;
      break;
    }
    const LRESULT added = SendMessageW(this->hwnd(), CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(wide.c_str()));
    if (added == CB_ERR || added == CB_ERRSPACE)
    {
      appliedAllItems = false;
      break;
    }
  }
  if (appliedAllItems)
  {
    this->appliedItems_ = *items;
    this->controlDelivery_ =
        previousCount == 0 && items->empty() ? PaintAnswer::exact(empty) : PaintAnswer::nativeScheduled();
  }
  else
  {
    // An empty applied-state cache makes the next relayout retry a non-empty
    // desired list instead of treating a partially populated HWND as current.
    this->appliedItems_.clear();
  }
  this->applyDropGeometry();
}

int Win32PopupMenuContext::dropHeight() const
{
  int itemHeight = static_cast<int>(SendMessage(this->hwnd(), CB_GETITEMHEIGHT, 0, 0));
  if (itemHeight <= 0)
  {
    itemHeight = baseHeight_ > 0 ? baseHeight_ : 18;
  }
  else if (this->controller())
  {
    itemHeight = this->controller()->displayScale().measurementToLu(itemHeight);
  }
  const loka::Vector<loka::core::String> *items = node_ ? node_->props.items_ : 0;
  int visibleItems = items ? static_cast<int>(items->size()) : 0;
  if (visibleItems > 8)
  {
    visibleItems = 8;
  }
  int result = baseHeight_ + itemHeight * visibleItems + 2;
  if (result < baseHeight_)
  {
    result = baseHeight_;
  }
  return result;
}

void Win32PopupMenuContext::applyDropGeometry()
{
  if (!this->hwnd())
  {
    return;
  }
  const loka::win32::Win32DisplayScale &scale = this->controller()->displayScale();
  this->resizeNativeWindow(this->hwnd(), scale.projectFrame(loka::core::Frame(0, 0, baseWidth_, this->dropHeight())));
}

void Win32PopupMenuContext::applySelection()
{
  using namespace loka::app::scene;
  this->controlDelivery_ = PaintAnswer::refused(PAINT_REFUSED_PROPS_UNRECONCILED);
  if (!this->hwnd() || !selectionState_)
  {
    return;
  }
  int index = selectionState_->get();
  if (index < 0)
  {
    index = -1;
  }
  const PaintDamage empty = {paintScope(), 0, 0, 0, 0, PAINT_COVERAGE_PAINT_ONLY};
  if (SendMessageW(this->hwnd(), CB_GETCURSEL, 0, 0) == index)
  {
    this->controlDelivery_ = PaintAnswer::exact(empty);
    return;
  }
  applyingFromState_ = true;
  const LRESULT selected = SendMessageW(this->hwnd(), CB_SETCURSEL, static_cast<WPARAM>(index), 0);
  applyingFromState_ = false;
  // CB_ERR is the successful deselection result for index -1 as well.
  if (selected != CB_ERR || index == -1)
    this->controlDelivery_ = PaintAnswer::nativeScheduled();
}

void Win32PopupMenuContext::applyEnabled()
{
  using namespace loka::app::scene;
  this->controlDelivery_ = PaintAnswer::refused(PAINT_REFUSED_PROPS_UNRECONCILED);
  if (!this->hwnd() || !enabledState_)
  {
    return;
  }
  const bool enabled = this->enabledState_->get();
  if ((IsWindowEnabled(this->hwnd()) != FALSE) == enabled)
  {
    const PaintDamage empty = {paintScope(), 0, 0, 0, 0, PAINT_COVERAGE_PAINT_ONLY};
    this->controlDelivery_ = PaintAnswer::exact(empty);
    return;
  }
  EnableWindow(this->hwnd(), enabled ? TRUE : FALSE);
  this->controlDelivery_ = PaintAnswer::nativeScheduled();
}

void Win32PopupMenuContext::syncStateFromControl()
{
  if (!this->hwnd() || !selectionState_)
  {
    return;
  }
  updatingFromControl_ = true;
  LRESULT index = SendMessage(this->hwnd(), CB_GETCURSEL, 0, 0);
  selectionSeat_.set(static_cast<int>(index), true);
  updatingFromControl_ = false;
  if (node_ && node_->props.onChange_)
  {
    node_->props.onChange_->emit();
  }
}

void Win32PopupMenuContext::SelectionChangedThunk(void *userData)
{
  Win32PopupMenuContext *self = static_cast<Win32PopupMenuContext *>(userData);
  if (self)
  {
    self->applySelection();
  }
}

void Win32PopupMenuContext::EnabledChangedThunk(void *userData)
{
  Win32PopupMenuContext *self = static_cast<Win32PopupMenuContext *>(userData);
  if (self)
  {
    self->applyEnabled();
  }
}

void RegisterWin32PopupMenuNodeHandler(loka::app::scene::PlatformNodeHandlerRegistry &registry)
{
  registry.registerHandler(&gWin32PopupMenuNodeHandler);
}
