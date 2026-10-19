#include "Win32FocusTests.hpp"
#include "Win32App.hpp"
#include "Win32Window.hpp"
#include "Win32ScenePlatformController.hpp"
#include "context/Win32ButtonContext.hpp"
#include "context/Win32EditTextContext.hpp"
#include "context/Win32TextEditorContext.hpp"
#include "context/Win32FocusParticipant.hpp"
#include "app/nodes/controls/EditText.hpp"
#include "app/nodes/controls/Button.hpp"
#include "app/nodes/controls/TextEditor.hpp"
#include "app/nodes/nestable/RowColumn.hpp"
#include "app/nodes/nestable/Show.hpp"
#include "app/nodes/boundary/StdComposition.hpp"
#include "platform/null/NullPlatformContext.hpp"
#include "support/Headless.hpp"
#include "support/LifecycleFactTestAccess.hpp"
#include "support/TestVerify.hpp"
#include "support/WindowAdmissionTestApp.hpp"
#include "testing/scene/SceneTestFlow.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>

namespace
{
  using namespace loka::app;
  using namespace loka::app::scene;
  using namespace loka::core;
  typedef Focused<unsigned short> Fact;
  struct Facts : HeadlessStateOwner
  {
    Focus<unsigned short> focus;
    NodeState<String> text;
    Reported<LineCursor> cursor;
    ObservableList<String> lines;
    Facts()
    {
      StateBatchBase::CreateImmediateState(this, this->cursor, LineCursor::None());
      // TextEditor borrows a non-null app-owned list (TextEditor.hpp).
      LOKA_VERIFY(this->lines.attach(this->tracker()->asPushTracker(), 4) == ATTACH_OK);
      LOKA_VERIFY(this->lines.insert(0, String("focus")) == EDIT_OK);
    }
  };
  Facts *composingFacts = 0;
  unsigned short composingInitialFocus = 0;
  class FocusRoot : public BoundaryNodeFor<FocusRoot>
  {
  public:
    explicit FocusRoot(const BoundaryPropsFor<FocusRoot> &p) : BoundaryNodeFor<FocusRoot>(p)
    {
      if (composingInitialFocus)
        this->state(composingFacts->focus, composingInitialFocus);
      else
        this->state(composingFacts->focus);
      this->state(composingFacts->text, String("old text"));
    }
    virtual void composeNode(NodeComposition &c)
    {
      // A top-level declare() sets the composition root, so the four controls
      // are one Column; its children are the tab order field(0..3) walks.
      c.declare(Column()
                << Button()
                << EditText(EditTextProps(composingFacts->text).focusedAs(composingFacts->focus, static_cast<unsigned short>(1)))
                << EditText(EditTextProps().focusedAs(composingFacts->focus, static_cast<unsigned short>(2)))
                << TextEditor(TextEditorProps(composingFacts->lines, composingFacts->cursor)
                                  .focusedAs(composingFacts->focus, static_cast<unsigned short>(3))));
    }
  };

  /** LOKA_WIN32_REFUSE_ACTIVATION=1 stands in for a desktop that never makes
      the test window active: a thread CBT hook refuses every HCBT_ACTIVATE
      for the fixture's lifetime, so the pins take their [skip] branch on any
      rig. Unset, it installs nothing. */
  class ActivationRefusal
  {
  public:
    ActivationRefusal() : hook_(0)
    {
      const char *requested = std::getenv("LOKA_WIN32_REFUSE_ACTIVATION");
      if (requested && std::strcmp(requested, "1") == 0)
      {
        this->hook_ = SetWindowsHookExW(WH_CBT, &ActivationRefusal::refuse, 0, GetCurrentThreadId());
        LOKA_VERIFY(this->hook_);
      }
    }
    ~ActivationRefusal()
    {
      if (this->hook_)
        UnhookWindowsHookEx(this->hook_);
    }

  private:
    static LRESULT CALLBACK refuse(int code, WPARAM wParam, LPARAM lParam)
    {
      return code == HCBT_ACTIVATE ? 1 : CallNextHookEx(0, code, wParam, lParam);
    }
    HHOOK hook_;
    ActivationRefusal(const ActivationRefusal &);
    ActivationRefusal &operator=(const ActivationRefusal &);
  };

