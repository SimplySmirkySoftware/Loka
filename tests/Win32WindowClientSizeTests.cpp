#include "support/WindowAdmissionTestApp.hpp"
#include "support/NativeCloseTestConfig.hpp"
#include "testing/app/AppTestAccess.hpp"
#include "Win32WindowClientSizeTests.hpp"
#include "support/TestVerify.hpp"
#include <cassert>
#include <cstdio>
#include <cwchar>
#include <windows.h>
#include "Win32App.hpp"
#include "Win32Window.hpp"
#include "Win32PlatformContext.hpp"
#include "testing/app/WindowTestAccess.hpp"
#include "Win32ScenePlatformController.hpp"
#include "app/Menu.hpp"
#include "app/MenuComposition.hpp"
#include "core/State.hpp"
#include "core/util/StateTrackerGuard.hpp"
#include "platform/null/NullPlatformContext.hpp"
#include "platform/Win32DisplayScale.hpp"

namespace
{
  class DefaultMenuConfig : public AppConfigurable
  {
  public:
    DefaultMenuConfig(PlatformContext *platform, const loka::app::MenuBarDefinition &bar)
        : AppConfigurable(platform), bar_(bar) {}
    virtual void compose(AppComposition &) {}
    virtual void composeDefaultMenu(loka::app::MenuComposition &c) { c << this->bar_; }
  private:
    const loka::app::MenuBarDefinition &bar_;
  };
  class MenuApplyingWin32App : public Win32App
  {
  public:
    explicit MenuApplyingWin32App(AppConfigurable *config = 0)
        : Win32App(config, GetModuleHandle(NULL), SW_SHOW)
    {
    }

    virtual ~MenuApplyingWin32App() {}
    void bootstrap() { App::run(); }
    /** Borrow the fixture Window only for a production completion. */
    void complete(Window &window)
    {
      if (!this->group_)
        this->group_ = new AppComponentGroup(std::vector<AppComponent *>());
      this->group_->adopt(&window);
      loka::core::Operation turn;
      turn.settle();
      this->admitAndApplyWindows();
      turn.close();
      this->reclaimWindows();
      this->group_->build();
    }
  protected:
    // Empty bootstrap is intentional: complete() later borrows the test Window.
    virtual void bootstrapWindowless() {}
  };

  void setWindowVisibility(Win32Window &window, bool visible)
  {
    {
      loka::core::StateTrackerGuard guard(window.getTracker());
      window.visibilityState().set(visible, true);
    }
    WindowAdmissionTestApp admission(window);
    admission.flush();
  }

  void setWindowFrame(Win32Window &window, const loka::core::Frame &frame)
  {
    loka::core::StateTrackerGuard guard(window.getTracker());
    window.frameState().set(frame);
  }

  loka::win32::Win32DisplayScale windowScale(HWND hwnd)
  {
    return loka::win32::Win32DisplayScale(
        loka::win32::Win32DisplayScale::forWindow(hwnd).dpi(),
        loka::win32::DefaultRailMetrics());
  }

  void assertLogicalClientSize(HWND hwnd, int expectedWidth, int expectedHeight)
  {
    RECT client;
    LOKA_VERIFY(GetClientRect(hwnd, &client));
    const loka::win32::Win32DisplayScale scale =
        windowScale(hwnd);
    const int expectedNativeWidth = scale.clientLengthToNative(expectedWidth).px;
    const int expectedNativeHeight = scale.clientLengthToNative(expectedHeight).px;
    const int actualWidth = client.right - client.left;
    const int actualHeight = client.bottom - client.top;
    LOKA_VERIFY(scale.clientCapacityToLu(actualWidth) == expectedWidth);
    LOKA_VERIFY(scale.clientCapacityToLu(actualHeight) == expectedHeight);
    printf("  declared=%dx%d client=%dx%d at %d%%\n",
           expectedWidth,
           expectedHeight,
           actualWidth,
           actualHeight,
           scale.percent());
    fflush(stdout);
    LOKA_VERIFY(actualWidth == expectedNativeWidth &&
                "declared window width must describe the Win32 client area");
    LOKA_VERIFY(actualHeight == expectedNativeHeight &&
                "declared window height must describe the Win32 client area");
  }

  RECT readWindowRect(HWND hwnd)
  {
    RECT rect;
    LOKA_VERIFY(GetWindowRect(hwnd, &rect));
    return rect;
  }

