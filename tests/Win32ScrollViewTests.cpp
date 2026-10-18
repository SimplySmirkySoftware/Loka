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
    LOKA_VERIFY(GetScrollInfo(viewport, bar, &info));
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
      LOKA_VERIFY((style & WS_VSCROLL) != 0);
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

    SendMessageW(viewport, WM_VSCROLL, MAKEWPARAM(SB_LINEDOWN, 0), 0);
    assert(offset.state().get().y == 16 &&
           "WM_VSCROLL must publish through the ScrollView NodeState door");
    controller.relayout(300, 140);
    const RECT after = childRectInParent(buttons[0], viewport);
    assert(after.top == before.top - 16);

    SendMessageW(viewport, WM_VSCROLL, MAKEWPARAM(SB_THUMBTRACK, 10), 0);
    assert(offset.state().get().y == 16 &&
           "thumb tracking is visual-only until the value settles");
    assert(scrollInfo(viewport).nPos == 10);
    SendMessageW(viewport, WM_VSCROLL, MAKEWPARAM(SB_THUMBPOSITION, 10), 0);
    assert(offset.state().get().y == 10 &&
           "thumb position must publish the settled fact");
    controller.relayout(300, 140);
    SendMessageW(viewport, WM_VSCROLL, MAKEWPARAM(SB_THUMBTRACK, 11), 0);
    assert(offset.state().get().y == 10);
    SendMessageW(viewport, WM_VSCROLL, MAKEWPARAM(SB_ENDSCROLL, 0), 0);
    assert(offset.state().get().y == 11 &&
           "end scroll must publish the last visual thumb position");
    controller.relayout(300, 140);

    // A click on an arrow or on the track sends its step and then, on release,
    // SB_ENDSCROLL, with no layout pass in between: the step must survive it.
    offset.state().set(loka::core::Frame(0, 0, 0, 0));
    controller.relayout(300, 140);
    SendMessageW(viewport, WM_VSCROLL, MAKEWPARAM(SB_LINEDOWN, 0), 0);
    SendMessageW(viewport, WM_VSCROLL, MAKEWPARAM(SB_ENDSCROLL, 0), 0);
    assert(offset.state().get().y == 16 && "an arrow click's end scroll must keep its line step");
    const SCROLLINFO clickInfo = scrollInfo(viewport);
    const int clickMaximum = maximumOffset(clickInfo);
    const int clickPage =
        clickInfo.nPage > static_cast<UINT>(clickMaximum) ? clickMaximum : static_cast<int>(clickInfo.nPage);
    SendMessageW(viewport, WM_VSCROLL, MAKEWPARAM(SB_PAGEDOWN, 0), 0);
    SendMessageW(viewport, WM_VSCROLL, MAKEWPARAM(SB_ENDSCROLL, 0), 0);
    assert(offset.state().get().y == (16 > clickMaximum - clickPage ? clickMaximum : 16 + clickPage)
           && "a track click's end scroll must keep its page step");
    assert(offset.state().get().y > 16 && "the page step must move in this fixture");
    controller.relayout(300, 140);

    const int rangeEnd = maximumOffset(scrollInfo(viewport));
    offset.state().set(loka::core::Frame(0, rangeEnd, 0, 0));
    controller.relayout(300, 140);
    SendMessageW(viewport, WM_VSCROLL, MAKEWPARAM(SB_LINEDOWN, 0), 0);
    assert(offset.state().get().y == rangeEnd &&
           "line scrolling at the range end must clamp without another write");
    offset.state().set(loka::core::Frame(0, rangeEnd - 3, 0, 0));
    SendMessageW(viewport, WM_VSCROLL, MAKEWPARAM(SB_LINEDOWN, 0), 0);
    assert(offset.state().get().y == rangeEnd && "a partial final line must clamp to the maximum");
    offset.state().set(loka::core::Frame(0, 3, 0, 0));
    SendMessageW(viewport, WM_VSCROLL, MAKEWPARAM(SB_LINEUP, 0), 0);
    assert(offset.state().get().y == 0 && "a partial first line must clamp to zero");

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
    assert(scrollInfo(viewport).nPos == (resizedMax > 0 ? resizedMax : 0));
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
    SendMessageW(viewport, WM_VSCROLL, MAKEWPARAM(SB_LINEDOWN, 0), 0);
    assert(outerOffset.state().get().y == 21);
    assert(innerOffset.state().get().y == 3);
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