  struct Fixture
  {
    ActivationRefusal refusal; // Precedes window: its first show is refused too.
    Facts facts;
    NullPlatformContext context;
    Win32Window window;
    WindowAdmissionTestApp app;
    static WindowProps props(Facts &facts, bool idle, unsigned short initialFocus)
    {
      composingFacts = &facts;
      composingInitialFocus = initialFocus;
      WindowProps p;
      p.frame(60, 60, 400, 300).visible(true).scene(new Scene(Boundary<FocusRoot>()));
      if (idle)
        p.idlePolicy(IdlePolicy::everyTick());
      return p;
    }
    explicit Fixture(bool idle = false, unsigned short initialFocus = 0)
        : window(&this->context, props(this->facts, idle, initialFocus)), app(this->window)
    {
      this->app.flush();
      LOKA_VERIFY(this->window.hwnd());
    }
    /** Activation is a desktop grant, not Loka behavior. False sends a pin to
        its [skip] branch, where only checks that need no active window run. */
    bool activate()
    {
      SetActiveWindow(this->window.hwnd());
      SetFocus(this->window.hwnd());
      return GetActiveWindow() == this->window.hwnd();
    }
    Node *field(unsigned index)
    {
      BoundaryNode *root = loka::dsl::testing::SceneTestAccess::rootBoundary(*this->window.scene());
      LOKA_VERIFY(root && root->childrenHead() && root->childrenHead()->asNestable());
      Node *node = root->childrenHead()->asNestable()->childrenHead();
      while (index-- && node)
        node = node->nextInComposition;
      LOKA_VERIFY(node);
      return node;
    }
    HWND edit(unsigned index)
    {
      NodeContext *context = this->field(index)->getContext();
      LOKA_VERIFY(context);
      return index == 3 ? static_cast<Win32TextEditorContext *>(context)->hwnd()
                        : static_cast<Win32EditTextContext *>(context)->hwnd();
    }
    Win32ScenePlatformController &rail()
    {
      return *static_cast<Win32ScenePlatformController *>(
          loka::dsl::testing::SceneTestAccess::platformController(*this->window.scene()));
    }
    /** Active: the rail answers and names no participant. Inactive: it
        declines. The preset answer is non-null so a stale out-value shows. */
    void readsNoParticipant(bool active)
    {
      NodeContext *answer = this->field(1)->getContext();
      if (active)
        LOKA_VERIFY(this->rail().readNativeFocus(answer) && !answer);
      else
        LOKA_VERIFY(!this->rail().readNativeFocus(answer));
    }
    void none() { LOKA_VERIFY(!(this->facts.focus.state()->get() != Fact::none())); }
    void expect(unsigned short key) { LOKA_VERIFY(this->facts.focus.state()->get().is(key)); }
  };

  void skipWithCondition(const char *pin, const char *condition, const char *skipped, const char *kept)
  {
    std::printf("[skip] %s: %s, so %s did not run; %s\n", pin, condition, skipped, kept);
    std::fflush(stdout);
  }

  void skipWithoutActivation(const char *pin, const char *skipped, const char *kept)
  {
    skipWithCondition(pin, "SetActiveWindow did not make the test window this thread's active window", skipped, kept);
  }

  void tab(HWND root)
  {
    MSG msg = {};
    msg.hwnd = GetFocus();
    msg.message = WM_KEYDOWN;
    msg.wParam = VK_TAB;
    LOKA_VERIFY(IsDialogMessageW(root, &msg));
  }

  /** Needs no activation: each participant HWND bears its own context's mark
      from attach, and the Button, which has GWLP_USERDATA, bears none. */
  void verifyMarks(Fixture &f)
  {
    NodeContext *button = f.field(0)->getContext();
    LOKA_VERIFY(button);
    LOKA_VERIFY(!Win32FocusParticipant::read(static_cast<Win32ButtonContext *>(button)->hwnd()));
    for (unsigned key = 1; key <= 3; ++key)
      LOKA_VERIFY(Win32FocusParticipant::read(f.edit(key)) == f.field(key)->getContext());
  }

