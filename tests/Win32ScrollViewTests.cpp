#include "app/layout/FallbackControlMetrics.hpp"
#include "Win32ScrollViewTests.hpp"

#include "Win32SceneLayout.hpp"
#include "support/TestVerify.hpp"

#include <cassert>
#include <cstdio>
#include <cwchar>
#include <vector>
#include <windows.h>

#include "Win32BuiltInSupport.hpp"
#include "Win32Window.hpp"
#include "context/Win32EditTextBridge.hpp"
#include "platform/null/NullPlatformContext.hpp"
#include "support/WindowAdmissionTestApp.hpp"
#include "core/util/StateTrackerGuard.hpp"
#include "Win32ScenePlatformController.hpp"
#include "app/nodes/controls/Button.hpp"
#include "app/nodes/nestable/RowColumn.hpp"
#include "app/nodes/nestable/ScrollView.hpp"
#include "app/scene/state/NodeState.hpp"
#include "core/State.hpp"
#include "core/StateTracker.hpp"
#include <climits>
#include "app/nodes/Text.hpp"
#include "app/nodes/nestable/Box.hpp"
#include "app/nodes/nestable/Fragment.hpp"
#include "app/ConfirmDialog.hpp"
#include "context/Win32TextContext.hpp"
#include "testing/scene/SceneTestFlow.hpp"
#include "app/nodes/nestable/Show.hpp"

namespace
{
  class OffsetFact
  {
  public:
    explicit OffsetFact(int initial)
        : storage_(initial),
          tracker_(),
          state_(&this->storage_, &this->tracker_),
          changeCount_(0)
    {
      this->tracker_.addState(&this->storage_);
      this->storage_.bind(&OffsetFact::ChangedThunk, this, false);
    }

    ~OffsetFact()
    {
      this->storage_.unbind(&OffsetFact::ChangedThunk, this);
    }

    loka::app::scene::NodeState<int> &state()
    {
      return this->state_;
    }

    int changeCount() const
    {
      return this->changeCount_;
    }

    void resetChangeCount()
    {
      this->changeCount_ = 0;
    }

  private:
    static void ChangedThunk(void *userData)
    {
      OffsetFact *self = static_cast<OffsetFact *>(userData);
      if (self)
      {
        ++self->changeCount_;
      }
    }

    loka::core::MutableState<int> storage_;
    loka::core::PushStateTracker tracker_;
    loka::app::scene::NodeState<int> state_;
    int changeCount_;

    OffsetFact(const OffsetFact &);
    OffsetFact &operator=(const OffsetFact &);
  };

  struct ClassWindowQuery
  {
    explicit ClassWindowQuery(const wchar_t *className)
        : className_(className),
          hwnd_(0)
    {
    }

    const wchar_t *className_;
    HWND hwnd_;
  };

  BOOL CALLBACK FindClassWindowThunk(HWND hwnd, LPARAM lParam)
  {
    ClassWindowQuery *query = reinterpret_cast<ClassWindowQuery *>(lParam);
    wchar_t className[64];
    const int length = GetClassNameW(
        hwnd, className, sizeof(className) / sizeof(className[0]));
    if (!query->hwnd_ && length > 0 &&
        std::wcscmp(className, query->className_) == 0)
    {
      query->hwnd_ = hwnd;
    }
    return TRUE;
  }

  HWND findChildWindowByClass(HWND root, const wchar_t *className)
  {
    ClassWindowQuery query(className);
    LOKA_VERIFY(EnumChildWindows(
        root, FindClassWindowThunk, reinterpret_cast<LPARAM>(&query)));
    return query.hwnd_;
  }

  struct DirectChildQuery
  {
    DirectChildQuery(HWND parent,
                     const wchar_t *className,
                     std::vector<HWND> &out)
        : parent_(parent),
          className_(className),
          out_(out)
    {
    }

    HWND parent_;
    const wchar_t *className_;
    std::vector<HWND> &out_;
  };

  BOOL CALLBACK CollectDirectChildrenThunk(HWND hwnd, LPARAM lParam)
  {
    DirectChildQuery *query = reinterpret_cast<DirectChildQuery *>(lParam);
    wchar_t className[64];
    const int length = GetClassNameW(
        hwnd, className, sizeof(className) / sizeof(className[0]));
    if (GetParent(hwnd) == query->parent_ && length > 0 &&
        std::wcscmp(className, query->className_) == 0)
    {
      query->out_.push_back(hwnd);
    }
    return TRUE;
  }

