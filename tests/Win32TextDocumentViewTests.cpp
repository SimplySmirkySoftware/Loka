#include "Win32TextDocumentViewTests.hpp"
#include "Win32Window.hpp"
#include "Win32ScenePlatformController.hpp"
#include "context/Win32TextDocumentViewContext.hpp"
#include "context/Win32FocusParticipant.hpp"
#include "context/Win32ContainerWindow.hpp"
#include "text/Win32TextStore.hpp"
#include "app/nodes/boundary/StdComposition.hpp"
#include "app/nodes/nestable/Box.hpp"
#include "app/nodes/nestable/Show.hpp"
#include "app/text/TextStoreSource.hpp"
#include "support/TextDocumentAccess.hpp"
#include "support/TestVerify.hpp"
#include "support/LifecycleFactTestAccess.hpp"
#include "support/WindowAdmissionTestApp.hpp"
#include "platform/null/NullPlatformContext.hpp"
#include "testing/scene/SceneTestFlow.hpp"
#include "core/util/StateTrackerGuard.hpp"
#include <imm.h>
#include <cstdio>
#include <string>

namespace
{
  using namespace loka::app;
  using namespace loka::app::scene;
  using namespace loka::win32::testing;
  typedef loka::dsl::testing::SceneTestAccess SceneAccess;
  bool useContainer = false;
  bool initialWrap = true;
  class ViewRoot : public BoundaryNodeFor<ViewRoot>
  {
  public:
    TextDocument text;
    Focus<int> focus;
    NodeState<bool> shown;
    NodeState<int> width;
    NodeState<TextViewAttachment> result;
    explicit ViewRoot(const BoundaryPropsFor<ViewRoot> &props)
        : BoundaryNodeFor<ViewRoot>(props)
    {
      this->document(this->text);
      this->state(this->focus);
      this->state(this->shown, true);
      this->state(this->width, 200);
      this->state(this->result, TextViewAttachment());
    }
    bool reconcile(ComponentContext &context, Node *node, NodeDefinitionBase &definition)
    {
      return this->reconcileParkedBranch(context, node, &definition);
    }
    virtual void composeNode(NodeComposition &c)
    {
      c.declare(Show(*this->shown.state())
                << ((useContainer ? ClipBox() : Box()).width(this->width.state()).height(100)
                    << TextDocumentEditor(this->text)
                           .attachment(this->result)
                           .focusedAs(this->focus, 1)
                           .presentation(TextViewPresentation(TextStyle(), 3, false, initialWrap))));
    }
  };
  TextDocumentEditorNode *findEditor(Node *node)
  {
    if (node->nodeTypeKey() == NodeTypeToken<TextDocumentEditorNode>())
      return static_cast<TextDocumentEditorNode *>(node);
    INestable *parent = node->asNestable();
    for (Node *child = parent ? parent->childrenHead() : 0; child; child = child->nextInComposition)
    {
      TextDocumentEditorNode *found = findEditor(child);
      if (found)
        return found;
    }
    return 0;
  }
  Node *findBox(Node *node)
  {
    if (node->asBoxNode())
      return node;
    INestable *parent = node->asNestable();
    for (Node *child = parent ? parent->childrenHead() : 0; child; child = child->nextInComposition)
    {
      Node *found = findBox(child);
      if (found)
        return found;
    }
    return 0;
  }
  TextReplacement replacement(const char *text)
  {
    loka::core::resource::Blob bytes = loka::core::resource::Blob::Create();
    LOKA_VERIFY(bytes.tryAssign(reinterpret_cast<const unsigned char *>(text), std::char_traits<char>::length(text)));
    bytes.sealBytes();
    TextReplacement result;
    LOKA_VERIFY(TextReplacement::FromUtf8(bytes, TextPlacement::Start(), result) == TEXT_DOCUMENT_OK);
    return result;
  }
  std::wstring native(HWND edit)
  {
    const int length = GetWindowTextLengthW(edit);
    std::wstring result(length + 1, L'\0');
    LOKA_VERIFY(GetWindowTextW(edit, &result[0], length + 1) == length);
    result.resize(length);
    return result;
  }
  struct Fixture
  {
    NullPlatformContext platform;
    Win32Window window;
    WindowAdmissionTestApp app;
    static WindowProps props(bool container, bool wrap)
    {
      useContainer = container;
      initialWrap = wrap;
      WindowProps p;
      p.frame(40, 40, 320, 240).visible(true).scene(new Scene(Boundary<ViewRoot>()));
      return p;
    }
    explicit Fixture(bool container = false, bool wrap = true)
        : window(&this->platform, props(container, wrap)),
          app(this->window)
    {
      this->app.flush();
      this->app.reconcileAtCompletion();
    }
    ViewRoot &root()
    {
      return *static_cast<ViewRoot *>(SceneAccess::rootBoundary(*this->window.scene()));
    }
    Win32ScenePlatformController &rail()
    {
      return *static_cast<Win32ScenePlatformController *>(SceneAccess::platformController(*this->window.scene()));
    }
    TextDocumentEditorNode &node()
    {
      TextDocumentEditorNode *node = findEditor(&this->root());
      LOKA_VERIFY(node);
      return *node;
    }
    Win32TextDocumentViewContext &view()
    {
      LOKA_VERIFY(this->node().getContext());
      return *static_cast<Win32TextDocumentViewContext *>(this->node().getContext());
    }
    Win32TextStore &store()
    {
      TextStoreRow *row = loka::app::testing::TextDocumentAccess::registration(this->root().text)->store();
      LOKA_VERIFY(row);
      return *static_cast<Win32TextStore *>(row);
    }
    void show(bool value)
    {
      {
        loka::core::StateTrackerGuard guard(this->root().tracker());
        this->root().shown.set(value);
      }
      this->app.flush();
      this->app.reconcileAtCompletion();
    }
    void replace(const char *body)
    {
      LOKA_VERIFY(this->root().text.replace(replacement(body)) == POST_ACCEPTED);
      this->app.reconcileAtCompletion();
    }
    bool activate()
    {
      SetActiveWindow(this->window.hwnd());
      return GetActiveWindow() == this->window.hwnd();
    }
  };
  // A child EDIT refuses PrintWindow with PW_CLIENTONLY or 0 (VM probe 2026-11-17: ok=0, sentinel kept);
  // PW_RENDERFULLCONTENT captures it and distinguishes COLOR_WINDOW from the read-only COLOR_3DFACE ground.
  const UINT kPrintFullContent = 0x00000002;
  COLORREF printedGround(HWND edit)
  {
    HDC dc = GetDC(edit);
    LOKA_VERIFY(dc);
    RECT client;
    GetClientRect(edit, &client);
    LOKA_VERIFY(client.right > 4 && client.bottom > 4);
    HDC memory = CreateCompatibleDC(dc);
    HBITMAP bitmap = CreateCompatibleBitmap(dc, client.right, client.bottom);
    LOKA_VERIFY(memory && bitmap);
    HGDIOBJ previous = SelectObject(memory, bitmap);
    const COLORREF expected = GetSysColor(COLOR_WINDOW);
    HBRUSH sentinel = CreateSolidBrush(expected ^ 0x00ffffff);
    FillRect(memory, &client, sentinel);
    LOKA_VERIFY(PrintWindow(edit, memory, kPrintFullContent));
    const COLORREF pixel = GetPixel(memory, client.right / 2, client.bottom - 4);
    SelectObject(memory, previous);
    DeleteObject(sentinel);
    DeleteObject(bitmap);
    DeleteDC(memory);
    ReleaseDC(edit, dc);
    return pixel;
  }
  /** Test-only subclass: native paint, visibility flags, and destruction witnesses. */
  struct Witness
  {
    HWND window;
    WNDPROC previous;
    unsigned paints, erases, fonts, shows, destroys, commands;
    UINT showFlags;
    RECT firstPaint;
    bool capturePixels;
    COLORREF firstPixel;
    HWND mustBeParked, parking;
    explicit Witness(HWND hwnd)
        : window(hwnd),
          previous(0),
          paints(0),
          erases(0),
          fonts(0),
          shows(0),
          destroys(0),
          commands(0),
          showFlags(0),
          capturePixels(false),
          firstPixel(CLR_INVALID),
          mustBeParked(0),
          parking(0)
    {
      SetRectEmpty(&this->firstPaint);
      LOKA_VERIFY(SetPropW(hwnd, L"Loka.Test.ViewWitness", this));
      this->previous =
          reinterpret_cast<WNDPROC>(SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&procedure)));
      LOKA_VERIFY(this->previous);
    }
    ~Witness()
    {
      if (IsWindow(this->window))
      {
        SetWindowLongPtrW(this->window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(this->previous));
        RemovePropW(this->window, L"Loka.Test.ViewWitness");
      }
    }
    static LRESULT CALLBACK procedure(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
    {
      Witness &self = *static_cast<Witness *>(GetPropW(hwnd, L"Loka.Test.ViewWitness"));
      if (msg == WM_PAINT)
      {
        if (!self.paints)
          GetWindowRect(hwnd, &self.firstPaint);
        ++self.paints;
      }
      if (msg == WM_ERASEBKGND)
        ++self.erases;
      if (msg == WM_SETFONT)
        ++self.fonts;
      if (msg == WM_COMMAND)
        ++self.commands;
      if (msg == WM_WINDOWPOSCHANGING)
      {
        const WINDOWPOS &position = *reinterpret_cast<WINDOWPOS *>(lp);
        if (position.flags & SWP_SHOWWINDOW)
        {
          ++self.shows;
          self.showFlags = position.flags;
        }
      }
      WNDPROC previous = self.previous;
      if (msg == WM_DESTROY)
        ++self.destroys;
      const LRESULT result = CallWindowProcW(previous, hwnd, msg, wp, lp);
      if (msg == WM_PAINT && self.paints == 1 && self.capturePixels)
        self.firstPixel = printedGround(hwnd);
      // WM_DESTROY enters Loka teardown before user32 destroys child windows.
      if (msg == WM_DESTROY && self.mustBeParked)
        LOKA_VERIFY(!IsWindow(self.mustBeParked) || GetParent(self.mustBeParked) == self.parking);
      if (msg == WM_NCDESTROY)
        RemovePropW(hwnd, L"Loka.Test.ViewWitness");
      return result;
    }
  };
  void detach(Win32TextDocumentViewContext &view)
  {
    view.onFactChanged(NODE_FACT_ATTACHED, NODE_FACT_DETACHED_RETAINED);
  }
  void attach(Win32TextDocumentViewContext &view)
  {
    view.readLifecycleFactOnAttach();
  }
  void verifyParked(HWND edit, HWND parking)
  {
    LOKA_VERIFY(IsWindow(edit) && GetParent(edit) == parking);
    LOKA_VERIFY(!(GetWindowLongW(edit, GWL_STYLE) & WS_VISIBLE));
    LOKA_VERIFY(!Win32FocusParticipant::read(edit));
    LOKA_VERIFY(!SendMessageW(edit, WM_GETFONT, 0, 0));
  }
  void verifyFrame(HWND edit, const RECT &expected)
  {
    RECT frame;
    LOKA_VERIFY(GetWindowRect(edit, &frame));
    MapWindowPoints(0, GetParent(edit), reinterpret_cast<POINT *>(&frame), 2);
    LOKA_VERIFY(EqualRect(&frame, &expected));
  }
} // namespace
void testWin32TextViewAttachDetach()
{
  Fixture f;
  f.replace("body");
  Win32TextDocumentViewContext &view = f.view();
  HWND edit = view.hwnd();
  LOKA_VERIFY(edit && GetParent(edit) == view.viewWindow());
  LOKA_VERIFY(GetWindowLongW(edit, GWL_STYLE) & WS_VSCROLL);
  LOKA_VERIFY(!(GetWindowLongW(edit, GWL_STYLE) & ES_AUTOHSCROLL));
  SendMessageW(edit, EM_SETSEL, 4, 4);
  SendMessageW(edit, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L"!"));
  SendMessageW(edit, EM_SETSEL, 1, 3);
  const LRESULT undo = SendMessageW(edit, EM_CANUNDO, 0, 0);
  const LRESULT modified = SendMessageW(edit, EM_GETMODIFY, 0, 0);
  const LRESULT selection = SendMessageW(edit, EM_GETSEL, 0, 0);
  LOKA_VERIFY(undo && modified);
  const bool active = f.activate();
  if (active)
    SetFocus(edit);
  detach(view);
  const HWND parking = GetParent(edit);
  LOKA_VERIFY(parking != view.viewWindow() && !view.hwnd());
  verifyParked(edit, parking);
  LOKA_VERIFY(native(edit) == L"body!" && SendMessageW(edit, EM_GETSEL, 0, 0) == selection);
  LOKA_VERIFY(SendMessageW(edit, EM_CANUNDO, 0, 0) == undo && SendMessageW(edit, EM_GETMODIFY, 0, 0) == modified);
  if (active)
    LOKA_VERIFY(GetFocus() == view.viewWindow());
  else
    std::printf("[skip] view detach focus: desktop did not grant activation\n");
  LOKA_VERIFY(SendMessageW(edit, EM_UNDO, 0, 0));
  LOKA_VERIFY(native(edit) == L"body");
  Witness witness(edit);
  attach(view);
  LOKA_VERIFY(view.hwnd() == edit && witness.fonts == 1 && witness.paints == 0);
  LOKA_VERIFY(!(GetWindowLongW(edit, GWL_STYLE) & WS_VISIBLE));
  f.app.flush();
  LOKA_VERIFY(GetWindowLongW(edit, GWL_STYLE) & WS_VISIBLE);
  f.show(false);
  verifyParked(edit, parking);
  f.show(true);
  LOKA_VERIFY(view.hwnd() == edit && native(edit) == L"body");
}
void testWin32TextViewLayoutShow()
{
  Fixture f;
  Win32TextDocumentViewContext &view = f.view();
  HWND edit = view.hwnd();
  PaintQuery query = {{1, 0, 0, 0, 0, 320, 240}, PLACEMENT_ELIGIBLE};
  PaintAnswer answer = view.queryPaintDamage(query);
  LOKA_VERIFY(answer.kind == PAINT_ANSWER_EXACT && !answer.damage.width && !answer.damage.height);
  Witness witness(edit);
  witness.capturePixels = true;
  detach(view);
  answer = view.queryPaintDamage(query);
  LOKA_VERIFY(answer.kind == PAINT_ANSWER_REFUSED && answer.reason == PAINT_REFUSED_NO_CONTEXT);
  attach(view);
  LOKA_VERIFY(witness.paints == 0 && witness.shows == 0);
  LayoutState state;
  state.x = 17;
  state.y = 19;
  state.width = 123;
  state.height = 67;
  view.layout(&f.rail(), state);
  LOKA_VERIFY(witness.shows == 1 && !(witness.showFlags & SWP_NOREDRAW));
  LOKA_VERIFY((witness.showFlags & (SWP_NOACTIVATE | SWP_NOZORDER)) == (SWP_NOACTIVATE | SWP_NOZORDER));
  verifyFrame(edit, f.rail().displayScale().projectFrame(loka::core::Frame(17, 19, 123, 67)).r);
  UpdateWindow(edit);
  LOKA_VERIFY(witness.paints > 0);
  RECT frame;
  GetWindowRect(edit, &frame);
  LOKA_VERIFY(EqualRect(&frame, &witness.firstPaint));
  LOKA_VERIFY(witness.firstPixel == GetSysColor(COLOR_WINDOW));
  detach(view);
  attach(view);
  witness.paints = 0;
  witness.firstPixel = CLR_INVALID;
  f.rail().relayout(300, 200);
  LOKA_VERIFY(witness.shows == 2 && (witness.showFlags & SWP_NOREDRAW));
  LOKA_VERIFY(witness.paints > 0 && witness.firstPixel == GetSysColor(COLOR_WINDOW));
  GetWindowRect(edit, &frame);
  LOKA_VERIFY(EqualRect(&frame, &witness.firstPaint));
  // A real retained return must never paint its stale 200-unit frame.
  f.show(false);
  witness.paints = 0;
  witness.firstPixel = CLR_INVALID;
  {
    loka::core::StateTrackerGuard guard(f.root().tracker());
    f.root().width.set(140);
  }
  LOKA_VERIFY(witness.paints == 0);
  f.show(true);
  GetWindowRect(edit, &frame);
  LOKA_VERIFY(witness.paints > 0 && EqualRect(&frame, &witness.firstPaint));
  LOKA_VERIFY(frame.right - frame.left == f.rail().displayScale().nativeLength(0, 140).px);
  LOKA_VERIFY(witness.firstPixel == GetSysColor(COLOR_WINDOW) && (witness.showFlags & SWP_NOREDRAW));
  // The EDIT's own visibility governs first show even under a hidden ancestor.
  ShowWindow(f.window.hwnd(), SW_HIDE);
  detach(view);
  attach(view);
  view.layout(&f.rail(), state);
  const unsigned shows = witness.shows;
  view.layout(&f.rail(), state);
  LOKA_VERIFY(witness.shows == shows);
  ShowWindow(f.window.hwnd(), SW_SHOWNA);
}

