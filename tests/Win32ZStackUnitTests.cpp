#include "Win32ZStackUnitTests.hpp"
#include "support/TestVerify.hpp"
#include <climits>
#include <cwchar>
#include <windows.h>
#include "Win32Window.hpp"
#include "Win32SceneLayout.hpp"
#include "Win32ScenePlatformController.hpp"
#include "context/Win32RetirableContext.hpp"
#include "context/Win32ScrollViewContext.hpp"
#include "platform/null/NullPlatformContext.hpp"
#include "support/WindowAdmissionTestApp.hpp"
#include "testing/Win32ScenePlatformTestAccess.hpp"
#include "testing/scene/SceneTestFlow.hpp"
#include "app/RectSurface.hpp"
#include "app/nodes/ImageView.hpp"
#include "app/nodes/controls/Button.hpp"
#include "app/nodes/nestable/Box.hpp"
#include "app/nodes/nestable/RowColumn.hpp"
#include "app/nodes/nestable/ScrollView.hpp"
#include "app/nodes/nestable/Show.hpp"
#include "app/nodes/nestable/ZStack.hpp"
#include "core/util/StateTrackerGuard.hpp"

namespace
{
  using namespace loka::app;
  using namespace loka::app::scene;
  typedef loka::dsl::testing::Win32ScenePlatformTestAccess Access;

  WindowProps hostProps()
  {
    WindowProps props;
    props.frame(40, 40, 360, 280).visible(false);
    return props;
  }
  struct Host
  {
    NullPlatformContext platform;
    Win32Window window;
    WindowAdmissionTestApp admission;
    Host()
        : window(&this->platform, hostProps()),
          admission(this->window)
    {
      {
        loka::core::StateTrackerGuard guard(this->window.getTracker());
        this->window.visibilityState().set(true);
      }
      this->admission.flush();
      LOKA_VERIFY(this->window.hwnd());
      ShowWindow(this->window.hwnd(), SW_HIDE);
    }
  };
  struct Fixture
  {
    Host host;
    Win32ScenePlatformController controller;
    Node *tree;
    Fixture()
        : controller(this->host.window.hwnd(), loka::win32::Win32DisplayScale(96, RailMetrics())),
          tree(0)
    {
    }
    ~Fixture()
    {
      this->controller.onChange(0, NODE_DIRTY_NONE, false);
      delete this->tree;
      this->controller.drainNativeRetirements();
    }
    HWND root()
    {
      return this->host.window.hwnd();
    }
    void layout(int width = 300, int height = 200)
    {
      this->controller.onChange(this->tree, NODE_DIRTY_NONE, false);
      this->controller.relayout(width, height);
    }
  };
  HWND childOfClass(HWND parent, const wchar_t *name)
  {
    return FindWindowExW(parent, NULL, name, NULL);
  }
  RECT rectInParent(HWND child)
  {
    RECT rect;
    LOKA_VERIFY(child && GetWindowRect(child, &rect));
    MapWindowPoints(NULL, GetParent(child), reinterpret_cast<POINT *>(&rect), 2);
    return rect;
  }
  HWND native(Node *node)
  {
    LOKA_VERIFY(node && node->getContext());
    return static_cast<Win32RetirableContext *>(node->getContext())->hwnd();
  }
  StackNode *buttonColumn(const char *label)
  {
    StackNode *column = new StackNode(StackProps(STACK_AXIS_COLUMN));
    column->addChild(new ButtonNode(ButtonProps().text(label)));
    return column;
  }
  ZStackNode *stackWithGround(Fixture &fixture, int height = 160)
  {
    ZStackNode *stack = new ZStackNode((ZStackProps()));
    stack->addChild(new RectSurfaceNode(RectSurfaceProps().size(200, height)));
    fixture.tree = stack;
    return stack;
  }
  void flushPaint(Fixture &fixture)
  {
    Access::flushPendingInvalidations(fixture.controller);
    MSG message;
    while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE))
    {
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
    UpdateWindow(fixture.root());
  }
  COLORREF capturedPixel(Fixture &fixture, int x, int y)
  {
    loka::core::resource::Image image;
    LOKA_VERIFY(Access::captureWindowClientBitmap(fixture.host.platform, fixture.root(), image));
    HDC dc = CreateCompatibleDC(NULL);
    LOKA_VERIFY(dc);
    HGDIOBJ previous = SelectObject(dc, static_cast<HBITMAP>(image.nativeHandle()));
    LOKA_VERIFY(previous && previous != HGDI_ERROR);
    const COLORREF color = GetPixel(dc, x, y);
    LOKA_VERIFY(color != CLR_INVALID);
    SelectObject(dc, previous);
    DeleteDC(dc);
    return color;
  }
  SCROLLINFO metrics(HWND viewport)
  {
    SCROLLINFO info;
    ZeroMemory(&info, sizeof(info));
    info.cbSize = sizeof(info);
    info.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    LOKA_VERIFY(GetScrollInfo(viewport, SB_VERT, &info));
    return info;
  }
} // namespace