  std::vector<HWND> directChildWindowsByClass(HWND parent,
                                               const wchar_t *className)
  {
    std::vector<HWND> result;
    DirectChildQuery query(parent, className, result);
    LOKA_VERIFY(EnumChildWindows(
        parent, CollectDirectChildrenThunk, reinterpret_cast<LPARAM>(&query)));
    return result;
  }

  RECT childRectInParent(HWND child, HWND parent)
  {
    RECT rect;
    LOKA_VERIFY(GetWindowRect(child, &rect));
    POINT topLeft = {rect.left, rect.top};
    POINT bottomRight = {rect.right, rect.bottom};
    LOKA_VERIFY(ScreenToClient(parent, &topLeft));
    LOKA_VERIFY(ScreenToClient(parent, &bottomRight));
    RECT result = {topLeft.x, topLeft.y, bottomRight.x, bottomRight.y};
    return result;
  }

  SCROLLINFO scrollInfo(HWND viewport)
  {
    SCROLLINFO info;
    ZeroMemory(&info, sizeof(info));
    info.cbSize = sizeof(info);
    info.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    LOKA_VERIFY(GetScrollInfo(viewport, SB_VERT, &info));
    return info;
  }

  int maximumOffset(const SCROLLINFO &info)
  {
    int maximum = info.nMax;
    if (info.nPage > 0)
    {
      maximum -= static_cast<int>(info.nPage - 1);
    }
    return maximum < info.nMin ? info.nMin : maximum;
  }

  HWND createHostWindow()
  {
    return CreateWindowExW(0,
                           L"STATIC",
                           L"scroll-view-host",
                           WS_OVERLAPPED,
                           0,
                           0,
                           360,
                           280,
                           NULL,
                           NULL,
                           GetModuleHandleW(NULL),
                           NULL);
  }

  void CountClickThunk(void *userData)
  {
    ++*static_cast<int *>(userData);
  }

  loka::app::StackNode *addButtonColumn(loka::app::ScrollViewNode &scrollView,
                                         int buttonCount,
                                         loka::core::EmitterState *firstOnClick = 0)
  {
    loka::app::StackNode *column =
        new loka::app::StackNode((loka::app::StackProps(loka::app::STACK_AXIS_COLUMN)));
    for (int i = 0; i < buttonCount; ++i)
    {
      loka::app::ButtonProps props;
      if (i == 0 && firstOnClick)
      {
        props.onClick(firstOnClick);
      }
      column->addChild(new loka::app::ButtonNode(props));
    }
    scrollView.addChild(column);
    return column;
  }

  void establishLayout(Win32ScenePlatformController &controller,
                       loka::app::scene::Node *root,
                       int width,
                       int height)
  {
    controller.onChange(root,
                        loka::app::scene::NODE_DIRTY_NONE,
                        false);
    controller.relayout(width, height);
  }
} // namespace

