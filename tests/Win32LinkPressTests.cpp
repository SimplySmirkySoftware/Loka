#include "Win32LinkPressTests.hpp"
#include "Win32InputDoor.hpp"
#include "support/TestVerify.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "core/StateTracker.hpp"
#define LOKA_STYLE_VOCAB_DEFINE_NAMES
#include "app/style/StyleVocab.hpp"
#undef LOKA_STYLE_VOCAB_DEFINE_NAMES

namespace
{
  using namespace loka::app;
  using namespace loka::app::scene;
  using namespace loka::core;
  struct PressWindows
  {
    HWND root, other;
    PressWindows()
    {
      root = CreateWindowExW(0,
                             L"STATIC",
                             L"Link press",
                             WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                             100,
                             100,
                             400,
                             200,
                             0,
                             0,
                             GetModuleHandleW(0),
                             0);
      other = CreateWindowExW(
          WS_EX_NOACTIVATE, L"STATIC", L"Cover", WS_POPUP | SS_NOTIFY, 0, 0, 200, 100, 0, 0, GetModuleHandleW(0), 0);
      LOKA_VERIFY(root && other);
    }
    ~PressWindows()
    {
      DestroyWindow(other);
      DestroyWindow(root);
    }
  };
  struct NativePress
  {
    PressWindows windows;
    Win32ScenePlatformController controller;
    PushStateTracker tracker;
    MutableState<LinkTarget> result;
    NodeState<LinkTarget> box;
    AttributedTextNode node;
    Win32AttributedTextContext *context;
    unsigned writes;
    NativePress()
        : controller(windows.root, loka::win32::Win32DisplayScale(96, RailMetrics())),
          result(LinkTarget()),
          box(&result, &tracker),
          node(AttributedText(Linked("MMMM", Bold, LinkTarget(String("x"))) + Styled("  ", Bold)
                              + Linked("MMMM", Bold, LinkTarget(String("x"))))
                   .activatedLink(box)
                   .props),
          context(0),
          writes(0)
    {
      tracker.addState(&result);
      result.bind(&changed, this, false);
      context = new Win32AttributedTextContext(&controller, windows.root, 0, 0, 240, 80, &node);
      node.setContext(context);
      LayoutState seat;
      seat.width = 240;
      context->layout(&controller, seat);
      paint();
    }
    ~NativePress()
    {
      if (GetCapture())
        LOKA_VERIFY(ReleaseCapture());
      result.unbind(&changed, this);
    }
    HWND hwnd() const
    {
      return context->paintHwnd();
    }
    void paint()
    {
      LOKA_VERIFY(InvalidateRect(hwnd(), 0, TRUE));
      LOKA_VERIFY(UpdateWindow(hwnd()));
    }
    void down(int x = 1)
    {
      SendMessageW(hwnd(), WM_LBUTTONDOWN, 0, MAKELPARAM(x, 1));
    }
    void up(int x = 1)
    {
      SendMessageW(hwnd(), WM_LBUTTONUP, 0, MAKELPARAM(x, 1));
    }
    static void changed(void *data)
    {
      NativePress &f = *static_cast<NativePress *>(data);
      LOKA_VERIFY(f.controller.borrowPhase().open());
      ++f.writes;
    }
  };
  struct MouseRetrievals
  {
    HWND link, cover;
    unsigned downs, ups, coverUps;
    /** Any button DOWN this thread retrieved, whatever window it targets. */
    unsigned threadDowns;
    MouseRetrievals(HWND c, HWND v)
        : link(c),
          cover(v),
          downs(0),
          threadDowns(0),
          ups(0),
          coverUps(0)
    {
    }
  };
  MouseRetrievals *retrievals = 0;
  LRESULT CALLBACK pressMouseHook(int code, WPARAM message, LPARAM param)
  {
    if (code == HC_ACTION && retrievals)
    {
      HWND target = reinterpret_cast<MOUSEHOOKSTRUCT *>(param)->hwnd;
      if (message == WM_LBUTTONDOWN || message == WM_NCLBUTTONDOWN)
        ++retrievals->threadDowns;
      if (target == retrievals->link && message == WM_LBUTTONDOWN)
        ++retrievals->downs;
      if (target == retrievals->link && message == WM_LBUTTONUP)
        ++retrievals->ups;
      if (target == retrievals->cover && message == WM_LBUTTONUP)
        ++retrievals->coverUps;
    }
    return CallNextHookEx(0, code, message, param);
  }
  LRESULT CALLBACK refuseInjectedMouse(int code, WPARAM message, LPARAM param)
  {
    if (code == HC_ACTION && (reinterpret_cast<MSLLHOOKSTRUCT *>(param)->flags & LLMHF_INJECTED))
      return 1;
    return CallNextHookEx(0, code, message, param);
  }
  /** LOKA_WIN32_REFUSE_INJECTED_INPUT=1 stands in for a desktop that never
      delivers injected input: a low-level hook swallows every injected mouse
      event while the pin waits, so the SendInput pins take their [skip] branch
      on any rig. Unset, it installs nothing. */
  struct MouseHook
  {
    HHOOK hook;
    HHOOK refusal;
    explicit MouseHook(MouseRetrievals &counts)
        : hook(0),
          refusal(0)
    {
      LOKA_VERIFY(!retrievals);
      retrievals = &counts;
      hook = SetWindowsHookExW(WH_MOUSE, pressMouseHook, 0, GetCurrentThreadId());
      LOKA_VERIFY(hook);
      const char *requested = std::getenv("LOKA_WIN32_REFUSE_INJECTED_INPUT");
      if (requested && std::strcmp(requested, "1") == 0)
      {
        refusal = SetWindowsHookExW(WH_MOUSE_LL, refuseInjectedMouse, GetModuleHandleW(0), 0);
        LOKA_VERIFY(refusal);
      }
    }
    ~MouseHook()
    {
      if (refusal)
        LOKA_VERIFY(UnhookWindowsHookEx(refusal));
      LOKA_VERIFY(UnhookWindowsHookEx(hook));
      retrievals = 0;
    }
  };
  void injectButton(DWORD flag)
  {
    INPUT input;
    ZeroMemory(&input, sizeof(input));
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = flag;
    LOKA_VERIFY(SendInput(1, &input, sizeof(input)) == 1);
  }
  void awaitMouse(const unsigned &count, unsigned expected, HWND capture = 0)
  {
    const DWORD start = GetTickCount();
    while (count < expected || (capture && GetCapture() != capture))
    {
      LOKA_VERIFY(GetTickCount() - start < 5000);
      MSG message;
      while (PeekMessageW(&message, 0, 0, 0, PM_REMOVE))
      {
        LOKA_VERIFY(GetTickCount() - start < 5000);
        TranslateMessage(&message);
        DispatchMessageW(&message);
      }
      Sleep(1);
    }
  }
  /** Whether the first injected DOWN reaches this thread is a desktop grant
      (input desktop, foreground, integrity level), not Loka behavior: Loka
      cannot route a DOWN the desktop never delivers. False releases the button
      and sends the pin to its [skip] branch only when no DOWN reached this
      thread at all; a DOWN retrieved for another window is a routing verdict,
      left to the caller's wait. Every later wait stays a verdict. */
  bool desktopDelivered(const char *pin, const unsigned &threadDowns, HWND root)
  {
    const DWORD start = GetTickCount();
    while (threadDowns < 1 && GetTickCount() - start < 5000)
    {
      MSG message;
      while (PeekMessageW(&message, 0, 0, 0, PM_REMOVE))
      {
        TranslateMessage(&message);
        DispatchMessageW(&message);
      }
      Sleep(1);
    }
    if (threadDowns >= 1)
      return true;
    injectButton(MOUSEEVENTF_LEFTUP);
    std::printf("[skip] %s: the desktop did not deliver the injected DOWN to this thread within 5 s "
                "(root is foreground: %s); the press checks did not run\n",
                pin,
                GetForegroundWindow() == root ? "yes" : "no");
    return false;
  }
  void aim(NativePress &f)
  {
    ShowWindow(f.windows.root, SW_SHOW);
    SetForegroundWindow(f.windows.root);
    f.paint();
    POINT point = {1, 1};
    LOKA_VERIFY(ClientToScreen(f.hwnd(), &point));
    LOKA_VERIFY(SetCursorPos(point.x, point.y));
  }
  BOOL CALLBACK dismissPressBox(HWND hwnd, LPARAM)
  {
    wchar_t name[32];
    if (GetClassNameW(hwnd, name, 32) && lstrcmpW(name, L"#32770") == 0)
      PostMessageW(hwnd, WM_CLOSE, 0, 0);
    return TRUE;
  }
  unsigned pressModalTicks = 0;
  void CALLBACK dismissPressTimer(HWND, UINT, UINT_PTR, DWORD)
  {
    LOKA_VERIFY(++pressModalTicks < 50);
    EnumThreadWindows(GetCurrentThreadId(), dismissPressBox, 0);
  }
} // namespace
void testWin32LinkPressOrdinary()
{
  NativePress f;
  f.down();
  LOKA_VERIFY(GetCapture() == f.hwnd());
  f.up();
  LOKA_VERIFY(f.writes == 1 && GetCapture() != f.hwnd());
  f.up();
  LOKA_VERIFY(f.writes == 1);
}
void testWin32LinkPressCaptureTheft()
{
  NativePress f;
  f.down();
  SetCapture(f.windows.other);
  f.up();
  LOKA_VERIFY(f.writes == 0 && GetCapture() == f.windows.other);
}
void testWin32LinkPressModal()
{
  NativePress f;
  f.down();
  pressModalTicks = 0;
  const UINT_PTR timer = SetTimer(0, 0, 100, dismissPressTimer);
  LOKA_VERIFY(timer);
  const int answer = MessageBoxW(f.windows.root, L"Timer-dismissed link press pin", L"Link press", MB_OK);
  LOKA_VERIFY(KillTimer(0, timer));
  LOKA_VERIFY(answer == IDOK);
  f.up();
  LOKA_VERIFY(f.writes == 0 && GetCapture() != f.hwnd());
}
void testWin32LinkPressCover()
{
  NativePress f;
  MouseRetrievals counts(f.hwnd(), f.windows.other);
  MouseHook hook(counts);
  aim(f);
  injectButton(MOUSEEVENTF_LEFTDOWN);
  if (!desktopDelivered("testWin32LinkPressCover", counts.threadDowns, f.windows.root))
    return;
  awaitMouse(counts.downs, 1, f.hwnd());
  RECT rect;
  LOKA_VERIFY(GetWindowRect(f.hwnd(), &rect));
  LOKA_VERIFY(SetWindowPos(f.windows.other, HWND_TOPMOST, rect.left, rect.top, 240, 80, SWP_NOACTIVATE));
  ShowWindow(f.windows.other, SW_SHOWNOACTIVATE);
  LOKA_VERIFY(GetCapture() == f.hwnd());
  injectButton(MOUSEEVENTF_LEFTUP);
  awaitMouse(counts.ups, 1);
  LOKA_VERIFY(f.writes == 1);
  // Positive routing control: without capture, a real click at the same point reaches the cover
  // (SS_NOTIFY makes the cover hit-test as its client). A lone UP with no DOWN is not delivered.
  LOKA_VERIFY(!GetCapture());
  injectButton(MOUSEEVENTF_LEFTDOWN);
  injectButton(MOUSEEVENTF_LEFTUP);
  awaitMouse(counts.coverUps, 1);
  LOKA_VERIFY(f.writes == 1 && counts.downs == 1 && counts.ups == 1);
}
void testWin32LinkPressPassThroughLabel()
{
  NativePress f;
  aim(f);
  RECT rect;
  LOKA_VERIFY(GetWindowRect(f.hwnd(), &rect));
  POINT origin = {rect.left, rect.top};
  LOKA_VERIFY(ScreenToClient(f.windows.root, &origin));
  HWND label = CreateWindowExW(0,
                               L"STATIC",
                               L"",
                               WS_CHILD | WS_VISIBLE,
                               origin.x,
                               origin.y,
                               rect.right - rect.left,
                               rect.bottom - rect.top,
                               f.windows.root,
                               0,
                               GetModuleHandleW(0),
                               0);
  LOKA_VERIFY(label);
  LOKA_VERIFY(SetWindowPos(label, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE));
  MouseRetrievals counts(f.hwnd(), label);
  MouseHook hook(counts);
  injectButton(MOUSEEVENTF_LEFTDOWN);
  if (!desktopDelivered("testWin32LinkPressPassThroughLabel", counts.threadDowns, f.windows.root))
  {
    LOKA_VERIFY(DestroyWindow(label));
    return;
  }
  awaitMouse(counts.downs, 1, f.hwnd());
  injectButton(MOUSEEVENTF_LEFTUP);
  awaitMouse(counts.ups, 1);
  LOKA_VERIFY(f.writes == 1 && counts.downs == 1 && counts.ups == 1);
  LOKA_VERIFY(DestroyWindow(label));
}

