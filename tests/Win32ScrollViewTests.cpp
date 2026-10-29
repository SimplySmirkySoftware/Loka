#include "app/nodes/boundary/StdComposition.hpp"
#include "app/nodes/nestable/ScrollViewSettlement.hpp"
#include "support/Headless.hpp"
#include "Win32InputDoor.hpp"
#include "app/layout/FallbackControlMetrics.hpp"
#include "Win32ScrollViewTests.hpp"

#include "Win32SceneLayout.hpp"
#include "testing/Win32ScenePlatformTestAccess.hpp"
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
#include "app/RectSurface.hpp"
#include "app/scene/state/NodeState.hpp"
#include "core/State.hpp"
#include "core/StateTracker.hpp"
#include <climits>
#include "app/nodes/Text.hpp"
#include "app/nodes/ImageView.hpp"
#include "app/nodes/nestable/Box.hpp"
#include "app/nodes/nestable/Fragment.hpp"
#include "app/ConfirmDialog.hpp"
#include "context/Win32TextContext.hpp"
#include "context/Win32ScrollViewContext.hpp"
#include "testing/scene/SceneTestFlow.hpp"
#include "app/nodes/nestable/Show.hpp"

namespace
{
  class ViewportFact
  {
  public:
    explicit ViewportFact(int initial)
        : storage_(loka::core::Frame(0, initial, 0, 0)),
          tracker_(),
          state_(&this->storage_, &this->tracker_),
          changeCount_(0)
    {
      this->tracker_.addState(&this->storage_);
      this->storage_.bind(&ViewportFact::ChangedThunk, this, false);
    }

    ~ViewportFact()
    {
      this->storage_.unbind(&ViewportFact::ChangedThunk, this);
    }

    loka::app::scene::NodeState<loka::core::Frame> &state()
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
      ViewportFact *self = static_cast<ViewportFact *>(userData);
      if (self)
      {
        ++self->changeCount_;
      }
    }

    loka::core::MutableState<loka::core::Frame> storage_;
    loka::core::PushStateTracker tracker_;
    loka::app::scene::NodeState<loka::core::Frame> state_;
    int changeCount_;

    ViewportFact(const ViewportFact &);
    ViewportFact &operator=(const ViewportFact &);
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