void testWin32ZStackLaterChildGetsUnitWindow()
{
  Fixture f;
  ZStackNode *stack = stackWithGround(f);
  StackNode *column = buttonColumn("B");
  stack->addChild(column);
  f.layout();
  HWND unit = childOfClass(f.root(), L"LOKA_STACK_UNIT");
  LOKA_VERIFY(unit && GetWindow(f.root(), GW_CHILD) == unit);
  LOKA_VERIFY(!FindWindowExW(f.root(), unit, L"LOKA_STACK_UNIT", NULL));
  const RECT rect = rectInParent(unit);
  LOKA_VERIFY(rect.left == Win32SceneLayout::kRootInset && rect.top == Win32SceneLayout::kRootInset);
  LOKA_VERIFY(rect.right - rect.left == 300 - 2 * Win32SceneLayout::kRootInset);
  LOKA_VERIFY(rect.bottom - rect.top == f.controller.rowLayoutMetrics().buttonHeight);
  HWND button = native(column->childrenHead());
  LOKA_VERIFY(GetParent(button) == unit);
  const RECT buttonRect = rectInParent(button);
  LOKA_VERIFY(buttonRect.left == 0 && buttonRect.top == 0);
  LOKA_VERIFY(GetParent(native(stack->childrenHead())) == f.root());
  ShowWindow(f.root(), SW_SHOWNOACTIVATE);
  const POINT inside = {rect.left + 2, rect.top + 2};
  HWND hit = ChildWindowFromPointEx(f.root(), inside, CWP_SKIPINVISIBLE);
  LOKA_VERIFY(hit == unit || hit == button);
}

