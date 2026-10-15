#include "app/layout/FallbackControlMetrics.hpp"
#include "Win32LiveResizeTests.hpp"
#include "Win32SceneLayout.hpp"

#include "support/TestVerify.hpp"

#include <cassert>
#include <cstdio>
#include <cwchar>
#include <windows.h>

#include "Win32BuiltInSupport.hpp"
#include "Win32ScenePlatformController.hpp"
#include "app/nodes/Text.hpp"
#include "app/nodes/controls/Button.hpp"
#include "app/nodes/controls/PopupMenu.hpp"
#include "app/nodes/nestable/RowColumn.hpp"
#include "app/nodes/nestable/ScrollView.hpp"
#include "app/scene/state/NodeState.hpp"
#include "app/scene/state/WriteSeat.hpp"
#include "context/Win32PopupMenuContext.hpp"
#include "core/State.hpp"
#include "core/StateTracker.hpp"
#include "platform/String.hpp"

namespace
{
  const wchar_t *kLiveResizeHostClass = L"LOKA_LIVE_RESIZE_TEST_HOST";
  int gLiveResizeHostPaintCount = 0;

  class RefusingUtf8String : public loka::platform::String
  {
  public:
    explicit RefusingUtf8String(int *attempts)
        : attempts_(attempts)
    {
    }

    virtual bool appendUtf8(std::string &) const
    {
      ++*this->attempts_;
      return false;
    }

  private:
    int *attempts_;
  };

  class ReentrantLayoutOffset
  {
  public:
    explicit ReentrantLayoutOffset(int initial)
        : storage_(loka::core::Frame(0, initial, 0, 0)),
          tracker_(),
          state_(&this->storage_, &this->tracker_),
          controller_(0),
          width_(0),
          height_(0),
          reentryCount_(0)
    {
      this->tracker_.addState(&this->storage_);
    }

    ~ReentrantLayoutOffset()
    {
      this->tracker_.setInvalidateCallback(0, 0);
    }

    loka::app::scene::NodeState<loka::core::Frame> &state()
    {
      return this->state_;
    }

    void reenterLayout(Win32ScenePlatformController *controller, int width, int height)
    {
      this->controller_ = controller;
      this->width_ = width;
      this->height_ = height;
      this->reentryCount_ = 0;
      this->tracker_.setInvalidateCallback(&ReentrantLayoutOffset::InvalidateThunk, this);
    }

    int reentryCount() const
    {
      return this->reentryCount_;
    }

  private:
    static void InvalidateThunk(void *userData)
    {
      ReentrantLayoutOffset *self = static_cast<ReentrantLayoutOffset *>(userData);
      assert(self && self->controller_);
      ++self->reentryCount_;
      self->controller_->relayout(self->width_, self->height_);
    }

    loka::core::MutableState<loka::core::Frame> storage_;
    loka::core::PushStateTracker tracker_;
    loka::app::scene::NodeState<loka::core::Frame> state_;
    Win32ScenePlatformController *controller_;
    int width_;
    int height_;
    int reentryCount_;

    ReentrantLayoutOffset(const ReentrantLayoutOffset &);
    ReentrantLayoutOffset &operator=(const ReentrantLayoutOffset &);
  };

  LRESULT CALLBACK LiveResizeHostWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
  {
    if (message == WM_PAINT)
    {
      ++gLiveResizeHostPaintCount;
      PAINTSTRUCT paint;
      HDC hdc = BeginPaint(hwnd, &paint);
      RECT rect;
      GetClientRect(hwnd, &rect);
      FillRect(hdc, &rect, reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1));
      EndPaint(hwnd, &paint);
      return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
  }

  void ensureLiveResizeHostClass()
  {
    WNDCLASSW existing;
    if (GetClassInfoW(GetModuleHandleW(NULL), kLiveResizeHostClass, &existing))
    {
      return;
    }
    WNDCLASSW definition;
    ZeroMemory(&definition, sizeof(definition));
    definition.lpfnWndProc = LiveResizeHostWndProc;
    definition.hInstance = GetModuleHandleW(NULL);
    definition.lpszClassName = kLiveResizeHostClass;
    definition.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    LOKA_VERIFY(RegisterClassW(&definition));
  }

  HWND createLiveResizeHost()
  {
    ensureLiveResizeHostClass();
    HWND hwnd = CreateWindowExW(0,
                                kLiveResizeHostClass,
                                L"live-resize-test-host",
                                WS_POPUP,
                                -10000,
                                -10000,
                                400,
                                260,
                                NULL,
                                NULL,
                                GetModuleHandleW(NULL),
                                NULL);
    LOKA_VERIFY(hwnd);
    ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    UpdateWindow(hwnd);
    return hwnd;
  }