  // The clamp targets the monitor the placed rectangle mostly covers, so on a
  // multi-monitor host a frame pushed past the primary's edge may legitimately
  // land on the neighbour: compare against the window's own monitor.
  RECT readWindowWorkArea(HWND hwnd)
  {
    MONITORINFO monitorInfo = {};
    monitorInfo.cbSize = sizeof(monitorInfo);
    LOKA_VERIFY(GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST),
                               &monitorInfo));
    return monitorInfo.rcWork;
  }

  void assertWindowInsideItsWorkArea(HWND hwnd)
  {
    const RECT work = readWindowWorkArea(hwnd);
    const RECT outer = readWindowRect(hwnd);
    LOKA_VERIFY(outer.left >= work.left);
    LOKA_VERIFY(outer.top >= work.top);
    LOKA_VERIFY(outer.right <= work.right);
    LOKA_VERIFY(outer.bottom <= work.bottom);
  }

  void pumpWindowMessages(unsigned int settleCycles)
  {
    for (unsigned int cycle = 0; cycle < settleCycles; ++cycle)
    {
      MSG message;
      while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE))
      {
        TranslateMessage(&message);
        DispatchMessageW(&message);
      }
    }
  }

  bool sameRect(const RECT &lhs, const RECT &rhs)
  {
    return lhs.left == rhs.left && lhs.top == rhs.top &&
           lhs.right == rhs.right && lhs.bottom == rhs.bottom;
  }

  void CountFrameNotification(void *userData)
  {
    int *count = static_cast<int *>(userData);
    if (count)
    {
      ++*count;
    }
  }

  struct Win32WidthIsNarrow : public loka::core::DerivedState<bool>::EvalFn
  {
    const loka::core::State<loka::core::Frame> *frame;

    explicit Win32WidthIsNarrow(const loka::core::State<loka::core::Frame> *frameState)
        : frame(frameState)
    {
    }

    virtual bool operator()()
    {
      return this->frame && this->frame->get().hasSize() && this->frame->get().width < 480;
    }
  };

  void resizeLogicalClient(HWND hwnd, int width, int height)
  {
    const loka::win32::Win32DisplayScale scale =
        windowScale(hwnd);
    RECT outer = {0,
                  0,
                  scale.clientLengthToNative(width).px,
                  scale.clientLengthToNative(height).px};
    const DWORD style = static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_STYLE));
    const DWORD exStyle = static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_EXSTYLE));
    LOKA_VERIFY(scale.adjustWindowRect(
        outer, style, GetMenu(hwnd) ? TRUE : FALSE, exStyle));
    LOKA_VERIFY(SetWindowPos(hwnd,
                            NULL,
                            0,
                            0,
                            outer.right - outer.left,
                            outer.bottom - outer.top,
                            SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE));
  }
} // namespace

void testWin32DisplayScaleProjectsLogicalEdges()
{
  printf("\n==== [testWin32DisplayScaleProjectsLogicalEdges] start ====\n");
  const loka::win32::Win32DisplayScale scale125(120);
  const loka::core::Frame leftLogical(1, 2, 3, 5);
  const loka::core::Frame rightLogical(4, 2, 7, 5);
  RECT leftNative;
  RECT rightNative;
  scale125.projectFrame(leftLogical, leftNative);
  scale125.projectFrame(rightLogical, rightNative);
  LOKA_VERIFY(leftNative.right == rightNative.left
              && "adjacent logical rectangles must retain one shared native edge");
  LOKA_VERIFY(scale125.percent() == 125);

  const loka::win32::Win32DisplayScale scale200(192);
  RECT doubled;
  scale200.projectFrame(loka::core::Frame(10, 20, 30, 40), doubled);
  LOKA_VERIFY(doubled.left == 20 && doubled.top == 40
              && doubled.right == 80 && doubled.bottom == 120);
  RECT desktopWindow = {1920, 240, 2520, 1040};
  const loka::core::Frame windowFrame = scale200.windowContentFrameFromNative(
      desktopWindow, 600, 800);
  LOKA_VERIFY(windowFrame == loka::core::Frame(1920, 240, 300, 400)
              && "a top-level desktop origin must not be scaled by one monitor's DPI");
  const loka::win32::Win32DisplayScale scale150(144);
  LOKA_VERIFY(scale200.scaleLengthFrom(scale150, 12) == 16
              && scale200.scaleLengthFrom(scale150, -12) == -16
              && "system-DPI metrics must convert edge deltas to the target DPI");

  const loka::win32::Win32DisplayScale invalid(0);
  LOKA_VERIFY(invalid.dpi() == 96 && invalid.percent() == 100
              && "an absent DPI capability must degrade to the unscaled projection");
  loka::win32::Win32DisplayScale untouched(192);
  LOKA_VERIFY(!loka::win32::Win32DisplayScale::queryForWindow(NULL, untouched));
  LOKA_VERIFY(untouched.dpi() == 192
              && "a declined display-scale query must leave its output untouched");
  printf("==== [testWin32DisplayScaleProjectsLogicalEdges] PASSED ====\n");
}