  SCROLLINFO scrollInfo(HWND viewport, int bar = SB_VERT)
  {
    SCROLLINFO info;
    ZeroMemory(&info, sizeof(info));
    info.cbSize = sizeof(info);
    info.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    LOKA_VERIFY(GetScrollInfo(bar == SB_VERT ? FindWindowExW(GetParent(viewport), NULL, L"SCROLLBAR", NULL) : viewport,
                              bar == SB_VERT ? SB_CTL : bar,
                              &info));
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

  // Gives the host window the client size an explicit relayout will use. A
  // Scene-driven pass lays out at the window's real client size, and the
  // first layout now publishes the viewport size, which dirties layout once;
  // a host that disagrees with the explicit size would let that second pass
  // replace the geometry under test.
  void sizeHostClient(HWND root, int width, int height)
  {
    RECT frame = {0, 0, width, height};
    LOKA_VERIFY(AdjustWindowRectEx(&frame,
                                   static_cast<DWORD>(GetWindowLongPtrW(root, GWL_STYLE)),
                                   FALSE,
                                   static_cast<DWORD>(GetWindowLongPtrW(root, GWL_EXSTYLE))));
    LOKA_VERIFY(SetWindowPos(root,
                             NULL,
                             0,
                             0,
                             frame.right - frame.left,
                             frame.bottom - frame.top,
                             SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE));
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

  void checkViewportSize(int contentWidth, int contentHeight, bool verticalOverflow, bool horizontalOverflow)
  {
    using namespace loka::app;
    HWND root = createHostWindow();
    LOKA_VERIFY(root);
    {
      Win32ScenePlatformController controller(root, loka::win32::Win32DisplayScale(96, RailMetrics()));
      RegisterWin32BuiltInSupport(controller);
      ViewportFact fact(ScrollViewProps::topOrigin().y);
      ScrollViewNode scroll(ScrollViewProps(fact.state()).contentInsets(ScrollViewContentInsets::of(12, 3)));
      scroll.addChild(new ImageViewNode(ImageViewProps().size(contentWidth, contentHeight)));
      establishLayout(controller, &scroll, 300, 140);
      HWND viewport = findChildWindowByClass(root, L"LOKA_SCROLL_VIEW");
      LOKA_VERIFY(viewport);
      // At 96 dpi content units are client pixels. The vertical bar remains
      // present even when disabled; insets affect the extent, not this size.
      const int width = 300 - 2 * Win32SceneLayout::kRootInset - GetSystemMetrics(SM_CXVSCROLL);
      const int height = 140 - 2 * Win32SceneLayout::kRootInset
                         - (horizontalOverflow ? controller.displayScale().systemMetric(SM_CYHSCROLL) : 0);
      const LONG_PTR style = GetWindowLongPtrW(viewport, GWL_STYLE);
      LOKA_VERIFY((style & WS_VSCROLL) == 0);
      LOKA_VERIFY(((style & WS_HSCROLL) != 0) == horizontalOverflow);
      const SCROLLINFO vertical = scrollInfo(viewport);
      LOKA_VERIFY((maximumOffset(vertical) > 0) == verticalOverflow);
      const loka::core::Frame expected(0, -12, width, height);
      const loka::core::Frame first = fact.state().get();
      LOKA_VERIFY(first == expected);
      const int firstWrites = fact.changeCount();
      LOKA_VERIFY(firstWrites == 1);
      fact.resetChangeCount();
      controller.relayout(300, 140);
      const loka::core::Frame second = fact.state().get();
      const int secondWrites = fact.changeCount();
      LOKA_VERIFY(second == expected);
      LOKA_VERIFY(secondWrites == 0);
      controller.onChange(0, scene::NODE_DIRTY_NONE, false);
    }
    LOKA_VERIFY(DestroyWindow(root));
  }
} // namespace

void testWin32ScrollViewViewportSizeNoOverflow()
{
  checkViewportSize(100, 17, false, false);
}

void testWin32ScrollViewViewportSizeVerticalOverflow()
{
  checkViewportSize(100, 400, true, false);
}

void testWin32ScrollViewViewportSizeBothOverflows()
{
  checkViewportSize(400, 400, true, true);
}

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
    ViewportFact offset(10);
    loka::app::ScrollViewNode scrollView(
        (loka::app::ScrollViewProps(offset.state())));
    addButtonColumn(scrollView, 6);
    establishLayout(controller, &scrollView, 300, 140);

    HWND viewport = findChildWindowByClass(root, L"LOKA_SCROLL_VIEW");
    LOKA_VERIFY(viewport);
    LOKA_VERIFY(GetParent(GetParent(viewport)) == root);
    const LONG_PTR viewportStyle = GetWindowLongPtr(viewport, GWL_STYLE);
    assert((viewportStyle & WS_VSCROLL) == 0);
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
      LOKA_VERIFY(GetParent(buttons[i]) == viewport
                  && "projected controls must be structurally clipped by the viewport HWND");
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
    LOKA_VERIFY(maximumOffset(info) == contentHeight - (140 - 2 * Win32SceneLayout::kRootInset));
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
    ViewportFact offset(0);
    loka::app::ScrollViewNode scrollView(
        (loka::app::ScrollViewProps(offset.state())));
    addButtonColumn(scrollView, 6);
    establishLayout(controller, &scrollView, 300, 140);
    HWND viewport = findChildWindowByClass(root, L"LOKA_SCROLL_VIEW");
    LOKA_VERIFY(viewport);
    std::vector<HWND> buttons = directChildWindowsByClass(viewport, L"Button");
    assert(!buttons.empty());
    const RECT atZero = childRectInParent(buttons[0], viewport);

    offset.state().set(loka::core::Frame(0, 7, 0, 0));
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

void testWin32ScrollViewInsetsPlaceContentBelowTheTopInset()
{
  using namespace loka::app;
  HWND root = createHostWindow();
  LOKA_VERIFY(root);
  {
    Win32ScenePlatformController controller(root, loka::win32::Win32DisplayScale(96, RailMetrics()));
    RegisterWin32BuiltInSupport(controller);
    ViewportFact offset(ScrollViewProps::topOrigin().y);
    scene::NodeState<loka::core::Frame> &offsetState = offset.state();
    ScrollViewNode scrollView(ScrollViewProps(offset.state()).contentInsets(ScrollViewContentInsets::of(12, 3)));
    scrollView.addChild(new ImageViewNode(ImageViewProps().size(100, 17)));
    establishLayout(controller, &scrollView, 300, 10 + 2 * Win32SceneLayout::kRootInset);
    HWND viewport = findChildWindowByClass(root, L"LOKA_SCROLL_VIEW");
    LOKA_VERIFY(viewport);
    HWND child = findChildWindowByClass(viewport, L"LOKA_IMAGE_VIEW");
    LOKA_VERIFY(child);
    const RECT seat = childRectInParent(viewport, root);
    const RECT atTop = childRectInParent(child, root);
    const SCROLLINFO info = scrollInfo(viewport);
    LOKA_VERIFY(atTop.top == seat.top + 12);
    LOKA_VERIFY(info.nMax == 17 + 15 - 1);
    LOKA_VERIFY(info.nPage == 10);
    LOKA_VERIFY(
        offsetState.get()
        == loka::core::Frame(0, -12, 300 - 2 * Win32SceneLayout::kRootInset - GetSystemMetrics(SM_CXVSCROLL), 10));

    offset.state().set(loka::core::Frame(0, 100, 0, 0));
    offset.resetChangeCount();
    controller.relayout(300, 10 + 2 * Win32SceneLayout::kRootInset);
    const SCROLLINFO after = scrollInfo(viewport);
    // Extent maximum 22 clamps the requested content origin 100: the bar
    // settles extent 22 and the published fact reads 22 - 12 = 10, so the
    // fact assertion proves the clamp was published, not merely preserved.
    // This pass placed on the request (the reclamp pin's rule); the next
    // pass places on the published clamp.
    LOKA_VERIFY(after.nPos == 22);
    LOKA_VERIFY(
        offsetState.get()
        == loka::core::Frame(0, 10, 300 - 2 * Win32SceneLayout::kRootInset - GetSystemMetrics(SM_CXVSCROLL), 10));
    const int published = offset.changeCount();
    LOKA_VERIFY(published == 1);
    controller.relayout(300, 10 + 2 * Win32SceneLayout::kRootInset);
    const RECT scrolled = childRectInParent(child, root);
    LOKA_VERIFY(scrolled.top == seat.top - 10);
    const int settled = offset.changeCount();
    LOKA_VERIFY(settled == 1);
    LOKA_VERIFY(
        offsetState.get()
        == loka::core::Frame(0, 10, 300 - 2 * Win32SceneLayout::kRootInset - GetSystemMetrics(SM_CXVSCROLL), 10));
    controller.onChange(0, scene::NODE_DIRTY_NONE, false);
  }
  LOKA_VERIFY(DestroyWindow(root));
}

void testWin32ScrollViewInsetsGrowTheHeightlessExtent()
{
  using namespace loka::app;
  HWND root = createHostWindow();
  LOKA_VERIFY(root);
  {
    Win32ScenePlatformController controller(root, loka::win32::Win32DisplayScale(96, RailMetrics()));
    RegisterWin32BuiltInSupport(controller);
    ViewportFact offset(ScrollViewProps::topOrigin().y);
    scene::NodeState<loka::core::Frame> &offsetState = offset.state();
    StackNode column(StackProps(STACK_AXIS_COLUMN).gap(0));
    BoxNode *leading = new BoxNode(BoxProps().setSize(100, 1));
    ScrollViewNode *scrollView =
        new ScrollViewNode(ScrollViewProps(offset.state()).contentInsets(ScrollViewContentInsets::of(12, 3)));
    scrollView->addChild(new BoxNode(BoxProps().setSize(100, 7)));
    column.addChild(leading);
    column.addChild(scrollView);
    column.addChild(new ImageViewNode(ImageViewProps().size(100, 1)));
    // The following sibling exposes the heightless return without a test API.
    establishLayout(controller, &column, 300, 2 * Win32SceneLayout::kRootInset);
    HWND viewport = findChildWindowByClass(root, L"LOKA_SCROLL_VIEW");
    HWND following = findChildWindowByClass(root, L"LOKA_IMAGE_VIEW");
    LOKA_VERIFY(viewport && following);
    const RECT seat = childRectInParent(viewport, root);
    const RECT afterSeat = childRectInParent(following, root);
    LOKA_VERIFY(afterSeat.top == seat.top + 7 + 15);

    // SHRT_MAX - 25 + 22 still fits; pin that near-limit success too.
    leading->props.setSize(100, static_cast<short>(SHRT_MAX - 25 - Win32SceneLayout::kRootInset));
    controller.relayout(300, 2 * Win32SceneLayout::kRootInset);
    const RECT nearLimit = childRectInParent(following, root);
    LOKA_VERIFY(nearLimit.top == SHRT_MAX - 3);
    const SCROLLINFO before = scrollInfo(viewport);
    offset.state().set(loka::core::Frame(0, 6, 0, 0));
    offset.resetChangeCount();
    leading->props.setSize(100, static_cast<short>(SHRT_MAX - 20 - Win32SceneLayout::kRootInset));
    controller.relayout(300, 2 * Win32SceneLayout::kRootInset);
    const RECT refused = childRectInParent(following, root);
    const SCROLLINFO after = scrollInfo(viewport);
    LOKA_VERIFY(refused.top == SHRT_MAX - 20);
    LOKA_VERIFY(after.nMin == before.nMin && after.nMax == before.nMax);
    LOKA_VERIFY(after.nPage == before.nPage && after.nPos == before.nPos);
    LOKA_VERIFY(offsetState.get() == loka::core::Frame(0, 6, 0, 0));
    const int changes = offset.changeCount();
    LOKA_VERIFY(changes == 0);
    controller.onChange(0, scene::NODE_DIRTY_NONE, false);
  }
  LOKA_VERIFY(DestroyWindow(root));
}

void testWin32ScrollViewInsetsRefuseAnExtentAboveShort()
{
  using namespace loka::app;
  HWND root = createHostWindow();
  LOKA_VERIFY(root);
  {
    Win32ScenePlatformController controller(root, loka::win32::Win32DisplayScale(96, RailMetrics()));
    RegisterWin32BuiltInSupport(controller);
    ViewportFact offset(0);
    scene::NodeState<loka::core::Frame> &offsetState = offset.state();
    ScrollViewNode scrollView((ScrollViewProps(offset.state())));
    scrollView.addChild(new BoxNode(BoxProps().setSize(100, 17)));
    establishLayout(controller, &scrollView, 300, 10 + 2 * Win32SceneLayout::kRootInset);
    HWND viewport = findChildWindowByClass(root, L"LOKA_SCROLL_VIEW");
    LOKA_VERIFY(viewport);
    const SCROLLINFO before = scrollInfo(viewport);
    scrollView.props.contentInsets(ScrollViewContentInsets::of(SHRT_MAX, 0));
    // Content origin zero keeps placement representable: only extent refuses.
    offset.resetChangeCount();
    controller.relayout(300, 10 + 2 * Win32SceneLayout::kRootInset);
    const SCROLLINFO after = scrollInfo(viewport);
    LOKA_VERIFY(after.nMin == before.nMin && after.nMax == before.nMax);
    LOKA_VERIFY(after.nPage == before.nPage && after.nPos == before.nPos);
    LOKA_VERIFY(
        offsetState.get()
        == loka::core::Frame(0, 0, 300 - 2 * Win32SceneLayout::kRootInset - GetSystemMetrics(SM_CXVSCROLL), 10));
    const int changes = offset.changeCount();
    LOKA_VERIFY(changes == 0);
    controller.onChange(0, scene::NODE_DIRTY_NONE, false);
  }
  LOKA_VERIFY(DestroyWindow(root));
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
    ViewportFact offset(0);
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

    SendMessageW(GetParent(viewport),
                 WM_VSCROLL,
                 MAKEWPARAM(SB_LINEDOWN, 0),
                 reinterpret_cast<LPARAM>(FindWindowExW(GetParent(viewport), NULL, L"SCROLLBAR", NULL)));
    LOKA_VERIFY(offset.state().get().y == 16 && "WM_VSCROLL must publish through the ScrollView NodeState door");
    controller.relayout(300, 140);
    const RECT after = childRectInParent(buttons[0], viewport);
    assert(after.top == before.top - 16);

    SendMessageW(GetParent(viewport),
                 WM_VSCROLL,
                 MAKEWPARAM(SB_THUMBTRACK, 10),
                 reinterpret_cast<LPARAM>(FindWindowExW(GetParent(viewport), NULL, L"SCROLLBAR", NULL)));
    LOKA_VERIFY(offset.state().get().y == 16 && "thumb tracking is visual-only until the value settles");
    LOKA_VERIFY(scrollInfo(viewport).nPos == 10);
    SendMessageW(GetParent(viewport),
                 WM_VSCROLL,
                 MAKEWPARAM(SB_THUMBPOSITION, 10),
                 reinterpret_cast<LPARAM>(FindWindowExW(GetParent(viewport), NULL, L"SCROLLBAR", NULL)));
    LOKA_VERIFY(offset.state().get().y == 10 && "thumb position must publish the settled fact");
    controller.relayout(300, 140);
    SendMessageW(GetParent(viewport),
                 WM_VSCROLL,
                 MAKEWPARAM(SB_THUMBTRACK, 11),
                 reinterpret_cast<LPARAM>(FindWindowExW(GetParent(viewport), NULL, L"SCROLLBAR", NULL)));
    LOKA_VERIFY(offset.state().get().y == 10);
    SendMessageW(GetParent(viewport),
                 WM_VSCROLL,
                 MAKEWPARAM(SB_ENDSCROLL, 0),
                 reinterpret_cast<LPARAM>(FindWindowExW(GetParent(viewport), NULL, L"SCROLLBAR", NULL)));
    LOKA_VERIFY(offset.state().get().y == 11 && "end scroll must publish the last visual thumb position");
    controller.relayout(300, 140);

    // A click on an arrow or on the track sends its step and then, on release,
    // SB_ENDSCROLL, with no layout pass in between: the step must survive it.
    offset.state().set(loka::core::Frame(0, 0, 0, 0));
    controller.relayout(300, 140);
    SendMessageW(GetParent(viewport),
                 WM_VSCROLL,
                 MAKEWPARAM(SB_LINEDOWN, 0),
                 reinterpret_cast<LPARAM>(FindWindowExW(GetParent(viewport), NULL, L"SCROLLBAR", NULL)));
    SendMessageW(GetParent(viewport),
                 WM_VSCROLL,
                 MAKEWPARAM(SB_ENDSCROLL, 0),
                 reinterpret_cast<LPARAM>(FindWindowExW(GetParent(viewport), NULL, L"SCROLLBAR", NULL)));
    LOKA_VERIFY(offset.state().get().y == 16 && "an arrow click's end scroll must keep its line step");
    const SCROLLINFO clickInfo = scrollInfo(viewport);
    const int clickMaximum = maximumOffset(clickInfo);
    const int clickPage =
        clickInfo.nPage > static_cast<UINT>(clickMaximum) ? clickMaximum : static_cast<int>(clickInfo.nPage);
    SendMessageW(GetParent(viewport),
                 WM_VSCROLL,
                 MAKEWPARAM(SB_PAGEDOWN, 0),
                 reinterpret_cast<LPARAM>(FindWindowExW(GetParent(viewport), NULL, L"SCROLLBAR", NULL)));
    SendMessageW(GetParent(viewport),
                 WM_VSCROLL,
                 MAKEWPARAM(SB_ENDSCROLL, 0),
                 reinterpret_cast<LPARAM>(FindWindowExW(GetParent(viewport), NULL, L"SCROLLBAR", NULL)));
    LOKA_VERIFY(offset.state().get().y == (16 > clickMaximum - clickPage ? clickMaximum : 16 + clickPage)
                && "a track click's end scroll must keep its page step");
    LOKA_VERIFY(offset.state().get().y > 16 && "the page step must move in this fixture");
    controller.relayout(300, 140);

    const int rangeEnd = maximumOffset(scrollInfo(viewport));
    offset.state().set(loka::core::Frame(0, rangeEnd, 0, 0));
    controller.relayout(300, 140);
    SendMessageW(GetParent(viewport),
                 WM_VSCROLL,
                 MAKEWPARAM(SB_LINEDOWN, 0),
                 reinterpret_cast<LPARAM>(FindWindowExW(GetParent(viewport), NULL, L"SCROLLBAR", NULL)));
    LOKA_VERIFY(offset.state().get().y == rangeEnd
                && "line scrolling at the range end must clamp without another write");
    offset.state().set(loka::core::Frame(0, rangeEnd - 3, 0, 0));
    SendMessageW(GetParent(viewport),
                 WM_VSCROLL,
                 MAKEWPARAM(SB_LINEDOWN, 0),
                 reinterpret_cast<LPARAM>(FindWindowExW(GetParent(viewport), NULL, L"SCROLLBAR", NULL)));
    LOKA_VERIFY(offset.state().get().y == rangeEnd && "a partial final line must clamp to the maximum");
    offset.state().set(loka::core::Frame(0, 3, 0, 0));
    SendMessageW(GetParent(viewport),
                 WM_VSCROLL,
                 MAKEWPARAM(SB_LINEUP, 0),
                 reinterpret_cast<LPARAM>(FindWindowExW(GetParent(viewport), NULL, L"SCROLLBAR", NULL)));
    LOKA_VERIFY(offset.state().get().y == 0 && "a partial first line must clamp to zero");

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
    ViewportFact offset(0);
    loka::app::scene::NodeState<loka::core::Frame> &offsetState = offset.state();
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
    LOKA_VERIFY(offsetState.get().y == expected);
    controller.relayout(300, 140);
    const SCROLLINFO afterWheel = scrollInfo(viewport);
    LOKA_VERIFY(afterWheel.nPos == expected);
    SendMessageW(viewport, WM_MOUSEWHEEL, up, position);
    LOKA_VERIFY(offsetState.get().y == 0);
    SendMessageW(viewport, WM_MOUSEWHEEL, MAKEWPARAM(0, -WHEEL_DELTA / 2), position);
    LOKA_VERIFY(offsetState.get().y == 0 && "sub-detent remainders are dropped");

    offsetState.set(loka::core::Frame(0, maximum, 0, 0));
    controller.relayout(300, 140);
    offset.resetChangeCount();
    SendMessageW(viewport, WM_MOUSEWHEEL, down, position);
    LOKA_VERIFY(offsetState.get()
                == loka::core::Frame(0,
                                     maximum,
                                     300 - 2 * Win32SceneLayout::kRootInset - GetSystemMetrics(SM_CXVSCROLL),
                                     140 - 2 * Win32SceneLayout::kRootInset));
    const int clampedChanges = offset.changeCount();
    LOKA_VERIFY(clampedChanges == 0);
    offsetState.set(loka::core::Frame(0, 0, 0, 0));
    controller.relayout(300, 140);

    std::vector<HWND> buttons = directChildWindowsByClass(viewport, L"Button");
    LOKA_VERIFY(!buttons.empty());
    SetFocus(buttons[0]);
    LOKA_VERIFY(GetFocus() == buttons[0]);
    SendMessageW(GetFocus(), WM_MOUSEWHEEL, down, position);
    LOKA_VERIFY(offsetState.get().y == expected && "focused Button must propagate to the viewport");
    offsetState.set(loka::core::Frame(0, 0, 0, 0));

    // Use the shipping single-line EditText style to exercise native default
    // propagation, without introducing a per-control subclass for the test.
    HWND edit = loka::win32::CreateEditTextControl(viewport, 10, 10, 80, 24);
    LOKA_VERIFY(edit);
    SetFocus(edit);
    LOKA_VERIFY(GetFocus() == edit);
    SendMessageW(GetFocus(), WM_MOUSEWHEEL, down, position);
    LOKA_VERIFY(offsetState.get().y == expected && "focused EditText must propagate to the viewport");
    LOKA_VERIFY(DestroyWindow(edit));
    offsetState.set(loka::core::Frame(0, 0, 0, 0));

    HWND bar = FindWindowExW(GetParent(viewport), NULL, L"SCROLLBAR", NULL);
    LOKA_VERIFY(bar);
    LOKA_VERIFY(SetWindowPos(root, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE));
    POINT overBar = {3, 30};
    LOKA_VERIFY(ClientToScreen(bar, &overBar));
    SendMessageW(bar, WM_MOUSEWHEEL, down, MAKELPARAM(overBar.x, overBar.y));
    LOKA_VERIFY(offsetState.get().y == expected);
    offsetState.set(loka::core::Frame(0, 0, 0, 0));
    SendMessageW(root, WM_MOUSEWHEEL, down, MAKELPARAM(overBar.x, overBar.y));
    LOKA_VERIFY(offsetState.get().y == expected);
    offsetState.set(loka::core::Frame(0, 0, 0, 0));

    // The real Win32Window WndProc must route by the message's screen point.
    LOKA_VERIFY(SetWindowPos(root, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE));
    LOKA_VERIFY(GetAncestor(WindowFromPoint(point), GA_ROOT) == root);
    SetFocus(root);
    LOKA_VERIFY(GetFocus() == root);
    SendMessageW(GetFocus(), WM_MOUSEWHEEL, down, position);
    LOKA_VERIFY(offsetState.get().y == expected && "root-directed wheel must find the viewport");
    offsetState.set(loka::core::Frame(0, 0, 0, 0));
    HWND outside = CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | WS_VISIBLE,
                                   310, 10, 30, 24, root, NULL, GetModuleHandleW(NULL), NULL);
    LOKA_VERIFY(outside);
    SetFocus(outside);
    LOKA_VERIFY(GetFocus() == outside);
    SendMessageW(GetFocus(), WM_MOUSEWHEEL, down, position);
    LOKA_VERIFY(offsetState.get().y == expected && "child outside viewport must propagate through root routing");
    LOKA_VERIFY(DestroyWindow(outside));
    offsetState.set(loka::core::Frame(0, 0, 0, 0));
    POINT outsidePoint = {320, 200};
    LOKA_VERIFY(ClientToScreen(root, &outsidePoint));
    SendMessageW(root, WM_MOUSEWHEEL, down, MAKELPARAM(outsidePoint.x, outsidePoint.y));
    LOKA_VERIFY(offsetState.get().y == 0 && "root routing must refuse a point outside the viewport");
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
    ViewportFact offset(100);
    loka::app::scene::NodeState<loka::core::Frame> &offsetState = offset.state();
    loka::app::ScrollViewNode scrollView(
        (loka::app::ScrollViewProps(offset.state())));
    addButtonColumn(scrollView, 6);
    establishLayout(controller, &scrollView, 300, 100);
    HWND viewport = findChildWindowByClass(root, L"LOKA_SCROLL_VIEW");
    LOKA_VERIFY(viewport);
    const int visibleWidth = 300 - 2 * Win32SceneLayout::kRootInset - GetSystemMetrics(SM_CXVSCROLL);
    const int initialMax = contentHeight - (100 - 2 * Win32SceneLayout::kRootInset);
    const int resizedMax = contentHeight - (240 - 2 * Win32SceneLayout::kRootInset);
    LOKA_VERIFY(offsetState.get()
                == loka::core::Frame(
                    0, initialMax < 100 ? initialMax : 100, visibleWidth, 100 - 2 * Win32SceneLayout::kRootInset));

    offset.resetChangeCount();
    controller.relayout(300, 240);
    LOKA_VERIFY(
        offsetState.get()
        == loka::core::Frame(0, resizedMax > 0 ? resizedMax : 0, visibleWidth, 240 - 2 * Win32SceneLayout::kRootInset));
    assert(offset.changeCount() == 1 &&
           "a range-changing resize must publish one clamped fact");
    LOKA_VERIFY(scrollInfo(viewport).nPos == (resizedMax > 0 ? resizedMax : 0));
    controller.relayout(300, 240);
    assert(offset.changeCount() == 1 &&
           "the settled clamp must not be published again");

    offset.state().set(loka::core::Frame(0, 20, visibleWidth, 240 - 2 * Win32SceneLayout::kRootInset));
    offset.resetChangeCount();
    controller.relayout(300, 100);
    LOKA_VERIFY(offsetState.get() == loka::core::Frame(0, 20, visibleWidth, 100 - 2 * Win32SceneLayout::kRootInset));
    assert(offset.changeCount() == 1 && "a still-valid offset must survive resize with one new-size fact write");

    // An offset far beyond the short range must not wedge the viewport in a
    // refusal loop: the arm pre-clamps before deriving the child scope, lays
    // out, and setScrollMetrics republishes the exact maximum.
    offset.state().set(loka::core::Frame(0, 40000, 0, 0));
    offset.resetChangeCount();
    controller.relayout(300, 100);
    LOKA_VERIFY(offsetState.get()
                == loka::core::Frame(0, initialMax, visibleWidth, 100 - 2 * Win32SceneLayout::kRootInset));
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
    ViewportFact outerOffset(5);
    ViewportFact innerOffset(3);
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
    SendMessageW(GetParent(viewport),
                 WM_VSCROLL,
                 MAKEWPARAM(SB_LINEDOWN, 0),
                 reinterpret_cast<LPARAM>(FindWindowExW(GetParent(viewport), NULL, L"SCROLLBAR", NULL)));
    LOKA_VERIFY(outerOffset.state().get().y == 21);
    LOKA_VERIFY(innerOffset.state().get().y == 3);
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
    ViewportFact offset(0);
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
    sizeHostClient(root, 300, visible + 2 * Win32SceneLayout::kRootInset);
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
      ViewportFact offset(0);
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
      const int offsetValue = offset.state().get().y;
      LOKA_VERIFY(offsetValue == 0);
      controller.onChange(0, scene::NODE_DIRTY_NONE, false);
    }
    LOKA_VERIFY(DestroyWindow(root));
  }
}