  /** Detach removes the mark and hides, reattach restores the mark, and
      retirement removes it before the deferred native destruction; none of
      that needs activation. An active window also reads and publishes at
      each step; an inactive one declines every read. */
  void verifyLifecycle(Fixture &f, bool active)
  {
    for (unsigned short key = 1; key <= 3; ++key)
    {
      Node *node = f.field(key);
      const HWND retired = f.edit(key);
      if (active)
      {
        SetFocus(retired);
        f.app.reconcileFocus();
        f.expect(key);
      }
      NotifySubtreeNodeDetached(node);
      LifecycleFactTestAccess::DeliverFacts(node);
      LOKA_VERIFY(!Win32FocusParticipant::read(retired));
      LOKA_VERIFY(!IsWindowVisible(retired));
      if (active)
        SetFocus(retired); // A hidden HWND must not provide a read source either.
      f.readsNoParticipant(active);
      f.app.reconcileFocus();
      f.none();
      NotifySubtreeNodeAttached(node);
      LifecycleFactTestAccess::DeliverFacts(node);
      LOKA_VERIFY(Win32FocusParticipant::read(retired) == node->getContext());
      if (active)
      {
        SetFocus(retired);
        f.app.reconcileFocus();
        f.expect(key);
      }
      LifecycleFactTestAccess::MarkSubtreeRetired(node);
      LifecycleFactTestAccess::DeliverFacts(node);
      LOKA_VERIFY(IsWindow(retired)); // Native destruction is deferred.
      LOKA_VERIFY(!Win32FocusParticipant::read(retired));
      f.readsNoParticipant(active);
      f.none();
    }
  }
}

void testWin32FocusReadAndRestore()
{
  Fixture f;
  verifyMarks(f);
  if (!f.activate())
  {
    skipWithoutActivation("testWin32FocusReadAndRestore",
                          "the Tab, publication, HELD and reactivation checks",
                          "the participant marks, the inactive read and the lifecycle marks still run.");
    f.readsNoParticipant(false);
    f.app.reconcileFocus();
    f.none(); // A declined read publishes nothing.
    verifyLifecycle(f, false);
    return;
  }
  LOKA_VERIFY(GetFocus() == f.window.hwnd());
  f.readsNoParticipant(true);
  f.app.reconcileFocus();
  f.none();
  // Negative control: even a marked root is not a descendant participant.
  LOKA_VERIFY(Win32FocusParticipant::attach(f.window.hwnd(), f.field(1)->getContext()));
  f.readsNoParticipant(true);
  Win32FocusParticipant::detach(f.window.hwnd());
  tab(f.window.hwnd());
  LOKA_VERIFY(GetFocus() != f.window.hwnd());
  LOKA_VERIFY(!Win32FocusParticipant::read(GetFocus()));
  f.app.reconcileFocus();
  f.none(); // Button has userdata, but no participant atom.
  NodeContext *answer = 0;
  for (unsigned short key = 1; key <= 3; ++key)
  {
    tab(f.window.hwnd());
    LOKA_VERIFY(GetFocus() == f.edit(key));
    LOKA_VERIFY(f.rail().readNativeFocus(answer) && answer == f.field(key)->getContext());
    f.app.reconcileFocus();
    f.expect(key);
  }

  // A second real top-level window exercises HELD and native activation restore.
  Win32Window other(&f.context, WindowProps().frame(500, 60, 160, 120).visible(true));
  WindowAdmissionTestApp otherApp(other);
  otherApp.flush();
  for (unsigned short key = 1; key <= 3; ++key)
  {
    SetActiveWindow(f.window.hwnd());
    SetFocus(f.edit(key));
    f.app.reconcileFocus();
    f.expect(key);
    SetActiveWindow(other.hwnd());
    SetFocus(other.hwnd());
    LOKA_VERIFY(GetActiveWindow() == other.hwnd());
    f.readsNoParticipant(false);
    f.app.reconcileFocus();
    f.expect(key);
    SetActiveWindow(f.window.hwnd());
    LOKA_VERIFY(GetFocus() == f.edit(key));
    f.app.reconcileFocus();
    f.expect(key);
  }
  // Reactivation restores only an HWND that still bears the published
  // context's mark; the fact itself stays HELD until the next read.
  const HWND editor = f.edit(3);
  Win32FocusParticipant::detach(editor);
  SetActiveWindow(other.hwnd());
  SetFocus(other.hwnd());
  SetActiveWindow(f.window.hwnd());
  LOKA_VERIFY(GetActiveWindow() == f.window.hwnd());
  LOKA_VERIFY(GetFocus() != editor);
  f.expect(3);
  LOKA_VERIFY(Win32FocusParticipant::attach(editor, f.field(3)->getContext()));
  // The WM_ACTIVATE minimized bit is the minimized-skip guard. A synthetic
  // message isolates it from OS focus policy.
  SetFocus(f.window.hwnd());
  SendMessageW(f.window.hwnd(), WM_ACTIVATE, MAKEWPARAM(WA_ACTIVE, TRUE), 0);
  LOKA_VERIFY(GetFocus() != f.edit(3));
  // Deactivation never restores. A real WA_INACTIVE arrives while this window
  // is still the active one, so a restore there would take focus until the
  // new active window overrides it; the synthetic message keeps it visible.
  SendMessageW(f.window.hwnd(), WM_ACTIVATE, MAKEWPARAM(WA_INACTIVE, FALSE), 0);
  LOKA_VERIFY(GetFocus() != f.edit(3));
  ShowWindow(f.window.hwnd(), SW_MINIMIZE);
  LOKA_VERIFY(IsIconic(f.window.hwnd()));
  SetActiveWindow(other.hwnd());
  SetFocus(other.hwnd());
  // Windows itself refuses focus into a minimized window's child, so no
  // separate IsIconic guard exists: this pins the outcome on a real iconic window.
  SendMessageW(f.window.hwnd(), WM_ACTIVATE, MAKEWPARAM(WA_CLICKACTIVE, FALSE), 0);
  LOKA_VERIFY(GetFocus() != f.edit(3));
  f.expect(3);
  ShowWindow(f.window.hwnd(), SW_RESTORE);
  SetActiveWindow(f.window.hwnd());

  verifyLifecycle(f, true);
}