void testWin32DpiChangeAcceptsSuggestedWindowRect()
{
  printf("\n==== [testWin32DpiChangeAcceptsSuggestedWindowRect] start ====\n");
  WindowProps props;
  props.frame(40, 40, 257, 163).visible(false);
  NullPlatformContext context;
  Win32Window window(&context, props);
  setWindowVisibility(window, true);
  HWND hwnd = window.hwnd();
  LOKA_VERIFY(hwnd && IsWindow(hwnd));

  RECT suggested = {120, 140, 720, 640};
  SendMessageW(hwnd,
               WM_DPICHANGED,
               MAKEWPARAM(192, 192),
               reinterpret_cast<LPARAM>(&suggested));
  const RECT actual = readWindowRect(hwnd);
  LOKA_VERIFY(sameRect(actual, suggested)
              && "WM_DPICHANGED must accept Win32's suggested top-level rectangle");

  setWindowVisibility(window, false);
  printf("==== [testWin32DpiChangeAcceptsSuggestedWindowRect] PASSED ====\n");
}

void testWin32WindowFrameIsClampedToWorkArea()
{
  printf("\n==== [testWin32WindowFrameIsClampedToWorkArea] start ====\n");
  MONITORINFO monitorInfo = {};
  monitorInfo.cbSize = sizeof(monitorInfo);
  LOKA_VERIFY(GetMonitorInfoW(MonitorFromWindow(NULL, MONITOR_DEFAULTTOPRIMARY),
                             &monitorInfo));
  const RECT work = monitorInfo.rcWork;
  NullPlatformContext context;
  WindowProps props;
  props.frame(work.right - 50, work.bottom - 50, 257, 163).visible(false);
  Win32Window window(&context, props);
  setWindowVisibility(window, true);
  HWND hwnd = window.hwnd();
  LOKA_VERIFY(hwnd && IsWindow(hwnd));
  assertWindowInsideItsWorkArea(hwnd);
  assertLogicalClientSize(hwnd, 257, 163);

  setWindowFrame(window, loka::core::Frame(work.right - 25, work.bottom - 25, 257, 163));
  pumpWindowMessages(4);
  assertWindowInsideItsWorkArea(hwnd);
  assertLogicalClientSize(hwnd, 257, 163);

  // A DPI transition arrives with a rectangle Windows sized for the
  // destination monitor but never fitted to its work area.
  const RECT placedWork = readWindowWorkArea(hwnd);
  RECT suggested = {placedWork.right - 40, placedWork.bottom - 40,
                    placedWork.right + 560, placedWork.bottom + 460};
  SendMessageW(hwnd,
               WM_DPICHANGED,
               MAKEWPARAM(192, 192),
               reinterpret_cast<LPARAM>(&suggested));
  assertWindowInsideItsWorkArea(hwnd);
  setWindowVisibility(window, false);

  const int oversizedWidth = work.right - work.left + 400;
  const int oversizedHeight = work.bottom - work.top + 400;
  WindowProps oversizedProps;
  oversizedProps.frame(work.left, work.top, oversizedWidth, oversizedHeight).visible(false);
  Win32Window oversizedWindow(&context, oversizedProps);
  setWindowVisibility(oversizedWindow, true);
  HWND oversizedHwnd = oversizedWindow.hwnd();
  LOKA_VERIFY(oversizedHwnd && IsWindow(oversizedHwnd));
  assertWindowInsideItsWorkArea(oversizedHwnd);
  RECT client;
  LOKA_VERIFY(GetClientRect(oversizedHwnd, &client));
  const loka::core::Frame placed = oversizedWindow.nativeFrame().get();
  const loka::core::Frame actual =
      windowScale(oversizedHwnd)
          .windowContentFrameFromNative(readWindowRect(oversizedHwnd),
                                        client.right - client.left,
                                        client.bottom - client.top);
  LOKA_VERIFY(placed == actual);
  LOKA_VERIFY(placed.width < oversizedWidth);
  LOKA_VERIFY(placed.height < oversizedHeight);
  setWindowVisibility(oversizedWindow, false);
  printf("==== [testWin32WindowFrameIsClampedToWorkArea] PASSED ====\n");
}