#include "support/Win32ImportRefusal.hpp"
namespace
{
  typedef HWND(WINAPI *PressSetCapture)(HWND);
  PressSetCapture nativeSetCapture = 0;
  unsigned nestedCaptureCalls = 0;
  bool nestedCaptureArmed = false;
  HWND WINAPI nestedPressCapture(HWND hwnd)
  {
    ++nestedCaptureCalls;
    const HWND previous = nativeSetCapture(hwnd);
    if (nestedCaptureArmed)
    {
      nestedCaptureArmed = false;
      SendMessageW(hwnd, WM_LBUTTONDOWN, 0, MAKELPARAM(1, 1));
      SendMessageW(hwnd, WM_LBUTTONUP, 0, MAKELPARAM(1, 1));
    }
    return previous;
  }
} // namespace
void testWin32LinkPressNested()
{
  NativePress f;
  nativeSetCapture = reinterpret_cast<PressSetCapture>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetCapture"));
  LOKA_VERIFY(nativeSetCapture);
  nestedCaptureCalls = 0;
  nestedCaptureArmed = true;
  {
    Win32ImportRefusal hook("user32.dll", "SetCapture", reinterpret_cast<ULONG_PTR>(&nestedPressCapture));
    f.down();
    LOKA_VERIFY(nestedCaptureCalls == 1 && f.writes == 0);
    f.up();
    LOKA_VERIFY(f.writes == 1);
  }
}
void testWin32LinkPressSeparatedRuns()
{
  NativePress f;
  // Locate a second link by its delivered target, then discriminate run identity
  // by pressing the first run and releasing at that same known second point.
  RECT rect;
  LOKA_VERIFY(GetClientRect(f.hwnd(), &rect));
  int second = -1;
  bool gap = false;
  for (int x = 1; x < rect.right; ++x)
  {
    const unsigned before = f.writes;
    f.down(x);
    f.up(x);
    if (f.writes == before)
      gap = true;
    else if (gap)
    {
      second = x;
      break;
    }
  }
  LOKA_VERIFY(second > 0);
  const unsigned before = f.writes;
  f.down();
  f.up(second);
  LOKA_VERIFY(f.writes == before);
}

