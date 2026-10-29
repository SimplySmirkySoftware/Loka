#include "context/Win32ClipBoxContext.hpp"
#include "Win32ZStackUnitTests.hpp"
#include "support/TestVerify.hpp"
#include <climits>
#include <cstdio>
#include <cwchar>
#include <windows.h>
#include "Win32Window.hpp"
#include "Win32SceneLayout.hpp"
#include "Win32ScenePlatformController.hpp"
#include "context/Win32RetirableContext.hpp"
#include "context/Win32ZStackContext.hpp"
#include "context/Win32ScrollViewContext.hpp"
#include "platform/null/NullPlatformContext.hpp"
#include "support/WindowAdmissionTestApp.hpp"
#include "testing/Win32ScenePlatformTestAccess.hpp"
#include "testing/scene/SceneTestFlow.hpp"
#include "app/RectSurface.hpp"
#include "app/nodes/ImageView.hpp"
#include "app/nodes/Text.hpp"
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
  // A ScrollView projects a frame whose child is the content viewport (#1547).
  HWND viewportUnder(HWND parent)
  {
    return childOfClass(childOfClass(parent, L"LOKA_SCROLL_FRAME"), L"LOKA_SCROLL_VIEW");
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
  void CountClickThunk(void *userData)
  {
    ++*static_cast<int *>(userData);
  }
  StackNode *buttonColumn(const char *label)
  {
    StackNode *column = new StackNode(StackProps(STACK_AXIS_COLUMN));
    column->addChild(new ButtonNode(ButtonProps().text(label)));
    return column;
  }
  ZStackNode *stackWithGround(Fixture &fixture, short height = 160)
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
  void completePaint(HWND window)
  {
    // Complete existing damage only; invalidating the unit here would repair
    // a lower sibling overwriting it and conceal the clipping mutation.
    UpdateWindow(window);
    for (HWND child = GetWindow(window, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT))
      completePaint(child);
  }
  COLORREF capturedPixel(Fixture &fixture, int x, int y)
  {
    completePaint(fixture.root());
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
  COLORREF windowPixel(HWND hwnd, int x, int y)
  {
    HDC dc = GetDC(hwnd);
    LOKA_VERIFY(dc != NULL);
    const COLORREF pixel = GetPixel(dc, x, y);
    ReleaseDC(hwnd, dc);
    LOKA_VERIFY(pixel != CLR_INVALID);
    return pixel;
  }
  // Thread-local hook lifetime is bounded by one synchronous test pass.
  class UnitOrderProbe
  {
  public:
    UnitOrderProbe(HWND first, HWND second)
        : first_(first),
          second_(second),
          changes_(0),
          hook_(0)
    {
      LOKA_VERIFY(!active_);
      active_ = this;
      this->hook_ = SetWindowsHookExW(WH_CALLWNDPROC, callback, NULL, GetCurrentThreadId());
      LOKA_VERIFY(this->hook_);
    }
    ~UnitOrderProbe()
    {
      LOKA_VERIFY(UnhookWindowsHookEx(this->hook_));
      active_ = 0;
    }
    unsigned changes() const
    {
      return this->changes_;
    }

  private:
    static LRESULT CALLBACK callback(int code, WPARAM wParam, LPARAM lParam)
    {
      if (code >= 0 && active_)
      {
        const CWPSTRUCT *message = reinterpret_cast<const CWPSTRUCT *>(lParam);
        if (message->message == WM_WINDOWPOSCHANGING
            && (message->hwnd == active_->first_ || message->hwnd == active_->second_))
        {
          const WINDOWPOS *position = reinterpret_cast<const WINDOWPOS *>(message->lParam);
          // Layout's MoveWindow also sends this message. Only z-order work
          // belongs to orderUnits; unchanged geometry is a separate contract.
          if (!(position->flags & SWP_NOZORDER))
            ++active_->changes_;
        }
      }
      return CallNextHookEx(NULL, code, wParam, lParam);
    }
    HWND first_;
    HWND second_;
    unsigned changes_;
    HHOOK hook_;
    static UnitOrderProbe *active_;
    UnitOrderProbe(const UnitOrderProbe &);
    UnitOrderProbe &operator=(const UnitOrderProbe &);
  };
  UnitOrderProbe *UnitOrderProbe::active_ = 0;
  SCROLLINFO metrics(HWND viewport)
  {
    SCROLLINFO info;
    ZeroMemory(&info, sizeof(info));
    info.cbSize = sizeof(info);
    info.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    LOKA_VERIFY(GetScrollInfo(FindWindowExW(GetParent(viewport), NULL, L"SCROLLBAR", NULL), SB_CTL, &info));
    return info;
  }
} // namespace

static void checkWin32ZStackLaterChildGetsUnitWindow(bool clipped)
{
  Fixture f;
  ZStackNode *stack = stackWithGround(f);
  StackNode *column = buttonColumn("B");
  stack->addChild(column);
  BoxProps wrapperProps;
  wrapperProps.childClip_ = clipped ? layout::CHILD_CLIP_OWN_RECT : layout::CHILD_CLIP_NONE;
  BoxNode *wrapper = new BoxNode(wrapperProps);
  if (clipped)
  {
    wrapper->addChild(stack);
    f.tree = wrapper;
  }
  else
    delete wrapper;
  f.layout();
  HWND parent =
      clipped ? static_cast<Win32ClipBoxContext *>(wrapper->getContext()->asClipBoxContext())->hwnd() : f.root();
  HWND unit = childOfClass(parent, L"LOKA_STACK_UNIT");
  LOKA_VERIFY(unit && GetWindow(parent, GW_CHILD) == unit);
  LOKA_VERIFY(!FindWindowExW(parent, unit, L"LOKA_STACK_UNIT", NULL));
  const RECT rect = rectInParent(unit);
  LOKA_VERIFY(rect.left == (clipped ? 0 : Win32SceneLayout::kRootInset)
              && rect.top == (clipped ? 0 : Win32SceneLayout::kRootInset));
  LOKA_VERIFY(rect.right - rect.left == 300 - 2 * Win32SceneLayout::kRootInset);
  LOKA_VERIFY(rect.bottom - rect.top == f.controller.rowLayoutMetrics().buttonHeight);
  HWND button = native(column->childrenHead());
  LOKA_VERIFY(GetParent(button) == unit);
  const RECT buttonRect = rectInParent(button);
  LOKA_VERIFY(buttonRect.left == 0 && buttonRect.top == 0);
  LOKA_VERIFY(GetParent(native(stack->childrenHead())) == parent);
  ShowWindow(f.root(), SW_SHOWNOACTIVATE);
  const POINT inside = {rect.left + 2, rect.top + 2};
  HWND hit = ChildWindowFromPointEx(parent, inside, CWP_SKIPINVISIBLE);
  LOKA_VERIFY(hit == unit || hit == button);
}

void testWin32ZStackLaterChildGetsUnitWindow()
{
  checkWin32ZStackLaterChildGetsUnitWindow(false);
  checkWin32ZStackLaterChildGetsUnitWindow(true);
}

void testWin32ZStackUnitAnswersContainerMessages()
{
  int clicks = 0;
  loka::core::EmitterState onClick;
  Fixture f;
  ZStackNode *stack = stackWithGround(f);
  StackNode *column = new StackNode(StackProps(STACK_AXIS_COLUMN));
  ButtonNode *buttonNode = new ButtonNode(ButtonProps().text("click").onClick(&onClick));
  TextNode *textNode = new TextNode(TextProps("unit"));
  column->addChild(buttonNode);
  column->addChild(textNode);
  stack->addChild(column);
  f.layout();
  HWND unit = childOfClass(f.root(), L"LOKA_STACK_UNIT");
  HWND button = native(buttonNode);
  HWND text = native(textNode);
  LOKA_VERIFY(unit && button && text);
  LOKA_VERIFY(GetParent(button) == unit);
  LOKA_VERIFY(GetParent(text) == unit);

  // DefWindowProcW would drop the command, return a system brush/default ink,
  // and answer erase with 0 because the container class brush is NULL.
  onClick.bind(&CountClickThunk, &clicks, false);
  SendMessageW(button, BM_CLICK, 0, 0);
  LOKA_VERIFY(clicks == 1);

  HDC dc = CreateCompatibleDC(NULL);
  LOKA_VERIFY(dc != NULL);
  const COLORREF sentinel = RGB(1, 2, 3);
  if (sentinel == GetSysColor(COLOR_BTNTEXT))
    std::printf("[skip] STATIC sentinel equals live BTNTEXT; text mutation not discriminated\n");
  SetTextColor(dc, sentinel);
  SetBkMode(dc, OPAQUE);
  const LRESULT brush =
      SendMessageW(unit, WM_CTLCOLORSTATIC, reinterpret_cast<WPARAM>(dc), reinterpret_cast<LPARAM>(text));
  LOKA_VERIFY(reinterpret_cast<HBRUSH>(brush) == GetStockObject(NULL_BRUSH));
  LOKA_VERIFY(GetTextColor(dc) == GetSysColor(COLOR_BTNTEXT));
  LOKA_VERIFY(GetBkMode(dc) == TRANSPARENT);
  LOKA_VERIFY(SendMessageW(unit, WM_ERASEBKGND, reinterpret_cast<WPARAM>(dc), 0) == 1);
  DeleteDC(dc);
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
  // A narrow surface has a fixed native height and leaves bare unit ground.
  RectSurfaceNode *unitChild = new RectSurfaceNode(RectSurfaceProps().size(40, 80));
  stack->addChild(unitChild);
  f.layout();
  HWND unit = childOfClass(f.root(), L"LOKA_STACK_UNIT");
  HWND ground = childOfClass(f.root(), L"LOKA_RECT_SURFACE");
  LOKA_VERIFY(unit && ground);
  ShowWindow(f.root(), SW_SHOWNOACTIVATE);
  LOKA_VERIFY(SetWindowPos(f.root(), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE));
  flushPaint(f);
  const RECT rect = rectInParent(unit);
  LOKA_VERIFY(rect.bottom - rect.top == 80);
  LOKA_VERIFY(rect.right - rect.left > 100);
  LOKA_VERIFY(GetSysColor(COLOR_BTNFACE) != GetSysColor(COLOR_WINDOWTEXT));
  LOKA_VERIFY(capturedPixel(f, rect.left + 100, rect.top + 50) == GetSysColor(COLOR_BTNFACE));
  // Positive control: the same lower surface really paints ink outside the unit.
  LOKA_VERIFY(capturedPixel(f, rect.left + 100, rect.top + 120) == GetSysColor(COLOR_WINDOWTEXT));
  // The root has no WS_CLIPCHILDREN and paints the same ground beneath children;
  // only a container-only repaint over a sentinel proves its WM_PAINT answer.
  {
    const COLORREF sentinel = RGB(0x12, 0x34, 0x56);
    HDC dc = GetDC(unit);
    LOKA_VERIFY(dc != NULL);
    RECT client;
    LOKA_VERIFY(GetClientRect(unit, &client));
    HBRUSH brush = CreateSolidBrush(sentinel);
    LOKA_VERIFY(brush != NULL);
    LOKA_VERIFY(FillRect(dc, &client, brush));
    DeleteObject(brush);
    ReleaseDC(unit, dc);
    LOKA_VERIFY(windowPixel(unit, 100, 50) == sentinel);
    LOKA_VERIFY(InvalidateRect(unit, NULL, FALSE));
    UpdateWindow(unit);
    if (sentinel == GetSysColor(COLOR_BTNFACE))
      std::printf("[skip] unit sentinel equals live BTNFACE; WM_PAINT not discriminated\n");
    else
      LOKA_VERIFY(windowPixel(unit, 100, 50) == GetSysColor(COLOR_BTNFACE));
  }
  for (int phase = 0; phase < 2; ++phase)
  {
    Access::resetRedrawStats(f.controller);
    {
      loka::core::StateTrackerGuard guard(&tracker);
      model.set(phase == 0 ? RectSurfaceModel() : ink);
    }
    // Paint-only tick: the ground carries the damage and the unit none (a
    // broad repaint would damage both). The COLOR_WINDOW read in phase 0 is
    // the proof that the ground painted; black also reads from an unpainted
    // surface, so the ink read alone would not be one.
    Access::flushPendingInvalidations(f.controller);
    LOKA_VERIFY(GetUpdateRect(ground, NULL, FALSE));
    LOKA_VERIFY(!GetUpdateRect(unit, NULL, FALSE));
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

void testWin32ZStackRootUnitChildRefusalKeepsPreviousRectangle()
{
  // Deliberate twin of the other native rail's root-unit refusal pin.
  Fixture f;
  ZStackNode *stack = stackWithGround(f);
  StackNode *column = new StackNode(StackProps(STACK_AXIS_COLUMN));
  ImageViewNode *leading = new ImageViewNode(ImageViewProps().size(180, 80));
  column->addChild(leading);
  column->addChild(new ButtonNode(ButtonProps().text("After")));
  stack->addChild(column);
  f.layout(300, 140);
  HWND unit = childOfClass(f.root(), L"LOKA_STACK_UNIT");
  LOKA_VERIFY(unit);
  const RECT before = rectInParent(unit);
  LOKA_VERIFY(before.bottom > before.top);
  leading->props.size(180, static_cast<short>(SHRT_MAX - Win32SceneLayout::kRootInset - Win32SceneLayout::kGap + 1));
  f.layout(300, 140);
  const RECT after = rectInParent(unit);
  LOKA_VERIFY(EqualRect(&after, &before));
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
  HWND viewport = viewportUnder(f.root());
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
  f.layout();
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
  LOKA_VERIFY(!childOfClass(unit, L"LOKA_SCROLL_FRAME"));
  LOKA_VERIFY(!childOfClass(f.root(), L"LOKA_SCROLL_FRAME"));
  LOKA_VERIFY(!GetWindow(unit, GW_CHILD));
}

static void checkWin32ZStackUnitForwardsWheelToViewport(bool clipped)
{
  loka::core::MutableState<loka::core::Frame> storage(loka::core::Frame(0, 0, 0, 0));
  NodeState<loka::core::Frame> origin(&storage, 0);
  Fixture f;
  ScrollViewNode *scroll = new ScrollViewNode(ScrollViewProps(origin));
  f.tree = scroll;
  ZStackNode *stack = new ZStackNode((ZStackProps()));
  BoxProps wrapperProps;
  wrapperProps.childClip_ = clipped ? layout::CHILD_CLIP_OWN_RECT : layout::CHILD_CLIP_NONE;
  BoxNode *wrapper = new BoxNode(wrapperProps);
  if (clipped)
  {
    wrapper->addChild(stack);
    scroll->addChild(wrapper);
  }
  else
  {
    delete wrapper;
    scroll->addChild(stack);
  }
  stack->addChild(new RectSurfaceNode(RectSurfaceProps().size(200, 600)));
  stack->addChild(buttonColumn("B"));
  f.layout(300, 140);
  HWND viewport = viewportUnder(f.root());
  HWND parent =
      clipped ? static_cast<Win32ClipBoxContext *>(wrapper->getContext()->asClipBoxContext())->hwnd() : viewport;
  HWND unit = childOfClass(parent, L"LOKA_STACK_UNIT");
  LOKA_VERIFY(unit);
  ShowWindow(f.root(), SW_SHOWNOACTIVATE);
  LOKA_VERIFY(SetWindowPos(f.root(), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE));
  flushPaint(f);
  POINT point = {5, 5};
  LOKA_VERIFY(ClientToScreen(unit, &point));
  HWND hit = WindowFromPoint(point);
  LOKA_VERIFY(hit == unit || GetParent(hit) == unit);
  HWND button = childOfClass(unit, L"Button");
  LOKA_VERIFY(button);
  // The control's default handling must reach the unit proc, whose default
  // handling in turn propagates to the viewport. Do not bypass it via lookup.
  SendMessageW(button, WM_MOUSEWHEEL, MAKEWPARAM(0, -WHEEL_DELTA), MAKELPARAM(point.x, point.y));
  UINT lines = 3;
  LOKA_VERIFY(SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0));
  const SCROLLINFO info = metrics(viewport);
  const int maximum = info.nMax - static_cast<int>(info.nPage) + 1;
  const UINT distance = lines == WHEEL_PAGESCROLL                 ? info.nPage
                        : lines > static_cast<UINT>(maximum) / 16 ? static_cast<UINT>(maximum)
                                                                  : lines * 16;
  LOKA_VERIFY(origin.get().y == (distance > static_cast<UINT>(maximum) ? maximum : static_cast<int>(distance)));
}

void testWin32ZStackUnitForwardsWheelToViewport()
{
  checkWin32ZStackUnitForwardsWheelToViewport(false);
  checkWin32ZStackUnitForwardsWheelToViewport(true);
}

namespace
{
  // The rail's line step is 16 px per wheel line or character (handleMouseWheel).
  int expectedWheelDistance(UINT setting, HWND viewport, int bar)
  {
    SCROLLINFO info;
    ZeroMemory(&info, sizeof(info));
    info.cbSize = sizeof(info);
    info.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    LOKA_VERIFY(GetScrollInfo(bar == SB_VERT ? FindWindowExW(GetParent(viewport), NULL, L"SCROLLBAR", NULL) : viewport,
                              bar == SB_VERT ? SB_CTL : bar,
                              &info));
    const int maximum = info.nMax - static_cast<int>(info.nPage) + 1;
    const UINT distance = setting == WHEEL_PAGESCROLL                 ? info.nPage
                          : setting > static_cast<UINT>(maximum) / 16 ? static_cast<UINT>(maximum)
                                                                      : setting * 16;
    return distance > static_cast<UINT>(maximum) ? maximum : static_cast<int>(distance);
  }
} // namespace

/** The unit's window is the ground ScrollView's sibling under the root, so a wheel that
    starts over the unit never reaches the ground: not through the root's pointer
    forwarding and not through the hit window's default propagation. Both axes (#1500). */
void testWin32ZStackUnitWheelLeavesGroundScrollView()
{
  UINT lines = 3;
  LOKA_VERIFY(SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0));
  UINT chars = 3;
  if (!SystemParametersInfoW(0x006C, 0, &chars, 0)) // SPI_GETWHEELSCROLLCHARS
    chars = 3;
  loka::core::MutableState<loka::core::Frame> storage(loka::core::Frame(0, 0, 0, 0));
  NodeState<loka::core::Frame> origin(&storage, 0);
  Fixture f;
  ZStackNode *stack = new ZStackNode((ZStackProps()));
  ScrollViewNode *scroll = new ScrollViewNode(ScrollViewProps(origin));
  scroll->addChild(new RectSurfaceNode(RectSurfaceProps().size(600, 600)));
  stack->addChild(scroll);
  stack->addChild(new BoxNode(BoxProps().setSize(300, 40)));
  f.tree = stack;
  f.layout(300, 140);
  HWND viewport = viewportUnder(f.root());
  HWND unit = childOfClass(f.root(), L"LOKA_STACK_UNIT");
  LOKA_VERIFY(viewport && unit && GetParent(GetParent(viewport)) == f.root() && GetParent(unit) == f.root());
  ShowWindow(f.root(), SW_SHOWNOACTIVATE);
  LOKA_VERIFY(SetWindowPos(f.root(), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE));
  flushPaint(f);
  POINT overUnit = {5, 5};
  LOKA_VERIFY(ClientToScreen(unit, &overUnit));
  HWND hit = WindowFromPoint(overUnit);
  LOKA_VERIFY(hit == unit || IsChild(unit, hit));
  RECT client;
  LOKA_VERIFY(GetClientRect(viewport, &client));
  POINT overGround = {5, client.bottom - 5}; // Below the unit's band.
  LOKA_VERIFY(ClientToScreen(viewport, &overGround));
  HWND groundHit = WindowFromPoint(overGround);
  LOKA_VERIFY(groundHit == viewport || IsChild(viewport, groundHit));
  for (int horizontal = 0; horizontal != 2; ++horizontal)
  {
    // A zero setting scrolls nothing, so neither the shield nor its control would discriminate.
    const char *axis = horizontal ? "horizontal" : "vertical";
    if ((horizontal ? chars : lines) == 0)
    {
      std::printf("[skip] testWin32ZStackUnitWheelLeavesGroundScrollView: system %s wheel scrolling is disabled, "
                  "so the %s half did not run\n",
                  axis,
                  axis);
      continue;
    }
    // WM_MOUSEHWHEEL: positive delta moves right, unlike WM_MOUSEWHEEL.
    const UINT message = horizontal ? 0x020E : WM_MOUSEWHEEL;
    const WPARAM step = MAKEWPARAM(0, horizontal ? WHEEL_DELTA : -WHEEL_DELTA);
    const loka::core::Frame before = origin.get();
    SendMessageW(hit, message, step, MAKELPARAM(overUnit.x, overUnit.y));
    SendMessageW(f.root(), message, step, MAKELPARAM(overUnit.x, overUnit.y));
    LOKA_VERIFY(origin.get().x == before.x && origin.get().y == before.y);
    const int expected = expectedWheelDistance(horizontal ? chars : lines, viewport, horizontal ? SB_HORZ : SB_VERT);
    SendMessageW(f.root(), message, step, MAKELPARAM(overGround.x, overGround.y));
    LOKA_VERIFY(horizontal ? origin.get().x == before.x + expected && origin.get().y == before.y
                           : origin.get().y == before.y + expected && origin.get().x == before.x);
  }
}

void testWin32ZStackNestedInUnitLaysOutNothing()
{
  Fixture f;
  ZStackNode *outer = stackWithGround(f);
  ZStackNode *inner = new ZStackNode((ZStackProps()));
  RectSurfaceNode *innerGround = new RectSurfaceNode(RectSurfaceProps().size(120, 80));
  inner->addChild(innerGround);
  StackNode *buttons = buttonColumn("Nested");
  inner->addChild(buttons);
  outer->addChild(inner);
  f.layout();
  HWND unit = childOfClass(f.root(), L"LOKA_STACK_UNIT");
  LOKA_VERIFY(unit);
  const RECT rect = rectInParent(unit);
  LOKA_VERIFY(rect.bottom == rect.top);
  LOKA_VERIFY(!GetWindow(unit, GW_CHILD));
  LOKA_VERIFY(!inner->getContext() && !innerGround->getContext());
  LOKA_VERIFY(!buttons->childrenHead()->getContext());
}

void testWin32ScrollViewHandlerChildRefusalBlocksCommit()
{
  loka::core::MutableState<loka::core::Frame> storage(loka::core::Frame(0, 1000, 0, 0));
  NodeState<loka::core::Frame> origin(&storage, 0);
  Fixture f;
  ScrollViewNode *scroll = new ScrollViewNode(ScrollViewProps(origin));
  f.tree = scroll;
  StackNode *column = new StackNode(StackProps(STACK_AXIS_COLUMN));
  scroll->addChild(column);
  BoxNode *box = new BoxNode(BoxProps().setSize(180, 400));
  column->addChild(box);
  column->addChild(new TextNode(TextProps("After")));
  f.layout(300, 140); // First pass publishes the exposed clamp.
  const int clamped = origin.get().y;
  LOKA_VERIFY(clamped > 0 && clamped < 1000);
  f.layout(300, 140); // Next pass consumes that origin before the refusal.
  HWND viewport = viewportUnder(f.root());
  const SCROLLINFO before = metrics(viewport);
  box->props.setSize(180, static_cast<short>(SHRT_MAX - Win32SceneLayout::kRootInset - Win32SceneLayout::kGap + 1));
  f.layout(300, 140);
  const SCROLLINFO after = metrics(viewport);
  LOKA_VERIFY(after.nMin == before.nMin && after.nMax == before.nMax);
  LOKA_VERIFY(after.nPage == before.nPage && after.nPos == before.nPos);
  LOKA_VERIFY(origin.get().y == clamped);
}

void testWin32ZStackGroundRefusalStopsUpperChildren()
{
  Fixture f;
  ScrollViewNode *scroll = new ScrollViewNode((ScrollViewProps()));
  f.tree = scroll;
  ZStackNode *stack = new ZStackNode((ZStackProps()));
  scroll->addChild(stack);
  // The surface returns an int bottom beyond short range without a container
  // handler. The ZStack's ground traversal must propagate that refusal itself.
  stack->addChild(new RectSurfaceNode(RectSurfaceProps().size(180, SHRT_MAX)));
  StackNode *buttons = buttonColumn("Must not materialize");
  stack->addChild(buttons);
  f.layout(300, 140);
  HWND viewport = viewportUnder(f.root());
  LOKA_VERIFY(viewport);
  LOKA_VERIFY(childOfClass(viewport, L"LOKA_RECT_SURFACE"));
  LOKA_VERIFY(!childOfClass(viewport, L"LOKA_STACK_UNIT"));
  LOKA_VERIFY(!buttons->childrenHead()->getContext());
}

void testWin32ZStackFirstPassRefusalKeepsUnitEmpty()
{
  Fixture f;
  ScrollViewNode *scroll = new ScrollViewNode((ScrollViewProps()));
  f.tree = scroll;
  ZStackNode *stack = new ZStackNode((ZStackProps()));
  scroll->addChild(stack);
  stack->addChild(new RectSurfaceNode(RectSurfaceProps().size(180, 80)));
  StackNode *column = new StackNode(StackProps(STACK_AXIS_COLUMN));
  column->addChild(new BoxNode(BoxProps().setSize(180, SHRT_MAX)));
  column->addChild(new TextNode(TextProps("Must not materialize")));
  stack->addChild(column);
  f.layout(300, 140);
  HWND viewport = viewportUnder(f.root());
  HWND unit = childOfClass(viewport, L"LOKA_STACK_UNIT");
  LOKA_VERIFY(unit);
  const RECT rect = rectInParent(unit);
  LOKA_VERIFY(rect.bottom == rect.top);
  LOKA_VERIFY(!GetWindow(unit, GW_CHILD));
}

void testWin32ZStackUnitOverScrollViewSurvivesPaintOnlyTick()
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
  ScrollViewNode *scroll = new ScrollViewNode((ScrollViewProps()));
  scroll->addChild(new RectSurfaceNode(RectSurfaceProps().model(&model).size(200, 160)));
  stack->addChild(scroll);
  StackNode *column = new StackNode(StackProps(STACK_AXIS_COLUMN));
  column->addChild(new ButtonNode(ButtonProps().text("B")));
  // The surface contributes exactly 80; the column adds the Button and gap.
  column->addChild(new RectSurfaceNode(RectSurfaceProps().size(40, 80)));
  stack->addChild(column);
  f.layout();
  HWND unit = childOfClass(f.root(), L"LOKA_STACK_UNIT");
  HWND button = native(column->childrenHead());
  HWND viewport = viewportUnder(f.root());
  HWND ground = viewport ? childOfClass(viewport, L"LOKA_RECT_SURFACE") : NULL;
  LOKA_VERIFY(unit && ground && GetParent(button) == unit);
  const RECT rect = rectInParent(unit);
  LOKA_VERIFY(rect.bottom - rect.top == f.controller.rowLayoutMetrics().buttonHeight + Win32SceneLayout::kGap + 80);
  LOKA_VERIFY(rect.bottom - rect.top < 120 && rect.right - rect.left > 100);
  ShowWindow(f.root(), SW_SHOWNOACTIVATE);
  LOKA_VERIFY(SetWindowPos(f.root(), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE));
  flushPaint(f);
  LOKA_VERIFY(capturedPixel(f, rect.left + 100, rect.top + 50) == GetSysColor(COLOR_BTNFACE));
  LOKA_VERIFY(capturedPixel(f, rect.left + 100, rect.top + 120) == GetSysColor(COLOR_WINDOWTEXT));
  for (int phase = 0; phase < 2; ++phase)
  {
    Access::resetRedrawStats(f.controller);
    {
      loka::core::StateTrackerGuard guard(&tracker);
      model.set(phase == 0 ? RectSurfaceModel() : ink);
    }
    // Paint-only tick through the viewport: the ground carries the damage and
    // the unit none. The COLOR_WINDOW read in phase 0 proves the ground painted
    // (black also reads from an unpainted surface).
    Access::flushPendingInvalidations(f.controller);
    LOKA_VERIFY(GetUpdateRect(ground, NULL, FALSE));
    LOKA_VERIFY(!GetUpdateRect(unit, NULL, FALSE));
    flushPaint(f);
    LOKA_VERIFY(capturedPixel(f, rect.left + 100, rect.top + 50) == GetSysColor(COLOR_BTNFACE));
    LOKA_VERIFY(capturedPixel(f, rect.left + 100, rect.top + 120)
                == GetSysColor(phase == 0 ? COLOR_WINDOW : COLOR_WINDOWTEXT));
    const POINT point = {2, 2};
    LOKA_VERIFY(ChildWindowFromPointEx(unit, point, CWP_SKIPINVISIBLE) == button);
    const POINT rootPoint = {rect.left + 2, rect.top + 2};
    LOKA_VERIFY(ChildWindowFromPointEx(f.root(), rootPoint, CWP_SKIPINVISIBLE) == unit);
  }
}