void testWin32DeclaredWindowSizeMeansClientArea()
{
  printf("\n==== [testWin32DeclaredWindowSizeMeansClientArea] start ====\n");
  const int declaredWidth = 257;
  const int declaredHeight = 163;
  loka::app::MenuBarDefinition menuBar;
  menuBar << (loka::app::Menu("File") << loka::app::MenuItem("Quit"));
  WindowProps props;
  props.frame(40, 40, declaredWidth, declaredHeight).visible(false);
  NullPlatformContext context;
  Win32Window window(&context, props);

  setWindowVisibility(window, true);
  HWND hwnd = window.hwnd();
  LOKA_VERIFY(hwnd && IsWindow(hwnd));

  const RECT declaredOuterRect = readWindowRect(hwnd);
  LOKA_VERIFY(declaredOuterRect.left == 40 && declaredOuterRect.top == 40
              && window.nativeFrame().get().x == 40
              && window.nativeFrame().get().y == 40
              && "a declared desktop origin must remain stable at non-100% DPI");
  assertLogicalClientSize(hwnd, declaredWidth, declaredHeight);
  LOKA_VERIFY(window.frameState().get().width == declaredWidth);
  LOKA_VERIFY(window.frameState().get().height == declaredHeight);

  MenuApplyingWin32App app;
  app.setActiveWindow(&window);
  // Native transition is attachment-owned; the App default is bootstrap-only.
  const loka::app::MenuSources menuBarSources = {&menuBar, 0, 0};
  window.menuAttachment().project(menuBarSources);
  assert(GetMenu(hwnd) && "the production menu path must attach the declared menu");
  assertLogicalClientSize(hwnd, declaredWidth, declaredHeight);
  LOKA_VERIFY(window.frameState().get().width == declaredWidth);
  LOKA_VERIFY(window.frameState().get().height == declaredHeight);

  const loka::app::MenuSources absent = {0, 0, 0};
  window.menuAttachment().project(absent);
  assert(!GetMenu(hwnd) && "the production menu path must detach a cleared menu");
  assertLogicalClientSize(hwnd, declaredWidth, declaredHeight);
  LOKA_VERIFY(window.frameState().get().width == declaredWidth);
  LOKA_VERIFY(window.frameState().get().height == declaredHeight);
  app.setActiveWindow(0);

  const int resizedWidth = 311;
  const int resizedHeight = 197;
  setWindowFrame(window, loka::core::Frame(40, 40, resizedWidth, resizedHeight));
  assertLogicalClientSize(hwnd, resizedWidth, resizedHeight);
  LOKA_VERIFY(window.frameState().get().width == resizedWidth);
  LOKA_VERIFY(window.frameState().get().height == resizedHeight);

  setWindowVisibility(window, false);
  printf("==== [testWin32DeclaredWindowSizeMeansClientArea] PASSED ====\n");
}

void testWin32ResizeMessageStoresTrackedContentSize()
{
  printf("\n==== [testWin32ResizeMessageStoresTrackedContentSize] start ====\n");
  WindowProps props;
  props.frame(40, 40, 640, 360).visible(false);
  NullPlatformContext context;
  Win32Window window(&context, props);
  setWindowVisibility(window, true);
  HWND hwnd = window.hwnd();
  LOKA_VERIFY(hwnd && IsWindow(hwnd));

  const loka::core::Frame declaredFrame = window.frameState().get();
  const loka::core::Frame beforeMinimize = window.nativeFrame().get();
  SendMessageW(hwnd, WM_SIZE, SIZE_MINIMIZED, MAKELPARAM(0, 0));
  LOKA_VERIFY(window.nativeFrame().get() == beforeMinimize &&
              "a minimized WM_SIZE must not replace the last native content frame");
  LOKA_VERIFY(window.frameState().get() == declaredFrame &&
              "a minimized WM_SIZE must not change the declared content frame");

  loka::core::State<loka::core::Frame> &nativeFrame = window.nativeFrame();
  loka::core::DerivedState<bool> narrow(&nativeFrame, new Win32WidthIsNarrow(&nativeFrame));
  loka::core::PushStateTracker *tracker = window.getTracker()->asPushTracker();
  assert(tracker);
  tracker->addState(&narrow);
  int notifications = 0;
  narrow.bind(&CountFrameNotification, &notifications, false);

  resizeLogicalClient(hwnd, 400, 280);
  const loka::core::Frame resized = window.nativeFrame().get();
  assert(resized.width == 400 && resized.height == 280 &&
         "a normal WM_SIZE must publish the tracked logical content size");
  LOKA_VERIFY(window.frameState().get() == declaredFrame &&
              "a native WM_SIZE must not echo into the declared content frame");
  assert(narrow.get() &&
         "derived window state must settle before the native resize transaction returns");
  assert(notifications == 1);

  tracker->removeState(&narrow);
  setWindowVisibility(window, false);
  printf("==== [testWin32ResizeMessageStoresTrackedContentSize] PASSED ====\n");
}