void testWin32ScrollViewHorizontalRangeAndBar()
{
  HWND root = createHostWindow();
  LOKA_VERIFY(root);
  {
    Win32ScenePlatformController controller(root, loka::win32::Win32DisplayScale(96, loka::app::RailMetrics()));
    RegisterWin32BuiltInSupport(controller);
    ViewportFact offset(0);
    loka::app::scene::NodeState<loka::core::Frame> &offsetState = offset.state();
    offset.state().set(loka::core::Frame(1000, 0, 0, 0));
    loka::app::ScrollViewNode scrollView((loka::app::ScrollViewProps(offset.state())));
    // Tall content keeps nPage observable: Win32 caps it at the content range.
    loka::app::StackNode *column = addButtonColumn(scrollView, 8);
    loka::app::ImageViewNode *image = new loka::app::ImageViewNode(loka::app::ImageViewProps().size(400, 10));
    column->addChild(image);
    establishLayout(controller, &scrollView, 300, 140);
    HWND viewport = findChildWindowByClass(root, L"LOKA_SCROLL_VIEW");
    HWND imageWindow = findChildWindowByClass(viewport, L"LOKA_IMAGE_VIEW");
    LOKA_VERIFY(viewport && imageWindow);
    RECT client;
    LOKA_VERIFY(GetClientRect(viewport, &client));
    const int maximum = 400 - controller.displayScale().clientCapacityToLu(client.right - client.left);
    const SCROLLINFO observedInfo1 = scrollInfo(viewport, SB_HORZ);
    const int observedMaximum2 = maximumOffset(observedInfo1);
    LOKA_VERIFY(observedMaximum2 == maximum);
    LOKA_VERIFY((GetWindowLongPtrW(viewport, GWL_STYLE) & WS_HSCROLL) != 0);
    LOKA_VERIFY(offsetState.get().x == maximum);
    LOKA_VERIFY(childRectInParent(imageWindow, viewport).left == -controller.displayScale().projectEdge(maximum));
    const int seatHeight = 140 - 2 * Win32SceneLayout::kRootInset;
    const SCROLLINFO observedInfo3 = scrollInfo(viewport);
    LOKA_VERIFY(observedInfo3.nPage == static_cast<UINT>(controller.displayScale().clientCapacityToLu(client.bottom)));
    const SCROLLINFO observedInfo4 = scrollInfo(viewport);
    LOKA_VERIFY(static_cast<int>(observedInfo4.nPage) < seatHeight);

    offset.state().set(loka::core::Frame(-7, 0, 0, 0));
    controller.relayout(300, 140);
    LOKA_VERIFY(offsetState.get().x == 0);
    LOKA_VERIFY(childRectInParent(imageWindow, viewport).left == 0);
    image->props.size(50, 10);
    offset.state().set(loka::core::Frame(1000, 0, 0, 0));
    controller.relayout(300, 140);
    LOKA_VERIFY((GetWindowLongPtrW(viewport, GWL_STYLE) & WS_HSCROLL) == 0);
    LOKA_VERIFY(offsetState.get().x == 0);
    const SCROLLINFO observedInfo5 = scrollInfo(viewport);
    LOKA_VERIFY(observedInfo5.nPage == static_cast<UINT>(seatHeight));
    LOKA_VERIFY(childRectInParent(imageWindow, viewport).left == 0);

    // A refused width cannot leave the requested origin stranded off screen.
    image->props.size(0, 10).attr(loka::app::ImageViewAttr().sizePolicy(loka::app::IMAGE_VIEW_SIZE_INTRINSIC));
    offset.state().set(loka::core::Frame(37, 0, 0, 0));
    controller.relayout(300, 140);
    LOKA_VERIFY(offsetState.get().x == 0);
    LOKA_VERIFY((GetWindowLongPtrW(viewport, GWL_STYLE) & WS_HSCROLL) == 0);
    controller.onChange(0, loka::app::scene::NODE_DIRTY_NONE, false);
  }
  LOKA_VERIFY(DestroyWindow(root));
}