namespace
{
  class CompletionApp : public Win32App
  {
  public:
    explicit CompletionApp(Window &window) : Win32App(0, GetModuleHandleW(0), SW_SHOW)
    {
      this->group_ = new AppComponentGroup(std::vector<AppComponent *>(1, &window));
      this->setActiveWindow(&window);
    }
    ~CompletionApp() { this->group_->build(); }
  };
  struct CompletionProbe
  {
    Fixture &fixture;
    unsigned step;
    const DWORD started;
    explicit CompletionProbe(Fixture &f)
        : fixture(f),
          step(0),
          started(GetTickCount())
    {
    }
    void tick()
    {
      if (this->step < 2)
        this->fixture.none();
      else if (!this->fixture.facts.focus.state()->get().is(static_cast<unsigned short>(this->step - 1)))
      {
        // A Tab's publishing completion runs after its message drain; a drain
        // slower than the timer period can dispatch the next tick first (#1378).
        // The fixture's window is this pin's only window, so losing activation
        // means another thread took it; the rail then declines every read.
        if (GetActiveWindow() != this->fixture.window.hwnd())
        {
          PostQuitMessage(0);
          return;
        }
        LOKA_VERIFY(GetTickCount() - this->started < 2000);
        return;
      }
      if (this->step == 4)
      {
        PostQuitMessage(0);
        return;
      }
      ++this->step;
      LOKA_VERIFY(PostMessageW(GetFocus(), WM_KEYDOWN, VK_TAB, 0));
    }
  };
  CompletionProbe *completionProbe = 0;
  void CALLBACK completionTimer(HWND, UINT, UINT_PTR, DWORD)
  {
    completionProbe->tick();
  }
}

void testWin32FocusCompletion()
{
  for (unsigned mode = 0; mode != 2; ++mode)
  {
    Fixture f(mode != 0);
    if (!f.activate())
    {
      skipWithoutActivation("testWin32FocusCompletion",
                            "the message-loop Tab and completion checks",
                            "testWin32FocusReadAndRestore keeps the checks that need no active window.");
      return;
    }
    CompletionApp app(f.window);
    CompletionProbe probe(f);
    completionProbe = &probe;
    const UINT_PTR timer = SetTimer(0, 0, 40, &completionTimer);
    LOKA_VERIFY(timer);
    app.run(); // Real PeekMessage/IsDialogMessage/admission/completion/WaitMessage.
    KillTimer(0, timer);
    completionProbe = 0;
    f.window.setApp(0);
    // Skip only when another thread holds the foreground; a window that lost
    // activation while this thread kept the foreground stays a red.
    const HWND foreground = GetForegroundWindow();
    if (!f.facts.focus.state()->get().is(3) && GetActiveWindow() != f.window.hwnd() && foreground
        && GetWindowThreadProcessId(foreground, 0) != GetCurrentThreadId())
    {
      skipWithCondition("testWin32FocusCompletion",
                        "the test window lost activation to another thread during the message loop",
                        "the remaining completion checks of this mode and any next mode",
                        "initial activation and preceding loop checks ran; testWin32FocusReadAndRestore keeps the "
                        "checks that need no active window.");
      return;
    }
    LOKA_VERIFY(probe.step == 4);
    f.expect(3);
  }
}


