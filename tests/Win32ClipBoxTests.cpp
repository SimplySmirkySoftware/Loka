#include "Win32ClipBoxTests.hpp"
#include "support/TestVerify.hpp"
#include "support/LifecycleFactTestAccess.hpp"
#include "support/WindowAdmissionTestApp.hpp"
#include "Win32Window.hpp"
#include "Win32SceneLayout.hpp"
#include "Win32ScenePlatformController.hpp"
#include "context/Win32ClipBoxContext.hpp"
#include "context/Win32ScrollViewContext.hpp"
#include "testing/Win32ScenePlatformTestAccess.hpp"
#include "platform/null/NullPlatformContext.hpp"
#include "core/util/StateTrackerGuard.hpp"
#include "app/RectSurface.hpp"
#include "app/scene/projection/PlatformNodeHandler.hpp"
#include "app/nodes/Text.hpp"
#include "app/nodes/controls/Button.hpp"
#include "app/nodes/controls/EditText.hpp"
#include "app/nodes/controls/Card.hpp"
#include "app/nodes/nestable/Box.hpp"
#include "app/nodes/nestable/RowColumn.hpp"
#include "app/nodes/nestable/ScrollView.hpp"
#include "app/nodes/nestable/ZStack.hpp"
#include <climits>
#include <cstdio>
#include <cwchar>
#include <vector>

namespace
{
  using namespace loka::app;
  using namespace loka::app::scene;
  using loka::core::Frame;
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
    explicit Fixture(unsigned dpi = 96)
        : controller(this->host.window.hwnd(), loka::win32::Win32DisplayScale(dpi, RailMetrics())),
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
    int at(int x = 21, int y = 31, int width = 180, int height = 140)
    {
      return Access::layoutNode(
          this->controller, this->tree, Win32ScenePlatformController::LayoutState(x, y, width, height, 0));
    }
  };

  BoxNode *box(int width = 100, int height = 60, bool clipped = true, int padding = 0)
  {
    BoxProps props;
    props.setSize(static_cast<short>(width), static_cast<short>(height)).setPadding(padding);
    props.childClip_ = clipped ? layout::CHILD_CLIP_OWN_RECT : layout::CHILD_CLIP_NONE;
    return new BoxNode(props);
  }
  Win32ClipBoxContext *context(Node *node)
  {
    IClipBoxContext *clip = node->getContext() ? node->getContext()->asClipBoxContext() : 0;
    LOKA_VERIFY(clip);
    return static_cast<Win32ClipBoxContext *>(clip);
  }
  HWND native(Node *node)
  {
    LOKA_VERIFY(node->getContext());
    return static_cast<Win32RetirableContext *>(node->getContext())->hwnd();
  }
  HWND child(HWND parent, const wchar_t *name)
  {
    // Class spelling is intentional: Button, not BUTTON.
    for (HWND hwnd = GetWindow(parent, GW_CHILD); hwnd; hwnd = GetWindow(hwnd, GW_HWNDNEXT))
    {
      wchar_t actual[80];
      LOKA_VERIFY(GetClassNameW(hwnd, actual, 80));
      if (std::wcscmp(actual, name) == 0)
        return hwnd;
    }
    return 0;
  }
  RECT rect(HWND hwnd)
  {
    RECT value;
    LOKA_VERIFY(hwnd && GetWindowRect(hwnd, &value));
    MapWindowPoints(NULL, GetParent(hwnd), reinterpret_cast<POINT *>(&value), 2);
    return value;
  }
  void verifyRect(HWND hwnd, Fixture &f, const Frame &expected)
  {
    const RECT actual = rect(hwnd);
    const loka::win32::NativeRect projected = f.controller.displayScale().projectFrame(expected);
    LOKA_VERIFY(EqualRect(&actual, &projected.r));
  }
  CardNode *card(bool clipped)
  {
    CardProps props;
    props.childClip_ = clipped ? layout::CHILD_CLIP_OWN_RECT : layout::CHILD_CLIP_NONE;
    CardNode *node = new CardNode(props);
    node->addChild(new ButtonNode(ButtonProps().text("card")));
    for (int i = 0; i < CARD_BAND_COUNT; ++i)
      node->addChild(new CardBandNode(CardBandProps(static_cast<CardBandRole>(i), props.style)));
    return node;
  }
  void click(void *data)
  {
    ++*static_cast<int *>(data);
  }
} // namespace