#include "support/WindowAdmissionTestApp.hpp"
#include "platform/null/NullPlatformContext.hpp"
#include "testing/scene/SceneTestFlow.hpp"
#include "app/nodes/boundary/StdComposition.hpp"
#include "app/nodes/nestable/Show.hpp"
namespace
{
  struct PressLifetimeFacts
  {
    WindowAdmissionTestApp *app;
    Win32Window *window;
    HWND child;
    int mode;
    unsigned writes;
    PressLifetimeFacts(int m)
        : app(0),
          window(0),
          child(0),
          mode(m),
          writes(0)
    {
    }
  };
  class PressRoot;
  struct PressRootProps : NodePropsBase<PressRootProps>
  {
    typedef PressRoot NodeType;
    struct TypeTag
    {
    };
    PressLifetimeFacts *facts;
    explicit PressRootProps(PressLifetimeFacts *f = 0)
        : facts(f)
    {
    }
    bool operator<(const PropsBase &rhs) const
    {
      return facts < static_cast<const PressRootProps &>(rhs).facts;
    }
  };
  class PressRoot : public StdCompositionBoundaryNodeBase<PressRootProps>
  {
  public:
    NodeState<bool> shown;
    NodeState<LinkTarget> activated;
    NodeState<AttributedString> text;
    explicit PressRoot(const PressRootProps &props)
        : StdCompositionBoundaryNodeBase<PressRootProps>(props)
    {
      this->state(this->shown, true);
      this->state(this->activated, LinkTarget());
      this->state(this->text, Linked("MMMM", Bold, LinkTarget(String("x"))));
    }
    virtual void declareBindings(BindingToken &token)
    {
      token.watch(*this->activated.state(), this, &PressRoot::changed);
    }
    virtual void composeNode(NodeComposition &composition)
    {
      composition.declare(Show(*this->shown.state()).destroyOnDetach()
                          << AttributedText(this->text.state()).activatedLink(this->activated));
    }
    void changed()
    {
      PressLifetimeFacts &f = *this->props.facts;
      if (!f.app)
        return;
      ++f.writes;
      if (f.mode == 0)
        this->shown.set(false);
      if (f.mode == 1)
        f.app->requestWindowClose(f.window);
      if (f.mode == 2)
        this->text.set(Linked("NNNN", Bold, LinkTarget(String("y"))));
      f.app->flush();
      LOKA_VERIFY(IsWindow(f.child) && IsWindowVisible(f.child));
    }
  };
  Node *pressFind(Node *node)
  {
    if (!node)
      return 0;
    if (node->asAttributedTextNode())
      return node;
    INestable *children = node->asNestable();
    for (Node *child = children ? children->childrenHead() : 0; child; child = child->nextInComposition)
      if (Node *found = pressFind(child))
        return found;
    return 0;
  }
  void pressLifetimeOnWindow(PressLifetimeFacts &facts, Win32Window &window)
  {
    const int mode = facts.mode;
    WindowAdmissionTestApp app(window);
    app.flush();
    Node *node = pressFind(loka::dsl::testing::SceneTestAccess::rootBoundary(*window.scene()));
    LOKA_VERIFY(node && node->getContext());
    Win32AttributedTextContext *context = static_cast<Win32AttributedTextContext *>(node->getContext());
    facts.child = context->paintHwnd();
    facts.window = &window;
    facts.app = &app;
    LOKA_VERIFY(InvalidateRect(facts.child, 0, TRUE));
    LOKA_VERIFY(UpdateWindow(facts.child));
    SendMessageW(facts.child, WM_LBUTTONDOWN, 0, MAKELPARAM(1, 1));
    SendMessageW(facts.child, WM_LBUTTONUP, 0, MAKELPARAM(1, 1));
    LOKA_VERIFY(facts.writes == 1 && IsWindow(facts.child) && IsWindowVisible(facts.child));
    app.flush();
    if (mode < 2)
      LOKA_VERIFY(!IsWindow(facts.child) || !IsWindowVisible(facts.child));
    else
      LOKA_VERIFY(node->asAttributedTextNode()->props.text_->get() == Linked("NNNN", Bold, LinkTarget(String("y"))));
  }
  void pressLifetime(int mode)
  {
    PressLifetimeFacts facts(mode);
    NullPlatformContext platform;
    const WindowProps props = WindowProps()
                                  .frame(100, 100, 400, 200)
                                  .visible(true)
                                  .scene(new Scene(Boundary<PressRoot>(PressRootProps(&facts))));
    if (mode == 1)
    {
      // The close queue takes ownership and deletes this window at completion.
      Win32Window *window = new Win32Window(&platform, props);
      pressLifetimeOnWindow(facts, *window);
    }
    else
    {
      Win32Window window(&platform, props);
      pressLifetimeOnWindow(facts, window);
    }
  }

} // namespace
void testWin32LinkPressHideDeferred()
{
  pressLifetime(0);
}
void testWin32LinkPressCloseDeferred()
{
  pressLifetime(1);
}
void testWin32LinkPressObserverProps()
{
  pressLifetime(2);
}