namespace
{
  class CompletionTailRoot : public BoundaryNodeFor<CompletionTailRoot>
  {
  public:
    Focus<unsigned short> focus;
    NodeState<bool> shown;
    Reported<LineCursor> cursor;
    RequestWithReply<LineCursor> request;
    ObservableList<String> lines;

    explicit CompletionTailRoot(const BoundaryPropsFor<CompletionTailRoot> &p)
        : BoundaryNodeFor<CompletionTailRoot>(p)
    {
      this->state(this->focus);
      this->state(this->shown, true);
      this->state(this->cursor, LineCursor::None());
      this->state(this->request, LineCursor::None());
    }
    virtual void attachNode(NodeComposition &)
    {
      StateTracker *owner = 0;
      if (this->lines.queryMutationTracker(owner) == EDIT_OK)
        return;
      LOKA_VERIFY(this->lines.attach(this->tracker()->asPushTracker(), 4) == ATTACH_OK);
      LOKA_VERIFY(this->lines.insert(0, String("focus tail")) == EDIT_OK);
    }
    virtual void composeNode(NodeComposition &c)
    {
      c.declare(Show(*this->shown.state()).destroyOnDetach()
                << TextEditor(TextEditorProps(this->lines, this->cursor).moveCaretTo(this->request)
                                  .focusedAs(this->focus, static_cast<unsigned short>(1))));
    }
  };

  TextEditorNode *completionTailEditor(Node *node)
  {
    if (node->nodeTypeKey() == NodeTypeToken<TextEditorNode>())
      return static_cast<TextEditorNode *>(node);
    INestable *children = node->asNestable();
    for (Node *child = children ? children->childrenHead() : 0; child; child = child->nextInComposition)
    {
      TextEditorNode *found = completionTailEditor(child);
      if (found) return found;
    }
    return 0;
  }

  class CompletionTailProbe
  {
  public:
    explicit CompletionTailProbe(Scene &scene)
        : scene_(scene), root_(*static_cast<CompletionTailRoot *>(
              loka::dsl::testing::SceneTestAccess::rootBoundary(scene))), replies_(0), fallbackFired_(false)
    {
      this->root_.cursor.state()->bind(&moved, this, false);
      this->root_.request.reply().state()->bind(&replied, this, false);
    }
    ~CompletionTailProbe()
    {
      this->root_.request.reply().state()->unbind(&replied, this);
      this->root_.cursor.state()->unbind(&moved, this);
    }
    void timeout()
    {
      this->fallbackFired_ = true;
      PostQuitMessage(0);
    }
    void verify()
    {
      LOKA_VERIFY(!this->fallbackFired_);
      LOKA_VERIFY(this->replies_ == 1);
      // PR C1: close commits the Seat after Win32's second admission, so the
      // editor is still present and its removal is pending after this tail.
      LOKA_VERIFY(completionTailEditor(&this->root_) != 0);
      LOKA_VERIFY(this->scene_.hasPendingInvalidation());
      LOKA_VERIFY(!this->scene_.isBorrowOpen());
      LOKA_VERIFY(!this->scene_.focus().isPublishing());
    }