void testWin32ZStackSettledUnitsSkipOrdering()
{
  Fixture f;
  ZStackNode *stack = stackWithGround(f);
  stack->addChild(buttonColumn("B"));
  stack->addChild(buttonColumn("C"));
  f.layout();
  HWND upper = childOfClass(f.root(), L"LOKA_STACK_UNIT");
  HWND lower = FindWindowExW(f.root(), upper, L"LOKA_STACK_UNIT", NULL);
  LOKA_VERIFY(upper && lower && GetWindow(lower, GW_HWNDPREV) == upper && !GetWindow(upper, GW_HWNDPREV));
  {
    UnitOrderProbe probe(lower, upper);
    f.layout();
    LOKA_VERIFY(probe.changes() == 0);
  }
  // Positive control: the same hook sees an actual native raise.
  {
    UnitOrderProbe probe(lower, upper);
    LOKA_VERIFY(SetWindowPos(lower, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOREDRAW));
    LOKA_VERIFY(probe.changes() > 0);
  }
}

void testWin32ZStackRetirementHidesBeforeDrain()
{
  Fixture f;
  Win32ZStackContext context(&f.controller);
  Win32ZStackContext::Unit **cursor = context.unitsBegin();
  Win32ZStackContext::Unit *unit = context.unitFor(1, cursor);
  LOKA_VERIFY(unit
              && unit->ensure(f.root(), f.controller.displayScale().projectFrame(loka::core::Frame(10, 10, 80, 80))));
  HWND hwnd = unit->hwnd();
  ShowWindow(f.root(), SW_SHOWNOACTIVATE);
  LOKA_VERIFY(IsWindowVisible(hwnd));
  context.onFactChanged(NODE_FACT_ATTACHED, NODE_FACT_RETIRED);
  LOKA_VERIFY(IsWindow(hwnd));
  LOKA_VERIFY(!IsWindowVisible(hwnd));
  f.controller.drainNativeRetirements();
  LOKA_VERIFY(!IsWindow(hwnd));
}