void testWin32ScrollViewParentsAndClipsProjectedChildren()
{
  std::printf("\n==== [testWin32ScrollViewParentsAndClipsProjectedChildren] start ====\n");
  HWND root = createHostWindow();
  LOKA_VERIFY(root);
  {
    Win32ScenePlatformController controller(root, loka::win32::Win32DisplayScale(96, loka::app::RailMetrics()));
    RegisterWin32BuiltInSupport(controller);
    const int contentHeight = 6 * controller.rowLayoutMetrics().buttonHeight + 5 * Win32SceneLayout::kGap;
    // Nodes are declared after the controller so they leave scope first:
    // ~Node is the last retire door, and the controller must still be alive
    // to receive the queued HWNDs (the ensure-contract convention; a node
    // that instead dies after the controller is torn down by
    // clearNodeContexts' releaseContext, which does not retire).
    OffsetFact offset(10);
    loka::app::ScrollViewNode scrollView(
        (loka::app::ScrollViewProps(offset.state())));
    addButtonColumn(scrollView, 6);
    establishLayout(controller, &scrollView, 300, 140);

    HWND viewport = findChildWindowByClass(root, L"LOKA_SCROLL_VIEW");
    LOKA_VERIFY(viewport);
    assert(GetParent(viewport) == root);
    const LONG_PTR viewportStyle = GetWindowLongPtr(viewport, GWL_STYLE);
    assert((viewportStyle & WS_VSCROLL) != 0);
    // #1199: transparent children need the parent's ground beneath them;
    // child parentage, checked below, provides structural clipping (#544).
    LOKA_VERIFY((viewportStyle & WS_CLIPCHILDREN) == 0);
    const LONG_PTR viewportExStyle = GetWindowLongPtr(viewport, GWL_EXSTYLE);
    assert((viewportExStyle & WS_EX_CONTROLPARENT) != 0 &&
           "IsDialogMessage only recurses into control parents; Tab must not skip the viewport's children");

    std::vector<HWND> buttons =
        directChildWindowsByClass(viewport, L"Button");
    assert(buttons.size() == 6);
    for (std::size_t i = 0; i < buttons.size(); ++i)
    {
      assert(GetParent(buttons[i]) == viewport &&
             "projected controls must be structurally clipped by the viewport HWND");
    }

    RECT viewportClient;
    LOKA_VERIFY(GetClientRect(viewport, &viewportClient));
    int maximumChildBottom = viewportClient.top;
    for (std::size_t i = 0; i < buttons.size(); ++i)
    {
      const RECT childRect = childRectInParent(buttons[i], viewport);
      if (childRect.bottom > maximumChildBottom)
      {
        maximumChildBottom = childRect.bottom;
      }
    }
    assert(maximumChildBottom > viewportClient.bottom &&
           "the tall subtree must extend beyond the viewport and rely on parent clipping");

    RECT first = childRectInParent(buttons[0], viewport);
    RECT second = childRectInParent(buttons[1], viewport);
    if (second.top < first.top)
    {
      const RECT swap = first;
      first = second;
      second = swap;
    }
    for (std::size_t i = 2; i < buttons.size(); ++i)
    {
      const RECT candidate = childRectInParent(buttons[i], viewport);
      if (candidate.top < first.top)
      {
        second = first;
        first = candidate;
      }
      else if (candidate.top < second.top)
      {
        second = candidate;
      }
    }
    assert(first.left == 0 && first.top == -10);
    assert(second.top == controller.rowLayoutMetrics().buttonHeight + Win32SceneLayout::kGap - 10
           && "content y must project as viewport-client y minus offset");

    const SCROLLINFO info = scrollInfo(viewport);
    assert(info.nPage == 140 - 2 * Win32SceneLayout::kRootInset);
    assert(maximumOffset(info) == contentHeight - (140 - 2 * Win32SceneLayout::kRootInset));
    assert(info.nPos == 10);

    // Release the root pointer before the nodes leave scope so the
    // controller's own teardown does not walk destroyed nodes.
    controller.onChange(0, loka::app::scene::NODE_DIRTY_NONE, false);
  }
  LOKA_VERIFY(DestroyWindow(root));
  std::printf("==== [testWin32ScrollViewParentsAndClipsProjectedChildren] PASSED ====\n");
}

void testWin32ScrollViewOffsetIsRelayoutInput()
{
  std::printf("\n==== [testWin32ScrollViewOffsetIsRelayoutInput] start ====\n");
  HWND root = createHostWindow();
  LOKA_VERIFY(root);
  {
    Win32ScenePlatformController controller(root, loka::win32::Win32DisplayScale(96, loka::app::RailMetrics()));
    RegisterWin32BuiltInSupport(controller);
    // Nodes after the controller: see the parents-and-clips pin.
    OffsetFact offset(0);
    loka::app::ScrollViewNode scrollView(
        (loka::app::ScrollViewProps(offset.state())));
    addButtonColumn(scrollView, 6);
    establishLayout(controller, &scrollView, 300, 140);
    HWND viewport = findChildWindowByClass(root, L"LOKA_SCROLL_VIEW");
    LOKA_VERIFY(viewport);
    std::vector<HWND> buttons = directChildWindowsByClass(viewport, L"Button");
    assert(!buttons.empty());
    const RECT atZero = childRectInParent(buttons[0], viewport);

    offset.state().set(7);
    controller.relayout(300, 140);
    const RECT afterChange = childRectInParent(buttons[0], viewport);
    const SCROLLINFO afterChangeInfo = scrollInfo(viewport);
    controller.relayout(300, 140);
    const RECT afterUnchangedRelayout = childRectInParent(buttons[0], viewport);

    assert(afterChange.top == atZero.top - 7);
    assert(afterChangeInfo.nPos == 7);
    assert(afterUnchangedRelayout.left == afterChange.left);
    assert(afterUnchangedRelayout.top == afterChange.top);
    assert(afterUnchangedRelayout.right == afterChange.right);
    assert(afterUnchangedRelayout.bottom == afterChange.bottom);

    controller.onChange(0, loka::app::scene::NODE_DIRTY_NONE, false);
  }
  LOKA_VERIFY(DestroyWindow(root));
  std::printf("==== [testWin32ScrollViewOffsetIsRelayoutInput] PASSED ====\n");
}