void testWin32NativeWindowCreationDoesNotEchoVisibility()
{
  printf("\n==== [testWin32NativeWindowCreationDoesNotEchoVisibility] start ====\n");
  WindowProps props;
  props.frame(40, 40, 257, 163).visible(false);
  NullPlatformContext context;
  Win32Window window(&context, props);
  int notifications = 0;
  window.visibilityState().bind(&CountFrameNotification, &notifications, false);

  setWindowVisibility(window, true);
  HWND hwnd = window.hwnd();
  LOKA_VERIFY(hwnd && IsWindow(hwnd));
  LOKA_VERIFY(IsWindowVisible(hwnd));
  printf("  visibility notifications after show=%d\n", notifications);
  fflush(stdout);
  assert(notifications == 1 &&
         "native window creation must not write visibilityState back (one application write, one notification)");

  setWindowVisibility(window, false);
  const HWND destroyedHwnd = window.hwnd();
  LOKA_VERIFY(destroyedHwnd == NULL);
  assert(notifications == 2 &&
         "native window destruction must not write visibilityState back either");

  window.visibilityState().unbind(&CountFrameNotification, &notifications);
  printf("==== [testWin32NativeWindowCreationDoesNotEchoVisibility] PASSED ====\n");
}

void testWin32AppOnlyMenuWindowSettles()
{
  printf("\n==== [testWin32AppOnlyMenuWindowSettles] start ====\n");
  const int declaredWidth = 257;
  const int declaredHeight = 163;
  loka::app::MenuBarDefinition appOnlyMenuBar;
  appOnlyMenuBar << (loka::app::AppMenu() << loka::app::MenuItem("About").actionType(
                                                loka::app::MENU_ACTION_ABOUT_APP));
  WindowProps props;
  props.frame(40, 40, declaredWidth, declaredHeight).visible(false);
  NullPlatformContext context;
  Win32Window window(&context, props);

  setWindowVisibility(window, true);
  HWND hwnd = window.hwnd();
  LOKA_VERIFY(hwnd && IsWindow(hwnd));
  const RECT initialOuterRect = readWindowRect(hwnd);

  DefaultMenuConfig config(&context, appOnlyMenuBar);
  MenuApplyingWin32App app(&config);
  app.bootstrap();
  app.setActiveWindow(&window);
  app.complete(window);
  pumpWindowMessages(16);

  const RECT settledOuterRect = readWindowRect(hwnd);
  printf("  initial outer=%ld,%ld-%ld,%ld settled outer=%ld,%ld-%ld,%ld\n",
         initialOuterRect.left,
         initialOuterRect.top,
         initialOuterRect.right,
         initialOuterRect.bottom,
         settledOuterRect.left,
         settledOuterRect.top,
         settledOuterRect.right,
         settledOuterRect.bottom);
  fflush(stdout);
  LOKA_VERIFY(sameRect(initialOuterRect, settledOuterRect) &&
              "an app-only menu must not grow the native window while it settles");
  assertLogicalClientSize(hwnd, declaredWidth, declaredHeight);

  app.setActiveWindow(0);
  setWindowVisibility(window, false);
  printf("==== [testWin32AppOnlyMenuWindowSettles] PASSED ====\n");
}