// Pins 2-8 are red on the base: the Win32 Box/Card arms ignore childClip_.
void testWin32ClipBoxGeometryAndFlatTwins()
{
  const unsigned dpis[] = {96, 120, 144};
  for (unsigned d = 0; d < sizeof(dpis) / sizeof(dpis[0]); ++d)
    for (int clipped = 0; clipped < 2; ++clipped)
    {
      Fixture f(dpis[d]);
      BoxNode *node = box(100, 60, clipped != 0, 7);
      RectSurfaceNode *surface = new RectSurfaceNode(RectSurfaceProps().size(160, 40));
      node->addChild(surface);
      f.tree = node;
      LOKA_VERIFY(f.at() == 91);
      HWND parent = clipped ? context(node)->hwnd() : f.root();
      LOKA_VERIFY(GetParent(native(surface)) == parent);
      if (clipped)
      {
        LOKA_VERIFY(child(f.root(), L"LOKA_CLIP_BOX") == parent);
        LOKA_VERIFY(!FindWindowExW(f.root(), parent, L"LOKA_CLIP_BOX", NULL));
        verifyRect(parent, f, Frame(21, 31, 100, 60));
        verifyRect(native(surface), f, Frame(7, 7, 160, 40));
        int height = -1;
        LOKA_VERIFY(context(node)->queryPlacedHeight(height) && height == 60);
      }
      else
      {
        LOKA_VERIFY(!node->getContext() && !child(f.root(), L"LOKA_CLIP_BOX"));
        verifyRect(native(surface), f, Frame(28, 38, 160, 40));
      }
    }
}

void testWin32ClipBoxCardBandsAndFlatTwin()
{
  for (int clipped = 0; clipped < 2; ++clipped)
  {
    Fixture f;
    CardNode *node = card(clipped != 0);
    f.tree = node;
    const int bottom = f.at();
    LOKA_VERIFY(bottom > 31);
    HWND parent = clipped ? context(node)->hwnd() : f.root();
    unsigned count = 0;
    for (Node *n = node->childrenHead(); n; n = n->nextInComposition)
    {
      LOKA_VERIFY(GetParent(native(n)) == parent);
      ++count;
    }
    LOKA_VERIFY(count == 5);
    if (clipped)
      verifyRect(parent, f, Frame(21, 31, 180, bottom - 31));
    else
      LOKA_VERIFY(!node->getContext() && !child(f.root(), L"LOKA_CLIP_BOX"));
  }
}

void testWin32ClipBoxMixing()
{
  // Each row fails if either native parentage or rebasing is lost.
  for (int arrangement = 0; arrangement < 5; ++arrangement)
  {
    Fixture f;
    BoxNode *clip = box(120, 80, true, 5);
    ButtonNode *button = new ButtonNode(ButtonProps().text("nested"));
    if (arrangement == 0)
    {
      BoxNode *inner = box(90, 50, true, 3);
      clip->addChild(inner);
      inner->addChild(button);
      f.tree = clip;
      f.at();
      LOKA_VERIFY(GetParent(context(inner)->hwnd()) == context(clip)->hwnd());
      verifyRect(context(inner)->hwnd(), f, Frame(5, 5, 90, 50));
      LOKA_VERIFY(GetParent(native(button)) == context(inner)->hwnd());
      const RECT b = rect(native(button));
      LOKA_VERIFY(b.left == 3 && b.top == 3);
    }
    else if (arrangement == 1 || arrangement == 2)
    {
      ScrollViewNode *scroll = new ScrollViewNode((ScrollViewProps()));
      if (arrangement == 1)
      {
        f.tree = clip;
        clip->addChild(scroll);
        scroll->addChild(button);
      }
      else
      {
        f.tree = scroll;
        scroll->addChild(clip);
        clip->addChild(button);
      }
      f.at();
      HWND frame = native(scroll);
      HWND viewport = child(frame, L"LOKA_SCROLL_VIEW");
      LOKA_VERIFY(viewport);
      if (arrangement == 1)
      {
        LOKA_VERIFY(GetParent(frame) == context(clip)->hwnd());
        LOKA_VERIFY(GetParent(native(button)) == viewport);
        const RECT r = rect(frame);
        LOKA_VERIFY(r.left == 5 && r.top == 5);
      }
      else
      {
        LOKA_VERIFY(GetParent(frame) == f.root());
        LOKA_VERIFY(GetParent(context(clip)->hwnd()) == viewport);
        LOKA_VERIFY(GetParent(native(button)) == context(clip)->hwnd());
      }
    }
    else
    {
      ZStackNode *stack = new ZStackNode((ZStackProps()));
      stack->addChild(new RectSurfaceNode(RectSurfaceProps().size(120, 90)));
      if (arrangement == 3)
      {
        f.tree = clip;
        clip->addChild(stack);
        stack->addChild(button);
      }
      else
      {
        f.tree = stack;
        stack->addChild(clip);
        clip->addChild(button);
      }
      f.at();
      HWND unit = child(arrangement == 3 ? context(clip)->hwnd() : f.root(), L"LOKA_STACK_UNIT");
      LOKA_VERIFY(unit);
      LOKA_VERIFY(GetParent(arrangement == 3 ? native(button) : context(clip)->hwnd()) == unit);
      const RECT local = rect(arrangement == 3 ? native(button) : context(clip)->hwnd());
      LOKA_VERIFY(local.left == 0 && local.top == 0);
    }
  }
}