namespace
{
  HWND replacementOld = 0, preparedEdit = 0;
  Win32TextDocumentViewContext *replacementView = 0;
  unsigned preparedTargets = 0, committedTargets = 0, freshPaints = 0, preparedPositions = 0;
  LRESULT CALLBACK observeReplacement(int code, WPARAM wp, LPARAM lp)
  {
    if (code >= 0 && replacementView)
    {
      const CWPSTRUCT &call = *reinterpret_cast<const CWPSTRUCT *>(lp);
      if (call.hwnd != replacementOld && Win32TextDocumentViewContext::fromWindow(call.hwnd) == replacementView)
      {
        preparedEdit = call.hwnd;
        if (call.message == WM_PAINT)
          ++freshPaints;
        if (call.message == WM_WINDOWPOSCHANGING)
        {
          const WINDOWPOS &position = *reinterpret_cast<const WINDOWPOS *>(call.lParam);
          if ((position.flags & SWP_NOREDRAW)
              && !(position.flags & (SWP_SHOWWINDOW | SWP_NOZORDER | SWP_NOMOVE | SWP_NOSIZE)))
          {
            LOKA_VERIFY(position.hwndInsertAfter != replacementOld);
            ++preparedPositions;
          }
        }
        if (call.message == WM_SETFONT)
        {
          LOKA_VERIFY(Win32FocusParticipant::target(replacementView) == replacementOld);
          LOKA_VERIFY(!Win32FocusParticipant::attachedTarget(replacementView->viewWindow(), call.hwnd));
          ++preparedTargets;
        }
        if (call.message == WM_SHOWWINDOW && call.wParam)
        {
          LOKA_VERIFY(Win32FocusParticipant::target(replacementView) == call.hwnd);
          LOKA_VERIFY(!Win32FocusParticipant::attachedTarget(replacementView->viewWindow(), replacementOld));
          ++committedTargets;
        }
      }
    }
    return CallNextHookEx(0, code, wp, lp);
  }
  struct ReplacementObserver
  {
    HHOOK hook;
    explicit ReplacementObserver(Win32TextDocumentViewContext &view)
    {
      replacementView = &view;
      replacementOld = view.hwnd();
      preparedEdit = 0;
      preparedTargets = committedTargets = freshPaints = preparedPositions = 0;
      this->hook = SetWindowsHookExW(WH_CALLWNDPROC, observeReplacement, 0, GetCurrentThreadId());
      LOKA_VERIFY(this->hook);
    }
    ~ReplacementObserver()
    {
      UnhookWindowsHookEx(this->hook);
      replacementView = 0;
    }
  };
} // namespace
void testWin32TextViewReplacement()
{
  for (unsigned focusCase = 0; focusCase != 4; ++focusCase)
  {
    Fixture f;
    f.replace("old");
    HWND old = f.view().hwnd();
    detach(f.view());
    HWND parking = GetParent(old);
    attach(f.view());
    f.rail().relayout(300, 200);
    HWND sibling = CreateWindowExW(0,
                                   L"BUTTON",
                                   L"neighbor",
                                   WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                   0,
                                   0,
                                   100,
                                   50,
                                   f.view().viewWindow(),
                                   0,
                                   GetModuleHandleW(0),
                                   0);
    LOKA_VERIFY(sibling);
    SetWindowPos(sibling, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    const HWND above = GetWindow(old, GW_HWNDPREV);
    const bool active = f.activate();
    if (focusCase == 2)
      SetActiveWindow(0);
    else if (active)
      SetFocus(focusCase == 0 ? old : sibling);
    if (focusCase == 3)
      ShowWindow(old, SW_HIDE);
    const bool wasVisible = (GetWindowLongW(old, GWL_STYLE) & WS_VISIBLE) != 0;
    const HWND focus = GetFocus();
    RECT oldFrame;
    GetWindowRect(old, &oldFrame);
    Witness parent(f.view().viewWindow()), oldPaint(old), siblingPaint(sibling);
    ReplacementObserver observer(f.view());
    TextStoreRow *fresh = 0;
    TextStoreReading reading;
    TextStoreSource source(replacement("new body"));
    Win32TextStore &oldRow = f.store();
    // The rail door is isolated from the completion's later layout/paint.
    LOKA_VERIFY(f.rail().createTextStore(source, &oldRow, fresh, reading) == TEXT_STORE_BORN);
    HWND edit = static_cast<Win32TextStore *>(fresh)->edit();
    LOKA_VERIFY(f.view().hwnd() == edit && preparedTargets && preparedPositions);
    LOKA_VERIFY((committedTargets != 0) == wasVisible);
    LOKA_VERIFY(((GetWindowLongW(edit, GWL_STYLE) & WS_VISIBLE) != 0) == wasVisible);
    verifyParked(old, parking);
    RECT freshFrame;
    GetWindowRect(edit, &freshFrame);
    LOKA_VERIFY(EqualRect(&oldFrame, &freshFrame));
    LOKA_VERIFY(GetWindow(edit, GW_HWNDPREV) == above);
    if (!(parent.erases == 0 && oldPaint.paints == 0 && siblingPaint.paints == 0 && freshPaints == 0))
      std::printf("replacement case %u: parent erases=%u paints=%u old paints=%u sibling paints=%u fresh paints=%u\n",
                  focusCase, parent.erases, parent.paints, oldPaint.paints, siblingPaint.paints, freshPaints);
    LOKA_VERIFY(parent.erases == 0 && oldPaint.paints == 0 && siblingPaint.paints == 0 && freshPaints == 0);
    LOKA_VERIFY(GetFocus() == (focus == old ? edit : focus));
    if (!active && focusCase != 2)
      std::printf("[skip] replacement focused case: activation refused\n");
    fresh->retire(); // Cuts the transferred claim before this isolated birth is reclaimed.
    f.rail().drainNativeRetirements();
    DestroyWindow(sibling);
  }
}
void testWin32TextViewReplacementRollback()
{
  for (unsigned stage = 0; stage != TEXT_VIEW_PREPARATION_COUNT; ++stage)
  {
    Fixture f;
    f.replace("old");
    HWND old = f.view().hwnd();
    detach(f.view());
    HWND parking = GetParent(old);
    attach(f.view());
    f.rail().relayout(300, 200);
    SendMessageW(old, EM_SETSEL, 3, 3);
    SendMessageW(old, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L"!"));
    SendMessageW(old, EM_SETSEL, 1, 2);
    const LRESULT selection = SendMessageW(old, EM_GETSEL, 0, 0);
    const HWND parent = GetParent(old), focus = GetFocus();
    const HWND above = GetWindow(old, GW_HWNDPREV);
    const LRESULT font = SendMessageW(old, WM_GETFONT, 0, 0);
    ReplacementObserver observer(f.view());
    failTextViewPreparations(static_cast<TextViewPreparation>(stage), 1);
    TextStoreSource source(replacement("refused"));
    TextStoreRow *fresh = 0;
    TextStoreReading reading;
    LOKA_VERIFY(f.rail().createTextStore(source, &f.store(), fresh, reading) == TEXT_STORE_REFUSED_UNSUPPORTED);
    LOKA_VERIFY(!fresh && f.view().hwnd() == old && native(old) == L"old!");
    LOKA_VERIFY(GetParent(old) == parent && GetFocus() == focus && GetWindow(old, GW_HWNDPREV) == above);
    LOKA_VERIFY(SendMessageW(old, EM_GETSEL, 0, 0) == selection && SendMessageW(old, EM_CANUNDO, 0, 0));
    LOKA_VERIFY(SendMessageW(old, EM_GETMODIFY, 0, 0) && SendMessageW(old, WM_GETFONT, 0, 0) == font);
    // Check partial rows before drain, including stages before their mark was installed.
    unsigned parked = 0;
    for (HWND child = GetWindow(parking, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT))
    {
      verifyParked(child, parking);
      LOKA_VERIFY(!GetWindowLongPtrW(child, GWLP_USERDATA));
      ++parked;
    }
    if (parked != 1)
      std::printf("rollback stage %u: parked children=%u\n", stage, parked);
    LOKA_VERIFY(parked == 1);
    for (HWND child = GetWindow(parent, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT))
      LOKA_VERIFY(child == old || Win32TextDocumentViewContext::fromWindow(child) != &f.view());
    // Terminal delivery cuts the claim before failed births are drained.
    LifecycleFactTestAccess::MarkSubtreeRetired(&f.node());
    LifecycleFactTestAccess::DeliverFacts(&f.node());
    LOKA_VERIFY(!Win32FocusParticipant::read(old));
    f.rail().drainNativeRetirements();
    LOKA_VERIFY(IsWindow(old) && GetWindow(parking, GW_CHILD) == old && !GetWindow(old, GW_HWNDNEXT));
  }
}
void testWin32TextViewFocus()
{
  Fixture f;
  HWND edit = f.view().hwnd();
  LOKA_VERIFY(Win32FocusParticipant::target(&f.view()) == edit);
  LOKA_VERIFY(Win32FocusParticipant::attachedTarget(f.window.hwnd(), edit) == &f.view());
  if (!f.activate())
  {
    std::printf("[skip] text view Focus/Tab: desktop did not grant activation; identity checks retained\n");
    return;
  }
  f.root().focus.post(1);
  f.app.reconcileAtCompletion();
  LOKA_VERIFY(GetFocus() == edit && f.root().focus.state()->get().is(1));
  NodeContext *focused = 0;
  LOKA_VERIFY(f.rail().readNativeFocus(focused) && focused == &f.view());
  SetFocus(f.window.hwnd());
  MSG tab = {};
  tab.hwnd = f.window.hwnd();
  tab.message = WM_KEYDOWN;
  tab.wParam = VK_TAB;
  LOKA_VERIFY(IsDialogMessageW(f.window.hwnd(), &tab));
  LOKA_VERIFY(GetFocus() == edit);
  Win32TextDocumentViewContext &view = f.view();
  f.show(false);
  LOKA_VERIFY(!Win32FocusParticipant::target(&view));
  LOKA_VERIFY(!Win32FocusParticipant::attachedTarget(f.window.hwnd(), edit));
  SetFocus(f.window.hwnd());
  IsDialogMessageW(f.window.hwnd(), &tab);
  LOKA_VERIFY(GetFocus() != edit);
}
namespace
{
  void defaultGround(HWND parent, HWND edit)
  {
    HDC dc = GetDC(edit);
    LOKA_VERIFY(dc);
    SetBkMode(dc, OPAQUE);
    HBRUSH brush = reinterpret_cast<HBRUSH>(
        SendMessageW(parent, WM_CTLCOLORSTATIC, reinterpret_cast<WPARAM>(dc), reinterpret_cast<LPARAM>(edit)));
    LOKA_VERIFY(brush && brush != GetStockObject(NULL_BRUSH) && GetBkMode(dc) == OPAQUE);
    const COLORREF expected = GetBkColor(dc);
    HDC memory = CreateCompatibleDC(dc);
    RECT client;
    GetClientRect(edit, &client);
    HBITMAP bitmap = CreateCompatibleBitmap(dc, client.right, client.bottom);
    HGDIOBJ previous = SelectObject(memory, bitmap);
    // Sentinel distinguishes an unpainted transparent ground from the native default.
    HBRUSH sentinel = CreateSolidBrush(RGB(255, 0, 255));
    FillRect(memory, &client, sentinel);
    LOKA_VERIFY(PrintWindow(edit, memory, kPrintFullContent));
    LOKA_VERIFY(GetPixel(memory, client.right / 2, client.bottom - 4) == expected);
    DeleteObject(sentinel);
    SelectObject(memory, previous);
    DeleteObject(bitmap);
    DeleteDC(memory);
    ReleaseDC(edit, dc);
  }
} // namespace
void testWin32TextViewPresentation()
{
  for (unsigned container = 0; container != 2; ++container)
  {
    Fixture f(container != 0);
    f.replace("read only");
    TextDocumentEditor next(
        TextDocumentEditorProps(f.node().props).presentation(TextViewPresentation(TextStyle() + Monospace, 7, true)));
    LOKA_VERIFY(next.applyPropsToNode(&f.node()));
    HWND edit = f.view().hwnd();
    LOKA_VERIFY(GetWindowLongW(edit, GWL_STYLE) & ES_READONLY);
    const int margin = f.rail().displayScale().nativeLength(0, 7).px;
    LOKA_VERIFY(SendMessageW(edit, EM_GETMARGINS, 0, 0) == MAKELPARAM(margin, margin));
    LOKA_VERIFY(reinterpret_cast<HFONT>(SendMessageW(edit, WM_GETFONT, 0, 0))
                == f.rail().fallbackFont(f.node().props.presentation_.style()));
    defaultGround(GetParent(edit), edit);
    SendMessageW(edit, WM_CHAR, L'X', 0);
    LOKA_VERIFY(native(edit) == L"read only");
    f.replace("document request");
    edit = f.view().hwnd();
    LOKA_VERIFY(native(edit) == L"document request" && (GetWindowLongW(edit, GWL_STYLE) & ES_READONLY));
    LOKA_VERIFY(SendMessageW(edit, EM_GETMARGINS, 0, 0) == MAKELPARAM(margin, margin));
    defaultGround(GetParent(edit), edit);
    // Unmarked static children retain their existing transparent treatment.
    HWND label =
        CreateWindowExW(0, L"STATIC", L"label", WS_CHILD, 0, 0, 40, 20, GetParent(edit), 0, GetModuleHandleW(0), 0);
    HDC dc = GetDC(label);
    HBRUSH brush = reinterpret_cast<HBRUSH>(SendMessageW(
        GetParent(edit), WM_CTLCOLORSTATIC, reinterpret_cast<WPARAM>(dc), reinterpret_cast<LPARAM>(label)));
    LOKA_VERIFY(brush == GetStockObject(NULL_BRUSH) && GetBkMode(dc) == TRANSPARENT);
    ReleaseDC(label, dc);
    DestroyWindow(label);
  }
}
void testWin32TextViewDpi()
{
  Fixture f;
  TextDocumentEditor next(
      TextDocumentEditorProps(f.node().props).presentation(TextViewPresentation(TextStyle() + Monospace, 5)));
  LOKA_VERIFY(next.applyPropsToNode(&f.node()));
  HWND edit = f.view().hwnd();
  Witness witness(edit);
  f.rail().updateDisplayScale(loka::win32::Win32DisplayScale(144));
  LOKA_VERIFY(witness.fonts == 1);
  LOKA_VERIFY(reinterpret_cast<HFONT>(SendMessageW(edit, WM_GETFONT, 0, 0))
              == f.rail().fallbackFont(f.node().props.presentation_.style()));
  detach(f.view());
  const HWND parking = GetParent(edit);
  f.rail().updateDisplayScale(loka::win32::Win32DisplayScale(192));
  verifyParked(edit, parking);
  attach(f.view());
  LOKA_VERIFY(reinterpret_cast<HFONT>(SendMessageW(edit, WM_GETFONT, 0, 0))
              == f.rail().fallbackFont(f.node().props.presentation_.style()));
  {
    const LRESULT margins = SendMessageW(edit, EM_GETMARGINS, 0, 0);
    if (margins != MAKELPARAM(10, 10))
      std::printf("dpi margins: left=%u right=%u expected 10,10\n", LOWORD(margins), HIWORD(margins));
  }
  LOKA_VERIFY(SendMessageW(edit, EM_GETMARGINS, 0, 0) == MAKELPARAM(10, 10));
  detach(f.view());
  typedef HANDLE(WINAPI * GetContext)(HWND);
  typedef HANDLE(WINAPI * SetContext)(HANDLE);
  typedef BOOL(WINAPI * Equal)(HANDLE, HANDLE);
  HMODULE user = GetModuleHandleW(L"user32.dll");
  GetContext get = reinterpret_cast<GetContext>(GetProcAddress(user, "GetWindowDpiAwarenessContext"));
  SetContext set = reinterpret_cast<SetContext>(GetProcAddress(user, "SetThreadDpiAwarenessContext"));
  Equal equal = reinterpret_cast<Equal>(GetProcAddress(user, "AreDpiAwarenessContextsEqual"));
  if (!get || !set || !equal)
    std::printf("[skip] view DPI mismatch: Windows 10 1607 awareness APIs unavailable\n");
  else
  {
    HANDLE saved = set(reinterpret_cast<HANDLE>(static_cast<INT_PTR>(-1)));
    LOKA_VERIFY(saved);
    HWND unaware =
        CreateWindowExW(0, L"STATIC", L"unaware view", WS_OVERLAPPED, 0, 0, 200, 100, 0, 0, GetModuleHandleW(0), 0);
    set(saved);
    LOKA_VERIFY(unaware);
    if (equal(get(edit), get(unaware)))
      std::printf("[skip] view DPI mismatch: fixture has no unequal native awareness pair\n");
    else
    {
      LOKA_VERIFY(!f.store().attach(unaware, f.view(), f.node().props.presentation_));
      verifyParked(edit, parking);
    }
    DestroyWindow(unaware);
  }
  failTextDpiQualifications(1);
  attach(f.view());
  LOKA_VERIFY(!f.view().hwnd());
  verifyParked(edit, parking);
  f.app.reconcileAtCompletion();
  LOKA_VERIFY(f.root().result.get().value() == TextViewAttachment::VIEW_REFUSED_UNSUPPORTED);
}
void testWin32TextViewWrapRefusal()
{
  Fixture f(false, false);
  HWND edit = f.store().edit();
  const HWND parking = GetParent(edit);
  LOKA_VERIFY(!f.view().hwnd());
  LOKA_VERIFY(f.root().result.get().value() == TextViewAttachment::VIEW_REFUSED_UNSUPPORTED);
  verifyParked(edit, parking);
  f.replace("body survives refusal");
  LOKA_VERIFY(native(f.store().edit()) == L"body survives refusal" && !f.view().hwnd());
  TextDocumentEditor next(TextDocumentEditorProps(f.node().props).presentation(TextViewPresentation()));
  LOKA_VERIFY(!next.applyPropsToNode(&f.node()));
  LOKA_VERIFY(!f.node().props.presentation_.wrap());
  // Incompatibility forces the ordinary-child reconciliation path to rematerialize.
  ComponentContext context;
  context.setBoundary(&f.root());
  context.setStateOwner(&f.root());
  context.setScene(f.window.scene());
  context.setPlatformController(&f.rail());
  Node *parent = findBox(&f.root());
  LOKA_VERIFY(parent);
  Box box;
  box << next;
  LOKA_VERIFY(f.root().reconcile(context, parent, box));
  f.rail().onChange(&f.root(), NODE_DIRTY_INITIAL, true);
  f.app.reconcileAtCompletion();
  LOKA_VERIFY(f.node().props.presentation_.wrap() && f.view().hwnd() == f.store().edit());
  LOKA_VERIFY(native(f.view().hwnd()) == L"body survives refusal");
}
void testWin32TextViewIme()
{
  Fixture f;
  HWND edit = f.view().hwnd();
  if (!f.activate())
  {
    std::printf(
        "[skip] 4w-2b Gate-1 section 5.8 IME: desktop activation unavailable; VM handoff must run positive case\n");
    return;
  }
  SetFocus(edit);
  HIMC context = ImmGetContext(edit);
  if (!context)
  {
    std::printf("[skip] 4w-2b Gate-1 section 5.8 IME: no native input context; VM handoff must run positive case\n");
    return;
  }
  wchar_t composition[] = L"\x304b\x306a";
  const BOOL set = ImmSetCompositionStringW(context, SCS_SETSTR, composition, 2 * sizeof(wchar_t), 0, 0);
  const LONG length = ImmGetCompositionStringW(context, GCS_COMPSTR, 0, 0);
  if (!set || length <= 0)
    std::printf(
        "[skip] 4w-2b Gate-1 section 5.8 IME: active IME refused synthetic composition; run real MS-IME on VM\n");
  else
  {
    TextStoreReading reading;
    LOKA_VERIFY(f.rail().readTextStore(f.store(), reading) && reading.marked());
    f.app.reconcileAtCompletion();
    unsigned long bodyLength = 0;
    LOKA_VERIFY(!f.root().text.queryLength(bodyLength));
    f.rail().commitMarkedText(f.store());
    LOKA_VERIFY(f.rail().readTextStore(f.store(), reading) && reading.marked());
  }
  ImmNotifyIME(context, NI_COMPOSITIONSTR, CPS_CANCEL, 0);
  ImmReleaseContext(edit, context);
}
void testWin32TextViewRetirement()
{
  for (unsigned mode = 0; mode != 4; ++mode)
  {
    Fixture f(true);
    HWND edit = f.view().hwnd();
    detach(f.view());
    HWND parking = GetParent(edit);
    attach(f.view());
    f.app.flush();
    HWND container = GetParent(edit);
    LOKA_VERIFY(container != f.window.hwnd());
    Witness parent(container), parked(parking), editWitness(edit);
    parent.mustBeParked = edit;
    parent.parking = parking;
    if (mode == 2)
    {
      // Exercise the same terminal delivery as container removal, before the safe point.
      Node *box = findBox(&f.root());
      LOKA_VERIFY(box);
      LifecycleFactTestAccess::MarkSubtreeRetired(box);
      LifecycleFactTestAccess::DeliverFacts(box);
      verifyParked(edit, parking);
      const unsigned beforeDrain = parked.commands;
      f.rail().drainNativeRetirements();
      LOKA_VERIFY(parent.destroys == 1 && IsWindow(edit));
      LOKA_VERIFY(parked.commands == beforeDrain);
    }
    else if (mode == 0)
    {
      // Retained descendants are detached before the container's delayed native retirement.
      f.show(false);
      verifyParked(edit, parking);
      LOKA_VERIFY(!parent.destroys);
      f.show(true);
      LOKA_VERIFY(f.view().hwnd() == edit);
    }
    else if (mode == 1)
      f.show(false); // Root teardown also traverses retained descendants.
    const unsigned commands = parked.commands;
    if (mode == 3)
      LOKA_VERIFY(DestroyWindow(f.window.hwnd()));
    else
    {
      {
        loka::core::StateTrackerGuard guard(f.window.getTracker());
        f.window.visibilityState().set(false);
      }
      f.app.flush();
    }
    LOKA_VERIFY(parent.destroys == 1 && editWitness.destroys == 1 && parked.destroys == 1);
    LOKA_VERIFY(parked.commands == commands);
    LOKA_VERIFY(!IsWindow(edit) && !IsWindow(parking));
  }
}

void testWin32TextViewClaimAuthority()
{
#ifdef LOKA_LIFECYCLE_AUDIT
  std::printf("[skip] claim/native-parent contradiction: audit builds assert this invariant; run release pin\n");
#else
  Fixture f(false, false);
  Win32TextStore &old = f.store();
  const HWND edit = old.edit(), parking = GetParent(edit);
  LOKA_VERIFY(!f.view().hwnd());
  // Deliberate negative control: native placement is not an attachment claim.
  LOKA_VERIFY(SetParent(edit, f.window.hwnd()));
  TextStoreSource source(replacement("claim authority"));
  TextStoreReading reading;
  TextStoreRow *fresh = 0;
  LOKA_VERIFY(f.rail().createTextStore(source, &old, fresh, reading) == TEXT_STORE_BORN);
  LOKA_VERIFY(GetParent(edit) == f.window.hwnd());
  LOKA_VERIFY(GetParent(static_cast<Win32TextStore *>(fresh)->edit()) == parking);
  LOKA_VERIFY(!f.view().hwnd());
  LOKA_VERIFY(SetParent(edit, parking));
  fresh->retire();
  f.rail().drainNativeRetirements();
#endif
}