void testWin32ZStackUnitPaintsOpaqueGround()
{
  loka::core::PushStateTracker tracker;
  RectSurfaceModel ink;
  ink.rectCount = 1;
  ink.rects[0] = RectSprite(0, 0, 200, 160);
  loka::core::MutableState<RectSurfaceModel> model(ink);
  tracker.addState(&model);
  Fixture f;
  ZStackNode *stack = new ZStackNode((ZStackProps()));
  f.tree = stack;
  stack->addChild(new RectSurfaceNode(RectSurfaceProps().model(&model).size(200, 160)));
  BoxNode *unitChild = new BoxNode(BoxProps().setSize(0, 80));
  unitChild->addChild(new ButtonNode(ButtonProps().text("B")));
  stack->addChild(unitChild);
  f.layout();
  HWND unit = childOfClass(f.root(), L"LOKA_STACK_UNIT");
  LOKA_VERIFY(unit);
  ShowWindow(f.root(), SW_SHOWNOACTIVATE);
  LOKA_VERIFY(SetWindowPos(f.root(), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE));
  flushPaint(f);
  const RECT rect = rectInParent(unit);
  LOKA_VERIFY(GetSysColor(COLOR_BTNFACE) != GetSysColor(COLOR_WINDOWTEXT));
  LOKA_VERIFY(capturedPixel(f, rect.left + 100, rect.top + 50) == GetSysColor(COLOR_BTNFACE));
  // Positive control: the same lower surface really paints ink outside the unit.
  LOKA_VERIFY(capturedPixel(f, rect.left + 100, rect.top + 120) == GetSysColor(COLOR_WINDOWTEXT));
  for (int phase = 0; phase < 2; ++phase)
  {
    Access::resetRedrawStats(f.controller);
    {
      loka::core::StateTrackerGuard guard(&tracker);
      model.set(phase == 0 ? RectSurfaceModel() : ink);
    }
    // Paint-only observer path: no relayout, root invalidation, or unit repaint.
    LOKA_VERIFY(Access::queuedRectInvalidates(f.controller) > 0);
    LOKA_VERIFY(!Access::lastOnChangeRequiredLayout(f.controller));
    flushPaint(f);
    LOKA_VERIFY(capturedPixel(f, rect.left + 100, rect.top + 50) == GetSysColor(COLOR_BTNFACE));
    LOKA_VERIFY(capturedPixel(f, rect.left + 100, rect.top + 120)
                == GetSysColor(phase == 0 ? COLOR_WINDOW : COLOR_WINDOWTEXT));
  }
}