  private:
    static void moved(void *data)
    {
      CompletionTailProbe &self = *static_cast<CompletionTailProbe *>(data);
      const ItemId line = self.root_.lines.at(0).id;
      if (self.root_.cursor.state()->get() != LineCursor(line, 1))
        return;
      LOKA_VERIFY(self.scene_.isBorrowOpen());
      LOKA_VERIFY(self.scene_.focus().isPublishing());
      self.root_.request.set(LineCursor(line, 3));
    }
    static void replied(void *data)
    {
      CompletionTailProbe &self = *static_cast<CompletionTailProbe *>(data);
      ++self.replies_;
      const Reply<LineCursor> reply = self.root_.request.reply().state()->get();
      LOKA_VERIFY(reply.kind() == Reply<LineCursor>::GRANTED);
      LOKA_VERIFY(reply.applied() == LineCursor(self.root_.lines.at(0).id, 3));
      LOKA_VERIFY(self.scene_.isBorrowOpen());
      LOKA_VERIFY(self.scene_.focus().isPublishing());
      self.root_.shown.set(false);
      LOKA_VERIFY(completionTailEditor(&self.root_));
      // Quit here: the next PeekMessage must exit before another iteration
      // can hide a missing second flush with its first flush.
      PostQuitMessage(0);
    }
    Scene &scene_;
    CompletionTailRoot &root_;
    unsigned replies_;
    bool fallbackFired_;
  };

  WNDPROC completionTailPrevious = 0;
  LRESULT CALLBACK completionTailFocusInput(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
  {
    const WNDPROC previous = completionTailPrevious;
    if (message != WM_SETFOCUS)
      return CallWindowProcW(previous, hwnd, message, wParam, lParam);
    // One-shot native input inside SetFocus, before the focus fact's tracker
    // transaction. Restore the context's procedure before input can retire it.
    SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(previous));
    const LRESULT result = CallWindowProcW(previous, hwnd, message, wParam, lParam);
    SendMessageW(hwnd, EM_SETSEL, 0, 0);
    SendMessageW(hwnd, WM_KEYDOWN, VK_RIGHT, 0);
    return result;
  }

  CompletionTailProbe *completionTailProbe = 0;
  void CALLBACK completionTailTimer(HWND, UINT, UINT_PTR, DWORD)
  {
    completionTailProbe->timeout();
  }
}

// The real Win32 pump is a clocked turn since #1073; the no-clock baseline for
// this completion ordering is the Null pin testFocusSeatCompletionProjectsNextTurn.
static void runWin32FocusCompletionTail()
{
  ActivationRefusal refusal;
  NullPlatformContext context;
  Win32Window window(&context, WindowProps().frame(60, 60, 400, 300).visible(true)
                                   .scene(new Scene(Boundary<CompletionTailRoot>())));
  WindowAdmissionTestApp admission(window);
  admission.flush();
  LOKA_VERIFY(window.hwnd());
  Scene &scene = *window.scene();
  CompletionTailRoot &root = *static_cast<CompletionTailRoot *>(
      loka::dsl::testing::SceneTestAccess::rootBoundary(scene));
  LOKA_VERIFY(completionTailEditor(&root) && completionTailEditor(&root)->getContext());
  SetActiveWindow(window.hwnd());
  SetFocus(window.hwnd());
  if (GetActiveWindow() != window.hwnd())
  {
    skipWithoutActivation("testWin32FocusCompletionTailClock",
                          "the focus completion and admission-timing checks",
                          "the initial editor attachment was verified.");
    return;
  }
  CompletionApp app(window);
  CompletionTailProbe probe(scene);
  completionTailProbe = &probe;
  const UINT_PTR timer = SetTimer(0, 0, 2000, &completionTailTimer);
  LOKA_VERIFY(timer);
  const HWND editor = static_cast<Win32TextEditorContext *>(completionTailEditor(&root)->getContext())->hwnd();
  completionTailPrevious = reinterpret_cast<WNDPROC>(
      SetWindowLongPtrW(editor, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&completionTailFocusInput)));
  LOKA_VERIFY(completionTailPrevious);
  root.focus.post(static_cast<unsigned short>(1));
  // Ensure this iteration handled a message. The late-quit mutation then
  // reaches another tail before waiting for the timer, even on a quiet queue.
  LOKA_VERIFY(PostMessageW(window.hwnd(), WM_NULL, 0, 0));
  app.run();
  KillTimer(0, timer);
  completionTailProbe = 0;
  // The fallback may quit before WM_SETFOCUS consumed the one-shot hook.
  if (IsWindow(editor))
    SetWindowLongPtrW(editor, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(completionTailPrevious));
  completionTailPrevious = 0;
  window.setApp(0);
  probe.verify();
  // PR C1: the Seat write made by focus completion settled at close, after the
  // tail's second admission; the next eligible tail removes the editor.
  admission.operationLoop();
  LOKA_VERIFY(!completionTailEditor(&root));
  LOKA_VERIFY(!scene.hasPendingInvalidation());
}