void testWin32ScrollViewHorizontalMessagePublishesOffsetFact()
{
  HWND root = createHostWindow();
  LOKA_VERIFY(root);
  {
    Win32ScenePlatformController controller(root, loka::win32::Win32DisplayScale(96, loka::app::RailMetrics()));
    RegisterWin32BuiltInSupport(controller);
    ViewportFact offset(5);
    loka::app::scene::NodeState<loka::core::Frame> &offsetState = offset.state();
    loka::app::ScrollViewNode scrollView((loka::app::ScrollViewProps(offset.state())));
    addButtonColumn(scrollView, 8)->addChild(new loka::app::ImageViewNode(loka::app::ImageViewProps().size(1000, 10)));
    establishLayout(controller, &scrollView, 300, 140);
    HWND viewport = findChildWindowByClass(root, L"LOKA_SCROLL_VIEW");
    LOKA_VERIFY(viewport);
    const SCROLLINFO info = scrollInfo(viewport, SB_HORZ);
    const int maximum = maximumOffset(info);
    RECT client;
    LOKA_VERIFY(GetClientRect(viewport, &client));
    const int page = controller.displayScale().clientCapacityToLu(client.right);
    LOKA_VERIFY(info.nPage == static_cast<UINT>(page));
    SendMessageW(viewport, WM_HSCROLL, MAKEWPARAM(SB_LINERIGHT, 0), 0);
    LOKA_VERIFY(offsetState.get().x == 16 && offsetState.get().y == 5);
    SendMessageW(viewport, WM_HSCROLL, MAKEWPARAM(SB_ENDSCROLL, 0), 0);
    LOKA_VERIFY(offsetState.get().x == 16 && offsetState.get().y == 5);
    SendMessageW(viewport, WM_HSCROLL, MAKEWPARAM(SB_PAGERIGHT, 0), 0);
    SendMessageW(viewport, WM_HSCROLL, MAKEWPARAM(SB_ENDSCROLL, 0), 0);
    LOKA_VERIFY(offsetState.get().x == 16 + page && offsetState.get().y == 5);
    SendMessageW(viewport, WM_HSCROLL, MAKEWPARAM(SB_THUMBTRACK, 10), 0);
    LOKA_VERIFY(offsetState.get().x == 16 + page && offsetState.get().y == 5);
    const SCROLLINFO observedInfo6 = scrollInfo(viewport, SB_HORZ);
    LOKA_VERIFY(observedInfo6.nPos == 10);
    SendMessageW(viewport, WM_HSCROLL, MAKEWPARAM(SB_THUMBPOSITION, 10), 0);
    LOKA_VERIFY(offsetState.get().x == 10 && offsetState.get().y == 5);
    SendMessageW(viewport, WM_HSCROLL, MAKEWPARAM(SB_THUMBTRACK, 11), 0);
    SendMessageW(viewport, WM_HSCROLL, MAKEWPARAM(SB_ENDSCROLL, 0), 0);
    LOKA_VERIFY(offsetState.get().x == 11 && offsetState.get().y == 5);
    SendMessageW(viewport, WM_HSCROLL, MAKEWPARAM(SB_LINELEFT, 0), 0);
    LOKA_VERIFY(offsetState.get().x == 0 && offsetState.get().y == 5);
    offset.state().set(loka::core::Frame(maximum - 3, 5, 0, 0));
    SendMessageW(viewport, WM_HSCROLL, MAKEWPARAM(SB_LINERIGHT, 0), 0);
    LOKA_VERIFY(offsetState.get().x == maximum && offsetState.get().y == 5);
    SendMessageW(viewport, WM_HSCROLL, MAKEWPARAM(SB_LINERIGHT, 0), 0);
    LOKA_VERIFY(offsetState.get().x == maximum && offsetState.get().y == 5);
    controller.onChange(0, loka::app::scene::NODE_DIRTY_NONE, false);
  }
  LOKA_VERIFY(DestroyWindow(root));
}

void testWin32ScrollViewHorizontalWheelStepsOffset()
{
  UINT chars = 3;
  if (!SystemParametersInfoW(0x006C, 0, &chars, 0))
    chars = 3;
  if (chars == 0)
  {
    std::printf(
        "[skip] testWin32ScrollViewHorizontalWheelStepsOffset: system horizontal wheel scrolling is disabled\n");
    return;
  }
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
    ViewportFact offset(5);
    loka::app::scene::NodeState<loka::core::Frame> &offsetState = offset.state();
    loka::app::ScrollViewNode scrollView((loka::app::ScrollViewProps(offset.state())));
    addButtonColumn(scrollView, 8)->addChild(new loka::app::ImageViewNode(loka::app::ImageViewProps().size(1000, 10)));
    establishLayout(controller, &scrollView, 300, 140);
    HWND viewport = findChildWindowByClass(root, L"LOKA_SCROLL_VIEW");
    LOKA_VERIFY(viewport);
    const SCROLLINFO info = scrollInfo(viewport, SB_HORZ);
    const int maximum = maximumOffset(info);
    const UINT distance = chars == WHEEL_PAGESCROLL                 ? info.nPage
                          : chars > static_cast<UINT>(maximum) / 16 ? static_cast<UINT>(maximum)
                                                                    : chars * 16;
    const int expected = distance > static_cast<UINT>(maximum) ? maximum : static_cast<int>(distance);
    // WM_MOUSEHWHEEL: positive delta moves right, unlike WM_MOUSEWHEEL.
    SendMessageW(viewport, 0x020E, MAKEWPARAM(0, WHEEL_DELTA), 0);
    LOKA_VERIFY(offsetState.get().x == expected && offsetState.get().y == 5);
    SendMessageW(viewport, 0x020E, MAKEWPARAM(0, -WHEEL_DELTA), 0);
    LOKA_VERIFY(offsetState.get().x == 0 && offsetState.get().y == 5);
    SendMessageW(viewport, 0x020E, MAKEWPARAM(0, WHEEL_DELTA / 2), 0);
    LOKA_VERIFY(offsetState.get().x == 0 && offsetState.get().y == 5);
    // Exercise the actual root forwarding path with the original message ID.
    POINT point = {5, 5};
    LOKA_VERIFY(ClientToScreen(viewport, &point));
    LOKA_VERIFY(SetWindowPos(root, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE));
    LOKA_VERIFY(GetAncestor(WindowFromPoint(point), GA_ROOT) == root);
    SendMessageW(root, 0x020E, MAKEWPARAM(0, WHEEL_DELTA), MAKELPARAM(point.x, point.y));
    LOKA_VERIFY(offsetState.get().x == expected && offsetState.get().y == 5);
    controller.onChange(0, loka::app::scene::NODE_DIRTY_NONE, false);
  }
}

void testWin32ScrollViewHorizontalBarShortensChildHeight()
{
  HWND root = createHostWindow();
  LOKA_VERIFY(root);
  {
    Win32ScenePlatformController controller(root, loka::win32::Win32DisplayScale(96, loka::app::RailMetrics()));
    RegisterWin32BuiltInSupport(controller);
    ViewportFact offset(0);
    loka::app::ScrollViewNode scrollView((loka::app::ScrollViewProps(offset.state())));
    // A width-only surface fills the height it is offered: with the horizontal
    // bar present that offer is the shortened client, so the content ends at
    // the client bottom and no vertical range appears.
    loka::app::RectSurfaceNode *surface = new loka::app::RectSurfaceNode(loka::app::RectSurfaceProps().size(400, 0));
    scrollView.addChild(surface);
    establishLayout(controller, &scrollView, 300, 140);
    HWND viewport = findChildWindowByClass(root, L"LOKA_SCROLL_VIEW");
    LOKA_VERIFY(viewport);
    LOKA_VERIFY((GetWindowLongPtrW(viewport, GWL_STYLE) & WS_HSCROLL) != 0);
    RECT client;
    LOKA_VERIFY(GetClientRect(viewport, &client));
    const SCROLLINFO vertical = scrollInfo(viewport);
    LOKA_VERIFY(vertical.nPage == static_cast<UINT>(controller.displayScale().clientCapacityToLu(client.bottom)));
    LOKA_VERIFY(maximumOffset(vertical) == 0
                && "a height-filling child must not create a vertical range under the horizontal bar");
    controller.onChange(0, loka::app::scene::NODE_DIRTY_NONE, false);
  }
  LOKA_VERIFY(DestroyWindow(root));
}

void testWin32ScrollViewRefusedLayoutKeepsHorizontalMetrics()
{
  using namespace loka::app;
  HWND root = createHostWindow();
  LOKA_VERIFY(root != NULL);
  {
    Win32ScenePlatformController controller(root, loka::win32::Win32DisplayScale(96, RailMetrics()));
    RegisterWin32BuiltInSupport(controller);
    ViewportFact offset(0);
    offset.state().set(loka::core::Frame(50, 0, 0, 0));
    ScrollViewNode scroll((ScrollViewProps(offset.state())));
    ImageViewNode *leading = new ImageViewNode(ImageViewProps().size(400, 100));
    int visits = 0;
    TextNode *following = new GapDispatchText(visits);
    scroll.addChild(leading);
    scroll.addChild(following);
    establishLayout(controller, &scroll, 300, 100);
    HWND viewport = findChildWindowByClass(root, L"LOKA_SCROLL_VIEW");
    LOKA_VERIFY(viewport != NULL);
    LOKA_VERIFY((GetWindowLongPtrW(viewport, GWL_STYLE) & WS_HSCROLL) != 0);
    const SCROLLINFO before = scrollInfo(viewport, SB_HORZ);
    const int establishedX = offset.state().get().x;
    LOKA_VERIFY(maximumOffset(before) > 0 && establishedX == 50);
    visits = 0;
    // Wider content whose gap crosses the short limit: the traversal refuses,
    // and the horizontal bar must keep the range and position of the last
    // successful layout instead of showing the refused pass's numbers.
    leading->props.size(1000, static_cast<short>(SHRT_MAX - Win32SceneLayout::kRootInset - Win32SceneLayout::kGap + 1));
    controller.relayout(300, 100);
    const SCROLLINFO after = scrollInfo(viewport, SB_HORZ);
    LOKA_VERIFY(visits == 0);
    LOKA_VERIFY(after.nMin == before.nMin && after.nMax == before.nMax);
    LOKA_VERIFY(after.nPage == before.nPage && after.nPos == before.nPos);
    const int retainedX = offset.state().get().x;
    LOKA_VERIFY(retainedX == 50);
    controller.onChange(0, scene::NODE_DIRTY_NONE, false);
  }
  LOKA_VERIFY(DestroyWindow(root));
}

namespace
{
  HWND verticalControl(HWND viewport)
  {
    HWND bar = FindWindowExW(GetParent(viewport), NULL, L"SCROLLBAR", NULL);
    LOKA_VERIFY(bar);
    return bar;
  }

  void verifyBar(HWND viewport, int top, int bottom)
  {
    HWND frame = GetParent(viewport);
    HWND bar = verticalControl(viewport);
    RECT client;
    LOKA_VERIFY(GetClientRect(frame, &client));
    const RECT rect = childRectInParent(bar, frame);
    LOKA_VERIFY(rect.left == client.right - GetSystemMetrics(SM_CXVSCROLL));
    LOKA_VERIFY(rect.right == client.right && rect.top == top && rect.bottom == bottom);
  }
} // namespace