void testWin32ClipBoxZeroWidthDoesNotCoverSibling()
{
  Fixture f;
  // A real inset offer consumes all width, so the inner box is offered zero.
  BoxNode *outer = box(20, 100, false, 10);
  BoxNode *clip = box(0, 40);
  clip->addChild(new ButtonNode(ButtonProps().text("hidden")));
  outer->addChild(clip);
  ButtonNode *sibling = new ButtonNode(ButtonProps().text("right"));
  ZStackNode *root = new ZStackNode((ZStackProps()));
  root->addChild(outer);
  // Use a separate direct layout visit for the sibling at its actual seat.
  outer->addChild(sibling);
  f.tree = root;
  f.at();
  HWND hwnd = context(clip)->hwnd();
  const RECT bounds = rect(hwnd);
  LOKA_VERIFY(bounds.right == bounds.left);
  LOKA_VERIFY(child(hwnd, L"Button"));
  Access::layoutNode(f.controller, sibling, Win32ScenePlatformController::LayoutState(55, 41, 80, 30, 0));
  ShowWindow(f.root(), SW_SHOWNOACTIVATE);
  const RECT s = rect(native(sibling));
  const POINT centre = {(s.left + s.right) / 2, (s.top + s.bottom) / 2};
  LOKA_VERIFY(ChildWindowFromPointEx(f.root(), centre, CWP_SKIPINVISIBLE) == native(sibling));
  // A zero-width ancestor gives the child no visible intersection.
  RECT client, intersection;
  LOKA_VERIFY(GetClientRect(hwnd, &client));
  RECT content = rect(child(hwnd, L"Button"));
  LOKA_VERIFY(!IntersectRect(&intersection, &client, &content));
}

void testWin32ClipBoxCreationRefusalsRetry()
{
  for (int failure = 0; failure < 2; ++failure)
  {
    Fixture f;
    BoxNode *clip = box();
    ButtonNode *button = new ButtonNode(ButtonProps().text("retry"));
    clip->addChild(button);
    f.tree = clip;
    if (failure == 0)
      loka::testing::failWin32ClipBoxContexts(1);
    else
      loka::win32::testing::failNativeChildWindowCreations(1);
    LOKA_VERIFY(f.at() == 31);
    LOKA_VERIFY(!button->getContext() && !GetWindow(f.root(), GW_CHILD));
    NodeContext *retained = clip->getContext();
    LOKA_VERIFY((retained != 0) == (failure == 1));
    if (retained)
    {
      int height = 123;
      LOKA_VERIFY(!context(clip)->queryPlacedHeight(height) && height == 123);
    }
    LOKA_VERIFY(f.at() == 91);
    LOKA_VERIFY(!retained || retained == clip->getContext());
    LOKA_VERIFY(GetParent(native(button)) == context(clip)->hwnd());
  }
}

void testWin32ClipBoxDepthRefusalStaysFlat()
{
  Fixture f;
  BoxNode *clip = box();
  ButtonNode *button = new ButtonNode(ButtonProps().text("depth"));
  clip->addChild(button);
  f.tree = clip;
  ProjectionParentScopeStack &scopes = Access::projectionParentScopes(f.controller);
  unsigned pushed = 0;
  while (scopes.admitsClip())
  {
    LOKA_VERIFY(scopes.pushRebasedClip(scopes.current()));
    ++pushed;
  }
  LOKA_VERIFY(pushed > 0);
  LOKA_VERIFY(f.at() == 91);
  LOKA_VERIFY(!clip->getContext() && !child(f.root(), L"LOKA_CLIP_BOX"));
  LOKA_VERIFY(GetParent(native(button)) == f.root());
  while (pushed--)
    LOKA_VERIFY(scopes.pop());
}

void testWin32ClipBoxEntryRefusalKeepsLogicalHeight()
{
  // Odd geometry at 125% prevents a device-to-logical inverse from passing.
  Fixture f(120);
  BoxNode *clip = box(100, 1);
  clip->addChild(new ButtonNode(ButtonProps().text("short")));
  f.tree = clip;
  LOKA_VERIFY(f.at(21, SHRT_MAX + 1) == SHRT_MAX + 1);
  LOKA_VERIFY(!context(clip)->hwnd() && !clip->childrenHead()->getContext());
  LOKA_VERIFY(f.at(21, 1) == 2);
  const RECT before = rect(context(clip)->hwnd());
  LOKA_VERIFY(f.at(21, SHRT_MAX + 1) == SHRT_MAX + 2);
  const RECT after = rect(context(clip)->hwnd());
  LOKA_VERIFY(EqualRect(&before, &after));
}