void testWin32ScrollViewMessagePublishesOffsetFact()
{
  std::printf("\n==== [testWin32ScrollViewMessagePublishesOffsetFact] start ====\n");
  HWND root = createHostWindow();
  LOKA_VERIFY(root);
  {
    Win32ScenePlatformController controller(root, loka::win32::Win32DisplayScale(96, loka::app::RailMetrics()));
    RegisterWin32BuiltInSupport(controller);
    // Nodes after the controller: see the parents-and-clips pin.
    loka::core::EmitterState firstClick;
    OffsetFact offset(0);
    loka::app::ScrollViewNode scrollView(
        (loka::app::ScrollViewProps(offset.state())));
    addButtonColumn(scrollView, 6, &firstClick);
    establishLayout(controller, &scrollView, 300, 140);
    HWND viewport = findChildWindowByClass(root, L"LOKA_SCROLL_VIEW");
    LOKA_VERIFY(viewport);
    std::vector<HWND> buttons = directChildWindowsByClass(viewport, L"Button");
    assert(!buttons.empty());
    const RECT before = childRectInParent(buttons[0], viewport);

    // A control parented to the viewport sends WM_COMMAND to the viewport,
    // not the root window; the viewport forwards it through the same
    // controller door Win32Window::WndProc uses, so the click still reaches
    // the Loka handler.
    int clicks = 0;
    firstClick.bind(&CountClickThunk, &clicks, false);
    SendMessageW(buttons[0], BM_CLICK, 0, 0);
    LOKA_VERIFY(clicks == 1 &&
                "a click inside the viewport must reach the Loka handler");

    SendMessageW(viewport, WM_VSCROLL, MAKEWPARAM(SB_LINEDOWN, 0), 0);
    assert(offset.state().get() == 16 &&
           "WM_VSCROLL must publish through the ScrollView NodeState door");
    controller.relayout(300, 140);
    const RECT after = childRectInParent(buttons[0], viewport);
    assert(after.top == before.top - 16);

    SendMessageW(viewport, WM_VSCROLL, MAKEWPARAM(SB_THUMBTRACK, 10), 0);
    assert(offset.state().get() == 16 &&
           "thumb tracking is visual-only until the value settles");
    assert(scrollInfo(viewport).nPos == 10);
    SendMessageW(viewport, WM_VSCROLL, MAKEWPARAM(SB_THUMBPOSITION, 10), 0);
    assert(offset.state().get() == 10 &&
           "thumb position must publish the settled fact");
    controller.relayout(300, 140);
    SendMessageW(viewport, WM_VSCROLL, MAKEWPARAM(SB_THUMBTRACK, 11), 0);
    assert(offset.state().get() == 10);
    SendMessageW(viewport, WM_VSCROLL, MAKEWPARAM(SB_ENDSCROLL, 0), 0);
    assert(offset.state().get() == 11 &&
           "end scroll must publish the last visual thumb position");
    controller.relayout(300, 140);

    // A click on an arrow or on the track sends its step and then, on release,
    // SB_ENDSCROLL, with no layout pass in between: the step must survive it.
    offset.state().set(0);
    controller.relayout(300, 140);
    SendMessageW(viewport, WM_VSCROLL, MAKEWPARAM(SB_LINEDOWN, 0), 0);
    SendMessageW(viewport, WM_VSCROLL, MAKEWPARAM(SB_ENDSCROLL, 0), 0);
    assert(offset.state().get() == 16 && "an arrow click's end scroll must keep its line step");
    const SCROLLINFO clickInfo = scrollInfo(viewport);
    const int clickMaximum = maximumOffset(clickInfo);
    const int clickPage =
        clickInfo.nPage > static_cast<UINT>(clickMaximum) ? clickMaximum : static_cast<int>(clickInfo.nPage);
    SendMessageW(viewport, WM_VSCROLL, MAKEWPARAM(SB_PAGEDOWN, 0), 0);
    SendMessageW(viewport, WM_VSCROLL, MAKEWPARAM(SB_ENDSCROLL, 0), 0);
    assert(offset.state().get() == (16 > clickMaximum - clickPage ? clickMaximum : 16 + clickPage)
           && "a track click's end scroll must keep its page step");
    assert(offset.state().get() > 16 && "the page step must move in this fixture");
    controller.relayout(300, 140);

    const int rangeEnd = maximumOffset(scrollInfo(viewport));
    offset.state().set(rangeEnd);
    controller.relayout(300, 140);
    SendMessageW(viewport, WM_VSCROLL, MAKEWPARAM(SB_LINEDOWN, 0), 0);
    assert(offset.state().get() == rangeEnd &&
           "line scrolling at the range end must clamp without another write");
    offset.state().set(rangeEnd - 3);
    SendMessageW(viewport, WM_VSCROLL, MAKEWPARAM(SB_LINEDOWN, 0), 0);
    assert(offset.state().get() == rangeEnd && "a partial final line must clamp to the maximum");
    offset.state().set(3);
    SendMessageW(viewport, WM_VSCROLL, MAKEWPARAM(SB_LINEUP, 0), 0);
    assert(offset.state().get() == 0 && "a partial first line must clamp to zero");

    controller.onChange(0, loka::app::scene::NODE_DIRTY_NONE, false);
  }
  LOKA_VERIFY(DestroyWindow(root));
  std::printf("==== [testWin32ScrollViewMessagePublishesOffsetFact] PASSED ====\n");
}