  // #857: the same binary measured 0 host paints over ssh versus 1 interactively.
  bool hostReceivesPaint(HWND host)
  {
    gLiveResizeHostPaintCount = 0;
    RedrawWindow(host, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW);
    const bool receivesPaint = gLiveResizeHostPaintCount != 0;
    gLiveResizeHostPaintCount = 0;
    return receivesPaint;
  }

  // Only the paint-count pin depends on paint delivery; the rest of each test
  // (layout, clamp, reentry) still runs on a desktop that delivers none.
  void printPaintCountSkip(const char *test)
  {
    std::printf("[skip] %s: this desktop delivers no WM_PAINT to the host (non-interactive session); "
                "the paint-count pin cannot discriminate here\n",
                test);
    std::fflush(stdout);
  }

  RECT childRectInParent(HWND child, HWND parent)
  {
    RECT rect;
    LOKA_VERIFY(GetWindowRect(child, &rect));
    MapWindowPoints(NULL, parent, reinterpret_cast<POINT *>(&rect), 2);
    return rect;
  }
} // namespace

// #549: every Text relayout used to synchronously redraw its parent subtree.
// Two Text children therefore presented two incomplete layouts during one
// root resize. The native layout pass positions every child without repaint,
// then presents the completed root exactly once.
void testWin32LayoutPresentsRootOnceAfterPositioningChildren()
{
  std::printf("\n==== [testWin32LayoutPresentsRootOnceAfterPositioningChildren] start ====\n");
  HWND root = createLiveResizeHost();
  const bool hostPaints = hostReceivesPaint(root);
  {
    Win32ScenePlatformController controller(root, loka::win32::Win32DisplayScale(96, loka::app::RailMetrics()));
    RegisterWin32BuiltInSupport(controller);
    loka::app::StackNode column((loka::app::StackProps(loka::app::STACK_AXIS_COLUMN)));
    column.addChild(new loka::app::TextNode((loka::app::TextProps("first"))));
    column.addChild(new loka::app::TextNode((loka::app::TextProps("second"))));

    controller.onChange(&column, loka::app::scene::NODE_DIRTY_NONE, false);
    controller.relayout(360, 220);
    gLiveResizeHostPaintCount = 0;
    controller.relayout(520, 300);

    if (hostPaints)
    {
      assert(gLiveResizeHostPaintCount == 1 && "one native layout pass must present one completed root frame");
    }
    else
    {
      printPaintCountSkip("testWin32LayoutPresentsRootOnceAfterPositioningChildren");
    }
    controller.onChange(0, loka::app::scene::NODE_DIRTY_NONE, false);
  }
  LOKA_VERIFY(DestroyWindow(root));
  std::printf("==== [testWin32LayoutPresentsRootOnceAfterPositioningChildren] PASSED ====\n");
}

// A ScrollView range clamp publishes through NodeState during layout. Scene's
// tracker callback can synchronously re-enter the full projection path. The
// nested traversal must share the outer native pass so debug builds do not
// assert and the root still presents only the completed outermost frame.
void testWin32ReentrantLayoutSharesOutermostPresentation()
{
  std::printf("\n==== [testWin32ReentrantLayoutSharesOutermostPresentation] start ====\n");
  HWND root = createLiveResizeHost();
  const bool hostPaints = hostReceivesPaint(root);
  {
    Win32ScenePlatformController controller(root, loka::win32::Win32DisplayScale(96, loka::app::RailMetrics()));
    RegisterWin32BuiltInSupport(controller);
    const int contentHeight = 6 * controller.rowLayoutMetrics().buttonHeight + 5 * Win32SceneLayout::kGap;
    ReentrantLayoutOffset offset(100);
    loka::app::ScrollViewNode scrollView((loka::app::ScrollViewProps(offset.state())));
    loka::app::StackNode *column =
        new loka::app::StackNode((loka::app::StackProps(loka::app::STACK_AXIS_COLUMN)));
    for (int index = 0; index < 6; ++index)
    {
      column->addChild(new loka::app::ButtonNode((loka::app::ButtonProps())));
    }
    scrollView.addChild(column);

    controller.onChange(&scrollView, loka::app::scene::NODE_DIRTY_NONE, false);
    controller.relayout(300, 100);
    const int initialMax = contentHeight - (100 - 2 * Win32SceneLayout::kRootInset);
    const int resizedMax = contentHeight - (240 - 2 * Win32SceneLayout::kRootInset);
    assert(offset.state().get().y == (initialMax < 100 ? initialMax : 100));
    offset.reenterLayout(&controller, 300, 240);
    gLiveResizeHostPaintCount = 0;

    controller.relayout(300, 240);

    assert(offset.state().get().y == (resizedMax > 0 ? resizedMax : 0));
    assert(offset.reentryCount() == 1 && "one range clamp must cause one synchronous layout reentry");
    if (hostPaints)
    {
      assert(gLiveResizeHostPaintCount == 1 && "nested layout must share one outermost native presentation");
    }
    else
    {
      printPaintCountSkip("testWin32ReentrantLayoutSharesOutermostPresentation");
    }
    controller.onChange(0, loka::app::scene::NODE_DIRTY_NONE, false);
  }
  LOKA_VERIFY(DestroyWindow(root));
  std::printf("==== [testWin32ReentrantLayoutSharesOutermostPresentation] PASSED ====\n");
}