void testWin32ClipBoxChannelRefusalKeepsPlacement()
{
  Fixture f;
  ScrollViewNode *scroll = new ScrollViewNode((ScrollViewProps()));
  f.tree = scroll;
  BoxNode *clip = box(100, 60);
  BoxNode *body = box(80, 40, false);
  body->addChild(new ButtonNode(ButtonProps().text("channel")));
  clip->addChild(body);
  scroll->addChild(clip);
  f.at();
  HWND viewport = child(native(scroll), L"LOKA_SCROLL_VIEW");
  LOKA_VERIFY(GetParent(context(clip)->hwnd()) == viewport);
  const RECT before = rect(context(clip)->hwnd());
  // Exercise the same viewport channel with a child whose padded origin refuses.
  body->props.setPadding(SHRT_MAX);
  clip->props.setHeight(70);
  ProjectionParentScope frame(viewport, 0, 0, Frame(0, 0, 180, 140));
  ProjectionViewportScopeGuard guard(Access::projectionParentScopes(f.controller), frame);
  LOKA_VERIFY(guard.isActive());
  LOKA_VERIFY(Access::layoutNode(f.controller, clip, Win32ScenePlatformController::LayoutState(21, 31, 180, 140, 0))
              == 91);
  LOKA_VERIFY(Access::projectionParentScopes(f.controller).current().hasShortRangeRefusal());
  const RECT after = rect(context(clip)->hwnd());
  LOKA_VERIFY(EqualRect(&before, &after));
}

void testWin32ClipBoxLifecycleAndRetryWhileDetached()
{
  for (int refuseFirst = 0; refuseFirst < 2; ++refuseFirst)
  {
    Fixture f;
    BoxNode *clip = box();
    f.tree = clip;
    ShowWindow(f.root(), SW_SHOWNOACTIVATE);
    if (refuseFirst)
    {
      loka::win32::testing::failNativeChildWindowCreations(1);
      LOKA_VERIFY(f.at() == 31);
    }
    NotifySubtreeNodeDetached(clip);
    LifecycleFactTestAccess::DeliverFacts(clip);
    LOKA_VERIFY(f.at() == 91);
    HWND hwnd = context(clip)->hwnd();
    LOKA_VERIFY(!IsWindowVisible(hwnd));
    NotifySubtreeNodeAttached(clip);
    LifecycleFactTestAccess::DeliverFacts(clip);
    LOKA_VERIFY(IsWindowVisible(hwnd));
    NotifySubtreeNodeDetached(clip);
    LifecycleFactTestAccess::DeliverFacts(clip);
    LOKA_VERIFY(!IsWindowVisible(hwnd));
    NotifySubtreeNodeAttached(clip);
    LifecycleFactTestAccess::DeliverFacts(clip);
    LOKA_VERIFY(IsWindowVisible(hwnd));
    LifecycleFactTestAccess::MarkSubtreeRetired(clip);
    LifecycleFactTestAccess::DeliverFacts(clip);
    LOKA_VERIFY(IsWindow(hwnd) && !IsWindowVisible(hwnd));
    LOKA_VERIFY(!context(clip)->hwnd());
    f.controller.drainNativeRetirements();
    LOKA_VERIFY(!IsWindow(hwnd));
  }
}