void testWin32ScrollViewInsetBarGeometry()
{
  using namespace loka::app;
  HWND root = createHostWindow();
  LOKA_VERIFY(root);
  ShowWindow(root, SW_SHOWNOACTIVATE);
  {
    Win32ScenePlatformController controller(root, loka::win32::Win32DisplayScale(96, RailMetrics()));
    RegisterWin32BuiltInSupport(controller);
    ViewportFact offset(0);
    ScrollViewNode scroll((ScrollViewProps(offset.state())));
    ImageViewNode *image = new ImageViewNode(ImageViewProps().size(100, 400));
    scroll.addChild(image);
    establishLayout(controller, &scroll, 300, 140);
    HWND frame = FindWindowExW(root, NULL, L"LOKA_SCROLL_FRAME", NULL);
    HWND viewport = FindWindowExW(frame, NULL, L"LOKA_SCROLL_VIEW", NULL);
    LOKA_VERIFY(frame && viewport);
    HWND bar = verticalControl(viewport);
    const int height = 140 - 2 * Win32SceneLayout::kRootInset;
    verifyBar(viewport, 0, height);
    RECT client;
    LOKA_VERIFY(GetClientRect(viewport, &client));
    LOKA_VERIFY(client.right == 300 - 2 * Win32SceneLayout::kRootInset - GetSystemMetrics(SM_CXVSCROLL));
    const int tops[] = {20, 0, 20};
    const int bottoms[] = {0, 25, 25};
    for (int i = 0; i != 3; ++i)
    {
      scroll.props.contentInsets(ScrollViewContentInsets::of(tops[i], bottoms[i]));
      controller.relayout(300, 140);
      verifyBar(viewport, tops[i], height - bottoms[i]);
    }
    scroll.props.contentInsets(ScrollViewContentInsets::of(80, 80));
    controller.relayout(300, 140);
    LOKA_VERIFY(!IsWindowVisible(bar));
    scroll.props.contentInsets(ScrollViewContentInsets::of(20, 1));
    image->props.size(400, 400);
    controller.relayout(300, 140);
    LOKA_VERIFY(IsWindowVisible(bar));
    LOKA_VERIFY((GetWindowLongPtrW(viewport, GWL_STYLE) & WS_HSCROLL) != 0);
    verifyBar(viewport, 20, height - GetSystemMetrics(SM_CYHSCROLL));
    // A foreign control notification is not our vertical scroll input.
    const int before = offset.state().get().y;
    SendMessageW(frame, WM_VSCROLL, MAKEWPARAM(SB_LINEDOWN, 0), reinterpret_cast<LPARAM>(viewport));
    LOKA_VERIFY(offset.state().get().y == before);
    SendMessageW(frame, WM_VSCROLL, MAKEWPARAM(SB_LINEDOWN, 0), reinterpret_cast<LPARAM>(bar));
    LOKA_VERIFY(offset.state().get().y == before + 16);
    controller.onChange(0, scene::NODE_DIRTY_NONE, false);
  }
  LOKA_VERIFY(DestroyWindow(root));
}

void testWin32ScrollViewInsetBarRefusalAndDpi()
{
  using namespace loka::app;
  HWND root = createHostWindow();
  LOKA_VERIFY(root);
  {
    Win32ScenePlatformController controller(root, loka::win32::Win32DisplayScale(96, RailMetrics()));
    RegisterWin32BuiltInSupport(controller);
    ViewportFact offset(0);
    ScrollViewNode scroll(ScrollViewProps(offset.state()).contentInsets(ScrollViewContentInsets::of(20, 30)));
    ImageViewNode *image = new ImageViewNode(ImageViewProps().size(100, 400));
    scroll.addChild(image);
    establishLayout(controller, &scroll, 300, 140);
    HWND viewport = findChildWindowByClass(root, L"LOKA_SCROLL_VIEW");
    HWND frame = GetParent(viewport);
    HWND bar = verticalControl(viewport);
    const SCROLLINFO before = scrollInfo(viewport);
    const RECT placement = childRectInParent(bar, frame);
    scroll.props.contentInsets(ScrollViewContentInsets::of(SHRT_MAX, SHRT_MAX));
    controller.relayout(300, 140);
    const RECT refused = childRectInParent(bar, frame);
    LOKA_VERIFY(EqualRect(&placement, &refused));
    const SCROLLINFO after = scrollInfo(viewport);
    LOKA_VERIFY(after.nMax == before.nMax && after.nPage == before.nPage && after.nPos == before.nPos);
    controller.updateDisplayScale(loka::win32::Win32DisplayScale(144, RailMetrics()));
    controller.relayout(300, 140);
    const loka::win32::Win32DisplayScale &scale = controller.displayScale();
    const int inset = Win32SceneLayout::kRootInset;
    const loka::core::Frame seat(inset, inset, 300 - 2 * inset, 140 - 2 * inset);
    const RECT native = scale.projectFrame(seat).r;
    const RECT top = scale.projectFrame(loka::core::Frame(seat.x, seat.y, seat.width, 20)).r;
    const RECT bottom = scale.projectFrame(loka::core::Frame(seat.x, seat.y + seat.height - 30, seat.width, 30)).r;
    const RECT dpi = childRectInParent(bar, frame);
    LOKA_VERIFY(dpi.top == top.bottom - top.top);
    LOKA_VERIFY(dpi.bottom == native.bottom - native.top - (bottom.bottom - bottom.top));
    LOKA_VERIFY(dpi.left == native.right - native.left - scale.systemMetric(SM_CXVSCROLL));
    LOKA_VERIFY(scrollInfo(viewport).nMax == before.nMax);
    controller.onChange(0, scene::NODE_DIRTY_NONE, false);
  }
  LOKA_VERIFY(DestroyWindow(root));

  // No successful pass: the initial committed clearance is empty, not props.
  root = createHostWindow();
  LOKA_VERIFY(root);
  {
    Win32ScenePlatformController controller(root, loka::win32::Win32DisplayScale(96, RailMetrics()));
    RegisterWin32BuiltInSupport(controller);
    ScrollViewNode scroll(ScrollViewProps().contentInsets(ScrollViewContentInsets::of(SHRT_MAX, 1)));
    scroll.addChild(new ImageViewNode(ImageViewProps().size(100, 400)));
    establishLayout(controller, &scroll, 300, 140);
    HWND viewport = findChildWindowByClass(root, L"LOKA_SCROLL_VIEW");
    LOKA_VERIFY(viewport);
    verifyBar(viewport, 0, 140 - 2 * Win32SceneLayout::kRootInset);
    controller.onChange(0, scene::NODE_DIRTY_NONE, false);
  }
  LOKA_VERIFY(DestroyWindow(root));
}

void testWin32ScrollViewSubordinateRetirement()
{
  using namespace loka::app;
  for (unsigned skip = 0; skip != 4; ++skip)
  {
    HWND root = createHostWindow();
    LOKA_VERIFY(root);
    {
      Win32ScenePlatformController controller(root, loka::win32::Win32DisplayScale(96, RailMetrics()));
      ScrollViewNode scroll((ScrollViewProps()));
      if (skip < 3)
        loka::win32::testing::failNativeChildWindowCreations(1, skip);
      Win32ScrollViewContext *context = new Win32ScrollViewContext(&controller, root, 0, 0, 200, 100, &scroll);
      loka::win32::testing::failNativeChildWindowCreations(0);
      LOKA_VERIFY(context->isValid() == (skip == 3));
      HWND frame = FindWindowExW(root, NULL, L"LOKA_SCROLL_FRAME", NULL);
      HWND viewport = frame ? FindWindowExW(frame, NULL, L"LOKA_SCROLL_VIEW", NULL) : NULL;
      HWND bar = frame ? FindWindowExW(frame, NULL, L"SCROLLBAR", NULL) : NULL;
      if (skip == 3)
      {
        LOKA_VERIFY(GetWindowLongPtrW(frame, GWLP_USERDATA) != 0);
        LOKA_VERIFY(GetWindowLongPtrW(viewport, GWLP_USERDATA) == 0);
        context->onFactChanged(scene::NODE_FACT_ATTACHED, scene::NODE_FACT_RETIRED);
      }
      if (frame)
        LOKA_VERIFY(GetWindowLongPtrW(frame, GWLP_USERDATA) == 0);
      delete context;
      // Native children survive until the drain, but cannot recover a context.
      const HWND survivors[] = {frame, viewport, bar};
      for (unsigned i = 0; i != 3; ++i)
      {
        if (!survivors[i])
          continue;
        LOKA_VERIFY(IsWindow(survivors[i]));
        SendMessageW(survivors[i], WM_VSCROLL, MAKEWPARAM(SB_LINEDOWN, 0), reinterpret_cast<LPARAM>(bar));
        SendMessageW(survivors[i], WM_MOUSEWHEEL, MAKEWPARAM(0, -WHEEL_DELTA), 0);
        SendMessageW(survivors[i], WM_COMMAND, 0, 0);
      }
      controller.drainNativeRetirements();
      LOKA_VERIFY(!FindWindowExW(root, NULL, L"LOKA_SCROLL_FRAME", NULL));
    }
    LOKA_VERIFY(DestroyWindow(root));
  }
}

void testWin32ScrollViewFrameTabAndArrowFocus()
{
  using namespace loka::app;
  HWND root = createHostWindow();
  LOKA_VERIFY(root);
  ShowWindow(root, SW_SHOW);
  {
    Win32ScenePlatformController controller(root, loka::win32::Win32DisplayScale(96, RailMetrics()));
    RegisterWin32BuiltInSupport(controller);
    ScrollViewNode scroll((ScrollViewProps()));
    addButtonColumn(scroll, 8);
    establishLayout(controller, &scroll, 300, 140);
    HWND frame = FindWindowExW(root, NULL, L"LOKA_SCROLL_FRAME", NULL);
    HWND viewport = FindWindowExW(frame, NULL, L"LOKA_SCROLL_VIEW", NULL);
    HWND bar = verticalControl(viewport);
    std::vector<HWND> buttons = directChildWindowsByClass(viewport, L"Button");
    LOKA_VERIFY(buttons.size() == 8);
    LOKA_VERIFY(GetNextDlgTabItem(root, NULL, FALSE) == buttons.front());
    LOKA_VERIFY(GetNextDlgTabItem(root, buttons.front(), TRUE) == buttons.back());
    LOKA_VERIFY(GetNextDlgTabItem(root, buttons.back(), FALSE) == buttons.front());
    SetFocus(buttons.front());
    LOKA_VERIFY(GetFocus() == buttons.front());
    RECT rect;
    LOKA_VERIFY(GetClientRect(bar, &rect));
    // Posting both messages allows the standard scrollbar's modal tracking
    // loop to consume the release, just like the native probe.
    LOKA_VERIFY(PostMessageW(bar, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(rect.right / 2, rect.bottom - 3)));
    LOKA_VERIFY(PostMessageW(bar, WM_LBUTTONUP, 0, MAKELPARAM(rect.right / 2, rect.bottom - 3)));
    MSG message;
    while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE))
    {
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
    LOKA_VERIFY(GetFocus() == buttons.front());
    controller.onChange(0, scene::NODE_DIRTY_NONE, false);
  }
  LOKA_VERIFY(DestroyWindow(root));
}

void testWin32ScrollViewInsetBarTinyAndFractionalSeat()
{
  using namespace loka::app;
  using namespace loka::app::scene;
  HWND root = createHostWindow();
  LOKA_VERIFY(root);
  ShowWindow(root, SW_SHOWNOACTIVATE);
  {
    Win32ScenePlatformController controller(root, loka::win32::Win32DisplayScale(144, RailMetrics()));
    ScrollViewNode node((ScrollViewProps()));
    Win32ScrollViewContext context(&controller, root, 1, 1, 101, 101, &node);
    LOKA_VERIFY(context.isValid());
    HWND viewport = FindWindowExW(context.hwnd(), NULL, L"LOKA_SCROLL_VIEW", NULL);
    HWND bar = FindWindowExW(context.hwnd(), NULL, L"SCROLLBAR", NULL);
    LOKA_VERIFY(viewport && bar);
    context.commitVerticalBarInsets(ScrollViewContentInsets::of(1, 3), loka::core::Frame(1, 1, 101, 101));
    // Odd absolute edges at 150% distinguish projecting two edges from a
    // second scale multiplication or scaling the inset length in isolation.
    context.relayout(2, 2, 101, 101);
    const loka::win32::Win32DisplayScale &scale = controller.displayScale();
    const RECT placed = childRectInParent(bar, context.hwnd());
    LOKA_VERIFY(placed.top == scale.projectEdge(3) - scale.projectEdge(2));
    LOKA_VERIFY(placed.bottom == scale.projectEdge(100) - scale.projectEdge(2));
    context.relayout(0, 0, 1, 10);
    RECT client;
    LOKA_VERIFY(GetClientRect(viewport, &client));
    LOKA_VERIFY(client.right == 0);
    const RECT narrow = childRectInParent(bar, context.hwnd());
    LOKA_VERIFY(narrow.left == 0 && narrow.right == scale.projectEdge(1));
    context.relayout(0, 0, 100, 0);
    LOKA_VERIFY(!IsWindowVisible(bar));
    context.onFactChanged(NODE_FACT_ATTACHED, NODE_FACT_RETIRED);
    controller.drainNativeRetirements();
  }
  LOKA_VERIFY(DestroyWindow(root));
}