void testWin32ScrollViewInsetBandAndCornerPaint()
{
  Fixture f;
  ZStackNode *stack = new ZStackNode((ZStackProps()));
  ScrollViewNode *scroll = new ScrollViewNode(ScrollViewProps().contentInsets(ScrollViewContentInsets::of(40, 30)));
  RectSurfaceNode *surface = new RectSurfaceNode(RectSurfaceProps().size(600, 600));
  scroll->addChild(surface);
  stack->addChild(scroll);
  stack->addChild(new BoxNode(BoxProps().setSize(300, 40)));
  f.tree = stack;
  f.layout(300, 140);
  HWND frame = FindWindowExW(f.root(), NULL, L"LOKA_SCROLL_FRAME", NULL);
  HWND viewport = FindWindowExW(frame, NULL, L"LOKA_SCROLL_VIEW", NULL);
  HWND bar = FindWindowExW(frame, NULL, L"SCROLLBAR", NULL);
  HWND unit = childOfClass(f.root(), L"LOKA_STACK_UNIT");
  LOKA_VERIFY(frame && viewport && bar && unit);
  ShowWindow(f.root(), SW_SHOWNOACTIVATE);
  LOKA_VERIFY(SetWindowPos(f.root(), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE));
  flushPaint(f);
  RECT frameClient;
  LOKA_VERIFY(GetClientRect(frame, &frameClient));
  POINT exposed = {frameClient.right - GetSystemMetrics(SM_CXVSCROLL) / 2, 41};
  POINT covered = {exposed.x, 20};
  LOKA_VERIFY(ClientToScreen(frame, &exposed));
  LOKA_VERIFY(ClientToScreen(frame, &covered));
  LOKA_VERIFY(WindowFromPoint(exposed) == bar);
  LOKA_VERIFY(WindowFromPoint(covered) == unit);
  const RECT barBefore = rectInParent(bar);
  const RECT seat = rectInParent(frame);
  const int cornerX = seat.right - 2;
  const int cornerY = seat.bottom - 2;
  LOKA_VERIFY(GetWindowLongPtrW(viewport, GWL_STYLE) & WS_HSCROLL);
  LOKA_VERIFY(capturedPixel(f, cornerX, cornerY) == GetSysColor(COLOR_BTNFACE));
  // Frame-only painting must exclude the document child's existing pixels.
  HWND content = childOfClass(viewport, L"LOKA_RECT_SURFACE");
  LOKA_VERIFY(content);
  completePaint(f.root());
  const COLORREF childBefore = capturedPixel(f, seat.left + 10, seat.top + 70);
  LOKA_VERIFY(childBefore == GetSysColor(COLOR_WINDOW));
  LOKA_VERIFY(InvalidateRect(frame, NULL, FALSE));
  // WS_CLIPCHILDREN keeps a frame invalidation out of its children: without it
  // Windows invalidates the viewport too, and the final pixels hide the extra pass.
  LOKA_VERIFY(!GetUpdateRect(viewport, NULL, FALSE));
  LOKA_VERIFY(UpdateWindow(frame));
  LOKA_VERIFY(!GetUpdateRect(content, NULL, FALSE));
  LOKA_VERIFY(capturedPixel(f, seat.left + 10, seat.top + 70) == childBefore);
  surface->props.size(100, 600);
  f.controller.relayout(300, 140);
  flushPaint(f);
  const RECT barAfter = rectInParent(bar);
  LOKA_VERIFY(EqualRect(&barBefore, &barAfter));
  LOKA_VERIFY(!(GetWindowLongPtrW(viewport, GWL_STYLE) & WS_HSCROLL));
  // Window ground currently resolves to BTNFACE too: the no-horizontal-bar
  // pixel pins the ground, but cannot distinguish omission of the corner fill.
  LOKA_VERIFY(capturedPixel(f, cornerX, cornerY) == GetSysColor(COLOR_BTNFACE));
}