void testWin32ScrollViewWheelStepsOffsetThroughLinePath()
{
  std::printf("\n==== [testWin32ScrollViewWheelStepsOffsetThroughLinePath] start ====\n");
  NullPlatformContext platform;
  WindowProps props;
  props.frame(40, 40, 360, 280).visible(false);
  Win32Window window(&platform, props);
  {
    loka::core::StateTrackerGuard guard(window.getTracker());
    window.visibilityState().set(true, true);
  }
  WindowAdmissionTestApp admission(window);
  admission.flush();
  HWND root = window.hwnd();
  LOKA_VERIFY(root);
  {
    Win32ScenePlatformController controller(root, loka::win32::Win32DisplayScale(96, loka::app::RailMetrics()));
    RegisterWin32BuiltInSupport(controller);
    // Nodes after the controller: see the parents-and-clips pin.
    OffsetFact offset(0);
    loka::app::scene::NodeState<int> &offsetState = offset.state();
    loka::app::ScrollViewNode scrollView(
        (loka::app::ScrollViewProps(offset.state())));
    // Eight buttons keep the range past three wheel lines with the 6 lu gap.
    addButtonColumn(scrollView, 8);
    establishLayout(controller, &scrollView, 300, 140);
    HWND viewport = findChildWindowByClass(root, L"LOKA_SCROLL_VIEW");
    LOKA_VERIFY(viewport);
    const SCROLLINFO info = scrollInfo(viewport);
    const int maximum = maximumOffset(info);
    UINT lines = 3;
    if (!SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0))
    {
      lines = 3;
    }
    LOKA_VERIFY(maximum > 3 * 16 && "default wheel distance must not hit the fixture's range end");
    // Clamp before multiplication so even a large system line setting cannot overflow.
    const UINT distance = lines == WHEEL_PAGESCROLL ? info.nPage
                         : lines > static_cast<UINT>(maximum) / 16
                             ? static_cast<UINT>(maximum) : lines * 16;
    const int expected = distance > static_cast<UINT>(maximum)
                             ? maximum : static_cast<int>(distance);
    POINT point = {5, 5};
    LOKA_VERIFY(ClientToScreen(viewport, &point));
    const LPARAM position = MAKELPARAM(point.x, point.y);
    const WPARAM down = MAKEWPARAM(0, -WHEEL_DELTA);
    const WPARAM up = MAKEWPARAM(0, WHEEL_DELTA);
    LOKA_VERIFY(SendMessageW(viewport, WM_MOUSEWHEEL, down, position) == 0);
    LOKA_VERIFY(offsetState.get() == expected);
    controller.relayout(300, 140);
    const SCROLLINFO afterWheel = scrollInfo(viewport);
    LOKA_VERIFY(afterWheel.nPos == expected);
    SendMessageW(viewport, WM_MOUSEWHEEL, up, position);
    LOKA_VERIFY(offsetState.get() == 0);
    SendMessageW(viewport, WM_MOUSEWHEEL, MAKEWPARAM(0, -WHEEL_DELTA / 2), position);
    LOKA_VERIFY(offsetState.get() == 0 && "sub-detent remainders are dropped");

    offsetState.set(maximum);
    controller.relayout(300, 140);
    offset.resetChangeCount();
    SendMessageW(viewport, WM_MOUSEWHEEL, down, position);
    LOKA_VERIFY(offsetState.get() == maximum);
    const int clampedChanges = offset.changeCount();
    LOKA_VERIFY(clampedChanges == 0);
    offsetState.set(0);
    controller.relayout(300, 140);

    std::vector<HWND> buttons = directChildWindowsByClass(viewport, L"Button");
    LOKA_VERIFY(!buttons.empty());
    SetFocus(buttons[0]);
    LOKA_VERIFY(GetFocus() == buttons[0]);
    SendMessageW(GetFocus(), WM_MOUSEWHEEL, down, position);
    LOKA_VERIFY(offsetState.get() == expected && "focused Button must propagate to the viewport");
    offsetState.set(0);

    // Use the shipping single-line EditText style to exercise native default
    // propagation, without introducing a per-control subclass for the test.
    HWND edit = loka::win32::CreateEditTextControl(viewport, 10, 10, 80, 24);
    LOKA_VERIFY(edit);
    SetFocus(edit);
    LOKA_VERIFY(GetFocus() == edit);
    SendMessageW(GetFocus(), WM_MOUSEWHEEL, down, position);
    LOKA_VERIFY(offsetState.get() == expected && "focused EditText must propagate to the viewport");
    LOKA_VERIFY(DestroyWindow(edit));
    offsetState.set(0);

    // The real Win32Window WndProc must route by the message's screen point.
    LOKA_VERIFY(SetWindowPos(root, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE));
    LOKA_VERIFY(GetAncestor(WindowFromPoint(point), GA_ROOT) == root);
    SetFocus(root);
    LOKA_VERIFY(GetFocus() == root);
    SendMessageW(GetFocus(), WM_MOUSEWHEEL, down, position);
    LOKA_VERIFY(offsetState.get() == expected && "root-directed wheel must find the viewport");
    offsetState.set(0);
    HWND outside = CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | WS_VISIBLE,
                                   310, 10, 30, 24, root, NULL, GetModuleHandleW(NULL), NULL);
    LOKA_VERIFY(outside);
    SetFocus(outside);
    LOKA_VERIFY(GetFocus() == outside);
    SendMessageW(GetFocus(), WM_MOUSEWHEEL, down, position);
    LOKA_VERIFY(offsetState.get() == expected && "child outside viewport must propagate through root routing");
    LOKA_VERIFY(DestroyWindow(outside));
    offsetState.set(0);
    POINT outsidePoint = {320, 200};
    LOKA_VERIFY(ClientToScreen(root, &outsidePoint));
    SendMessageW(root, WM_MOUSEWHEEL, down, MAKELPARAM(outsidePoint.x, outsidePoint.y));
    LOKA_VERIFY(offsetState.get() == 0 && "root routing must refuse a point outside the viewport");
    controller.onChange(0, loka::app::scene::NODE_DIRTY_NONE, false);
  }
  std::printf("==== [testWin32ScrollViewWheelStepsOffsetThroughLinePath] PASSED ====\n");
}