void testWin32ZStackUnitsStackInCompositionOrder()
{
  using namespace loka::dsl::testing;
  loka::core::MutableState<bool> visible(false);
  Fixture f;
  NodeDefinitionBase *definition =
      (ZStack() << RectSurface().size(200, 140) << (Column() << (Show(visible) << Button("B")))
                << (Column() << Button("C")))
          .clone();
  LOKA_VERIFY(definition);
  Scene scene(definition);
  LOKA_VERIFY(scene.mount(&f.controller));
  SceneTestAccess::updateAttached(scene, true);
  f.controller.relayout(300, 200);
  HWND upper = childOfClass(f.root(), L"LOKA_STACK_UNIT");
  HWND lower = FindWindowExW(f.root(), upper, L"LOKA_STACK_UNIT", NULL);
  HWND ground = childOfClass(f.root(), L"LOKA_RECT_SURFACE");
  LOKA_VERIFY(upper && lower && ground);
  LOKA_VERIFY(GetWindow(lower, GW_HWNDPREV) == upper);
  LOKA_VERIFY(GetWindow(ground, GW_HWNDPREV) == lower);
  LOKA_VERIFY(childOfClass(upper, L"Button"));
  LOKA_VERIFY(!childOfClass(lower, L"Button"));
  RECT empty = rectInParent(lower);
  LOKA_VERIFY(empty.bottom == empty.top);
  visible.set(true);
  scene.flushInvalidation();
  f.controller.relayout(300, 200);
  LOKA_VERIFY(childOfClass(lower, L"Button"));
  LOKA_VERIFY(GetWindow(lower, GW_HWNDPREV) == upper);
  LOKA_VERIFY(GetWindow(ground, GW_HWNDPREV) == lower);
  LOKA_VERIFY(GetWindow(f.root(), GW_CHILD) == upper);
  // A later-created control and an externally disturbed unit order are repaired
  // on every layout pass, not just the pass that allocates the context.
  SetWindowPos(lower, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
  f.controller.relayout(300, 200);
  LOKA_VERIFY(GetWindow(lower, GW_HWNDPREV) == upper);
  visible.set(false);
  scene.flushInvalidation();
  f.controller.relayout(300, 200);
  empty = rectInParent(lower);
  LOKA_VERIFY(empty.bottom == empty.top);
  SceneTestAccess::unmount(scene);
  f.controller.drainNativeRetirements();
}

void testWin32ZStackUnitTakesTabFirst()
{
  Fixture f;
  StackNode *column = new StackNode(StackProps(STACK_AXIS_COLUMN));
  f.tree = column;
  ButtonNode *a = new ButtonNode(ButtonProps().text("A"));
  column->addChild(a);
  ZStackNode *stack = new ZStackNode((ZStackProps()));
  stack->addChild(new RectSurfaceNode(RectSurfaceProps().size(200, 100)));
  StackNode *buttons = buttonColumn("B");
  stack->addChild(buttons);
  column->addChild(stack);
  f.layout();
  ShowWindow(f.root(), SW_SHOWNOACTIVATE);
  HWND b = native(buttons->childrenHead());
  LOKA_VERIFY(GetNextDlgTabItem(f.root(), NULL, FALSE) == b);
  LOKA_VERIFY(GetNextDlgTabItem(f.root(), b, FALSE) == native(a));
}

void testWin32ZStackUnitInsideViewportPropagatesRefusal()
{
  loka::core::MutableState<loka::core::Frame> storage(loka::core::Frame(0, 1000, 0, 0));
  NodeState<loka::core::Frame> origin(&storage, 0);
  Fixture f;
  ScrollViewNode *scroll = new ScrollViewNode(ScrollViewProps(origin));
  f.tree = scroll;
  StackNode *content = new StackNode(StackProps(STACK_AXIS_COLUMN));
  scroll->addChild(content);
  ZStackNode *stack = new ZStackNode((ZStackProps()));
  content->addChild(stack);
  RectSurfaceNode *ground = new RectSurfaceNode(RectSurfaceProps().size(200, 400));
  stack->addChild(ground);
  StackNode *unitChild = new StackNode(StackProps(STACK_AXIS_COLUMN));
  ImageViewNode *leading = new ImageViewNode(ImageViewProps().size(180, 80));
  unitChild->addChild(leading);
  ButtonNode *following = new ButtonNode(ButtonProps().text("B"));
  unitChild->addChild(following);
  stack->addChild(unitChild);
  f.layout(300, 140); // Exposes the clamp, published through the fact channel.
  HWND viewport = childOfClass(f.root(), L"LOKA_SCROLL_VIEW");
  LOKA_VERIFY(viewport);
  const SCROLLINFO before = metrics(viewport);
  const int clamped = origin.get().y;
  LOKA_VERIFY(clamped > 0 && clamped < 1000);
  f.layout(300, 140); // Geometry now consumes the published origin.
  HWND unit = childOfClass(viewport, L"LOKA_STACK_UNIT");
  LOKA_VERIFY(unit);
  const RECT rect = rectInParent(unit);
  RECT client;
  LOKA_VERIFY(GetClientRect(viewport, &client));
  LOKA_VERIFY(rect.left == 0 && rect.top == -clamped);
  LOKA_VERIFY(rect.right == client.right);
  const RECT button = rectInParent(native(following));
  LOKA_VERIFY(button.top == 80 + Win32SceneLayout::kGap);
  ground->props.size(200, 10); // A wrongly committed pass would change the range.
  leading->props.size(180, static_cast<short>(SHRT_MAX - Win32SceneLayout::kRootInset - Win32SceneLayout::kGap + 1));
  f.layout(300, 140);
  const SCROLLINFO after = metrics(viewport);
  LOKA_VERIFY(after.nMin == before.nMin && after.nMax == before.nMax);
  LOKA_VERIFY(after.nPage == before.nPage && after.nPos == before.nPos);
  LOKA_VERIFY(origin.get().y == clamped);
  const RECT retained = rectInParent(unit);
  LOKA_VERIFY(EqualRect(&retained, &rect));
}

void testWin32ZStackUnitCreationRefusalLaysOutNothing()
{
  Fixture f;
  ZStackNode *stack = stackWithGround(f);
  // Establish only the ground HWND, so the injected refusal hits the unit.
  LayoutState seat;
  seat.width = 200;
  seat.height = 160;
  LOKA_VERIFY(f.controller.prepareProjectedLayout(stack->childrenHead(), seat));
  StackNode *column = buttonColumn("B");
  stack->addChild(column);
  loka::win32::testing::failNativeChildWindowCreations(1);
  f.layout();
  loka::win32::testing::failNativeChildWindowCreations(0);
  LOKA_VERIFY(!childOfClass(f.root(), L"LOKA_STACK_UNIT"));
  LOKA_VERIFY(!childOfClass(f.root(), L"Button"));
  LOKA_VERIFY(!column->childrenHead()->getContext());
  f.layout();
  HWND unit = childOfClass(f.root(), L"LOKA_STACK_UNIT");
  LOKA_VERIFY(unit && GetParent(native(column->childrenHead())) == unit);
  LOKA_VERIFY(!childOfClass(f.root(), L"Button"));
}

void testWin32ScrollViewRefusedInsideUnit()
{
  Fixture f;
  ZStackNode *stack = stackWithGround(f);
  ScrollViewNode *scroll = new ScrollViewNode((ScrollViewProps()));
  scroll->addChild(buttonColumn("B"));
  stack->addChild(scroll);
  f.layout();
  HWND unit = childOfClass(f.root(), L"LOKA_STACK_UNIT");
  LOKA_VERIFY(unit);
  LOKA_VERIFY(!scroll->getContext());
  LOKA_VERIFY(!childOfClass(unit, L"LOKA_SCROLL_VIEW"));
  LOKA_VERIFY(!childOfClass(f.root(), L"LOKA_SCROLL_VIEW"));
  LOKA_VERIFY(!GetWindow(unit, GW_CHILD));
}

void testWin32ZStackUnitForwardsWheelToViewport()
{
  loka::core::MutableState<loka::core::Frame> storage(loka::core::Frame(0, 0, 0, 0));
  NodeState<loka::core::Frame> origin(&storage, 0);
  Fixture f;
  ScrollViewNode *scroll = new ScrollViewNode(ScrollViewProps(origin));
  f.tree = scroll;
  ZStackNode *stack = new ZStackNode((ZStackProps()));
  scroll->addChild(stack);
  stack->addChild(new RectSurfaceNode(RectSurfaceProps().size(200, 600)));
  stack->addChild(buttonColumn("B"));
  f.layout(300, 140);
  HWND viewport = childOfClass(f.root(), L"LOKA_SCROLL_VIEW");
  HWND unit = childOfClass(viewport, L"LOKA_STACK_UNIT");
  LOKA_VERIFY(unit);
  ShowWindow(f.root(), SW_SHOWNOACTIVATE);
  LOKA_VERIFY(SetWindowPos(f.root(), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE));
  flushPaint(f);
  POINT point = {5, 5};
  LOKA_VERIFY(ClientToScreen(unit, &point));
  HWND hit = WindowFromPoint(point);
  LOKA_VERIFY(hit == unit || GetParent(hit) == unit);
  LOKA_VERIFY(Win32ScrollViewContext::forwardMouseWheel(
      f.root(), WM_MOUSEWHEEL, MAKEWPARAM(0, -WHEEL_DELTA), MAKELPARAM(point.x, point.y)));
  UINT lines = 3;
  LOKA_VERIFY(SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0));
  const SCROLLINFO info = metrics(viewport);
  const int maximum = info.nMax - static_cast<int>(info.nPage) + 1;
  const UINT distance = lines == WHEEL_PAGESCROLL                 ? info.nPage
                        : lines > static_cast<UINT>(maximum) / 16 ? static_cast<UINT>(maximum)
                                                                  : lines * 16;
  LOKA_VERIFY(origin.get().y == (distance > static_cast<UINT>(maximum) ? maximum : static_cast<int>(distance)));
}