namespace
{
  typedef int(WINAPI *ExposureClipFn)(HDC, LPRECT);
  ExposureClipFn exposureNativeClip = 0;
  /** Stack-owned observation of the production drawer's actual window DC. */
  struct ExposurePaints
  {
    HWND window;
    unsigned complete, partial;
    explicit ExposurePaints(HWND hwnd)
        : window(hwnd),
          complete(0),
          partial(0)
    {
    }
    void observe(HDC dc, int kind, const RECT &clip)
    {
      if (WindowFromDC(dc) != this->window)
        return;
      LOKA_VERIFY(kind != ERROR);
      RECT seat;
      LOKA_VERIFY(GetClientRect(this->window, &seat));
      if (kind == SIMPLEREGION && EqualRect(&clip, &seat))
        ++this->complete;
      else
        ++this->partial;
    }
  };
  ExposurePaints *exposurePaints = 0;
  int WINAPI exposureClip(HDC dc, LPRECT clip)
  {
    const int kind = exposureNativeClip(dc, clip);
    exposurePaints->observe(dc, kind, *clip);
    return kind;
  }
  class ExposureObservation
  {
    Win32ImportRefusal hook_;

  public:
    explicit ExposureObservation(ExposurePaints &paints)
        : hook_("gdi32.dll", "GetClipBox", reinterpret_cast<ULONG_PTR>(&exposureClip))
    {
      LOKA_VERIFY(!exposurePaints);
      exposurePaints = &paints;
    }
    ~ExposureObservation()
    {
      exposurePaints = 0;
    }