void testWin32ScrollViewResizeReclampsOffsetOnce()
{
  std::printf("\n==== [testWin32ScrollViewResizeReclampsOffsetOnce] start ====\n");
  HWND root = createHostWindow();
  LOKA_VERIFY(root);
  {
    Win32ScenePlatformController controller(root, loka::win32::Win32DisplayScale(96, loka::app::RailMetrics()));
    RegisterWin32BuiltInSupport(controller);
    const int contentHeight = 6 * controller.rowLayoutMetrics().buttonHeight + 5 * Win32SceneLayout::kGap;
    // Nodes after the controller: see the parents-and-clips pin.
    OffsetFact offset(100);
    loka::app::ScrollViewNode scrollView(
        (loka::app::ScrollViewProps(offset.state())));
    addButtonColumn(scrollView, 6);
    establishLayout(controller, &scrollView, 300, 100);
    HWND viewport = findChildWindowByClass(root, L"LOKA_SCROLL_VIEW");
    LOKA_VERIFY(viewport);
    const int initialMax = contentHeight - (100 - 2 * Win32SceneLayout::kRootInset);
    const int resizedMax = contentHeight - (240 - 2 * Win32SceneLayout::kRootInset);
    assert(offset.state().get() == (initialMax < 100 ? initialMax : 100));

    offset.resetChangeCount();
    controller.relayout(300, 240);
    assert(offset.state().get() == (resizedMax > 0 ? resizedMax : 0));
    assert(offset.changeCount() == 1 &&
           "a range-changing resize must publish one clamped fact");
    assert(scrollInfo(viewport).nPos == (resizedMax > 0 ? resizedMax : 0));
    controller.relayout(300, 240);
    assert(offset.changeCount() == 1 &&
           "the settled clamp must not be published again");

    offset.state().set(20);
    offset.resetChangeCount();
    controller.relayout(300, 100);
    assert(offset.state().get() == 20);
    assert(offset.changeCount() == 0 &&
           "a still-valid offset must survive resize without a fact write");

    // An offset far beyond the short range must not wedge the viewport in a
    // refusal loop: the arm pre-clamps before deriving the child scope, lays
    // out, and setScrollMetrics republishes the exact maximum.
    offset.state().set(40000);
    offset.resetChangeCount();
    controller.relayout(300, 100);
    assert(offset.state().get() == contentHeight - (100 - 2 * Win32SceneLayout::kRootInset)
           && "an oversized offset must recover to the content maximum");
    assert(offset.changeCount() == 1);
    controller.relayout(300, 100);
    assert(offset.changeCount() == 1);

    // SIF_DISABLENOSCROLL: the scrollbar keeps occupying its width when the
    // content fits, so the client width children lay out against never
    // changes between passes.
    HWND viewport2 = findChildWindowByClass(root, L"LOKA_SCROLL_VIEW");
    RECT clientWhileOverflowing;
    LOKA_VERIFY(GetClientRect(viewport2, &clientWhileOverflowing));
    controller.relayout(300, 400);
    RECT clientWhileFitting;
    LOKA_VERIFY(GetClientRect(viewport2, &clientWhileFitting));
    assert(clientWhileFitting.right == clientWhileOverflowing.right &&
           "the disabled scrollbar must keep its width when content fits");

    controller.onChange(0, loka::app::scene::NODE_DIRTY_NONE, false);
  }
  LOKA_VERIFY(DestroyWindow(root));
  std::printf("==== [testWin32ScrollViewResizeReclampsOffsetOnce] PASSED ====\n");
}