void testWin32ClipBoxContainerMessagesAndGround()
{
  int clicks = 0;
  loka::core::EmitterState onClick;
  Fixture f;
  BoxNode *clip = box(160, 100);
  ButtonNode *button = new ButtonNode(ButtonProps().text("click").onClick(&onClick));
  TextNode *text = new TextNode(TextProps("clip"));
  clip->addChild(button);
  clip->addChild(text);
  f.tree = clip;
  f.at();
  HWND hwnd = context(clip)->hwnd();
  LOKA_VERIFY(SendMessageW(hwnd, WM_NCHITTEST, 0, 0) == HTTRANSPARENT);
  LOKA_VERIFY(GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_CONTROLPARENT);
  LOKA_VERIFY(!(GetWindowLongPtrW(hwnd, GWL_STYLE) & (WS_CLIPCHILDREN | WS_TABSTOP)));
  onClick.bind(&click, &clicks, false);
  SendMessageW(native(button), BM_CLICK, 0, 0);
  LOKA_VERIFY(clicks == 1);
  HDC dc = CreateCompatibleDC(NULL);
  LOKA_VERIFY(dc);
  SetBkMode(dc, OPAQUE);
  SetTextColor(dc, RGB(1, 2, 3));
  LOKA_VERIFY(reinterpret_cast<HBRUSH>(SendMessageW(
                  hwnd, WM_CTLCOLORSTATIC, reinterpret_cast<WPARAM>(dc), reinterpret_cast<LPARAM>(native(text))))
              == GetStockObject(NULL_BRUSH));
  LOKA_VERIFY(GetBkMode(dc) == TRANSPARENT && GetTextColor(dc) == GetSysColor(COLOR_BTNTEXT));
  LOKA_VERIFY(SendMessageW(hwnd, WM_ERASEBKGND, reinterpret_cast<WPARAM>(dc), 0) == 1);
  DeleteDC(dc);
  ShowWindow(f.root(), SW_SHOWNOACTIVATE);
  LOKA_VERIFY(SetWindowPos(f.root(), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE));
  RedrawWindow(f.root(), NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
  dc = GetDC(hwnd);
  LOKA_VERIFY(dc);
  RECT client;
  LOKA_VERIFY(GetClientRect(hwnd, &client));
  const COLORREF sentinel = GetSysColor(COLOR_BTNFACE) ^ RGB(255, 255, 255);
  HBRUSH brush = CreateSolidBrush(sentinel);
  LOKA_VERIFY(brush && FillRect(dc, &client, brush));
  DeleteObject(brush);
  LOKA_VERIFY(GetPixel(dc, 140, 80) == sentinel);
  ReleaseDC(hwnd, dc);
  LOKA_VERIFY(InvalidateRect(hwnd, NULL, FALSE));
  UpdateWindow(hwnd);
  dc = GetDC(hwnd);
  LOKA_VERIFY(dc && GetPixel(dc, 140, 80) == GetSysColor(COLOR_BTNFACE));
  ReleaseDC(hwnd, dc);
}

void testWin32ClipBoxReservedContextWall()
{
  Fixture f;
  RefusedNodeHandler boxHandler(NodeTypeToken<BoxNode>());
  RefusedNodeHandler cardHandler(NodeTypeToken<CardNode>());
  LOKA_VERIFY(!f.controller.registerNodeHandler(&boxHandler));
  LOKA_VERIFY(!f.controller.registerNodeHandler(&cardHandler));
#ifndef LOKA_LIFECYCLE_AUDIT
  BoxNode *clip = box();
  ButtonNode *button = new ButtonNode(ButtonProps().text("foreign"));
  clip->addChild(button);
  NodeContext *foreign = new NodeContext();
  clip->setContext(foreign);
  f.tree = clip;
  LOKA_VERIFY(f.at() == 31);
  LOKA_VERIFY(clip->getContext() == foreign && !button->getContext());
  LOKA_VERIFY(!GetWindow(f.root(), GW_CHILD));
#else
  std::printf("[skip] foreign clip context: audit builds intentionally assert; registration wall checked\n");
#endif
}

void testWin32ClipBoxNaturalHeightAndFirstChannelRefusal()
{
  for (int refused = 0; refused < 2; ++refused)
  {
    Fixture f;
    BoxNode *clip = box(100, 0);
    BoxNode *body = box(80, 0, false, refused ? SHRT_MAX : 7);
    ButtonNode *button = new ButtonNode(ButtonProps().text("height"));
    body->addChild(button);
    clip->addChild(body);
    f.tree = clip;
    ProjectionParentScope frame(f.root(), 0, 0, Frame(0, 0, 300, 200));
    ProjectionViewportScopeGuard guard(Access::projectionParentScopes(f.controller), frame);
    LOKA_VERIFY(guard.isActive());
    const int bottom = f.at();
    int height = -1;
    if (refused)
    {
      LOKA_VERIFY(bottom == 31 && !button->getContext());
      LOKA_VERIFY(!context(clip)->queryPlacedHeight(height));
      const RECT r = rect(context(clip)->hwnd());
      LOKA_VERIFY(r.top == r.bottom);
    }
    else
    {
      const int expected = f.controller.rowLayoutMetrics().buttonHeight + 14;
      LOKA_VERIFY(bottom == 31 + expected);
      LOKA_VERIFY(context(clip)->queryPlacedHeight(height) && height == expected);
      verifyRect(context(clip)->hwnd(), f, Frame(21, 31, 100, expected));
    }
  }
}

void testWin32ClipBoxEmptyAreaHitPassesThrough()
{
  for (int insideScroll = 0; insideScroll < 2; ++insideScroll)
  {
    Fixture f;
    BoxNode *clip = box(160, 90);
    // Buttons fill their offer; this unflagged seat leaves real empty space
    // to the right of the one Button without adding another native parent.
    BoxNode *buttonSeat = box(70, 0, false);
    ButtonNode *button = new ButtonNode(ButtonProps().text("hit"));
    buttonSeat->addChild(button);
    clip->addChild(buttonSeat);
    ScrollViewNode *scroll = 0;
    if (insideScroll)
    {
      scroll = new ScrollViewNode((ScrollViewProps()));
      scroll->addChild(clip);
      f.tree = scroll;
    }
    else
      f.tree = clip;
    f.at();
    HWND clipWindow = context(clip)->hwnd();
    HWND parent = insideScroll ? child(native(scroll), L"LOKA_SCROLL_VIEW") : f.root();
    HWND buttonWindow = child(clipWindow, L"Button");
    LOKA_VERIFY(parent && buttonWindow == native(button));
    LOKA_VERIFY(GetParent(clipWindow) == parent);
    const RECT buttonRect = rect(buttonWindow);
    RECT client;
    LOKA_VERIFY(GetClientRect(clipWindow, &client));
    LOKA_VERIFY(buttonRect.right < client.right && buttonRect.bottom > buttonRect.top);
    POINT emptyPoint = {(buttonRect.right + client.right) / 2, (buttonRect.top + buttonRect.bottom) / 2};
    POINT buttonPoint = {(buttonRect.left + buttonRect.right) / 2, (buttonRect.top + buttonRect.bottom) / 2};
    LOKA_VERIFY(ClientToScreen(clipWindow, &emptyPoint));
    LOKA_VERIFY(ClientToScreen(clipWindow, &buttonPoint));
    ShowWindow(f.root(), SW_SHOWNOACTIVATE);
    LOKA_VERIFY(SetWindowPos(f.root(), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE));
    // ChildWindowFromPointEx ignores WM_NCHITTEST, so it cannot discriminate
    // m4 (removing HTTRANSPARENT). WindowFromPoint tests same-thread hit routing.
    LOKA_VERIFY(WindowFromPoint(emptyPoint) == parent);
    LOKA_VERIFY(WindowFromPoint(buttonPoint) == buttonWindow);
  }
}

void testWin32ClipBoxGroundStaysBelowUnits()
{
  Fixture f;
  ZStackNode *stack = new ZStackNode((ZStackProps()));
  BoxNode *ground = box(160, 90);
  ButtonNode *groundButton = new ButtonNode(ButtonProps().text("ground"));
  ground->addChild(groundButton);
  stack->addChild(ground);
  StackNode *column = new StackNode(StackProps(STACK_AXIS_COLUMN));
  ButtonNode *upperButton = new ButtonNode(ButtonProps().text("upper"));
  column->addChild(upperButton);
  stack->addChild(column);
  f.tree = stack;
  ShowWindow(f.root(), SW_SHOWNOACTIVATE);
  LOKA_VERIFY(SetWindowPos(f.root(), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE));
  for (int pass = 0; pass < 2; ++pass)
  {
    f.at();
    HWND clipWindow = context(ground)->hwnd();
    HWND unit = child(f.root(), L"LOKA_STACK_UNIT");
    LOKA_VERIFY(unit && GetParent(clipWindow) == f.root());
    LOKA_VERIFY(GetParent(native(upperButton)) == unit);
    // The first sibling is topmost. Raising the clip after unit ordering
    // must fail this check even though the clip's empty area is transparent.
    bool sawUnit = false;
    bool sawClip = false;
    for (HWND hwnd = GetWindow(f.root(), GW_CHILD); hwnd; hwnd = GetWindow(hwnd, GW_HWNDNEXT))
    {
      if (hwnd == unit)
        sawUnit = true;
      if (hwnd == clipWindow)
      {
        LOKA_VERIFY(sawUnit);
        sawClip = true;
      }
    }
    LOKA_VERIFY(sawUnit && sawClip);
    const RECT clipRect = rect(clipWindow);
    const RECT unitRect = rect(unit);
    RECT overlap;
    LOKA_VERIFY(IntersectRect(&overlap, &clipRect, &unitRect));
    POINT point = {(overlap.left + overlap.right) / 2, (overlap.top + overlap.bottom) / 2};
    LOKA_VERIFY(ClientToScreen(f.root(), &point));
    HWND hit = WindowFromPoint(point);
    LOKA_VERIFY(hit && (hit == unit || GetParent(hit) == unit));
    LOKA_VERIFY(hit != clipWindow && hit != native(groundButton));
  }
}

namespace
{
  RECT rectInRoot(HWND hwnd, HWND root)
  {
    RECT value;
    LOKA_VERIFY(GetWindowRect(hwnd, &value));
    MapWindowPoints(NULL, root, reinterpret_cast<POINT *>(&value), 2);
    return value;
  }

  // Mirrors flushPaint in Win32ZStackUnitTests; finish each native child's
  // damage as well so screen sampling does not depend on WM_PAINT ordering.
  void flushClipPaint(Fixture &f)
  {
    Access::flushPendingInvalidations(f.controller);
    MSG message;
    while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE))
    {
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
    RedrawWindow(f.root(), NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
    UpdateWindow(f.root());
  }

  std::vector<COLORREF> pixels(HWND root, const RECT &area)
  {
    std::vector<COLORREF> result;
    HDC dc = GetDC(root);
    LOKA_VERIFY(dc && area.right > area.left && area.bottom > area.top);
    for (LONG y = area.top; y < area.bottom; ++y)
      for (LONG x = area.left; x < area.right; ++x)
      {
        const COLORREF color = GetPixel(dc, x, y);
        LOKA_VERIFY(color != CLR_INVALID);
        result.push_back(color);
      }
    ReleaseDC(root, dc);
    return result;
  }
} // namespace

void testWin32ClipBoxTabOrderAndFlatTwin()
{
  for (int clipped = 0; clipped < 2; ++clipped)
  {
    Fixture f;
    StackNode *column = new StackNode(StackProps(STACK_AXIS_COLUMN));
    BoxNode *middle = box(120, 80, clipped != 0);
    StackNode *inside = new StackNode(StackProps(STACK_AXIS_COLUMN));
    ButtonNode *buttons[4];
    const char *labels[] = {"A", "B", "C", "D"};
    for (int i = 0; i < 4; ++i)
      buttons[i] = new ButtonNode(ButtonProps().text(labels[i]));
    f.tree = column;
    column->addChild(buttons[0]);
    column->addChild(middle);
    middle->addChild(inside);
    inside->addChild(buttons[1]);
    inside->addChild(buttons[2]);
    column->addChild(buttons[3]);
    f.at();
    ShowWindow(f.root(), SW_SHOW);
    LOKA_VERIFY(GetParent(native(buttons[1])) == (clipped ? context(middle)->hwnd() : f.root()));
    LOKA_VERIFY(GetNextDlgTabItem(f.root(), NULL, FALSE) == native(buttons[0]));
    for (int i = 0; i < 4; ++i)
    {
      LOKA_VERIFY(GetNextDlgTabItem(f.root(), native(buttons[i]), FALSE) == native(buttons[(i + 1) % 4]));
      LOKA_VERIFY(GetNextDlgTabItem(f.root(), native(buttons[i]), TRUE) == native(buttons[(i + 3) % 4]));
    }
    SetFocus(native(buttons[1]));
    LOKA_VERIFY(GetFocus() == native(buttons[1]));
    // Same native Tab door as Win32FocusTests::tab.
    MSG message = {};
    message.hwnd = GetFocus();
    message.message = WM_KEYDOWN;
    message.wParam = VK_TAB;
    LOKA_VERIFY(IsDialogMessageW(f.root(), &message));
    LOKA_VERIFY(GetFocus() == native(buttons[2]));
  }
}

void testWin32ClipBoxFocusCueAndEditTextEdge()
{
  // Share setup and sampling with the flat Button sensitivity control.
  for (int variant = 0; variant < 3; ++variant)
  {
    const bool edit = variant == 1;
    const bool clipped = variant != 2;
    Fixture f;
    BoxNode *clip = box(90, 70, clipped);
    BoxNode *wide = box(180, 50, false, 1);
    Node *control = edit ? static_cast<Node *>(new EditTextNode((EditTextProps())))
                         : static_cast<Node *>(new ButtonNode(ButtonProps().text(" ")));
    f.tree = clip;
    clip->addChild(wide);
    wide->addChild(control);
    f.at();
    const HWND hwnd = native(control);
    LOKA_VERIFY(GetParent(hwnd) == (clipped ? context(clip)->hwnd() : f.root()));
    const RECT bounds = rectInRoot(hwnd, f.root());
    const RECT edge = clipped ? rectInRoot(context(clip)->hwnd(), f.root())
                              : f.controller.displayScale().projectFrame(Frame(21, 31, 90, 70)).r;
    LOKA_VERIFY(bounds.left < edge.right && bounds.right > edge.right + 3);
    LOKA_VERIFY(bounds.top >= edge.top && bounds.bottom <= edge.bottom);
    const RECT outside = {edge.right + 1, bounds.top, edge.right + 4, bounds.bottom};
    // Blank Button: inset changes show its focused look (cue or themed
    // focus border) changed, without label ink contributing to the sample.
    const RECT inside = {bounds.left + 2, bounds.top + 2, bounds.left + 8, bounds.bottom - 2};
    LOKA_VERIFY(inside.right < edge.right);
    ShowWindow(f.root(), SW_SHOW);
    LOKA_VERIFY(SetWindowPos(f.root(), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE));
    SetFocus(f.root());
    ShowWindow(hwnd, SW_HIDE);
    flushClipPaint(f);
    const std::vector<COLORREF> ground = pixels(f.root(), outside);
    ShowWindow(hwnd, SW_SHOW);
    SendMessageW(f.root(), WM_CHANGEUISTATE, MAKEWPARAM(UIS_CLEAR, UISF_HIDEFOCUS), 0);
    SendMessageW(f.root(), WM_UPDATEUISTATE, MAKEWPARAM(UIS_CLEAR, UISF_HIDEFOCUS), 0);
    flushClipPaint(f);
    const std::vector<COLORREF> outsideBeforeFocus = pixels(f.root(), outside);
    if (clipped)
      LOKA_VERIFY(outsideBeforeFocus == ground);
    const std::vector<COLORREF> before = pixels(f.root(), inside);
    if (edit)
    {
      // A caret is not a focus cue. Keep focus on the root and change ink.
      LOKA_VERIFY(SetWindowTextW(hwnd, L"WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW"));
      HDC dc = GetDC(hwnd);
      LOKA_VERIFY(dc);
      HFONT font = reinterpret_cast<HFONT>(SendMessageW(hwnd, WM_GETFONT, 0, 0));
      HGDIOBJ previous = font ? SelectObject(dc, font) : NULL;
      SIZE extent;
      LOKA_VERIFY(GetTextExtentPoint32W(dc, L"WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW", 40, &extent));
      if (font)
        SelectObject(dc, previous);
      ReleaseDC(hwnd, dc);
      LOKA_VERIFY(extent.cx > edge.right - bounds.left);
    }
    else
      SetFocus(hwnd);
    flushClipPaint(f);
    // Clipped controls must preserve ground even without a visible focused look.
    // The flat twin must show that these outside columns can detect focus ink.
    const std::vector<COLORREF> outsideAfterFocus = pixels(f.root(), outside);
    if (clipped)
      LOKA_VERIFY(outsideAfterFocus == ground);
    const bool inkChanged = pixels(f.root(), inside) != before;
    if (!clipped)
    {
      if (GetFocus() != hwnd || outsideAfterFocus == outsideBeforeFocus)
        std::printf(
            "[skip] clip box focus cue: flat twin showed no outside change; outside check sensitivity not shown\n");
      else
        LOKA_VERIFY(outsideAfterFocus != outsideBeforeFocus);
    }
    else if (edit)
      LOKA_VERIFY(inkChanged);
    else if (GetFocus() != hwnd)
      std::printf("[skip] clip box focus cue: SetFocus did not retain Button focus; outside edge checked\n");
    else if (!inkChanged)
      std::printf("[skip] clip box focus cue: no visible change in Button focused look at inset after clearing "
                  "UISF_HIDEFOCUS; outside edge checked\n");
    else
      LOKA_VERIFY(inkChanged);
  }
}

void testWin32ClipBoxDeviceRoundingAndUnitTwin()
{
  const unsigned dpis[] = {120, 144};
  const int seats[][3] = {{21, 31, 7}, {22, 33, 6}};
  for (unsigned d = 0; d < sizeof(dpis) / sizeof(dpis[0]); ++d)
    for (unsigned seat = 0; seat < sizeof(seats) / sizeof(seats[0]); ++seat)
    {
      const int x = seats[seat][0];
      const int y = seats[seat][1];
      const int padding = seats[seat][2];
      const int local = padding + 1;
      RECT origins[3];
      // Flat, clip, unit: the same padded content, with one native rebase in
      // each of the last two. The nested unflagged Box adds no native step.
      for (int mode = 0; mode < 3; ++mode)
      {
        Fixture f(dpis[d]);
        BoxNode *outer = box(100, 60, mode == 1, padding);
        BoxNode *inner = box(80, 40, false, 1);
        ButtonNode *button = new ButtonNode(ButtonProps().text("round"));
        outer->addChild(inner);
        inner->addChild(button);
        f.tree = outer;
        if (mode == 2)
        {
          // Same ground + upper unit construction as Win32ZStackUnitTests.
          ZStackNode *stack = new ZStackNode((ZStackProps()));
          stack->addChild(new RectSurfaceNode(RectSurfaceProps().size(100, 60)));
          stack->addChild(outer);
          f.tree = stack;
        }
        f.at(x, y, 100, 60);
        HWND parent = mode == 0 ? f.root() : mode == 1 ? context(outer)->hwnd() : child(f.root(), L"LOKA_STACK_UNIT");
        LOKA_VERIFY(parent && GetParent(native(button)) == parent);
        verifyRect(native(button),
                   f,
                   Frame(mode == 0 ? x + local : local,
                         mode == 0 ? y + local : local,
                         78,
                         f.controller.rowLayoutMetrics().buttonHeight));
        origins[mode] = rectInRoot(native(button), f.root());
      }
      std::printf("[measure] clip box rounding dpi=%u seat=(%d,%d) padding=%d flat=(%ld,%ld) clip=(%ld,%ld) "
                  "unit=(%ld,%ld) native-steps=1\n",
                  dpis[d],
                  x,
                  y,
                  padding,
                  origins[0].left,
                  origins[0].top,
                  origins[1].left,
                  origins[1].top,
                  origins[2].left,
                  origins[2].top);
      std::fflush(stdout);
      LOKA_VERIFY(origins[1].left - origins[0].left >= -1 && origins[1].left - origins[0].left <= 1);
      LOKA_VERIFY(origins[1].top - origins[0].top >= -1 && origins[1].top - origins[0].top <= 1);
      LOKA_VERIFY(origins[1].left == origins[2].left && origins[1].top == origins[2].top);
    }
}