void testWin32MenuRebuildPreservesMovedWindowFrame()
{
  printf("\n==== [testWin32MenuRebuildPreservesMovedWindowFrame] start ====\n");
  const int declaredX = 40;
  const int declaredY = 40;
  const int declaredWidth = 257;
  const int declaredHeight = 163;
  const int movedX = 233;
  const int movedY = 177;
  loka::app::MenuBarDefinition initialMenuBar;
  initialMenuBar << (loka::app::Menu("File") << loka::app::MenuItem("Initial"));
  loka::app::MenuBarDefinition rebuiltMenuBar;
  rebuiltMenuBar << (loka::app::Menu("File") << loka::app::MenuItem("Rebuilt"));
  WindowProps props;
  props.frame(declaredX, declaredY, declaredWidth, declaredHeight).visible(false);
  NullPlatformContext context;
  Win32Window window(&context, props);

  setWindowVisibility(window, true);
  HWND hwnd = window.hwnd();
  LOKA_VERIFY(hwnd && IsWindow(hwnd));

  MenuApplyingWin32App app;
  app.setActiveWindow(&window);
  // Native transition is attachment-owned; the App default is bootstrap-only.
  const loka::app::MenuSources initialMenuBarSources = {&initialMenuBar, 0, 0};
  window.menuAttachment().project(initialMenuBarSources);
  assert(GetMenu(hwnd) && "the production menu path must attach the initial menu");
  LOKA_VERIFY(SetWindowPos(hwnd,
                           NULL,
                           movedX,
                           movedY,
                           0,
                           0,
                           SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE));
  const RECT movedOuterRect = readWindowRect(hwnd);
  RECT movedClientRect;
  LOKA_VERIFY(GetClientRect(hwnd, &movedClientRect));

  const loka::app::MenuSources rebuiltMenuBarSources = {&rebuiltMenuBar, 0, 0};
  window.menuAttachment().project(rebuiltMenuBarSources);

  const RECT rebuiltOuterRect = readWindowRect(hwnd);
  RECT rebuiltClientRect;
  LOKA_VERIFY(GetClientRect(hwnd, &rebuiltClientRect));
  printf("  declared origin=%d,%d moved origin=%ld,%ld rebuilt origin=%ld,%ld client=%ldx%ld -> %ldx%ld\n",
         declaredX,
         declaredY,
         movedOuterRect.left,
         movedOuterRect.top,
         rebuiltOuterRect.left,
         rebuiltOuterRect.top,
         movedClientRect.right - movedClientRect.left,
         movedClientRect.bottom - movedClientRect.top,
         rebuiltClientRect.right - rebuiltClientRect.left,
         rebuiltClientRect.bottom - rebuiltClientRect.top);
  fflush(stdout);
  assert(rebuiltOuterRect.left == movedOuterRect.left &&
         rebuiltOuterRect.top == movedOuterRect.top &&
         "menu rebuild must preserve the actual Win32 window position");
  LOKA_VERIFY(sameRect(movedClientRect, rebuiltClientRect) &&
              "menu rebuild must preserve the Win32 client size");

  app.setActiveWindow(0);
  setWindowVisibility(window, false);
  printf("==== [testWin32MenuRebuildPreservesMovedWindowFrame] PASSED ====\n");
}

void testWin32WindowDestructionDestroysNativeWindow()
{
  printf("\n==== [testWin32WindowDestructionDestroysNativeWindow] start ====\n");
  HWND hwnd = NULL;
  {
    WindowProps props;
    props.frame(40, 40, 257, 163).visible(false);
    NullPlatformContext context;
    Win32Window window(&context, props);
    setWindowVisibility(window, true);
    hwnd = window.hwnd();
    LOKA_VERIFY(hwnd != NULL);
    const BOOL windowCreated = IsWindow(hwnd);
    LOKA_VERIFY(windowCreated);
  }
  const BOOL windowRemaining = IsWindow(hwnd);
  LOKA_VERIFY(!windowRemaining);
  printf("==== [testWin32WindowDestructionDestroysNativeWindow] PASSED ====\n");
}