void testWin32NestedScrollViewRefusesWithoutDisturbingOuterScope()
{
  std::printf("\n==== [testWin32NestedScrollViewRefusesWithoutDisturbingOuterScope] start ====\n");
  HWND root = createHostWindow();
  LOKA_VERIFY(root);
  {
    Win32ScenePlatformController controller(root, loka::win32::Win32DisplayScale(96, loka::app::RailMetrics()));
    RegisterWin32BuiltInSupport(controller);
    // Nodes after the controller: see the parents-and-clips pin.
    OffsetFact outerOffset(5);
    OffsetFact innerOffset(3);
    loka::app::ScrollViewNode outer(
        (loka::app::ScrollViewProps(outerOffset.state())));
    loka::app::ScrollViewNode *inner = new loka::app::ScrollViewNode(
        (loka::app::ScrollViewProps(innerOffset.state())));
    loka::app::StackNode *innerColumn = addButtonColumn(*inner, 1);
    loka::app::ButtonNode *innerButton =
        innerColumn->childrenHead()->asButtonNode();
    outer.addChild(inner);
    addButtonColumn(outer, 6);
    establishLayout(controller, &outer, 300, 140);

    HWND viewport = findChildWindowByClass(root, L"LOKA_SCROLL_VIEW");
    LOKA_VERIFY(viewport);
    assert(findChildWindowByClass(viewport, L"LOKA_SCROLL_VIEW") == 0 &&
           "a nested ScrollView must refuse before materializing an HWND");
    LOKA_VERIFY(inner->getContext() == 0);
    LOKA_VERIFY(innerButton && innerButton->getContext() == 0);

    std::vector<HWND> outerButtons =
        directChildWindowsByClass(viewport, L"Button");
    assert(outerButtons.size() == 6);
    const RECT before = childRectInParent(outerButtons[0], viewport);
    SendMessageW(viewport, WM_VSCROLL, MAKEWPARAM(SB_LINEDOWN, 0), 0);
    assert(outerOffset.state().get() == 21);
    assert(innerOffset.state().get() == 3);
    controller.relayout(300, 140);
    const RECT after = childRectInParent(outerButtons[0], viewport);
    assert(after.top == before.top - 16 &&
           "the outer scope must remain live after the inner refusal");

    controller.onChange(0, loka::app::scene::NODE_DIRTY_NONE, false);
  }
  LOKA_VERIFY(DestroyWindow(root));
  std::printf("==== [testWin32NestedScrollViewRefusesWithoutDisturbingOuterScope] PASSED ====\n");
}