// #549: geometry refresh used to reset and repopulate every ComboBox item,
// then reapply selection. Item data is native identity that survives a pure
// geometry change only when relayout stops rebuilding the content channel.
void testWin32PopupRelayoutPreservesNativeItems()
{
  std::printf("\n==== [testWin32PopupRelayoutPreservesNativeItems] start ====\n");
  HWND root = createLiveResizeHost();
  {
    Win32ScenePlatformController controller(root, loka::win32::Win32DisplayScale(96, loka::app::RailMetrics()));
    const char *itemLiterals[] = {"Apple", "Banana", "Cherry"};
    loka::core::MutableState<int> selection(1);
    int materializeAttempts = 0;
    loka::app::PopupMenuProps props;
    props.items(itemLiterals, 3).selectedIndex(loka::app::scene::WriteSeat<int>(&selection));
    loka::app::PopupMenuNode node(props);
    Win32PopupMenuContext context(&controller, root, 10, 10, 120, 24, &node);
    HWND popup = context.hwnd();
    LOKA_VERIFY(popup);
    LOKA_VERIFY(SendMessageW(popup, CB_GETCOUNT, 0, 0) == 3);
    const LPARAM itemIdentity = static_cast<LPARAM>(0x549);
    LOKA_VERIFY(SendMessageW(popup, CB_SETITEMDATA, 0, itemIdentity) != CB_ERR);

    context.relayout(30, 40, 180, 24);

    const RECT rect = childRectInParent(popup, root);
    assert(rect.left == 30 && rect.top == 40 && rect.right - rect.left == 180);
    LOKA_VERIFY(SendMessageW(popup, CB_GETCOUNT, 0, 0) == 3);
    LOKA_VERIFY(SendMessageW(popup, CB_GETITEMDATA, 0, 0) == itemIdentity);
    LOKA_VERIFY(SendMessageW(popup, CB_GETCURSEL, 0, 0) == 1);

    const char *replacementLiterals[] = {"One", "Two"};
    node.props.items(replacementLiterals, 2);
    context.relayout(30, 40, 180, 24);
    LOKA_VERIFY(SendMessageW(popup, CB_GETCOUNT, 0, 0) == 2);
    wchar_t firstItem[16];
    LOKA_VERIFY(SendMessageW(popup, CB_GETLBTEXT, 0, reinterpret_cast<LPARAM>(firstItem)) != CB_ERR);
    assert(std::wcscmp(firstItem, L"One") == 0);
    LOKA_VERIFY(SendMessageW(popup, CB_GETCURSEL, 0, 0) == 1);

    loka::Vector<loka::core::String> failingItems;
    failingItems.push_back(loka::core::String::FromPlatform(
        loka::core::Managed<loka::platform::String>::Wrap(new RefusingUtf8String(&materializeAttempts))));
    node.props.items(failingItems);
    context.relayout(30, 40, 180, 24);
    context.relayout(30, 40, 180, 24);
    assert(materializeAttempts == 2 && "failed native content application must retry on the next relayout");

    context.onFactChanged(loka::app::scene::NODE_FACT_ATTACHED, loka::app::scene::NODE_FACT_RETIRED);
    controller.drainNativeRetirements();
  }
  LOKA_VERIFY(DestroyWindow(root));
  std::printf("==== [testWin32PopupRelayoutPreservesNativeItems] PASSED ====\n");
}