namespace
{
  using namespace loka::app;
  using namespace loka::app::scene;
  using loka::core::Frame;
  typedef loka::app::testing::SettleTrace<ScrollTarget, Frame> ScrollTrace;

  class ScrollRequestStorage : private HeadlessStateOwner
  {
  public:
    NodeState<Frame> viewport;
    Request<ScrollTarget> request;
    explicit ScrollRequestStorage(bool immediate = true, int x = 0)
    {
      if (immediate)
      {
        StateBatchBase::CreateImmediateState(this, this->viewport, Frame(x, 0, 0, 0));
        StateBatchBase::CreateImmediateState(this, this->request, ScrollTarget::None());
      }
    }
  };

  struct ScrollValues
  {
    NodeState<Frame> &fact;
    std::vector<Frame> values;
    explicit ScrollValues(NodeState<Frame> &value)
        : fact(value)
    {
      this->fact.state()->bind(&changed, this, false);
    }
    ~ScrollValues()
    {
      this->fact.state()->unbind(&changed, this);
    }
    static void changed(void *data)
    {
      ScrollValues &self = *static_cast<ScrollValues *>(data);
      self.values.push_back(self.fact.get());
    }
  };

  struct ScrollRequestHost
  {
    HWND host;
    ScrollRequestHost()
        : host(createHostWindow())
    {
      LOKA_VERIFY(this->host);
    }
    ~ScrollRequestHost()
    {
      LOKA_VERIFY(DestroyWindow(this->host));
    }
  };

  struct ScrollRequestFixture : ScrollRequestHost
  {
    ScrollRequestStorage storage;
    Win32ScenePlatformController controller;
    ScrollViewNode node;
    BoxNode *content;
    ScrollValues observed;
    ScrollRequestFixture(int width = 600, int height = 700, int initialX = 0, int top = 0)
        : storage(true, initialX),
          controller(host, loka::win32::Win32DisplayScale(96, RailMetrics())),
          node(ScrollViewProps(storage.viewport)
                   .scrollTo(storage.request)
                   .contentInsets(ScrollViewContentInsets::of(top, 0))),
          content(new BoxNode(Box().width(width).height(height).props)),
          observed(storage.viewport)
    {
      LOKA_VERIFY(this->host);
      RegisterWin32BuiltInSupport(this->controller);
      this->node.addChild(this->content);
      this->controller.onChange(&this->node, NODE_DIRTY_NONE, false);
      ScrollTrace::instance().clear();
    }
    ~ScrollRequestFixture()
    {
      loka::win32::testing::failScrollInfoReads(0);
      loka::win32::testing::failNativeChildWindowCreations(0);
      this->controller.onChange(0, NODE_DIRTY_NONE, false);
    }
    void visit(int height = 160)
    {
      this->controller.relayout(300, height == 0 ? 2 * Win32SceneLayout::kRootInset : height);
    }
    HWND viewport() const
    {
      return findChildWindowByClass(this->host, L"LOKA_SCROLL_VIEW");
    }
    Win32ScrollViewContext &context()
    {
      return *static_cast<Win32ScrollViewContext *>(this->node.getContext());
    }
    void post(int x, int y)
    {
      this->observed.values.clear();
      ScrollTrace::instance().clear();
      this->storage.request.set(ScrollTarget::Origin(x, y));
    }
    void result(ScrollRequestResult expected)
    {
      LOKA_VERIFY(this->storage.request.get().isNone());
      LOKA_VERIFY(ScrollTrace::instance().size() != 0);
      const loka::app::testing::SettleTraceRow<ScrollTarget, Frame> &row =
          ScrollTrace::instance().at(ScrollTrace::instance().size() - 1);
      LOKA_VERIFY(row.count == 1 && row.seam[0] == expected);
    }
  };
} // namespace

void testWin32ScrollRequestBothAxesAndNoEcho()
{
  ScrollRequestFixture f(600, 700, 0, 12);
  f.visit();
  f.post(30, 40);
  f.visit();
  f.result(SCROLL_OK);
  LOKA_VERIFY(f.observed.values.size() == 1);
  LOKA_VERIFY(f.observed.values[0].x == 30 && f.observed.values[0].y == 40);
  LOKA_VERIFY(scrollInfo(f.viewport(), SB_HORZ).nPos == 30);
  LOKA_VERIFY(scrollInfo(f.viewport()).nPos == 40 + 12);
  LOKA_VERIFY(Win32InputDoor::scroll(f.context(), SB_VERT, SB_ENDSCROLL, 0));
  LOKA_VERIFY(f.observed.values.size() == 1);
  LOKA_VERIFY(Win32InputDoor::scroll(f.context(), SB_VERT, SB_LINEDOWN, 0));
  LOKA_VERIFY(f.observed.values.size() == 2);
  LOKA_VERIFY(f.observed.values[1].x == 30 && f.observed.values[1].y == 56);
  f.visit();
  f.post(30, 56);
  loka::win32::testing::failScrollInfoReads(1);
  f.visit(); // Equal target selects no axis and performs no read.
  f.result(SCROLL_OK);
  LOKA_VERIFY(f.observed.values.empty());
  loka::win32::testing::failScrollInfoReads(0);
}

void testWin32ScrollRequestClampReadBack()
{
  ScrollRequestFixture f;
  f.visit();
  f.post(30000, 30000);
  f.visit();
  f.result(SCROLL_OK);
  const SCROLLINFO x = scrollInfo(f.viewport(), SB_HORZ), y = scrollInfo(f.viewport());
  LOKA_VERIFY(f.observed.values.size() == 1);
  LOKA_VERIFY(f.observed.values[0].x == maximumOffset(x) && f.observed.values[0].y == maximumOffset(y));
  LOKA_VERIFY(x.nPos == maximumOffset(x) && y.nPos == maximumOffset(y));
}

void testWin32ScrollRequestReadFailures()
{
  // Both pre-reads precede both setters; either read-back failure reports nothing.
  for (unsigned skip = 0; skip != 4; ++skip)
  {
    ScrollRequestFixture f;
    f.visit();
    const Frame before = f.storage.viewport.get();
    f.post(30, 40);
    loka::win32::testing::failScrollInfoReads(1, skip);
    f.visit();
    f.result(SCROLL_UNAVAILABLE);
    LOKA_VERIFY(f.observed.values.empty() && f.storage.viewport.get() == before);
    LOKA_VERIFY(scrollInfo(f.viewport(), SB_HORZ).nPos == (skip < 2 ? 0 : 30));
    LOKA_VERIFY(scrollInfo(f.viewport()).nPos == (skip < 2 ? 0 : 40));
    f.visit();
    LOKA_VERIFY(scrollInfo(f.viewport(), SB_HORZ).nPos == 0 && scrollInfo(f.viewport()).nPos == 0);
    LOKA_VERIFY(f.observed.values.empty());
  }
}

void testWin32ScrollRequestAbsentAxesAndHeightless()
{
  for (int heightless = 0; heightless != 2; ++heightless)
  {
    ScrollRequestFixture f(heightless ? 600 : 20);
    const int height = heightless ? 0 : 160;
    f.visit(height);
    const Frame before = f.storage.viewport.get();
    f.post(30, 0);
    f.visit(height);
    f.result(SCROLL_UNAVAILABLE);
    LOKA_VERIFY(f.storage.viewport.get() == before && f.observed.values.empty());
    f.post(0, 40);
    f.visit(height);
    f.result(SCROLL_OK);
    LOKA_VERIFY(f.storage.viewport.get().x == 0 && f.storage.viewport.get().y == 40);
  }
}

void testWin32ScrollRequestHeightlessKeepsUnmovedX()
{
  ScrollRequestFixture f(600, 700, 20);
  f.visit(0);
  LOKA_VERIFY(f.storage.viewport.get().x == 20);
  LOKA_VERIFY(scrollInfo(f.viewport(), SB_HORZ).nPos == 0);
  f.post(20, 40);
  f.visit(0);
  f.result(SCROLL_OK);
  LOKA_VERIFY(f.observed.values.size() == 1);
  LOKA_VERIFY(f.observed.values[0].x == 20 && f.observed.values[0].y == 40);
}

void testWin32ScrollRequestZeroRange()
{
  ScrollRequestFixture f(20, 10);
  f.visit();
  const Frame before = f.storage.viewport.get();
  LOKA_VERIFY(maximumOffset(scrollInfo(f.viewport())) == 0);
  f.post(0, 40);
  f.visit();
  f.result(SCROLL_OK);
  LOKA_VERIFY(f.storage.viewport.get() == before && f.observed.values.empty());
}

void testWin32ScrollRequestRefusedVisits()
{
  {
    ScrollRequestFixture f(20, SHRT_MAX, 0, 1);
    f.post(0, 40);
    f.visit();
    f.result(SCROLL_UNAVAILABLE);
    LOKA_VERIFY(f.observed.values.empty());
  }
  {
    ScrollRequestFixture f;
    f.post(0, 40);
    loka::win32::testing::failNativeChildWindowCreations(1);
    f.visit();
    f.result(SCROLL_UNAVAILABLE);
    LOKA_VERIFY(!f.node.getContext() && f.observed.values.empty());
  }
  {
    // The inner boxes outlive the fixture that owns the nested node borrowing them.
    ScrollRequestStorage inner;
    ScrollRequestFixture f;
    ScrollViewNode *nested = new ScrollViewNode(ScrollViewProps(inner.viewport).scrollTo(inner.request));
    f.node.addChild(nested);
    inner.request.set(ScrollTarget::Origin(0, 40));
    f.visit();
    LOKA_VERIFY(inner.request.get().isNone() && inner.viewport.get() == Frame(0, 0, 0, 0));
    LOKA_VERIFY(!nested->getContext());
    LOKA_VERIFY(ScrollTrace::instance().size() != 0);
    LOKA_VERIFY(ScrollTrace::instance().at(0).seam[0] == SCROLL_UNAVAILABLE);
  }
}