void testWin32FocusCompletionTailClock() { runWin32FocusCompletionTail(); }

void testWin32FocusPostedRequest()
{
  Fixture f;
  if (!f.activate())
  {
    skipWithoutActivation("testWin32FocusPostedRequest", "posted focus and preservation checks",
                          "the existing lifecycle mark checks remain independent of activation.");
    return;
  }
  SetFocus(f.edit(1));
  f.app.reconcileFocus();
  f.expect(1);
  LOKA_VERIFY(SetWindowTextW(f.edit(2), L"target text"));
  for (unsigned short key = 2; key <= 3; ++key)
  {
    const HWND target = f.edit(key);
    SendMessageW(target, EM_SETSEL, 1, 3);
    wchar_t before[32] = {0};
    LOKA_VERIFY(GetWindowTextW(target, before, 32) > 0);
    const LRESULT selection = SendMessageW(target, EM_GETSEL, 0, 0);
    const LRESULT line = SendMessageW(target, EM_GETFIRSTVISIBLELINE, 0, 0);
    const int horizontal = GetScrollPos(target, SB_HORZ);
    f.facts.focus.post(key);
    f.app.reconcileFocus();
    LOKA_VERIFY(GetFocus() == target);
    f.expect(key);
    LOKA_VERIFY(GetActiveWindow() == f.window.hwnd());
    wchar_t after[32] = {0};
    LOKA_VERIFY(GetWindowTextW(target, after, 32) > 0);
    LOKA_VERIFY(std::wcscmp(before, after) == 0);
    LOKA_VERIFY(SendMessageW(target, EM_GETSEL, 0, 0) == selection);
    LOKA_VERIFY(SendMessageW(target, EM_GETFIRSTVISIBLELINE, 0, 0) == line);
    LOKA_VERIFY(GetScrollPos(target, SB_HORZ) == horizontal);
  }
  // A successful SetFocus can return zero: there need not be a previous HWND.
  SetFocus(0);
  LOKA_VERIFY(!GetFocus());
  LOKA_VERIFY(f.rail().applyNativeFocus(*f.field(2)->getContext()));
  LOKA_VERIFY(GetFocus() == f.edit(2));
}

void testWin32FocusInactiveRequest()
{
  Fixture f;
  if (!f.activate())
  {
    skipWithoutActivation("testWin32FocusInactiveRequest", "inactive write and request retention checks",
                          "the existing inactive read pin still runs.");
    return;
  }
  SetFocus(f.edit(1));
  f.app.reconcileFocus();
  f.facts.focus.post(2);
  const FocusBinding request = EditTextProps().focusedAs(f.facts.focus, static_cast<unsigned short>(2)).focus_;
  Win32Window other(&f.context, WindowProps().frame(500, 60, 160, 120).visible(true));
  WindowAdmissionTestApp otherApp(other);
  otherApp.flush();
  SetActiveWindow(other.hwnd());
  SetFocus(other.hwnd());
  LOKA_VERIFY(GetActiveWindow() == other.hwnd());
  const HWND previous = GetFocus();
  // Exercise the door separately: common admission would otherwise mask its guard.
  LOKA_VERIFY(!f.rail().applyNativeFocus(*f.field(2)->getContext()));
  LOKA_VERIFY(GetFocus() == previous && GetActiveWindow() == other.hwnd());
  f.app.reconcileFocus();
  LOKA_VERIFY(request.requested());
  f.expect(1);
  SetActiveWindow(f.window.hwnd());
  LOKA_VERIFY(GetFocus() == f.edit(1));
  f.app.reconcileFocus();
  LOKA_VERIFY(!request.requested());
  LOKA_VERIFY(GetFocus() == f.edit(2));
  f.expect(2);
}