void testWin32AppDestructionLeavesWindowOwnedMenu()
{
  printf("\n==== [testWin32AppDestructionLeavesWindowOwnedMenu] start ====\n");
  loka::app::MenuBarDefinition menuBar;
  menuBar << (loka::app::Menu("File") << loka::app::MenuItem("Quit"));
  WindowProps props;
  props.frame(40, 40, 257, 163).visible(false);
  NullPlatformContext context;
  Win32Window window(&context, props);
  setWindowVisibility(window, true);
  HWND hwnd = window.hwnd();
  LOKA_VERIFY(hwnd != NULL);
  int frameNotifications = 0;
  window.frameState().deferBind(&CountFrameNotification, &frameNotifications);

  for (int run = 0; run < 2; ++run)
  {
    HMENU menu = NULL;
    {
      DefaultMenuConfig config(&context, menuBar);
      MenuApplyingWin32App app(&config);
      app.bootstrap();
      app.setActiveWindow(&window);
      app.complete(window);
      menu = GetMenu(hwnd);
      LOKA_VERIFY(menu != NULL);
      const BOOL menuCreated = IsMenu(menu);
      LOKA_VERIFY(menuCreated);
      frameNotifications = 0;
      // The borrowed Window leaves this App before its bootstrap bar can die,
      // so the next App's identity cannot alias a freed base.
      window.setApp(0);
    }

    // The fixture owns the Window beyond the App: its native menu stays put.
    LOKA_VERIFY(frameNotifications == 0);
    assertLogicalClientSize(hwnd, 257, 163);
    const HMENU attachedMenu = GetMenu(hwnd);
    LOKA_VERIFY(attachedMenu == menu);
    const BOOL menuRemaining = IsMenu(menu);
    LOKA_VERIFY(menuRemaining);
  }

  const BOOL windowRemaining = IsWindow(hwnd);
  LOKA_VERIFY(windowRemaining);
  window.frameState().deferUnbind(&CountFrameNotification, &frameNotifications);
  setWindowVisibility(window, false);
  printf("==== [testWin32AppDestructionLeavesWindowOwnedMenu] PASSED ====\n");
}

void testWin32NativeWindowDestructionReleasesMenuWithoutStateNotification()
{
  printf("\n==== [testWin32NativeWindowDestructionReleasesMenuWithoutStateNotification] start ====\n");
  loka::app::MenuBarDefinition menuBar;
  menuBar << (loka::app::Menu("File") << loka::app::MenuItem("Quit"));
  NullPlatformContext context;
  DefaultMenuConfig config(&context, menuBar);
  MenuApplyingWin32App app(&config);
  WindowProps props;
  props.frame(40, 40, 257, 163).visible(false);
  Win32Window window(&context, props);
  window.setApp(&app);
  setWindowVisibility(window, true);
  app.bootstrap();
  app.complete(window);
  HMENU menu = GetMenu(window.hwnd());
  LOKA_VERIFY(menu != NULL);
  const BOOL menuCreated = IsMenu(menu);
  LOKA_VERIFY(menuCreated);
  int frameNotifications = 0;
  window.frameState().deferBind(&CountFrameNotification, &frameNotifications);

  setWindowVisibility(window, false);

  LOKA_VERIFY(frameNotifications == 0);
  const HWND destroyedHwnd = window.hwnd();
  LOKA_VERIFY(destroyedHwnd == NULL);
  Window *activeWindow = app.activeWindow();
  LOKA_VERIFY(activeWindow == NULL);
  const BOOL menuRemaining = IsMenu(menu);
  LOKA_VERIFY(!menuRemaining);
  window.frameState().deferUnbind(&CountFrameNotification, &frameNotifications);
  printf("==== [testWin32NativeWindowDestructionReleasesMenuWithoutStateNotification] PASSED ====\n");
}

void testWin32WindowEditedMarkPrefixesTheNativeTitle()
{
  Win32PlatformContext context;
  WindowProps props;
  props.frame(40, 40, 257, 163).visible(false);
  Win32Window window(&context, props);
  setWindowVisibility(window, true);
  LOKA_VERIFY(window.hwnd() != NULL);

  for (int pass = 0; pass < 2; ++pass)
  {
    const bool edited = pass == 0;
    {
      loka::core::Operation turn;
      {
        loka::core::StateTrackerGuard guard(window.getTracker());
        loka::app::testing::WindowTestAccess::applyDocumentFacts(
            window, loka::app::DocumentFacts(loka::core::String("Doc"), edited));
      }
      turn.settle();
      turn.close();
    }
    // The second pass changes only edited, so title observation alone cannot pass.
    wchar_t title[16] = {};
    LOKA_VERIFY(GetWindowTextW(window.hwnd(), title, 16) == (edited ? 4 : 3));
    LOKA_VERIFY(std::wcscmp(title, edited ? L"*Doc" : L"Doc") == 0);
  }
  setWindowVisibility(window, false);
}