void testWin32ScrollViewSettledInputSequences()
{
  // Characterization uses only pre-PR-3 APIs, so it also runs at 904b823b.
  HWND host = createHostWindow();
  LOKA_VERIFY(host);
  {
    Win32ScenePlatformController controller(host, loka::win32::Win32DisplayScale(96, RailMetrics()));
    RegisterWin32BuiltInSupport(controller);
    ViewportFact fact(0);
    ScrollViewNode node((ScrollViewProps(fact.state())));
    node.addChild(new BoxNode(Box().width(2000).height(2000).props));
    establishLayout(controller, &node, 300, 160);
    ScrollValues observed(fact.state());
    Win32ScrollViewContext &context = *static_cast<Win32ScrollViewContext *>(node.getContext());
    const int page = static_cast<int>(scrollInfo(context.viewportHwnd()).nPage);
    LOKA_VERIFY(Win32InputDoor::scroll(context, SB_VERT, SB_LINEDOWN, 0));
    LOKA_VERIFY(Win32InputDoor::scroll(context, SB_VERT, SB_PAGEDOWN, 0));
    LOKA_VERIFY(Win32InputDoor::scroll(context, SB_VERT, SB_THUMBTRACK, 200));
    LOKA_VERIFY(observed.values.size() == 2);
    LOKA_VERIFY(Win32InputDoor::scroll(context, SB_VERT, SB_THUMBPOSITION, 200));
    LOKA_VERIFY(Win32InputDoor::scroll(context, SB_VERT, SB_THUMBTRACK, 240));
    LOKA_VERIFY(observed.values.size() == 3);
    LOKA_VERIFY(Win32InputDoor::scroll(context, SB_VERT, SB_ENDSCROLL, 0));
    LOKA_VERIFY(Win32InputDoor::scroll(context, SB_VERT, SB_ENDSCROLL, 0));
    LOKA_VERIFY(observed.values.size() == 4);
    const int expected[] = {16, 16 + page, 200, 240};
    for (unsigned i = 0; i != 4; ++i)
      LOKA_VERIFY(observed.values[i].x == 0 && observed.values[i].y == expected[i]);
    for (int axis = 0; axis != 2; ++axis)
    {
      const int bar = axis == 0 ? SB_VERT : SB_HORZ;
      UINT lines = 3;
      if (!SystemParametersInfoW(axis == 0 ? SPI_GETWHEELSCROLLLINES : 0x006C, 0, &lines, 0))
        lines = 3;
      const bool pages = lines == WHEEL_PAGESCROLL;
      const UINT steps = pages ? 1 : lines;
      if (steps < 2)
        std::printf("[skip] wheel per-step mutation discrimination on axis %d: system steps=%u\n", bar, steps);
      const SCROLLINFO info = scrollInfo(context.viewportHwnd(), bar);
      int position = axis == 0 ? 240 : 0;
      const int step = pages ? static_cast<int>(info.nPage) : 16;
      std::vector<int> sequence;
      for (UINT i = 0; i != steps; ++i)
      {
        const int next = position + step > maximumOffset(info) ? maximumOffset(info) : position + step;
        if (next != position)
          sequence.push_back(next);
        position = next;
      }
      observed.values.clear();
      Win32InputDoor::mouseWheel(
          context, bar, MAKEWPARAM(0, static_cast<WORD>(axis == 0 ? -WHEEL_DELTA : WHEEL_DELTA)));
      LOKA_VERIFY(observed.values.size() == sequence.size());
      for (std::size_t i = 0; i != sequence.size(); ++i)
        LOKA_VERIFY((axis == 0 ? observed.values[i].y : observed.values[i].x) == sequence[i]);
    }
    controller.onChange(0, NODE_DIRTY_NONE, false);
  }
  LOKA_VERIFY(DestroyWindow(host));
}

namespace
{
  class ScrollRoot;
  struct ScrollRootProps : NodePropsBase<ScrollRootProps>
  {
    typedef ScrollRoot NodeType;
    struct TypeTag
    {
    };
    ScrollRequestStorage *storage;
    explicit ScrollRootProps(ScrollRequestStorage *value = 0)
        : storage(value)
    {
    }
    bool operator<(const PropsBase &rhs) const
    {
      return this->storage < static_cast<const ScrollRootProps &>(rhs).storage;
    }
  };
  class ScrollRoot : public StdCompositionBoundaryNodeBase<ScrollRootProps>
  {
  public:
    explicit ScrollRoot(const ScrollRootProps &props)
        : StdCompositionBoundaryNodeBase<ScrollRootProps>(props)
    {
      this->state(this->props.storage->viewport, Frame());
      this->state(this->props.storage->request, ScrollTarget::None());
    }
    virtual void composeNode(NodeComposition &composition)
    {
      composition.declare(
          ScrollView(ScrollViewProps(this->props.storage->viewport).scrollTo(this->props.storage->request))
          << Box().width(400).height(500));
    }
  };
  class ScrollSceneController : public Win32ScenePlatformController
  {
  public:
    unsigned focusReads;
    bool project;
    explicit ScrollSceneController(HWND host, bool initialProjection = true)
        : Win32ScenePlatformController(host, loka::win32::Win32DisplayScale(96, RailMetrics())),
          focusReads(0),
          project(initialProjection)
    {
      RegisterWin32BuiltInSupport(*this);
    }
    virtual void onChange(Node *root, NodeDirtyFlags flags, bool full)
    {
      Win32ScenePlatformController::onChange(root, this->project ? flags : NODE_DIRTY_NONE, full);
    }
    virtual void onBoundaryApply(Node *root,
                                 BoundaryNode *boundary,
                                 const BoundaryLocalApplyInfo &info,
                                 const PlatformApplyPlan &plan)
    {
      Win32ScenePlatformController::onBoundaryApply(root, boundary, info, plan);
      if (this->project && (info.hasLayoutWork || plan.hasLayoutWork()))
        this->relayout(0, 0);
    }
    virtual bool readNativeFocus(NodeContext *&out)
    {
      ++this->focusReads;
      out = 0;
      return true;
    }
  };
  class ScrollLogicalWindow : public Window
  {

  public:
    unsigned syncs, admissions, drains;
    ScrollLogicalWindow(ScrollRequestStorage &storage)
        : Window(0, WindowProps().scene(new Scene(Boundary<ScrollRoot>(ScrollRootProps(&storage))))),
          syncs(0),
          admissions(0),
          drains(0)
    {
    }
    virtual ~ScrollLogicalWindow()
    {
      this->unmountSceneForTeardown(*this->scene());
    }
    virtual bool hasLiveScenePlatform() const
    {
      return true;
    }
    virtual void applyNativeVisibility()
    {
      ++this->admissions;
    }
    virtual bool hasPendingScenePlatformSync() const
    {
      return true;
    }
    virtual void synchronizeScenePlatform()
    {
      ++this->syncs;
    }
    virtual void drainNativeRetirements()
    {
      ++this->drains;
    }
  };
  struct ScrollSceneFixture : ScrollRequestHost
  {
    ScrollRequestStorage storage;
    ScrollSceneController controller;
    ScrollLogicalWindow window;
    WindowAdmissionTestApp app;
    unsigned callbacks;
    bool probe, expectRun;
    ScrollSceneFixture()
        : storage(false),
          controller(host),
          window(storage),
          app(window),
          callbacks(0),
          probe(false),
          expectRun(false)
    {
      LOKA_VERIFY(window.scene()->mount(&controller));
      app.flush();
      loka::dsl::testing::SceneTestAccess::updateAttached(*window.scene(), true);
      window.scene()->requestInvalidate(NODE_DIRTY_LAYOUT);
      app.flush();
      while (window.scene()->hasPendingInvalidation())
        LOKA_VERIFY(window.scene()->flushInvalidation());
      storage.viewport.state()->bind(&changed, this, false);
    }
    ~ScrollSceneFixture()
    {
      storage.viewport.state()->unbind(&changed, this);
    }
    static void changed(void *data)
    {
      ScrollSceneFixture &f = *static_cast<ScrollSceneFixture *>(data);
      ++f.callbacks;
      if (!f.probe)
        return;
      LOKA_VERIFY(f.controller.borrowPhase().open());
      LOKA_VERIFY(f.window.scene()->isRunInProgress() == f.expectRun);
      const unsigned syncs = f.window.syncs, drains = f.window.drains;
      const unsigned admissions = f.window.admissions, focus = f.controller.focusReads;
      f.window.scene()->requestInvalidate(NODE_DIRTY_LAYOUT);
      LOKA_VERIFY(!f.window.scene()->flushInvalidation());
      LOKA_VERIFY(!f.window.flushSceneInvalidation());
      f.app.admitAndApplyWindows();
      f.app.reconcileFocus();
      LOKA_VERIFY(f.window.syncs == syncs && f.window.drains == drains);
      LOKA_VERIFY(f.window.admissions == admissions && f.controller.focusReads == focus);
    }
    void ordinaryFlush()
    {
      const unsigned syncs = window.syncs, drains = window.drains;
      const unsigned admissions = window.admissions, focus = controller.focusReads;
      app.flush();
      app.reconcileFocus();
      LOKA_VERIFY(window.syncs > syncs && window.drains > drains);
      LOKA_VERIFY(window.admissions > admissions && controller.focusReads > focus);
      LOKA_VERIFY(!window.scene()->hasPendingInvalidation());
    }
  };
  void sceneBorrow(bool probe)
  {
    ScrollSceneFixture f;
    f.expectRun = true;
    f.probe = probe;
    f.storage.request.set(ScrollTarget::Origin(25, 40));
    while (f.window.scene()->hasPendingInvalidation())
      LOKA_VERIFY(f.window.scene()->flushInvalidation());
    LOKA_VERIFY(f.storage.request.get().isNone());
    LOKA_VERIFY(f.storage.viewport.get().x == 25 && f.storage.viewport.get().y == 40);
    LOKA_VERIFY(f.callbacks == 1);
    f.probe = false;
    f.ordinaryFlush();
  }
  struct MountProbe
  {
    Scene &scene;
    ScrollSceneController &controller;
    unsigned calls;
    MountProbe(Scene &s, ScrollSceneController &c)
        : scene(s),
          controller(c),
          calls(0)
    {
    }
    static void changed(void *data)
    {
      MountProbe &p = *static_cast<MountProbe *>(data);
      ++p.calls;
      LOKA_VERIFY(p.controller.borrowPhase().open() && !p.scene.isRunInProgress());
      p.scene.requestInvalidate(NODE_DIRTY_LAYOUT);
      LOKA_VERIFY(!p.scene.flushInvalidation());
    }
  };
  void deferredMount()
  {
    ScrollRequestStorage storage(false);
    ScrollRequestHost native;
    ScrollSceneController controller(native.host, false);
    ScrollLogicalWindow window(storage);
    WindowAdmissionTestApp app(window);
    LOKA_VERIFY(window.scene()->mount(&controller));
    loka::dsl::testing::SceneTestAccess::updateAttached(*window.scene(), true);
    MountProbe probe(*window.scene(), controller);
    storage.viewport.state()->bind(&MountProbe::changed, &probe, false);
    controller.project = true;
    // Win32 onChange projects the mounted tree outside a Scene run as well.
    controller.onChange(loka::dsl::testing::SceneTestAccess::rootNode(*window.scene()), NODE_DIRTY_LAYOUT, false);
    LOKA_VERIFY(probe.calls == 1);
    LOKA_VERIFY(window.scene()->hasPendingInvalidation() && !controller.borrowPhase().open());
    storage.viewport.state()->unbind(&MountProbe::changed, &probe);
    app.flush();
    LOKA_VERIFY(!window.scene()->hasPendingInvalidation());
  }
} // namespace

void testWin32ScrollRequestSceneBorrow()
{
  sceneBorrow(true);
}
void testWin32ScrollRequestDeferredMount()
{
  deferredMount();
}