void testWin32ScrollViewContainerGapsAndNoSeats()
{
  using namespace loka::app;
  using namespace loka::app::scene;
  using namespace loka::dsl;
  using namespace loka::dsl::testing;
  HWND root = createHostWindow();
  LOKA_VERIFY(root != NULL);
  {
    Win32ScenePlatformController controller(root, loka::win32::Win32DisplayScale(96, RailMetrics()));
    OffsetFact offset(0);
    loka::core::MutableState<bool> hidden(false);
    // A ConfirmDialog needs a result channel; the STATIC host gives it no transport, so nothing opens.
    loka::core::MutableState<ConfirmResult> resultStorage;
    NodeState<ConfirmResult> result(&resultStorage, 0);
    NodeDefinitionBase *definition = (ScrollView(ScrollViewProps(offset.state())).testId("gapScroll")
                                      << Text("First") << Fragment() << (Show(hidden) << Text("Hidden"))
                                      << ConfirmDialog().result(result) << Text("Second") << Text("Third"))
                                         .clone();
    LOKA_VERIFY(definition != 0);
    Scene scene(definition);
    LOKA_VERIFY(scene.mount(&controller));
    SceneTestAccess::updateAttached(scene, true);
    const int visible = 30;
    controller.relayout(300, visible + 2 * Win32SceneLayout::kRootInset);
    HWND viewport = findChildWindowByClass(root, L"LOKA_SCROLL_VIEW");
    LOKA_VERIFY(viewport != NULL);
    int bottom = 0;
    int totalHeight = 0;
    for (int i = 0; i < 3; ++i)
    {
      TextNode *text = 0;
      FlowError error;
      LOKA_VERIFY(ResolveSelector(&scene, WithinAnchor("gapScroll").descendant<TextNode>(i + 1), text, error)
                  == FLOW_STEP_SUCCEEDED);
      LOKA_VERIFY(text && text->getContext());
      const RECT frame = childRectInParent(static_cast<Win32TextContext *>(text->getContext())->hwnd(), viewport);
      LOKA_VERIFY(frame.top == bottom + (i > 0 ? Win32SceneLayout::kGap : 0));
      totalHeight += static_cast<int>(frame.bottom - frame.top);
      bottom = static_cast<int>(frame.bottom);
    }
    const SCROLLINFO info = scrollInfo(viewport);
    LOKA_VERIFY(info.nPage == static_cast<UINT>(visible));
    const int maximum = maximumOffset(info);
    LOKA_VERIFY(maximum == totalHeight + 2 * Win32SceneLayout::kGap - visible);
    SceneTestAccess::unmount(scene);
    controller.drainNativeRetirements();
  }
  LOKA_VERIFY(DestroyWindow(root));
}

namespace
{
  // The seat verdict reads kind(), while dispatch asks asBoundary(). Counting
  // only the latter distinguishes refusal before layout from later narrowing.
  class GapDispatchText : public loka::app::TextNode
  {
  public:
    explicit GapDispatchText(int &visits)
        : TextNode(loka::app::TextProps("Following")),
          visits_(visits)
    {
    }
    virtual loka::app::scene::BoundaryNode *asBoundary()
    {
      ++this->visits_;
      return 0;
    }

  private:
    int &visits_;
  };
} // namespace

void testWin32ScrollViewGappedOriginRefuses()
{
  using namespace loka::app;
  for (int generic = 0; generic < 2; ++generic)
  {
    HWND root = createHostWindow();
    LOKA_VERIFY(root != NULL);
    {
      Win32ScenePlatformController controller(root, loka::win32::Win32DisplayScale(96, RailMetrics()));
      OffsetFact offset(0);
      int visits = 0;
      ScrollViewNode scroll((ScrollViewProps(offset.state())));
      scene::NestableNode *content = &scroll;
      if (generic)
      {
        FragmentNode *fragment = new FragmentNode((FragmentProps()));
        scroll.addChild(fragment);
        content = fragment;
      }
      BoxNode *leading = new BoxNode(BoxProps().setSize(100, 100));
      TextNode *following = new GapDispatchText(visits);
      content->addChild(leading);
      content->addChild(following);
      establishLayout(controller, &scroll, 300, 100);
      HWND viewport = findChildWindowByClass(root, L"LOKA_SCROLL_VIEW");
      LOKA_VERIFY(viewport != NULL);
      const SCROLLINFO before = scrollInfo(viewport);
      LOKA_VERIFY(visits > 0);
      visits = 0;
      const int maximum = maximumOffset(before);
      LOKA_VERIFY(maximum > 0);
      // The old end fits short; only the intervening gap crosses the limit.
      leading->props.setSize(100,
                             static_cast<short>(SHRT_MAX - Win32SceneLayout::kRootInset - Win32SceneLayout::kGap + 1));
      controller.relayout(300, 100);
      const SCROLLINFO after = scrollInfo(viewport);
      LOKA_VERIFY(visits == 0);
      LOKA_VERIFY(after.nMin == before.nMin && after.nMax == before.nMax);
      LOKA_VERIFY(after.nPage == before.nPage && after.nPos == before.nPos);
      const int offsetValue = offset.state().get();
      LOKA_VERIFY(offsetValue == 0);
      controller.onChange(0, scene::NODE_DIRTY_NONE, false);
    }
    LOKA_VERIFY(DestroyWindow(root));
  }
}