namespace
{
  class CloseAskedWin32App : public Win32App
  {
  public:
    explicit CloseAskedWin32App(AppConfigurable &config)
        : Win32App(&config, GetModuleHandle(NULL), SW_SHOW) {}
    virtual void quit() {}
    using App::admitAndApplyWindows;
    using App::reclaimWindows;
    Win32Window *firstWindow()
    {
      LOKA_VERIFY(this->group_ != 0);
      LOKA_VERIFY(this->group_->getComponents().size() == 1);
      return this->group_->getComponents()[0]->asWindow()->asWin32Window();
    }
  };
}

void testWin32WmCloseAsksKeyedWindowWithoutDestroying()
{
  NativeCloseTestContext<Win32Window> context;
  NativeCloseTestConfig config(context, true);
  const loka::core::ItemId key = config.roster.open(loka::app::DocumentFacts());
  LOKA_VERIFY(!key.isNone());
  CloseAskedWin32App app(config);
  app.App::run();
  // The HWND is created by admission, not by run(): the Null model of Win32App's tail.
  RunWindowAdmissionOperation(app);
  Win32Window *window = app.firstWindow();
  LOKA_VERIFY(window != 0);
  const HWND hwnd = window->hwnd();
  LOKA_VERIFY(hwnd && IsWindow(hwnd));
  const size_t pending = loka::app::testing::AppTestAccess::pendingWindowCloseCount(app);
  SendMessageW(hwnd, WM_CLOSE, 0, 0);
  LOKA_VERIFY(IsWindow(hwnd));
  LOKA_VERIFY(loka::app::testing::AppTestAccess::pendingWindowCloseCount(app) == pending);
  LOKA_VERIFY(config.roster.closeAsked().get().id == key);
}

void testWin32WmCloseDestroysUnkeyedWindowAsToday()
{
  NativeCloseTestContext<Win32Window> context;
  NativeCloseTestConfig config(context, false);
  CloseAskedWin32App app(config);
  app.App::run();
  // The HWND is created by admission, not by run(): the Null model of Win32App's tail.
  RunWindowAdmissionOperation(app);
  Win32Window *window = app.firstWindow();
  LOKA_VERIFY(window != 0);
  const HWND hwnd = window->hwnd();
  LOKA_VERIFY(hwnd && IsWindow(hwnd));
  LOKA_VERIFY(loka::app::testing::AppTestAccess::pendingWindowCloseCount(app) == 0);
  SendMessageW(hwnd, WM_CLOSE, 0, 0);
  LOKA_VERIFY(!IsWindow(hwnd));
  LOKA_VERIFY(loka::app::testing::AppTestAccess::pendingWindowCloseCount(app) == 1);
}

void testWin32DisplayScaleSystemMetricFollowsDpi()
{
  printf("\n==== [testWin32DisplayScaleSystemMetricFollowsDpi] start ====\n");
  using loka::win32::Win32DisplayScale;
  const Win32DisplayScale system(96, loka::app::RailMetrics());
  LOKA_VERIFY(system.systemMetric(SM_CYHSCROLL) == GetSystemMetrics(SM_CYHSCROLL));
  typedef int(WINAPI * GetSystemMetricsForDpiFn)(int, UINT);
  HMODULE user32 = GetModuleHandleW(L"user32.dll");
  GetSystemMetricsForDpiFn forDpi =
      user32 ? reinterpret_cast<GetSystemMetricsForDpiFn>(GetProcAddress(user32, "GetSystemMetricsForDpi")) : 0;
  if (!forDpi)
  {
    printf("[skip] GetSystemMetricsForDpi is unavailable on this Windows; the system metric is the only answer\n");
  }
  else
  {
    // A per-monitor-DPI window on a 192 dpi monitor gets the thicker bar
    // Windows adds there, not the system-DPI one.
    const Win32DisplayScale doubled(192, loka::app::RailMetrics());
    LOKA_VERIFY(doubled.systemMetric(SM_CYHSCROLL) == forDpi(SM_CYHSCROLL, 192));
    LOKA_VERIFY(doubled.systemMetric(SM_CYHSCROLL) > system.systemMetric(SM_CYHSCROLL));
  }
  printf("==== [testWin32DisplayScaleSystemMetricFollowsDpi] PASSED ====\n");
}