namespace
{
  struct NativeScrollStorage : ScrollRequestStorage
  {
    NodeState<Frame> followingExtent;
    NativeScrollStorage()
        : ScrollRequestStorage(false)
    {
    }
  };
  class NativeScrollRoot;
  struct NativeScrollProps : NodePropsBase<NativeScrollProps>
  {
    typedef NativeScrollRoot NodeType;
    struct TypeTag
    {
    };
    NativeScrollStorage *storage;
    explicit NativeScrollProps(NativeScrollStorage *value = 0)
        : storage(value)
    {
    }
    bool operator<(const PropsBase &rhs) const
    {
      return this->storage < static_cast<const NativeScrollProps &>(rhs).storage;
    }
  };
  class NativeScrollRoot : public StdCompositionBoundaryNodeBase<NativeScrollProps>
  {
  public:
    explicit NativeScrollRoot(const NativeScrollProps &props)
        : StdCompositionBoundaryNodeBase<NativeScrollProps>(props)
    {
      this->state(this->props.storage->viewport, Frame());
      this->state(this->props.storage->request, ScrollTarget::None());
      this->state(this->props.storage->followingExtent, Frame());
    }
    virtual void composeNode(NodeComposition &composition)
    {
      composition.declare(
          VStack() << (ScrollView(ScrollViewProps(this->props.storage->viewport).scrollTo(this->props.storage->request))
                       << Box().width(2000).height(2000))
                   << RectSurface().laidOutExtent(this->props.storage->followingExtent));
    }
  };
  struct NativeScrollProbe
  {
    Win32ScenePlatformController &controller;
    NativeScrollStorage &storage;
    unsigned visits, following;
    NativeScrollProbe(Win32ScenePlatformController &c, NativeScrollStorage &s)
        : controller(c),
          storage(s),
          visits(0),
          following(0)
    {
    }
    static void viewport(void *data)
    {
      NativeScrollProbe &p = *static_cast<NativeScrollProbe *>(data);
      ++p.visits;
      LOKA_VERIFY(p.controller.borrowPhase().open());
    }
    static void afterVisit(void *data)
    {
      NativeScrollProbe &p = *static_cast<NativeScrollProbe *>(data);
      ++p.following;
      // This sibling publishes after the ScrollView arm's visit scope has closed.
      LOKA_VERIFY(p.controller.borrowPhase().open());
      LOKA_VERIFY(p.storage.request.get().isNone());
      LOKA_VERIFY(p.storage.viewport.get().x == 30 && p.storage.viewport.get().y == 40);
    }
  };
} // namespace

void testWin32ScrollRequestNativeSizeOuterBorrow()
{
  NativeScrollStorage storage;
  NullPlatformContext platform;
  Win32Window window(&platform,
                     WindowProps()
                         .frame(50, 50, 360, 260)
                         .visible(true)
                         .scene(new Scene(Boundary<NativeScrollRoot>(NativeScrollProps(&storage)))));
  WindowAdmissionTestApp app(window);
  app.flush();
  Win32ScenePlatformController *controller = static_cast<Win32ScenePlatformController *>(
      loka::dsl::testing::SceneTestAccess::platformController(*window.scene()));
  LOKA_VERIFY(controller && window.hwnd());
  {
    BorrowScope posting(*controller);
    storage.request.set(ScrollTarget::Origin(30, 40));
  }
  LOKA_VERIFY(!storage.request.get().isNone());
  NativeScrollProbe probe(*controller, storage);
  storage.viewport.state()->bind(&NativeScrollProbe::viewport, &probe, false);
  storage.followingExtent.state()->bind(&NativeScrollProbe::afterVisit, &probe, false);
  SendMessageW(window.hwnd(), WM_SIZE, SIZE_RESTORED, MAKELPARAM(480, 320));
  LOKA_VERIFY(probe.visits > 0 && probe.following == 1);
  LOKA_VERIFY(storage.request.get().isNone() && !controller->borrowPhase().open());
  storage.viewport.state()->unbind(&NativeScrollProbe::viewport, &probe);
  storage.followingExtent.state()->unbind(&NativeScrollProbe::afterVisit, &probe);
  app.flush();
}

void testWin32ScrollRequestFirstVisit()
{
  ScrollRequestFixture f;
  LOKA_VERIFY(!f.node.getContext());
  f.post(30, 40);
  f.visit();
  f.result(SCROLL_OK);
  LOKA_VERIFY(f.observed.values.size() == 2);
  LOKA_VERIFY(f.observed.values[0].x == 0 && f.observed.values[0].y == 0);
  LOKA_VERIFY(f.observed.values[1].x == 30 && f.observed.values[1].y == 40);
  LOKA_VERIFY(scrollInfo(f.viewport(), SB_HORZ).nPos == 30 && scrollInfo(f.viewport()).nPos == 40);
}

namespace
{
  int layoutUnderRebasedClips(Win32ScenePlatformController &controller,
                              loka::app::ScrollViewNode &node,
                              const Win32ScenePlatformController::LayoutState &state,
                              const HWND *parents,
                              int depth,
                              int index = 0)
  {
    using namespace loka::app::scene;
    typedef loka::dsl::testing::Win32ScenePlatformTestAccess Access;
    if (index == depth)
      return Access::layoutNode(controller, &node, state);
    ProjectionParentScopeStack &scopes = Access::projectionParentScopes(controller);
    ProjectionParentScope clip;
    LOKA_VERIFY(scopes.current().deriveScrolled(
        parents[index], index == 0 ? 11 : 6, index == 0 ? 13 : 8, loka::core::Frame(0, 0, 300, 240), clip));
    ProjectionRebasedClipScopeGuard guard(scopes, clip);
    LOKA_VERIFY(guard.isActive());
    return layoutUnderRebasedClips(controller, node, state, parents, depth, index + 1);
  }
} // namespace

void testWin32ScrollViewRebasedClipSeat()
{
  using namespace loka::app;
  typedef loka::dsl::testing::Win32ScenePlatformTestAccess Access;
  // Red on the base: every CLIP case is refused by the active-depth entry test.
  // Root twins isolate native seat rounding from content traversal and facts.
  WNDCLASSW wc;
  ZeroMemory(&wc, sizeof(wc));
  wc.lpfnWndProc = DefWindowProcW;
  wc.hInstance = GetModuleHandleW(NULL);
  wc.lpszClassName = L"LOKA_TEST_SCROLL_CLIP";
  LOKA_VERIFY(RegisterClassW(&wc) || GetLastError() == ERROR_CLASS_ALREADY_EXISTS);
  const unsigned dpis[] = {96, 120, 144};
  struct ClipCase
  {
    int depth;
    bool horizontal;
  };
  const ClipCase cases[] = {{1, false}, {1, true}, {2, false}, {2, true}};
  for (unsigned d = 0; d != sizeof(dpis) / sizeof(dpis[0]); ++d)
  {
    int heightRoundingWitnesses = 0;
    for (unsigned c = 0; c != sizeof(cases) / sizeof(cases[0]); ++c)
    {
      // Four edge phases and two odd heights cover the 125% metric residues.
      for (int phase = 0; phase != 4; ++phase)
      {
        std::printf("[rebased clip] dpi=%u depth=%d horizontal=%d phase=%d\n",
                    dpis[d],
                    cases[c].depth,
                    cases[c].horizontal,
                    phase);
        HWND root = createHostWindow();
        LOKA_VERIFY(root);
        const loka::win32::Win32DisplayScale scale(dpis[d], RailMetrics());
        HWND parents[2];
        for (int i = 0; i != 2; ++i)
        {
          const RECT r = scale.projectFrame(loka::core::Frame(i == 0 ? 11 : 6, i == 0 ? 13 : 8, 300, 240)).r;
          parents[i] = CreateWindowExW(0,
                                       wc.lpszClassName,
                                       L"",
                                       WS_CHILD,
                                       r.left,
                                       r.top,
                                       r.right - r.left,
                                       r.bottom - r.top,
                                       i == 0 ? root : parents[0],
                                       NULL,
                                       wc.hInstance,
                                       NULL);
          LOKA_VERIFY(parents[i]);
        }
        {
          Win32ScenePlatformController controller(root, scale);
          RegisterWin32BuiltInSupport(controller);
          ViewportFact clippedFact(9), rootFact(9);
          ScrollViewNode clipped(ScrollViewProps(clippedFact.state()).contentInsets(ScrollViewContentInsets::of(3, 5)));
          ScrollViewNode twin(ScrollViewProps(rootFact.state()).contentInsets(ScrollViewContentInsets::of(3, 5)));
          addButtonColumn(clipped, 6);
          addButtonColumn(twin, 6);
          clipped.addChild(new BoxNode(Box().width(cases[c].horizontal ? 400 : 20).height(400).props));
          twin.addChild(new BoxNode(Box().width(cases[c].horizontal ? 400 : 20).height(400).props));
          const int x = 41 + phase, y = 51 + phase;
          const int height = cases[c].depth == 1 ? 101 : 103;
          const int seatX = x - (cases[c].depth == 1 ? 11 : 17);
          const int seatY = y - (cases[c].depth == 1 ? 13 : 21);
          const Win32ScenePlatformController::LayoutState content(x, y, 101, height, Win32SceneLayout::kGap);
          const Win32ScenePlatformController::LayoutState seat(seatX, seatY, 101, height, Win32SceneLayout::kGap);
          if (cases[c].horizontal)
          {
            if (scale.clientHeightBelowHorizontalBar(loka::core::Frame(x, y, 101, height))
                != scale.clientHeightBelowHorizontalBar(loka::core::Frame(seatX, seatY, 101, height)))
              ++heightRoundingWitnesses;
            else
              std::printf("[skip] m5 discrimination in this cell: equal rounded client heights\n");
          }
          // Two visits cover both construction and retained relayout.
          for (int visit = 0; visit != 2; ++visit)
          {
            const int rootBottom = Access::layoutNode(controller, &twin, seat);
            const int clipBottom = layoutUnderRebasedClips(controller, clipped, content, parents, cases[c].depth);
            LOKA_VERIFY(rootBottom - seatY == height && clipBottom - y == height);
            LOKA_VERIFY(Access::projectionParentScopes(controller).activeDepth() == 0);
            LOKA_VERIFY(clipped.getContext() && twin.getContext());
            Win32ScrollViewContext *actual = static_cast<Win32ScrollViewContext *>(clipped.getContext());
            Win32ScrollViewContext *expected = static_cast<Win32ScrollViewContext *>(twin.getContext());
            LOKA_VERIFY(GetParent(actual->hwnd()) == parents[cases[c].depth - 1]);
            const RECT frame = childRectInParent(actual->hwnd(), parents[cases[c].depth - 1]);
            const RECT wanted = scale.projectFrame(loka::core::Frame(seatX, seatY, 101, height)).r;
            LOKA_VERIFY(EqualRect(&frame, &wanted));
            HWND actualButton = findChildWindowByClass(actual->viewportHwnd(), L"Button");
            HWND expectedButton = findChildWindowByClass(expected->viewportHwnd(), L"Button");
            LOKA_VERIFY(actualButton && expectedButton);
            const RECT child = childRectInParent(actualButton, actual->viewportHwnd());
            const RECT rootChild = childRectInParent(expectedButton, expected->viewportHwnd());
            LOKA_VERIFY(EqualRect(&child, &rootChild));
            HWND actualBar = FindWindowExW(actual->hwnd(), NULL, L"SCROLLBAR", NULL);
            HWND expectedBar = FindWindowExW(expected->hwnd(), NULL, L"SCROLLBAR", NULL);
            LOKA_VERIFY(actualBar && expectedBar);
            const RECT bar = childRectInParent(actualBar, actual->hwnd());
            const RECT rootBar = childRectInParent(expectedBar, expected->hwnd());
            LOKA_VERIFY(EqualRect(&bar, &rootBar));
            LOKA_VERIFY(((GetWindowLongPtrW(actual->viewportHwnd(), GWL_STYLE) & WS_HSCROLL) != 0)
                        == cases[c].horizontal);
            LOKA_VERIFY(clippedFact.state().get() == rootFact.state().get());
            const SCROLLINFO actualInfo = scrollInfo(actual->viewportHwnd());
            const SCROLLINFO expectedInfo = scrollInfo(expected->viewportHwnd());
            LOKA_VERIFY(actualInfo.nMax == expectedInfo.nMax && actualInfo.nMax > 400);
            LOKA_VERIFY(actualInfo.nPage == expectedInfo.nPage && actualInfo.nPos == expectedInfo.nPos);
          }
        }
        // Node retirement and controller drain precede destruction of clip parents.
        LOKA_VERIFY(DestroyWindow(root));
      }
    }
    // At 96 DPI m4/m5 cannot differ; at 150% m5 also depends on the native metric.
    // The two 125% height residues guarantee at least one m5 witness.
    if (dpis[d] == 120)
      LOKA_VERIFY(heightRoundingWitnesses > 0);
  }
}