void testWin32FocusWriteAdmission()
{
  Fixture f;
  Fixture other;
  if (!f.activate())
  {
    skipWithoutActivation("testWin32FocusWriteAdmission", "mark, disabled and foreign-controller refusal checks",
                          "the existing lifecycle mark checks still run.");
    return;
  }
  SetFocus(f.edit(1));
  const HWND previous = GetFocus();
  const HWND target = f.edit(2);
  NodeContext *const targetContext = f.field(2)->getContext();
  Win32FocusParticipant::detach(target);
  LOKA_VERIFY(!f.rail().applyNativeFocus(*targetContext));
  LOKA_VERIFY(GetFocus() == previous);
  LOKA_VERIFY(Win32FocusParticipant::attach(target, targetContext));
  EnableWindow(target, FALSE);
  LOKA_VERIFY(!f.rail().applyNativeFocus(*targetContext));
  LOKA_VERIFY(GetFocus() == previous);
  EnableWindow(target, TRUE);
  LOKA_VERIFY(!f.rail().applyNativeFocus(*other.field(2)->getContext()));
  LOKA_VERIFY(GetFocus() == previous);
}

namespace
{
  WNDPROC killFocusPrevious = 0;
  void retireFocusTarget(void *data)
  {
    Fixture &f = *static_cast<Fixture *>(data);
    Node *target = f.field(2);
    // Use the fixture's lifecycle seam to perform the same terminal delivery
    // and context release that a synchronous Scene replacement performs.
    LifecycleFactTestAccess::MarkSubtreeRetired(target);
    LifecycleFactTestAccess::DeliverFacts(target);
    f.rail().releaseNodeContexts(target);
  }
  LRESULT CALLBACK commitOnKillFocus(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
  {
    if (message == WM_KILLFOCUS)
    {
      // Deterministic IME stand-in: native text -> EN_CHANGE -> State observer.
      LOKA_VERIFY(SetWindowTextW(hwnd, L"committed text"));
    }
    return CallWindowProcW(killFocusPrevious, hwnd, message, wParam, lParam);
  }
}

void testWin32FocusWriteReentry()
{
  Fixture f;
  if (!f.activate())
  {
    skipWithoutActivation("testWin32FocusWriteReentry", "kill-focus text commit and target retirement checks",
                          "Linux ASan cannot execute this native pin.");
    return;
  }
  const HWND oldEdit = f.edit(1);
  const HWND target = f.edit(2);
  SetFocus(oldEdit);
  f.app.reconcileFocus();
  f.expect(1);
  const FocusBinding request = EditTextProps().focusedAs(f.facts.focus, static_cast<unsigned short>(2)).focus_;
  f.facts.text.state()->bind(&retireFocusTarget, &f, false);
  killFocusPrevious = reinterpret_cast<WNDPROC>(
      SetWindowLongPtrW(oldEdit, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&commitOnKillFocus)));
  LOKA_VERIFY(killFocusPrevious);
  f.facts.focus.post(2);
  f.app.reconcileFocus();
  SetWindowLongPtrW(oldEdit, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(killFocusPrevious));
  killFocusPrevious = 0;
  f.facts.text.state()->unbind(&retireFocusTarget, &f);
  LOKA_VERIFY(!f.field(2)->getContext()); // Positive control: the observer did retire it.
  LOKA_VERIFY(!Win32FocusParticipant::read(target));
  LOKA_VERIFY(!request.requested());
  NodeContext *actual = 0;
  LOKA_VERIFY(f.rail().readNativeFocus(actual));
  if (actual == f.field(1)->getContext())
    f.expect(1);
  else if (actual == f.field(3)->getContext())
    f.expect(3);
  else
  {
    LOKA_VERIFY(!actual);
    f.none(); // A retired HWND may retain native focus but cannot publish key 2.
  }
}

void testWin32FocusInitialRequest()
{
  Fixture f(false, 2);
  const FocusBinding request = EditTextProps().focusedAs(f.facts.focus, static_cast<unsigned short>(2)).focus_;
  LOKA_VERIFY(request.requested());
  f.none(); // First WM_ACTIVATE has no publication to restore.
  if (!f.activate())
  {
    skipWithoutActivation("testWin32FocusInitialRequest", "initial native focus completion",
                          "the declared initial request is verified pending.");
    return;
  }
  f.app.reconcileFocus();
  LOKA_VERIFY(!request.requested());
  LOKA_VERIFY(GetFocus() == f.edit(2));
  f.expect(2);
}