  private:
    ExposureObservation(const ExposureObservation &);
    ExposureObservation &operator=(const ExposureObservation &);
  };
} // namespace
void testWin32LinkPressPartialExposure()
{
  NativePress f;
  exposureNativeClip = reinterpret_cast<ExposureClipFn>(GetProcAddress(GetModuleHandleW(L"gdi32.dll"), "GetClipBox"));
  LOKA_VERIFY(exposureNativeClip);
  ExposurePaints paints(f.hwnd());
  ExposureObservation observation(paints);
  aim(f);
  // Settle activation/exposure before establishing the complete-paint control.
  const DWORD start = GetTickCount();
  MSG message;
  while (PeekMessageW(&message, 0, 0, 0, PM_REMOVE))
  {
    LOKA_VERIFY(GetTickCount() - start < 5000);
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
  const unsigned before = paints.complete;
  f.paint();
  LOKA_VERIFY(paints.complete > before);
  const unsigned complete = paints.complete, partial = paints.partial;
  RECT exposed;
  LOKA_VERIFY(GetClientRect(f.hwnd(), &exposed));
  LOKA_VERIFY(exposed.right > 2 && exposed.bottom > 0);
  exposed.right /= 2;
  LOKA_VERIFY(InvalidateRect(f.hwnd(), &exposed, TRUE));
  LOKA_VERIFY(UpdateWindow(f.hwnd()));
  LOKA_VERIFY(paints.partial > partial && paints.complete == complete);
  MouseRetrievals counts(f.hwnd(), f.windows.other);
  MouseHook mouse(counts);
  injectButton(MOUSEEVENTF_LEFTDOWN);
  if (!desktopDelivered("testWin32LinkPressPartialExposure", counts.threadDowns, f.windows.root))
    return;
  awaitMouse(counts.downs, 1, f.hwnd());
  LOKA_VERIFY(paints.complete == complete);
  injectButton(MOUSEEVENTF_LEFTUP);
  awaitMouse(counts.ups, 1);
  LOKA_VERIFY(paints.complete == complete && f.writes == 1 && counts.downs == 1 && counts.ups == 1);
}
