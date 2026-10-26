#include "Win32PlatformContext.hpp"
#include "support/TextBufferFixture.hpp"
#include "testing/core/StateTrackerTestAccess.hpp"
#include "support/LifecycleFactTestAccess.hpp"
#include "support/TextEditorStateOwner.hpp"
#include "support/TextEditorAccess.hpp"
#include "support/TextEditorReportRefusal.hpp"
#include "Win32EditTextBridgeTests.hpp"
#include "support/TestVerify.hpp"
#include "support/LokaAllocFailure.hpp"
#include "context/Win32TextEditorContext.hpp"
#include "context/Win32EditTextBridge.hpp"
#include "app/nodes/controls/TextEditorDiff.hpp"
#include "Win32ScenePlatformController.hpp"
#include "Win32BuiltInSupport.hpp"
#include "platform/StringUTF8.hpp"
#include <cstdio>
#include <vector>

namespace loka
{
  namespace testing
  {
    class Win32TextEditorAccess
    {
    public:
      static app::TextEditorNode *node(const Win32TextEditorContext &context)
      {
        return context.node_;
      }
      static const app::scene::SeamKey<app::TextEditorNode> &key(const Win32TextEditorContext &context)
      {
        return context.key_;
      }
      static unsigned restores(const Win32TextEditorContext &context)
      {
        return context.restores_;
      }
      static app::EditorResult status(const Win32TextEditorContext &context)
      {
        return context.status_;
      }
      static WNDPROC nativeProcedure(const Win32TextEditorContext &context)
      {
        return context.previousProc_;
      }
      static bool pending(const Win32TextEditorContext &context)
      {
        return context.phase_ == Win32TextEditorContext::RETRY;
      }
    };
  } // namespace testing
} // namespace loka
namespace
{
  using namespace loka::app;
  using namespace loka::app::scene;
  using namespace loka::core;
  typedef loka::testing::Win32TextEditorAccess EditorAccess;
  const wchar_t kHostController[] = L"Loka.Editor.TestHost";
  const wchar_t kNotification[] = L"Loka.Editor.TestNotification";
  const wchar_t kProbe[] = L"Loka.Editor.TestProbe";

  const wchar_t kChangeObservation[] = L"Loka.Editor.TestChangeObservation";
  /** Stack-scoped observation of native text before the controller can restore it. */
  class ChangeObservation
  {
  public:
    explicit ChangeObservation(HWND host)
        : host_(host),
          arrivals_(0),
          native_(),
          status_(EDITOR_OK)
    {
      LOKA_VERIFY(SetPropW(this->host_, kChangeObservation, this));
    }
    ~ChangeObservation()
    {
      RemovePropW(this->host_, kChangeObservation);
    }
    void arriving(HWND editor)
    {
      ++this->arrivals_;
      const int length = GetWindowTextLengthW(editor);
      std::vector<wchar_t> text(static_cast<std::size_t>(length) + 1);
      const int copied = GetWindowTextW(editor, &text[0], length + 1);
      this->native_.assign(&text[0], static_cast<std::size_t>(copied));
    }
    void handled(HWND editor)
    {
      Win32TextEditorContext *context = Win32TextEditorContext::fromWindow(editor);
      LOKA_VERIFY(context);
      this->status_ = EditorAccess::status(*context);
    }
    unsigned arrivals() const
    {
      return this->arrivals_;
    }
    const std::wstring &native() const
    {
      return this->native_;
    }
    EditorResult status() const
    {
      return this->status_;
    }

  private:
    ChangeObservation(const ChangeObservation &);
    ChangeObservation &operator=(const ChangeObservation &);
    HWND host_;
    unsigned arrivals_;
    std::wstring native_;
    EditorResult status_;
  };

  LRESULT CALLBACK HostProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
  {
    Win32ScenePlatformController *controller =
        static_cast<Win32ScenePlatformController *>(GetPropW(window, kHostController));
    if (message == WM_COMMAND && controller)
    {
      HANDLE previousNotification = GetPropW(window, kNotification);
      LOKA_VERIFY(SetPropW(window, kNotification, controller));
      ChangeObservation *observation =
          HIWORD(wParam) == EN_CHANGE ? static_cast<ChangeObservation *>(GetPropW(window, kChangeObservation)) : 0;
      if (observation)
        observation->arriving(reinterpret_cast<HWND>(lParam));
      const bool handled = controller->handleCommand(wParam, lParam);
      if (observation)
        observation->handled(reinterpret_cast<HWND>(lParam));
      if (previousNotification)
        LOKA_VERIFY(SetPropW(window, kNotification, previousNotification));
      else
        RemovePropW(window, kNotification);
      if (handled)
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
  }
  HWND createHost()
  {
    WNDCLASSW nativeClass = {0};
    nativeClass.lpfnWndProc = &HostProc;
    nativeClass.hInstance = GetModuleHandleW(NULL);
    nativeClass.lpszClassName = L"LokaTextEditorTestHost";
    LOKA_VERIFY(RegisterClassW(&nativeClass) || GetLastError() == ERROR_CLASS_ALREADY_EXISTS);
    HWND window = CreateWindowExW(0,
                                  nativeClass.lpszClassName,
                                  L"editor-host",
                                  WS_OVERLAPPED,
                                  0,
                                  0,
                                  640,
                                  480,
                                  NULL,
                                  NULL,
                                  nativeClass.hInstance,
                                  NULL);
    LOKA_VERIFY(window);
    return window;
  }
  /** Native message instrumentation; setter failures live in TestingHooks.cpp. */
  struct Probe
  {
    HWND window;
    WNDPROC previous;
    unsigned lineReads, textReads, sets;
    std::vector<WPARAM> selections;
    bool echo, restoredInsideNotification, seedUndo;
    explicit Probe(HWND value)
        : window(value),
          previous(0),
          lineReads(0),
          textReads(0),
          sets(0),
          echo(false),
          restoredInsideNotification(false),
          seedUndo(false)
    {
      LOKA_VERIFY(SetPropW(window, kProbe, this));
      previous = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&proc)));
      LOKA_VERIFY(previous);
    }
    ~Probe()
    {
      // A retired context has already removed its route and restored EDIT's
      // original procedure. Do not reinstall that retired route here.
      if (IsWindow(window) && Win32TextEditorContext::fromWindow(window))
        SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(previous));
      if (IsWindow(window))
        RemovePropW(window, kProbe);
    }
    static LRESULT CALLBACK proc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
      Probe &probe = *static_cast<Probe *>(GetPropW(window, kProbe));
      if (message == EM_SETSEL)
        probe.selections.push_back(wParam);
      if (message == EM_GETLINE)
        ++probe.lineReads;
      if (message == WM_GETTEXT)
        ++probe.textReads;
      if (message == WM_SETTEXT)
      {
        ++probe.sets;
        if (GetPropW(GetParent(window), kNotification))
          probe.restoredInsideNotification = true;
        if (probe.echo)
          SendMessageW(GetParent(window), WM_COMMAND, MAKEWPARAM(0, EN_CHANGE), reinterpret_cast<LPARAM>(window));
      }
      const LRESULT result = CallWindowProcW(probe.previous, window, message, wParam, lParam);
      if (message == WM_SETTEXT && result && probe.seedUndo)
      {
        probe.seedUndo = false;
        // WM_SETTEXT normally resets EDIT undo itself. Seed a real undo record
        // with unchanged final text to discriminate the explicit empty-undo
        // door independently of that incidental setter behavior. Bypass only
        // the input wrapper, as a native replacement implementation would.
        Win32TextEditorContext *context = Win32TextEditorContext::fromWindow(window);
        LOKA_VERIFY(context);
        WNDPROC native = EditorAccess::nativeProcedure(*context);
        CallWindowProcW(native, window, EM_SETSEL, 0, 0);
        CallWindowProcW(native, window, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L"x"));
        CallWindowProcW(native, window, EM_SETSEL, 0, 1);
        CallWindowProcW(native, window, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L""));
        LOKA_VERIFY(SendMessageW(window, EM_CANUNDO, 0, 0));
      }
      return result;
    }
  };
  class PlainOnlyHighlighter : public LineHighlighter
  {
  public:
    mutable unsigned calls;
    PlainOnlyHighlighter()
        : calls(0)
    {
    }
    virtual bool highlight(const String &line, AttributedString::Builder &out) const
    {
      ++this->calls;
      return out.append(line, Bold);
    }
  };
  struct Fixture : loka::app::testing::TextEditorStateOwner
  {
    HWND host;
    Win32ScenePlatformController controller;
    loka::testing::TextBufferFixture lines;
    TextEditorNode *node;
    Win32TextEditorContext *context;
    Fixture(unsigned short count = 3, const std::string &text = "abcd", unsigned short capacity = 256)
        : host(createHost()),
          controller(host, loka::win32::Win32DisplayScale(96, RailMetrics())),
          lines(),
          node(0),
          context(0)
    {
      LOKA_VERIFY(SetPropW(host, kHostController, &controller));
      RegisterWin32BuiltInSupport(controller);
      LOKA_VERIFY(lines.attach(&tracker, capacity) == ATTACH_OK);
      for (unsigned short i = 0; i < count; ++i)
        LOKA_VERIFY(lines.insert(i, String(text)) == EDIT_OK);
      node = new TextEditorNode(TextEditorProps(lines, cursor).moveCaretTo(request));
      // The initial caret is a fact the seam publishes, not an app request:
      // this rail delivers requests only from PR 3 of #873 onward.
      // Over-capacity fixtures keep the seam's refusal; every other fixture seeds.
      if (count)
      {
        const EditorResult seeded = loka::app::testing::TextEditorAccess::document(*node).moveCaret(
            LineCursor(lines.at(0).id, text.size() < 2 ? static_cast<int>(text.size()) : 2));
        LOKA_VERIFY(seeded == EDITOR_OK || seeded == EDITOR_CAPACITY);
      }
      LayoutState bounds;
      bounds.x = 10;
      bounds.y = 20;
      bounds.width = 300;
      bounds.height = 120;
      bounds.spacing = 4;
      LOKA_VERIFY(controller.prepareProjectedLayout(node, bounds));
      context = static_cast<Win32TextEditorContext *>(node->getContext());
      LOKA_VERIFY(context && context->hwnd() && IsWindowUnicode(context->hwnd()));
    }
    ~Fixture()
    {
      loka::win32::testing::failTextEditorSets(loka::win32::testing::TEXT_EDITOR_SET_REFUSED, 0);
      delete node;
      controller.drainNativeRetirements();
      RemovePropW(host, kHostController);
      LOKA_VERIFY(DestroyWindow(host));
    }
    void type(wchar_t value)
    {
      if (value == L'\b')
      {
        SendMessageW(context->hwnd(), WM_CHAR, value, 1);
        return;
      }
      DWORD start = 0, end = 0;
      SendMessageW(context->hwnd(), EM_GETSEL, reinterpret_cast<WPARAM>(&start), reinterpret_cast<LPARAM>(&end));
      SendMessageW(context->hwnd(), EM_SETSEL, start, end);
      const wchar_t text[] = {value, 0};
      // EDIT sends EN_CHANGE to HostProc for replacement; multiline WM_SETTEXT
      // is a projection operation and deliberately sends no input notification.
      SendMessageW(context->hwnd(), EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(value == L'\r' ? L"\r\n" : text));
    }
    std::wstring native() const
    {
      std::wstring result;
      loka::win32::ReadEditTextWide(context->hwnd(), result);
      return result;
    }
    std::wstring committedNative() const
    {
      std::string logical;
      std::wstring desired;
      LOKA_VERIFY(loka::app::testing::TextEditorAccess::document(*node).project(logical) == EDITOR_OK);
      LOKA_VERIFY(loka::win32::TextEditorToWide(logical, desired));
      return desired;
    }
    void matches() const
    {
      const std::wstring desired = committedNative();
      LOKA_VERIFY(native() == desired);
      LOKA_VERIFY(GetWindowTextLengthW(context->hwnd()) == static_cast<int>(desired.size()));
    }
  };
  /** Observe the real derived cleanup at the hook/base boundary, before queueing. */
  class RetirementOrderEditor : public Win32TextEditorContext
  {
  public:
    RetirementOrderEditor(Fixture &fixture, const LayoutState &bounds)
        : Win32TextEditorContext(&fixture.controller, fixture.host, bounds,
                                 fixture.node, EditorAccess::key(*fixture.context))
    {
    }

  protected:
    virtual void onNativeFact(NodeLifecycleFact previous, NodeLifecycleFact next)
    {
      const bool borrowed = this->controller()->borrowPhase().open();
      Win32TextEditorContext::onNativeFact(previous, next);
      if (next == NODE_FACT_RETIRED)
      {
        // Both moved steps have completed, but the base has not stripped or
        // queued the HWND yet. Moving retireWindow back into the hook fails.
        LOKA_VERIFY(EditorAccess::node(*this) == 0);
        LOKA_VERIFY(this->controller()->borrowPhase().open() == borrowed);
        LOKA_VERIFY(this->hwnd() && IsWindow(this->hwnd()));
        LOKA_VERIFY(GetWindowLongPtrW(this->hwnd(), GWLP_USERDATA) == reinterpret_cast<LONG_PTR>(this));
        LOKA_VERIFY(!Win32TextEditorContext::fromWindow(this->hwnd()));
        LOKA_VERIFY(reinterpret_cast<WNDPROC>(GetWindowLongPtrW(this->hwnd(), GWLP_WNDPROC))
                    == EditorAccess::nativeProcedure(*this));
        LOKA_VERIFY(!(GetWindowLongPtrW(this->hwnd(), GWL_STYLE) & WS_VISIBLE));
        LOKA_VERIFY(GetWindowLongPtrW(this->hwnd(), GWL_STYLE) & ES_READONLY);
      }
    }
  };

  void verifyBaseOwnedRetirement()
  {
    Fixture fixture;
    LayoutState bounds;
    bounds.x = 0;
    bounds.y = 0;
    bounds.width = 180;
    bounds.height = 80;
    RetirementOrderEditor context(fixture, bounds);
    Win32RetirableContext &base = context;
    const HWND child = base.hwnd();
    LOKA_VERIFY(child && IsWindow(child));
    LOKA_VERIFY(Win32TextEditorContext::fromWindow(child) == &context);
    // TextEditor routes via a property; seed USERDATA to discriminate the
    // base's unconditional stripping as well as its deferred destruction.
    SetWindowLongPtrW(child, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&context));
    base.onFactChanged(NODE_FACT_ATTACHED, NODE_FACT_RETIRED);
    LOKA_VERIFY(!base.hwnd() && !EditorAccess::node(context));
    LOKA_VERIFY(!fixture.controller.borrowPhase().open());
    LOKA_VERIFY(IsWindow(child) && GetWindowLongPtrW(child, GWLP_USERDATA) == 0);
    fixture.controller.drainNativeRetirements();
    LOKA_VERIFY(!IsWindow(child));
  }

  void expectSelection(const Fixture &fixture, DWORD expected)
  {
    DWORD start = 0, end = 0;
    SendMessageW(fixture.context->hwnd(), EM_GETSEL, reinterpret_cast<WPARAM>(&start), reinterpret_cast<LPARAM>(&end));
    LOKA_VERIFY(start == expected && end == expected);
  }
  /** Finite reposts, with nested props deliveries that must not consume them. */
  class RequestOnReport
  {
  public:
    RequestOnReport(Fixture &fixture, LineCursor next, LineCursor last = LineCursor::None())
        : fixture_(fixture), next_(next), last_(last), reports_()
    {
      this->fixture_.cursor.state()->bind(&changed, this, false);
    }
    ~RequestOnReport()
    {
      this->fixture_.cursor.state()->unbind(&changed, this);
    }
    const std::vector<LineCursor> &reports() const
    {
      return this->reports_;
    }

  private:
    static void changed(void *data)
    {
      RequestOnReport &self = *static_cast<RequestOnReport *>(data);
      self.reports_.push_back(self.fixture_.cursor.state()->get());
      if (self.next_.isNone())
        return;
      const LineCursor next = self.next_;
      self.next_ = self.last_;
      self.last_ = LineCursor::None();
      StateTrackerGuard guard(&self.fixture_.tracker);
      self.fixture_.request.set(next);
      self.fixture_.context->onPropsApplied();
      LOKA_VERIFY(self.fixture_.request.get() == next);
    }
    Fixture &fixture_;
    LineCursor next_;
    LineCursor last_;
    std::vector<LineCursor> reports_;
  };
  /** Repost on every report so a duplicated entry completion is observable. */
  class RequestOnEveryReport
  {
  public:
    explicit RequestOnEveryReport(Fixture &fixture)
        : fixture_(fixture), reports_()
    {
      this->fixture_.cursor.state()->bind(&changed, this, false);
    }
    ~RequestOnEveryReport()
    {
      this->fixture_.cursor.state()->unbind(&changed, this);
    }
    const std::vector<LineCursor> &reports() const
    {
      return this->reports_;
    }

  private:
    static void changed(void *data)
    {
      RequestOnEveryReport &self = *static_cast<RequestOnEveryReport *>(data);
      const LineCursor reported = self.fixture_.cursor.state()->get();
      self.reports_.push_back(reported);
      // Fail an unbounded mutation promptly instead of exhausting the test host.
      LOKA_VERIFY(self.reports_.size() <= 4);
      const LineCursor next(reported.line, reported.column == 0 ? 1 : 0);
      StateTrackerGuard guard(&self.fixture_.tracker);
      self.fixture_.request.set(next);
      self.fixture_.context->onPropsApplied();
      LOKA_VERIFY(self.fixture_.request.get() == next);
    }
    Fixture &fixture_;
    std::vector<LineCursor> reports_;
  };
  /** Observe both real publications while posting between delete and insert. */
  class RequestBetweenChanges
  {
  public:
    explicit RequestBetweenChanges(Fixture &fixture)
        : fixture_(fixture), publications_()
    {
      const_cast<State<ListRevision> &>(this->fixture_.lines.revision()).bind(&changed, this, false);
    }
    ~RequestBetweenChanges()
    {
      const_cast<State<ListRevision> &>(this->fixture_.lines.revision()).unbind(&changed, this);
    }
    const std::vector<std::wstring> &publications() const
    {
      return this->publications_;
    }

  private:
    static void changed(void *data)
    {
      RequestBetweenChanges &self = *static_cast<RequestBetweenChanges *>(data);
      self.publications_.push_back(self.fixture_.native());
      const LineCursor next(self.fixture_.lines.at(0).id, 0);
      if (self.publications_.size() == 1)
      {
        StateTrackerGuard guard(&self.fixture_.tracker);
        self.fixture_.request.set(next);
      }
      self.fixture_.context->onPropsApplied();
      LOKA_VERIFY(self.fixture_.request.get() == next);
    }
    Fixture &fixture_;
    std::vector<std::wstring> publications_;
  };
  void printUndoText(const char *label, const std::wstring &wide, const std::string &before)
  {
    // Escape UTF-16 units so non-ASCII and line endings survive console encoding.
    std::printf("[undo failure] %s UTF-16=", label);
    for (std::size_t i = 0; i < wide.size(); ++i)
    {
      const unsigned unit = static_cast<unsigned>(wide[i]);
      if (unit >= 32 && unit < 127 && unit != '\\')
        std::putchar(static_cast<int>(unit));
      else
        std::printf("\\u%04x", unit);
    }
    std::printf("\n");
    std::string logical;
    if (!loka::win32::TextEditorFromWide(wide.data(), wide.size(), logical))
    {
      std::printf("[undo failure] %s diff unavailable: non-ASCII/native conversion refused\n", label);
      return;
    }
    const loka::app::TextEditorLineDiff diff = loka::app::DiffTextEditorLines(before, logical);
    const char *kind = diff.before() == 0 && diff.after() == 0   ? "unchanged"
                       : diff.before() == 1 && diff.after() == 1 ? "single-line"
                       : diff.before() == 1 && diff.after() == 2 ? "split-candidate"
                       : diff.before() == 2 && diff.after() == 1 ? "join-candidate"
                                                                 : "range";
    std::printf("[undo failure] %s diff first=%d last-before=%d last-after=%d before=%d after=%d kind=%s\n",
                label,
                diff.first(),
                diff.before() ? diff.first() + diff.before() - 1 : -1,
                diff.after() ? diff.first() + diff.after() - 1 : -1,
                diff.before(),
                diff.after(),
                kind);
  }
  void paste(HWND window, const wchar_t *text)
  {
    const std::wstring value(text);
    HGLOBAL storage = GlobalAlloc(GMEM_MOVEABLE, (value.size() + 1) * sizeof(wchar_t));
    LOKA_VERIFY(storage);
    wchar_t *target = static_cast<wchar_t *>(GlobalLock(storage));
    LOKA_VERIFY(target);
    for (std::size_t i = 0; i <= value.size(); ++i)
      target[i] = value.c_str()[i];
    GlobalUnlock(storage);
    LOKA_VERIFY(OpenClipboard(window));
    LOKA_VERIFY(EmptyClipboard());
    LOKA_VERIFY(SetClipboardData(CF_UNICODETEXT, storage));
    LOKA_VERIFY(CloseClipboard());
    SendMessageW(window, WM_PASTE, 0, 0);
  }
  struct Snapshot
  {
    ListRevision revision;
    LineCursor cursor;
    std::string text;
    std::vector<ItemId> ids;
    explicit Snapshot(const Fixture &fixture)
        : revision(fixture.lines.revision().get()),
          cursor(fixture.cursor.state()->get())
    {
      LOKA_VERIFY(loka::app::testing::TextEditorAccess::document(*fixture.node).project(text) == EDITOR_OK);
      for (unsigned short i = 0; i < fixture.lines.size(); ++i)
        ids.push_back(fixture.lines.at(i).id);
    }
    void unchanged(const Fixture &fixture) const
    {
      LOKA_VERIFY(!(revision != fixture.lines.revision().get()) && cursor == fixture.cursor.state()->get());
      std::string actual;
      LOKA_VERIFY(loka::app::testing::TextEditorAccess::document(*fixture.node).project(actual) == EDITOR_OK
                  && text == actual);
      LOKA_VERIFY(ids.size() == fixture.lines.size());
      for (unsigned short i = 0; i < fixture.lines.size(); ++i)
        LOKA_VERIFY(ids[i] == fixture.lines.at(i).id);
    }
  };
  struct Observer
  {
    Fixture &fixture;
    unsigned notifications, settled;
    bool nested, deferred;
    explicit Observer(Fixture &value)
        : fixture(value),
          notifications(0),
          settled(0),
          nested(false),
          deferred(false)
    {
      const_cast<State<ListRevision> &>(fixture.lines.revision()).bind(&changed, this, false);
    }
    ~Observer()
    {
      const_cast<State<ListRevision> &>(fixture.lines.revision()).unbind(&changed, this);
    }
    static void changed(void *data)
    {
      Observer &observer = *static_cast<Observer *>(data);
      ++observer.notifications;
      LOKA_VERIFY(observer.fixture.controller.borrowPhase().open());
      const PaintQuery query = {Win32RetirableContext::paintScope(), PLACEMENT_ELIGIBLE};
      const PaintAnswer answer = observer.fixture.context->queryPaintDamage(query);
      LOKA_VERIFY(answer.kind == PAINT_ANSWER_EXACT);
      loka::core::testing::PushStateTrackerTestAccess::defer(observer.fixture.tracker, &flushed, &observer);
      if (observer.nested)
      {
        observer.nested = false;
        const Snapshot before(observer.fixture);
        observer.fixture.type(L'Z');
        before.unchanged(observer.fixture);
      }
    }
    static void flushed(void *data)
    {
      Observer &observer = *static_cast<Observer *>(data);
      ++observer.settled;
      LOKA_VERIFY(observer.fixture.controller.borrowPhase().open());
      if (observer.deferred)
      {
        observer.deferred = false;
        const Snapshot before(observer.fixture);
        observer.fixture.type(L'Z');
        before.unchanged(observer.fixture);
      }
    }
  };
  /** Force a post-report repair, including selection from the new committed fact. */
  class EditOnReply
  {
  public:
    explicit EditOnReply(Fixture &fixture)
        : fixture_(fixture)
    {
      fixture.request.reply().state()->bind(&changed, this, false);
    }
    ~EditOnReply()
    {
      this->fixture_.request.reply().state()->unbind(&changed, this);
    }

  private:
    static void changed(void *data)
    {
      Fixture &fixture = static_cast<EditOnReply *>(data)->fixture_;
      const ItemId line = fixture.lines.at(0).id;
      StateTrackerGuard guard(&fixture.tracker);
      LOKA_VERIFY(fixture.lines.update(line, String("changed")) == EDIT_OK);
      LOKA_VERIFY(loka::app::testing::TextEditorAccess::document(*fixture.node).moveCaret(LineCursor(line, 5))
                  == EDITOR_OK);
      fixture.context->onPropsApplied();
    }
    Fixture &fixture_;
  };
  void detachOnReply(void *data)
  {
    NotifySubtreeNodeDetached(static_cast<Fixture *>(data)->node);
  }
  void testWin32TextEditorSettlement()
  {
    typedef loka::app::testing::SettleTrace<LineCursor> Trace;
    {
      Fixture fixture(1);
      const String text = String::FromPlatform(Managed<loka::platform::String>::Wrap(
          new loka::app::testing::TextEditorReportRefusal(fixture.request)));
      LOKA_VERIFY(fixture.lines.update(fixture.lines.at(0).id, text) == EDIT_OK);
      fixture.context->onPropsApplied();
      const LineCursor before = fixture.cursor.state()->get();
      Probe probe(fixture.context->hwnd());
      {
        StateTrackerGuard guard(&fixture.tracker);
        fixture.request.set(LineCursor(before.line, 4));
      }
      fixture.context->onPropsApplied();
      const Reply<LineCursor> reply = fixture.request.reply().state()->get();
      LOKA_VERIFY(reply.kind() == Reply<LineCursor>::REFUSED && reply.reason() == EDITOR_ALLOCATION);
      LOKA_VERIFY(fixture.cursor.state()->get() == before);
      // Positive native-apply control before checking the repaired final selection.
      LOKA_VERIFY(!probe.selections.empty() && probe.selections.front() == 4);
      expectSelection(fixture, static_cast<DWORD>(before.column));
      // Selection-only repair must preserve EDIT undo/text rather than replace them.
      LOKA_VERIFY(probe.sets == 0);
    }
    {
      Fixture fixture;
      Trace &trace = Trace::instance();
      trace.clear();
      Probe probe(fixture.context->hwnd());
      const LineCursor requested(fixture.lines.at(1).id, 99);
      {
        StateTrackerGuard guard(&fixture.tracker);
        fixture.request.set(requested);
      }
      fixture.context->onPropsApplied();
      const Reply<LineCursor> reply = fixture.request.reply().state()->get();
      LOKA_VERIFY(reply.kind() == Reply<LineCursor>::CLAMPED);
      LOKA_VERIFY(reply.requested() == requested && reply.applied() == LineCursor(requested.line, 4));
      LOKA_VERIFY(trace.size() == 1);
      LOKA_VERIFY(trace.at(0).stimulus == SETTLE_PROPS && trace.at(0).count == 1);
      LOKA_VERIFY(trace.at(0).takes[0].kind() == Reply<LineCursor>::CLAMPED && trace.at(0).seam[0] == EDITOR_OK);
      LOKA_VERIFY(trace.at(0).before == LineCursor(fixture.lines.at(0).id, 2));
      LOKA_VERIFY(trace.at(0).after == reply.applied());
      // Positive control precedes the empty-settle observation.
      LOKA_VERIFY(!probe.selections.empty());
      trace.clear();
      probe.selections.clear();
      fixture.context->onPropsApplied();
      LOKA_VERIFY(trace.size() == 0 && probe.selections.empty());
      const PaintQuery query = {Win32RetirableContext::paintScope(), PLACEMENT_ELIGIBLE};
      LOKA_VERIFY(fixture.context->queryPaintDamage(query).kind == PAINT_ANSWER_EXACT);
      // Use a real removed identity, then reconcile before posting it.
      const LineCursor stale(fixture.lines.at(2).id, 1);
      LOKA_VERIFY(fixture.lines.remove(stale.line) == EDIT_OK);
      fixture.context->onPropsApplied();
      const LineCursor before = fixture.cursor.state()->get();
      trace.clear();
      probe.selections.clear();
      {
        StateTrackerGuard guard(&fixture.tracker);
        fixture.request.set(stale);
      }
      fixture.context->onPropsApplied();
      const Reply<LineCursor> refused = fixture.request.reply().state()->get();
      LOKA_VERIFY(refused.kind() == Reply<LineCursor>::REFUSED && refused.reason() == EDITOR_STALE_ID);
      LOKA_VERIFY(refused.requested() == stale && fixture.cursor.state()->get() == before);
      LOKA_VERIFY(trace.size() == 1 && trace.at(0).count == 1 && trace.at(0).before == trace.at(0).after);
      LOKA_VERIFY(trace.at(0).seam[0] == EDITOR_STALE_ID && probe.selections.empty());
    }
    // Predicted SetTimer-failure pin: an outside-input NON_ASCII rejection with
    // a queued request must end with request None, Refused(EDITOR_UNAVAILABLE),
    // unchanged cursor, and one SETTLE_INPUT trace row containing one refusal.
    // The VM fixture cannot inject SetTimer allocation failure. The FAIL_ARM
    // Null SettlementProbe in testTextEditorSettlementSeam discriminates that
    // common-driver path; the cases below inject native text replacement failure.
    // Reentrancy variant (also predicted): on None, repost B and call onPropsApplied.
    // COMMIT must leave B pending until this Refused(A) has been published; no
    // nested Granted(B), fact write, or extra settlement trace row may occur.
    for (int failRestore = 0; failRestore < 2; ++failRestore)
    {
      Fixture fixture;
      Trace &trace = Trace::instance();
      trace.clear();
      const LineCursor before = fixture.cursor.state()->get();
      const LineCursor requested(fixture.lines.at(1).id, 1);
      if (failRestore)
        loka::win32::testing::failTextEditorSets(loka::win32::testing::TEXT_EDITOR_SET_REFUSED, 1);
      {
        StateTrackerGuard guard(&fixture.tracker);
        fixture.request.set(requested);
      }
      fixture.type(static_cast<wchar_t>(0xff21));
      LOKA_VERIFY(fixture.request.get().isNone());
      LOKA_VERIFY(trace.size() == 1 && trace.at(0).stimulus == SETTLE_INPUT && trace.at(0).count == 1);
      const Reply<LineCursor> reply = fixture.request.reply().state()->get();
      if (failRestore)
      {
        LOKA_VERIFY(reply.kind() == Reply<LineCursor>::REFUSED && reply.reason() == EDITOR_UNAVAILABLE);
        LOKA_VERIFY(fixture.cursor.state()->get() == before && trace.at(0).before == trace.at(0).after);
        // finishTake releases COMMIT; only finishSettle restores the retry scope.
        LOKA_VERIFY(EditorAccess::pending(*fixture.context));
        SendMessageW(fixture.context->hwnd(), WM_TIMER, 853, 0);
        LOKA_VERIFY(!EditorAccess::pending(*fixture.context) && EditorAccess::status(*fixture.context) == EDITOR_OK);
        LOKA_VERIFY(trace.size() == 1);
      }
      else
      {
        LOKA_VERIFY(reply.kind() == Reply<LineCursor>::GRANTED && reply.applied() == requested);
        LOKA_VERIFY(fixture.cursor.state()->get() == requested);
        expectSelection(fixture, 7);
      }
    }
    {
      Fixture fixture;
      fixture.request.reply().state()->bind(&detachOnReply, &fixture, false);
      loka::win32::testing::failTextEditorSets(loka::win32::testing::TEXT_EDITOR_SET_REFUSED, 1);
      {
        StateTrackerGuard guard(&fixture.tracker);
        fixture.request.set(LineCursor(fixture.lines.at(1).id, 1));
      }
      fixture.type(static_cast<wchar_t>(0xff21));
      fixture.request.reply().state()->unbind(&detachOnReply, &fixture);
      LOKA_VERIFY(fixture.node->lifecycleFact() == NODE_FACT_DETACHED_RETAINED);
      LOKA_VERIFY(!EditorAccess::pending(*fixture.context));
      Probe probe(fixture.context->hwnd());
      SendMessageW(fixture.context->hwnd(), WM_TIMER, 853, 0);
      LOKA_VERIFY(probe.sets == 0);
    }
    {
      Fixture fixture;
      NotifySubtreeNodeDetached(fixture.node);
      // Notify only changes logical facts. A mounted Scene delivers their diff
      // at apply; this bare-node fixture must drive that same delivery walk.
      LifecycleFactTestAccess::DeliverFacts(fixture.node);
      LOKA_VERIFY(EditorAccess::status(*fixture.context) == EDITOR_UNAVAILABLE);
      LOKA_VERIFY(!(GetWindowLongPtrW(fixture.context->hwnd(), GWL_STYLE) & WS_VISIBLE));
      fixture.request.reply().state()->bind(&detachOnReply, &fixture, false);
      const LineCursor requested(fixture.lines.at(1).id, 1);
      Trace &trace = Trace::instance();
      trace.clear();
      {
        StateTrackerGuard guard(&fixture.tracker);
        fixture.request.set(requested);
      }
      NotifySubtreeNodeAttached(fixture.node);
      LOKA_VERIFY(fixture.node->lifecycleFact() == NODE_FACT_ATTACHED);
      LOKA_VERIFY(fixture.request.get() == requested && trace.size() == 0);
      LifecycleFactTestAccess::DeliverFacts(fixture.node);
      fixture.request.reply().state()->unbind(&detachOnReply, &fixture);
      LOKA_VERIFY(fixture.node->lifecycleFact() == NODE_FACT_DETACHED_RETAINED);
      LOKA_VERIFY(fixture.request.get().isNone());
      LOKA_VERIFY(trace.size() == 1 && trace.at(0).stimulus == SETTLE_ATTACH && trace.at(0).count == 1);
      LOKA_VERIFY(trace.at(0).takes[0].kind() == Reply<LineCursor>::GRANTED);
      // Host visibility is irrelevant: inspect the child's own visible style.
      LOKA_VERIFY(!(GetWindowLongPtrW(fixture.context->hwnd(), GWL_STYLE) & WS_VISIBLE));
    }
    {
      Fixture fixture;
      EditOnReply edit(fixture);
      {
        StateTrackerGuard guard(&fixture.tracker);
        fixture.request.set(LineCursor(fixture.lines.at(1).id, 1));
      }
      fixture.context->onPropsApplied();
      fixture.matches();
      LOKA_VERIFY(fixture.cursor.state()->get() == LineCursor(fixture.lines.at(0).id, 5));
      expectSelection(fixture, 5);
    }
  }
  /** Reentrant native input forces finishTake to repair the projection. */
  struct CommandRepair
  {
    Fixture &fixture;
    std::vector<LRESULT> visible;
    explicit CommandRepair(Fixture &value)
        : fixture(value)
    {
      this->fixture.cursor.state()->bind(&changed, this, false);
    }
    ~CommandRepair()
    {
      this->fixture.cursor.state()->unbind(&changed, this);
    }
    static void changed(void *data)
    {
      CommandRepair &self = *static_cast<CommandRepair *>(data);
      const HWND window = self.fixture.context->hwnd();
      self.visible.push_back(SendMessageW(window, EM_GETFIRSTVISIBLELINE, 0, 0));
      SendMessageW(window, WM_KEYDOWN, VK_RIGHT, 1);
    }
  };
  void testWin32TextEditorCommands()
  {
    typedef loka::app::testing::SettleTrace<EditorCommand, LineCursor> Trace;
    typedef loka::app::testing::SettleTraceCapture<LineCursor> Capture;
    // Both ordinary completion and a post-report projection repair must retain
    // the page viewport. The latter discriminates the finishTake cache refresh.
    for (int repair = 0; repair < 2; ++repair)
    {
      Fixture fixture(128);
      const HWND window = fixture.context->hwnd();
      LOKA_VERIFY((NodePropsApplier<TextEditorNode, TextEditorProps>::apply(
          fixture.node,
          TextEditorProps(fixture.lines, fixture.cursor).moveCaretTo(fixture.request).command(fixture.commands))));
      RECT rect = {0};
      SendMessageW(window, EM_GETRECT, 0, reinterpret_cast<LPARAM>(&rect));
      HDC dc = GetDC(window);
      LOKA_VERIFY(dc);
      const HFONT font = reinterpret_cast<HFONT>(SendMessageW(window, WM_GETFONT, 0, 0));
      LOKA_VERIFY(font);
      const HGDIOBJ old = SelectObject(dc, font);
      TEXTMETRICW metrics = {0};
      LOKA_VERIFY(GetTextMetricsW(dc, &metrics));
      SelectObject(dc, old);
      ReleaseDC(window, dc);
      const LONG height = metrics.tmHeight + metrics.tmExternalLeading;
      LOKA_VERIFY(height > 0);
      const unsigned visible = static_cast<unsigned>((rect.bottom - rect.top) / height);
      LOKA_VERIFY(visible > 1 && 2 * (visible - 1) < fixture.lines.size());
      const unsigned short target = static_cast<unsigned short>(2 * (visible - 1));
      Capture::clear();
      {
        StateTrackerGuard guard(&fixture.tracker);
        LOKA_VERIFY(fixture.commands.post(EditorCommand(EditorCommand::PAGE_DOWN)) == POST_ACCEPTED);
        LOKA_VERIFY(fixture.commands.post(EditorCommand(EditorCommand::PAGE_DOWN)) == POST_ACCEPTED);
      }
      if (repair)
      {
        CommandRepair observer(fixture);
        fixture.context->onPropsApplied();
        LOKA_VERIFY(observer.visible.size() == 2 && observer.visible.back() > 0);
        LOKA_VERIFY(SendMessageW(window, EM_GETFIRSTVISIBLELINE, 0, 0) == observer.visible.back());
        LOKA_VERIFY(EditorAccess::restores(*fixture.context) == 0);
      }
      else
        fixture.context->onPropsApplied();
      const Trace &trace = Trace::instance();
      LOKA_VERIFY(trace.size() == 1 && trace.at(0).count == 2);
      for (unsigned i = 0; i < 2; ++i)
      {
        LOKA_VERIFY(trace.at(0).takes[i].kind() == Reply<EditorCommand>::GRANTED);
        LOKA_VERIFY(trace.at(0).takes[i].requested().kind() == EditorCommand::PAGE_DOWN);
        LOKA_VERIFY(trace.at(0).seam[i] == EDITOR_OK);
      }
      LOKA_VERIFY(fixture.commands.state()->get().isNone() && fixture.commands.pending() == 0);
      LOKA_VERIFY(fixture.cursor.state()->get() == LineCursor(fixture.lines.at(target).id, 2));
      const LRESULT offset = SendMessageW(window, EM_LINEINDEX, static_cast<WPARAM>(target), 0);
      LOKA_VERIFY(offset >= 0);
      expectSelection(fixture, static_cast<DWORD>(offset + 2));
      const LRESULT first = SendMessageW(window, EM_GETFIRSTVISIBLELINE, 0, 0);
      std::fprintf(stderr,
                   "commands: repair=%d first=%ld target=%u visible=%u rect=%ld..%ld height=%ld lines=%u\n",
                   repair, static_cast<long>(first), static_cast<unsigned>(target), visible,
                   static_cast<long>(rect.top), static_cast<long>(rect.bottom), static_cast<long>(height),
                   static_cast<unsigned>(fixture.lines.size()));
      LOKA_VERIFY(first > 0 && first <= target && target < first + static_cast<LRESULT>(visible));
      fixture.matches();
      Capture::clear();
    }
    // Geometry edges on a fresh, unscrolled control: EDIT keeps a formatting
    // frame shorter than scrolled-in text, so these cannot follow a page move.
    {
      Fixture fixture(128);
      const HWND window = fixture.context->hwnd();
      LOKA_VERIFY((NodePropsApplier<TextEditorNode, TextEditorProps>::apply(
          fixture.node,
          TextEditorProps(fixture.lines, fixture.cursor).moveCaretTo(fixture.request).command(fixture.commands))));
      const LineCursor start = fixture.cursor.state()->get();
      RECT rect = {0};
      SendMessageW(window, EM_GETRECT, 0, reinterpret_cast<LPARAM>(&rect));
      HDC dc = GetDC(window);
      LOKA_VERIFY(dc);
      const HGDIOBJ old = SelectObject(dc, reinterpret_cast<HFONT>(SendMessageW(window, WM_GETFONT, 0, 0)));
      TEXTMETRICW metrics = {0};
      LOKA_VERIFY(GetTextMetricsW(dc, &metrics));
      SelectObject(dc, old);
      ReleaseDC(window, dc);
      const LONG height = metrics.tmHeight + metrics.tmExternalLeading;
      // A positive-height formatting frame with no complete line declines.
      rect.bottom = rect.top + height - 1;
      SendMessageW(window, EM_SETRECTNP, 0, reinterpret_cast<LPARAM>(&rect));
      RECT took = {0};
      SendMessageW(window, EM_GETRECT, 0, reinterpret_cast<LPARAM>(&took));
      if (took.bottom - took.top >= height)
        std::fprintf(stderr, "[skip] EDIT kept a %ld px formatting frame; the decline branch is not pinned here\n",
                     static_cast<long>(took.bottom - took.top));
      else
      {
        Capture::clear();
        {
          StateTrackerGuard guard(&fixture.tracker);
          LOKA_VERIFY(fixture.commands.post(EditorCommand(EditorCommand::PAGE_DOWN)) == POST_ACCEPTED);
        }
        fixture.context->onPropsApplied();
        LOKA_VERIFY(fixture.commands.reply().state()->get().kind() == Reply<EditorCommand>::REFUSED);
        LOKA_VERIFY(fixture.commands.reply().state()->get().reason() == EDITOR_UNAVAILABLE);
        LOKA_VERIFY(fixture.cursor.state()->get() == start);
      }
      // One complete line plus a partial line is a one-line page (step one).
      rect.bottom = rect.top + height + height / 2;
      SendMessageW(window, EM_SETRECTNP, 0, reinterpret_cast<LPARAM>(&rect));
      Capture::clear();
      {
        StateTrackerGuard guard(&fixture.tracker);
        LOKA_VERIFY(fixture.commands.post(EditorCommand(EditorCommand::PAGE_DOWN)) == POST_ACCEPTED);
      }
      fixture.context->onPropsApplied();
      LOKA_VERIFY(fixture.commands.reply().state()->get().kind() == Reply<EditorCommand>::GRANTED);
      LOKA_VERIFY(fixture.cursor.state()->get() == LineCursor(fixture.lines.at(1).id, start.column));
      Capture::clear();
    }
    {
      Fixture fixture(128);
      LOKA_VERIFY((NodePropsApplier<TextEditorNode, TextEditorProps>::apply(
          fixture.node,
          TextEditorProps(fixture.lines, fixture.cursor).moveCaretTo(fixture.queue).command(fixture.commands))));
      Capture::clear();
      {
        StateTrackerGuard guard(&fixture.tracker);
        for (unsigned short row = 1; row <= 3; ++row)
          LOKA_VERIFY(fixture.queue.post(LineCursor(fixture.lines.at(row).id, 2)) == POST_ACCEPTED);
        LOKA_VERIFY(fixture.commands.post(EditorCommand(EditorCommand::PAGE_DOWN)) == POST_ACCEPTED);
      }
      fixture.context->onPropsApplied();
      LOKA_VERIFY(Trace::instance().size() == 0);
      LOKA_VERIFY(!fixture.commands.state()->get().isNone());
      LOKA_VERIFY(fixture.cursor.state()->get() == LineCursor(fixture.lines.at(2).id, 2));
      fixture.context->onPropsApplied();
      LOKA_VERIFY(Trace::instance().size() == 1 && Trace::instance().at(0).count == 1);
      LOKA_VERIFY(Trace::instance().at(0).takes[0].kind() == Reply<EditorCommand>::GRANTED);
      LOKA_VERIFY(fixture.commands.state()->get().isNone());
      LOKA_VERIFY(fixture.lines.find(fixture.cursor.state()->get().line) > 3);
      Capture::clear();
    }
  }
  void testWin32TextEditorCaretRequests()
  {
    {
      Fixture fixture;
      LOKA_VERIFY(fixture.request.get().isNone());
      expectSelection(fixture, 2);
      const LineCursor requested(fixture.lines.at(1).id, 99);
      {
        StateTrackerGuard guard(&fixture.tracker);
        fixture.request.set(requested);
      }
      fixture.context->onPropsApplied();
      expectSelection(fixture, 10);
      LOKA_VERIFY(fixture.cursor.state()->get() == LineCursor(requested.line, 4));
      LOKA_VERIFY(fixture.request.get().isNone());
      Probe probe(fixture.context->hwnd());
      fixture.context->onPropsApplied();
      LOKA_VERIFY(probe.selections.empty());
    }
    {
      Fixture fixture;
      const LineCursor first(fixture.lines.at(0).id, 1), next(fixture.lines.at(1).id, 3);
      RequestOnReport repost(fixture, next);
      {
        StateTrackerGuard guard(&fixture.tracker);
        fixture.request.set(first);
      }
      fixture.context->onPropsApplied();
      LOKA_VERIFY(repost.reports().size() == 2);
      LOKA_VERIFY(repost.reports()[0] == first && repost.reports()[1] == next);
      LOKA_VERIFY(fixture.request.get().isNone() && fixture.cursor.state()->get() == next);
      expectSelection(fixture, 9);
    }
    {
      Fixture fixture;
      const LineCursor first(fixture.lines.at(0).id, 1), second(fixture.lines.at(1).id, 2),
          third(fixture.lines.at(2).id, 3);
      RequestOnReport repost(fixture, second, third);
      {
        StateTrackerGuard guard(&fixture.tracker);
        fixture.request.set(first);
      }
      fixture.context->onPropsApplied();
      // Predicted: the unbounded consumer reports all three in this one site.
      LOKA_VERIFY(repost.reports().size() == 2 && fixture.request.get() == third);
      LOKA_VERIFY(fixture.cursor.state()->get() == second);
      expectSelection(fixture, 8);
      fixture.context->onPropsApplied();
      LOKA_VERIFY(repost.reports().size() == 3 && fixture.request.get().isNone());
      LOKA_VERIFY(fixture.cursor.state()->get() == third);
      expectSelection(fixture, 15);
    }
    {
      Fixture fixture;
      const LineCursor next(fixture.lines.at(1).id, 1);
      RequestOnReport repost(fixture, next);
      SendMessageW(fixture.context->hwnd(), WM_KEYDOWN, VK_LEFT, 1);
      LOKA_VERIFY(fixture.request.get().isNone() && fixture.cursor.state()->get() == next);
      expectSelection(fixture, 7);
    }
    {
      Fixture fixture;
      const LineCursor next(fixture.lines.at(1).id, 1);
      {
        StateTrackerGuard guard(&fixture.tracker);
        fixture.request.set(next);
      }
      LOKA_VERIFY(SetWindowTextW(fixture.context->hwnd(), L"abxcd\r\nabcd\r\nabcd"));
      SendMessageW(fixture.host, WM_COMMAND, MAKEWPARAM(0, EN_CHANGE), reinterpret_cast<LPARAM>(fixture.context->hwnd()));
      LOKA_VERIFY(fixture.lines.at(0).value.equals(String("abxcd")));
      LOKA_VERIFY(fixture.request.get().isNone() && fixture.cursor.state()->get() == next);
      expectSelection(fixture, 8);
    }
    {
      Fixture fixture;
      Observer observer(fixture);
      RequestBetweenChanges between(fixture);
      SendMessageW(fixture.context->hwnd(), EM_SETSEL, 1, 3);
      SendMessageW(fixture.context->hwnd(), EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L"Q"));
      LOKA_VERIFY(between.publications().size() == 2);
      LOKA_VERIFY(between.publications()[0] == L"ad\r\nabcd\r\nabcd");
      LOKA_VERIFY(between.publications()[1] == L"aQd\r\nabcd\r\nabcd");
      LOKA_VERIFY(observer.notifications == 2 && observer.settled == 2);
      LOKA_VERIFY(fixture.request.get().isNone());
      LOKA_VERIFY(fixture.cursor.state()->get() == LineCursor(fixture.lines.at(0).id, 0));
      expectSelection(fixture, 0);
      LOKA_VERIFY(SendMessageW(fixture.context->hwnd(), EM_CANUNDO, 0, 0));
      fixture.matches();
    }
  }
  void testWin32TextEditorEntryDeliveryBound()
  {
    // Stale props, rejected input, RETRY through props, and RETRY timer are
    // distinct entries, each with one completion and a two-take budget.
    for (int entry = 0; entry < 4; ++entry)
    {
      Fixture fixture;
      if (entry >= 2)
      {
        loka::win32::testing::failTextEditorSets(loka::win32::testing::TEXT_EDITOR_SET_REFUSED, 1);
        fixture.type(static_cast<wchar_t>(0xff21));
        LOKA_VERIFY(EditorAccess::pending(*fixture.context));
      }
      RequestOnEveryReport repost(fixture);
      const ItemId line = fixture.lines.at(0).id;
      {
        StateTrackerGuard guard(&fixture.tracker);
        if (entry == 0)
          LOKA_VERIFY(fixture.lines.update(line, String("changed")) == EDIT_OK);
        fixture.request.set(LineCursor(line, 1));
      }
      if (entry == 1)
        fixture.type(static_cast<wchar_t>(0xff21));
      else if (entry == 3)
        SendMessageW(fixture.context->hwnd(), WM_TIMER, 853, 0);
      else
        fixture.context->onPropsApplied();
      // Predicted red on #879's base for entries 0/1: the helper and outer
      // completion each take twice, producing four reports instead of two.
      LOKA_VERIFY(repost.reports().size() == 2);
      LOKA_VERIFY(repost.reports()[0] == LineCursor(line, 1));
      LOKA_VERIFY(repost.reports()[1] == LineCursor(line, 0));
      LOKA_VERIFY(fixture.request.get() == LineCursor(line, 1));
      LOKA_VERIFY(fixture.cursor.state()->get() == LineCursor(line, 0));
      expectSelection(fixture, 0);
      LOKA_VERIFY(EditorAccess::status(*fixture.context) == EDITOR_OK);
      LOKA_VERIFY(!EditorAccess::pending(*fixture.context));
      fixture.matches();
    }
  }
  void testWin32TextEditorReverseBoundaryCaret()
  {
    Fixture fixture;
    // Delete inside the second line, then cross its preceding CRLF both ways.
    SendMessageW(fixture.context->hwnd(), EM_SETSEL, 8, 8);
    fixture.type(L'\b');
    LOKA_VERIFY(fixture.lines.at(1).value.equals(String("acd")));
    expectSelection(fixture, 7);
    LOKA_VERIFY(fixture.cursor.state()->get() == LineCursor(fixture.lines.at(1).id, 1));
    Probe probe(fixture.context->hwnd());
    const UINT keys[] = {VK_LEFT, VK_LEFT, VK_RIGHT, VK_RIGHT};
    const DWORD offsets[] = {6, 4, 6, 7};
    const unsigned short rows[] = {1, 0, 1, 1};
    const int columns[] = {0, 4, 0, 1};
    for (unsigned i = 0; i < 4; ++i)
    {
      SendMessageW(fixture.context->hwnd(), WM_KEYDOWN, keys[i], 1);
      fixture.context->onPropsApplied();
      expectSelection(fixture, offsets[i]);
      LOKA_VERIFY(fixture.cursor.state()->get() == LineCursor(fixture.lines.at(rows[i]).id, columns[i]));
      LOKA_VERIFY(probe.selections.empty());
    }
    // Positive control for the EM_SETSEL instrument, after the report-only run.
    SendMessageW(fixture.context->hwnd(), EM_SETSEL, 1, 3);
    LOKA_VERIFY(probe.selections.size() == 1);
    fixture.context->onPropsApplied();
    DWORD start = 0, end = 0;
    SendMessageW(fixture.context->hwnd(), EM_GETSEL, reinterpret_cast<WPARAM>(&start), reinterpret_cast<LPARAM>(&end));
    LOKA_VERIFY(start == 1 && end == 3 && probe.selections.size() == 1);
    LOKA_VERIFY(SendMessageW(fixture.context->hwnd(), EM_CANUNDO, 0, 0));
  }
  void restored(Fixture &fixture, const Snapshot &snapshot, const Observer &observer)
  {
    snapshot.unchanged(fixture);
    fixture.matches();
    LOKA_VERIFY(observer.notifications == 0 && observer.settled == 0);
    LOKA_VERIFY(EditorAccess::restores(*fixture.context) == 1);
    LOKA_VERIFY(!SendMessageW(fixture.context->hwnd(), EM_CANUNDO, 0, 0));
    DWORD start = 0, end = 0;
    SendMessageW(fixture.context->hwnd(), EM_GETSEL, reinterpret_cast<WPARAM>(&start), reinterpret_cast<LPARAM>(&end));
    LOKA_VERIFY(start == 2 && end == 2);
  }
  // EDIT replaces a selection as two EN_CHANGE notifications (delete, then
  // insert), so a keystroke, Return, or paste over a selection commits twice;
  // each commit is one validated batch and the intermediate document is real.
  void verifyReplacement(Fixture &fixture, const Observer &observer, const char *text, unsigned short row, int column,
                         unsigned publications)
  {
    std::string actual;
    LOKA_VERIFY(loka::app::testing::TextEditorAccess::document(*fixture.node).project(actual) == EDITOR_OK
                && actual == text);
    LOKA_VERIFY(fixture.cursor.state()->get() == LineCursor(fixture.lines.at(row).id, column));
    LOKA_VERIFY(observer.notifications == publications && observer.settled == publications);
    LOKA_VERIFY(EditorAccess::status(*fixture.context) == EDITOR_OK);
    LOKA_VERIFY(EditorAccess::restores(*fixture.context) == 0);
    fixture.matches();
  }
  void verifyNativeCaret(void *data)
  {
    const Fixture &fixture = *static_cast<Fixture *>(data);
    DWORD start = 0, end = 0;
    SendMessageW(fixture.context->hwnd(), EM_GETSEL, reinterpret_cast<WPARAM>(&start), reinterpret_cast<LPARAM>(&end));
    const int row = static_cast<int>(SendMessageW(fixture.context->hwnd(), EM_LINEFROMCHAR, end, 0));
    const int offset = static_cast<int>(SendMessageW(fixture.context->hwnd(), EM_LINEINDEX, row, 0));
    LOKA_VERIFY(row >= 0 && row < fixture.lines.size() && offset >= 0);
    LOKA_VERIFY(fixture.cursor.state()->get()
                == LineCursor(fixture.lines.at(static_cast<unsigned short>(row)).id, static_cast<int>(end) - offset));
  }
  void testWin32TextEditorMultilineTyping()
  {
    Fixture fixture;
    Observer observer(fixture);
    const ItemId first = fixture.lines.at(0).id;
    SendMessageW(fixture.context->hwnd(), EM_SETSEL, 0, 16);
    fixture.type(L'b');
    verifyReplacement(fixture, observer, "b", 0, 1, 2);
    LOKA_VERIFY(fixture.lines.size() == 1 && fixture.lines.at(0).id == first);
  }
  void testWin32TextEditorMultilineBackspace()
  {
    Fixture fixture;
    Observer observer(fixture);
    const ItemId first = fixture.lines.at(0).id;
    SendMessageW(fixture.context->hwnd(), EM_SETSEL, 1, 15);
    fixture.type(L'\b');
    verifyReplacement(fixture, observer, "ad", 0, 1, 1);
    LOKA_VERIFY(fixture.lines.size() == 1 && fixture.lines.at(0).id == first);
  }
  void testWin32TextEditorMultilineReturn()
  {
    Fixture fixture;
    Observer observer(fixture);
    const ItemId first = fixture.lines.at(0).id;
    const ItemId last = fixture.lines.at(2).id;
    SendMessageW(fixture.context->hwnd(), EM_SETSEL, 1, 15);
    fixture.type(L'\r');
    verifyReplacement(fixture, observer, "a\rd", 1, 0, 2);
    LOKA_VERIFY(fixture.lines.size() == 2 && fixture.lines.at(0).id == first);
    LOKA_VERIFY(fixture.lines.find(last) < 0);
  }
  void testWin32TextEditorMultilinePaste()
  {
    // Full capacity: removal must make room before replacement rows are inserted.
    Fixture fixture(3, "abcd", 3);
    Observer observer(fixture);
    const ItemId first = fixture.lines.at(0).id;
    const ItemId last = fixture.lines.at(2).id;
    SendMessageW(fixture.context->hwnd(), EM_SETSEL, 1, 15);
    paste(fixture.context->hwnd(), L"Q\r\nR\r\nS");
    verifyReplacement(fixture, observer, "aQ\rR\rSd", 2, 1, 2);
    LOKA_VERIFY(fixture.lines.size() == 3 && fixture.lines.at(0).id == first);
    LOKA_VERIFY(fixture.lines.find(last) < 0);
  }
  void testWin32TextEditorMultilineInsertionPaste()
  {
    Fixture fixture;
    Observer observer(fixture);
    SendMessageW(fixture.context->hwnd(), EM_SETSEL, 14, 14);
    paste(fixture.context->hwnd(), L"Q\r\nR");
    // The caret lands on a row that did not exist before the commit: only the
    // post-state RowCursor can name it.
    verifyReplacement(fixture, observer, "abcd\rabcd\rabQ\rRcd", 3, 1, 1);
    verifyNativeCaret(&fixture);
    LOKA_VERIFY(fixture.lines.size() == 4);
  }
  void testWin32TextEditorMultilineUndo()
  {
    Fixture fixture;
    Observer observer(fixture);
    const ItemId first = fixture.lines.at(0).id;
    SendMessageW(fixture.context->hwnd(), EM_SETSEL, 0, 16);
    // Deleting the selection gives EDIT one unambiguous undo record.
    fixture.type(L'\b');
    verifyReplacement(fixture, observer, "", 0, 0, 1);
    LOKA_VERIFY(SendMessageW(fixture.context->hwnd(), EM_CANUNDO, 0, 0));
    ChangeObservation undo(fixture.host);
    LOKA_VERIFY(SendMessageW(fixture.context->hwnd(), EM_UNDO, 0, 0));
    LOKA_VERIFY(undo.arrivals() > 0 && undo.status() == EDITOR_OK);
    LOKA_VERIFY(fixture.lines.size() == 3 && fixture.lines.at(0).id == first);
    for (unsigned short row = 0; row < fixture.lines.size(); ++row)
      LOKA_VERIFY(fixture.lines.at(row).value.equals(String("abcd")));
    verifyNativeCaret(&fixture);
    LOKA_VERIFY(observer.notifications == 2 && observer.settled == 2);
    LOKA_VERIFY(EditorAccess::restores(*fixture.context) == 0);
    fixture.matches();
  }
} // namespace

void testWin32TextEditorQueuedRequests()
{
  // Deliberate Win32/Mac twin: one ordinary props completion per assertion group.
  typedef loka::app::testing::SettleTrace<LineCursor> Trace;
  typedef loka::app::scene::Reply<LineCursor> CaretReply;
  Fixture fixture;
  // Bare fixture: apply through the Props applier (the Definition door compares
  // Props type identity, which a prebuilt core library does not share on this rail).
  LOKA_VERIFY((loka::app::scene::NodePropsApplier<TextEditorNode, TextEditorProps>::apply(
      fixture.node, TextEditor(fixture.lines, fixture.cursor).moveCaretTo(fixture.queue).props)));
  fixture.context->onPropsApplied();
  Trace &trace = Trace::instance();
  const LineCursor first(fixture.lines.at(0).id, 0);
  const LineCursor second(fixture.lines.at(1).id, 1);
  const LineCursor third(fixture.lines.at(2).id, 3);
  for (int posts = 2; posts <= 3; ++posts)
  {
    const LineCursor before = fixture.cursor.state()->get();
    trace.clear();
    {
      StateTrackerGuard guard(&fixture.tracker);
      LOKA_VERIFY(fixture.queue.post(first) == POST_ACCEPTED);
      LOKA_VERIFY(fixture.queue.post(second) == POST_ACCEPTED);
      if (posts == 3)
        LOKA_VERIFY(fixture.queue.post(third) == POST_ACCEPTED);
    }
    fixture.context->onPropsApplied();
    LOKA_VERIFY(trace.size() == 1 && trace.overwritten() == 0);
    LOKA_VERIFY(trace.at(0).stimulus == SETTLE_PROPS && trace.at(0).count == 2);
    LOKA_VERIFY(trace.at(0).before == before && trace.at(0).after == second);
    for (unsigned i = 0; i < 2; ++i)
    {
      const LineCursor expected = i == 0 ? first : second;
      LOKA_VERIFY(trace.at(0).takes[i].kind() == CaretReply::GRANTED);
      LOKA_VERIFY(trace.at(0).takes[i].requested() == expected && trace.at(0).takes[i].applied() == expected);
      LOKA_VERIFY(trace.at(0).seam[i] == EDITOR_OK);
    }
    const CaretReply reply = fixture.queue.reply().state()->get();
    LOKA_VERIFY(reply.kind() == CaretReply::GRANTED && reply.requested() == second && reply.applied() == second);
    LOKA_VERIFY(fixture.cursor.state()->get() == second && fixture.queue.pending() == 0);
    if (posts == 2)
      LOKA_VERIFY(fixture.queue.state()->get().isNone());
    else
    {
      // pending() counts only the ring: the third request remains in the live slot.
      LOKA_VERIFY(fixture.queue.state()->get() == third);
      trace.clear();
      fixture.context->onPropsApplied();
      LOKA_VERIFY(trace.size() == 1 && trace.at(0).count == 1 && trace.at(0).stimulus == SETTLE_PROPS);
      LOKA_VERIFY(trace.at(0).before == second && trace.at(0).after == third);
      LOKA_VERIFY(trace.at(0).takes[0].kind() == CaretReply::GRANTED && trace.at(0).seam[0] == EDITOR_OK);
      LOKA_VERIFY(trace.at(0).takes[0].requested() == third && trace.at(0).takes[0].applied() == third);
      const CaretReply last = fixture.queue.reply().state()->get();
      LOKA_VERIFY(last.kind() == CaretReply::GRANTED && last.requested() == third && last.applied() == third);
      LOKA_VERIFY(fixture.cursor.state()->get() == third);
      LOKA_VERIFY(fixture.queue.pending() == 0 && fixture.queue.state()->get().isNone());
    }
  }
}

void testWin32TextEditorConversion()
{
  Win32PlatformContext rail;
  TextCapacity maximum;
  LOKA_VERIFY(rail.queryTextEditorCapacity(maximum));
  LOKA_VERIFY(maximum.lines == 256 && maximum.bytes == 8192);
  std::wstring wide;
  std::string logical;
  LOKA_VERIFY(loka::win32::TextEditorToWide("first\r\rlast\r", wide));
  LOKA_VERIFY(wide == L"first\r\n\r\nlast\r\n");
  LOKA_VERIFY(loka::win32::TextEditorFromWide(wide.data(), wide.size(), logical));
  LOKA_VERIFY(logical == "first\r\rlast\r");
  LOKA_VERIFY(loka::win32::TextEditorFromWide(L"a\rb\nc\r\nd", 8, logical) && logical == "a\rb\rc\rd");
  LOKA_VERIFY(loka::win32::TextEditorToWide(std::string(8192, 'a'), wide) && wide.size() == 8192);
  LOKA_VERIFY(loka::win32::TextEditorFromWide(wide.data(), wide.size(), logical) && logical.size() == 8192);
  LOKA_VERIFY(!loka::win32::TextEditorFromWide(L"\xff21", 1, logical));
  LOKA_VERIFY(!loka::win32::TextEditorToWide(std::string(1, static_cast<char>(0x80)), wide));
}
void testWin32TextEditorActionsUseLineQueries()
{
  testWin32TextEditorCommands();
  testWin32TextEditorCaretRequests();
  testWin32TextEditorSettlement();
  testWin32TextEditorReverseBoundaryCaret();
  testWin32TextEditorEntryDeliveryBound();
  testWin32TextEditorMultilineTyping();
  testWin32TextEditorMultilineBackspace();
  testWin32TextEditorMultilineReturn();
  testWin32TextEditorMultilinePaste();
  testWin32TextEditorMultilineInsertionPaste();
  testWin32TextEditorMultilineUndo();
  Fixture fixture(128);
  Probe probe(fixture.context->hwnd());
  Observer observer(fixture);
  const ItemId first = fixture.lines.at(0).id, second = fixture.lines.at(1).id;
  const ListRevision before = fixture.lines.revision().get();
  fixture.type(L'x');
  LOKA_VERIFY(observer.notifications == 1 && observer.settled == 1);
  LOKA_VERIFY(fixture.lines.revision().get().content == before.content + 1);
  LOKA_VERIFY(fixture.lines.revision().get().structure == before.structure);
  LOKA_VERIFY(fixture.lines.revision().get().change.kind == LIST_UPDATE);
  LOKA_VERIFY(fixture.lines.at(0).value.equals(String("abxcd"))
              && fixture.cursor.state()->get() == LineCursor(first, 3));
  LOKA_VERIFY(probe.lineReads == 0 && probe.textReads == 1);
  fixture.type(L'\r');
  LOKA_VERIFY(observer.notifications == 2 && observer.settled == 2);
  LOKA_VERIFY(fixture.lines.size() == 129 && fixture.lines.at(2).id == second);
  LOKA_VERIFY(fixture.lines.at(0).value.equals(String("abx")) && fixture.lines.at(1).value.equals(String("cd")));
  LOKA_VERIFY(fixture.cursor.state()->get() == LineCursor(fixture.lines.at(1).id, 0));
  LOKA_VERIFY(fixture.lines.revision().get().change.kind == LIST_BATCH);
  fixture.type(L'\b');
  LOKA_VERIFY(observer.notifications == 3 && observer.settled == 3);
  LOKA_VERIFY(fixture.lines.size() == 128 && fixture.lines.at(1).id == second);
  LOKA_VERIFY(fixture.lines.revision().get().change.kind == LIST_BATCH);
  LOKA_VERIFY(fixture.cursor.state()->get() == LineCursor(first, 3));
  fixture.matches();
  const unsigned sets = probe.sets;
  fixture.context->onPropsApplied();
  LOKA_VERIFY(probe.sets == sets && SendMessageW(fixture.context->hwnd(), EM_CANUNDO, 0, 0));
  const ListRevision beforeMove = fixture.lines.revision().get();
  SendMessageW(fixture.context->hwnd(), WM_KEYDOWN, VK_LEFT, 1);
  LOKA_VERIFY(fixture.cursor.state()->get() == LineCursor(first, 2));
  LOKA_VERIFY(!(fixture.lines.revision().get() != beforeMove));
  SendMessageW(fixture.context->hwnd(), EM_SETSEL, 1, 3);
  SendMessageW(fixture.context->hwnd(), WM_KEYUP, VK_SHIFT, 1);
  fixture.context->onPropsApplied();
  DWORD selectionStart = 0, selectionEnd = 0;
  SendMessageW(fixture.context->hwnd(),
               EM_GETSEL,
               reinterpret_cast<WPARAM>(&selectionStart),
               reinterpret_cast<LPARAM>(&selectionEnd));
  LOKA_VERIFY(selectionStart == 1 && selectionEnd == 3);
  const PaintQuery echoQuery = {Win32RetirableContext::paintScope(), PLACEMENT_ELIGIBLE};
  LOKA_VERIFY(fixture.context->queryPaintDamage(echoQuery).kind == PAINT_ANSWER_EXACT);
  // Isolate this replacement from EDIT's prior typing/deletion undo record.
  SendMessageW(fixture.context->hwnd(), EM_EMPTYUNDOBUFFER, 0, 0);
  {
    ChangeObservation replacement(fixture.host);
    SendMessageW(fixture.context->hwnd(), EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L"Q"));
    // Positive control: a zero undo count is meaningful only if this route fires.
    LOKA_VERIFY(replacement.arrivals() > 0);
    LOKA_VERIFY(replacement.native() == fixture.native());
  }
  LOKA_VERIFY(fixture.lines.at(0).value.equals(String("aQcd")));
  fixture.matches();
  const LRESULT distant = SendMessageW(fixture.context->hwnd(), EM_LINEINDEX, 10, 0);
  LOKA_VERIFY(distant >= 0);
  SendMessageW(fixture.context->hwnd(), EM_SETSEL, distant, distant);
  LOKA_VERIFY(SendMessageW(fixture.context->hwnd(), EM_CANUNDO, 0, 0));
  std::string beforeUndo;
  LOKA_VERIFY(loka::app::testing::TextEditorAccess::document(*fixture.node).project(beforeUndo) == EDITOR_OK);
  {
    ChangeObservation undo(fixture.host);
    const LRESULT undone = SendMessageW(fixture.context->hwnd(), EM_UNDO, 0, 0);
    // EDIT's single-level undo need not reconstruct the pre-replacement text.
    // The model must accept the actual native result and its final caret.
    const std::wstring native = fixture.native(); // Reads back with GetWindowTextW.
    const std::wstring nativeFirst = native.substr(0, native.find_first_of(L"\r\n"));
    std::string logicalFirst;
    const bool converted = loka::win32::TextEditorFromWide(nativeFirst.data(), nativeFirst.size(), logicalFirst);
    const bool lineMatches =
        converted && fixture.lines.at(0).value.equals(String::Utf8(logicalFirst.data(), logicalFirst.size()));
    DWORD caretStart = 0, caretEnd = 0;
    SendMessageW(
        fixture.context->hwnd(), EM_GETSEL, reinterpret_cast<WPARAM>(&caretStart), reinterpret_cast<LPARAM>(&caretEnd));
    const int caretLine = static_cast<int>(SendMessageW(fixture.context->hwnd(), EM_LINEFROMCHAR, caretEnd, 0));
    const int caretOffset = static_cast<int>(SendMessageW(fixture.context->hwnd(), EM_LINEINDEX, caretLine, 0));
    const bool caretMatches = caretLine >= 0 && caretLine < fixture.lines.size() && caretOffset >= 0
                              && fixture.cursor.state()->get()
                                     == LineCursor(fixture.lines.at(static_cast<unsigned short>(caretLine)).id,
                                                   static_cast<int>(caretEnd) - caretOffset);
    if (!undone || !lineMatches || !caretMatches || EditorAccess::restores(*fixture.context) != 0
        || undo.arrivals() == 0)
    {
      const StringBuffer model = fixture.lines.at(0).value.bufferWithEncoding(StringEncodingUtf8);
      std::printf(
          "[undo failure] EM_UNDO=%ld EN_CHANGE=%u model line 0=%.*s restores=%u status=%d notification-status=%d\n",
          static_cast<long>(undone),
          undo.arrivals(),
          static_cast<int>(model.length()),
          static_cast<const char *>(model.data()),
          EditorAccess::restores(*fixture.context),
          static_cast<int>(EditorAccess::status(*fixture.context)),
          static_cast<int>(undo.status()));
      std::printf("[undo failure] native caret line=%d column=%d model cursor matches=%d\n",
                  caretLine,
                  static_cast<int>(caretEnd) - caretOffset,
                  caretMatches ? 1 : 0);
      printUndoText("after EM_UNDO", native, beforeUndo);
      if (undo.arrivals())
        printUndoText("at EN_CHANGE before dispatch", undo.native(), beforeUndo);
      std::fflush(stdout);
    }
    LOKA_VERIFY(undone);
    LOKA_VERIFY(lineMatches);
    LOKA_VERIFY(caretMatches);
    LOKA_VERIFY(undo.arrivals() > 0);
  }
  LOKA_VERIFY(fixture.lines.at(0).id == first && fixture.lines.size() == 128);
  LOKA_VERIFY(fixture.lines.at(10).value.equals(String("abcd")));
  LOKA_VERIFY(EditorAccess::restores(*fixture.context) == 0);
  fixture.matches();
  // A trailing empty logical line must preserve the model, caret and CRLF projection.
  const unsigned short lineCount = fixture.lines.size();
  const int nativeEnd = GetWindowTextLengthW(fixture.context->hwnd());
  SendMessageW(fixture.context->hwnd(), EM_SETSEL, nativeEnd, nativeEnd);
  fixture.type(L'\r');
  LOKA_VERIFY(fixture.lines.size() == lineCount + 1);
  const unsigned short last = static_cast<unsigned short>(fixture.lines.size() - 1);
  LOKA_VERIFY(fixture.lines.at(last).value.equals(String("")));
  LOKA_VERIFY(fixture.cursor.state()->get() == LineCursor(fixture.lines.at(last).id, 0));
  const std::wstring trailing = fixture.native();
  LOKA_VERIFY(trailing.size() >= 2 && trailing.compare(trailing.size() - 2, 2, L"\r\n") == 0);
  LOKA_VERIFY(GetWindowTextLengthW(fixture.context->hwnd()) == static_cast<int>(fixture.committedNative().size()));
  fixture.matches();
  fixture.type(L'z');
  LOKA_VERIFY(fixture.lines.at(last).value.equals(String("z")));
  LOKA_VERIFY(fixture.cursor.state()->get() == LineCursor(fixture.lines.at(last).id, 1));
  fixture.matches();
}
void testWin32TextEditorRefusalRestoresAndClearsUndo()
{
  {
    Fixture fixture(256);
    Probe probe(fixture.context->hwnd());
    probe.seedUndo = true;
    Observer observer(fixture);
    SendMessageW(fixture.context->hwnd(), EM_LINESCROLL, 0, 80);
    const LRESULT visible = SendMessageW(fixture.context->hwnd(), EM_GETFIRSTVISIBLELINE, 0, 0);
    LOKA_VERIFY(visible > 0);
    const Snapshot before(fixture);
    fixture.type(L'\r');
    restored(fixture, before, observer);
    LOKA_VERIFY(probe.sets == 1 && !probe.restoredInsideNotification);
    LOKA_VERIFY(SendMessageW(fixture.context->hwnd(), EM_GETFIRSTVISIBLELINE, 0, 0) == visible);
    fixture.type(L'x');
    LOKA_VERIFY(observer.notifications == 1);
    fixture.matches();
  }
  {
    Fixture fixture;
    Observer observer(fixture);
    const Snapshot before(fixture);
    fixture.type(static_cast<wchar_t>(0xff21));
    restored(fixture, before, observer);
    fixture.type(L'x');
    LOKA_VERIFY(observer.notifications == 1);
  }
  {
    Fixture fixture;
    Observer observer(fixture);
    const Snapshot before(fixture);
    const std::wstring oversized(8193, L'x');
    SendMessageW(fixture.context->hwnd(), EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(oversized.c_str()));
    restored(fixture, before, observer);
    fixture.type(L'x');
    LOKA_VERIFY(observer.notifications == 1);
  }
  {
    Fixture fixture(1, std::string(8192, 'a'));
    Observer observer(fixture);
    const Snapshot before(fixture);
    fixture.type(L'x');
    restored(fixture, before, observer);
  }
  {
    Fixture fixture;
    Observer observer(fixture);
    const Snapshot before(fixture);
    loka::core::testing::failLokaAllocRaw("TextEditor", "Scratch", 1);
    fixture.type(L'\r');
    loka::core::testing::allowLokaAllocRaw();
    restored(fixture, before, observer);
    fixture.type(L'x');
    LOKA_VERIFY(observer.notifications == 1);
  }
  {
    Fixture fixture;
    Observer observer(fixture);
    const Snapshot before(fixture);
    // An unsolicited notification has no subclass input stack to unwind.
    // Its refusal stays read-only until a separate timer dispatch.
    LOKA_VERIFY(SetWindowTextW(fixture.context->hwnd(), L"bad\xff21"));
    before.unchanged(fixture);
    LOKA_VERIFY(observer.notifications == 0 && !EditorAccess::pending(*fixture.context));
    SendMessageW(fixture.host, WM_COMMAND, MAKEWPARAM(0, EN_CHANGE), reinterpret_cast<LPARAM>(fixture.context->hwnd()));
    before.unchanged(fixture);
    LOKA_VERIFY(EditorAccess::pending(*fixture.context) && EditorAccess::restores(*fixture.context) == 0);
    SendMessageW(fixture.context->hwnd(), WM_TIMER, 853, 0);
    restored(fixture, before, observer);
    fixture.type(L'x');
    LOKA_VERIFY(observer.notifications == 1);
  }
  {
    Fixture fixture;
    LOKA_VERIFY(fixture.lines.replaceWithOne(String("fresh")) == EDIT_OK);
    Observer observer(fixture);
    const Snapshot before(fixture);
    fixture.type(L'x');
    restored(fixture, before, observer);
    fixture.type(L'y');
    LOKA_VERIFY(fixture.lines.at(0).value.equals(String("fryesh")));
  }
  {
    Fixture fixture(1);
    for (unsigned int i = 1; i < 65535; ++i)
    {
      ItemId temporary;
      LOKA_VERIFY(fixture.lines.insert(1, String(), &temporary) == EDIT_OK);
      LOKA_VERIFY(fixture.lines.remove(temporary) == EDIT_OK);
    }
    fixture.context->onPropsApplied();
    Observer observer(fixture);
    const Snapshot before(fixture);
    fixture.type(L'\r');
    restored(fixture, before, observer);
    fixture.type(L'x');
    LOKA_VERIFY(observer.notifications == 1);
  }
  {
    Fixture fixture(1, std::string(8191, 'a'));
    fixture.type(L'x');
    const Snapshot committed(fixture);
    const int nativeUnits = GetWindowTextLengthW(fixture.context->hwnd());
    if (committed.text.size() != 8192 || nativeUnits != 8192 || EditorAccess::restores(*fixture.context) != 0
        || EditorAccess::status(*fixture.context) != EDITOR_OK)
    {
      std::printf("[cap failure] committed logical=%lu native UTF-16=%d restores=%u status=%d\n",
                  static_cast<unsigned long>(committed.text.size()),
                  nativeUnits,
                  EditorAccess::restores(*fixture.context),
                  static_cast<int>(EditorAccess::status(*fixture.context)));
      std::fflush(stdout);
    }
    std::string line;
    LOKA_VERIFY(loka::platform::CollectUtf8(fixture.lines.at(0).value, line));
    LOKA_VERIFY(line.size() == 8192 && committed.text.size() == 8192 && nativeUnits == 8192);
    LOKA_VERIFY(EditorAccess::status(*fixture.context) == EDITOR_OK);
    fixture.matches();
    LOKA_VERIFY(GetWindowTextLengthW(fixture.context->hwnd()) == static_cast<int>(fixture.committedNative().size()));
    LOKA_VERIFY(EditorAccess::restores(*fixture.context) == 0);
  }
  {
    // 8191 logical bytes across two lines; one insertion reaches the cap.
    Fixture fixture(2, std::string(4095, 'a'));
    fixture.type(L'x');
    const Snapshot before(fixture);
    LOKA_VERIFY(before.text.size() == 8192);
    LOKA_VERIFY(fixture.committedNative().size() == 8193);
    fixture.matches();
    Observer observer(fixture);
    fixture.type(L'x');
    before.unchanged(fixture);
    fixture.matches();
    LOKA_VERIFY(observer.notifications == 0 && observer.settled == 0);
    LOKA_VERIFY(EditorAccess::restores(*fixture.context) == 1 && !EditorAccess::pending(*fixture.context));
    LOKA_VERIFY(!SendMessageW(fixture.context->hwnd(), EM_CANUNDO, 0, 0));
  }
  {
    Fixture fixture(1, std::string(8193, 'a'));
    LOKA_VERIFY(EditorAccess::status(*fixture.context) == EDITOR_CAPACITY && fixture.native().empty());
    LOKA_VERIFY(GetWindowLongPtrW(fixture.context->hwnd(), GWL_STYLE) & ES_READONLY);
    LOKA_VERIFY(fixture.lines.update(fixture.lines.at(0).id, String("a")) == EDIT_OK);
    fixture.context->onPropsApplied();
    LOKA_VERIFY(EditorAccess::status(*fixture.context) == EDITOR_OK);
    fixture.type(L'x');
    fixture.matches();
    LOKA_VERIFY(fixture.lines.update(fixture.lines.at(0).id, String(std::string(8193, 'a'))) == EDIT_OK);
    fixture.context->onPropsApplied();
    LOKA_VERIFY(EditorAccess::status(*fixture.context) == EDITOR_CAPACITY && fixture.native().empty());
  }
}
void testWin32TextEditorNestedInput()
{
  verifyBaseOwnedRetirement();
  for (int deferred = 0; deferred < 2; ++deferred)
  {
    Fixture fixture;
    Observer observer(fixture);
    observer.nested = deferred == 0;
    observer.deferred = deferred != 0;
    Probe probe(fixture.context->hwnd());
    probe.echo = true;
    LOKA_VERIFY(!fixture.controller.borrowPhase().open());
    fixture.type(L'x');
    LOKA_VERIFY(!fixture.controller.borrowPhase().open());
    LOKA_VERIFY(observer.notifications == 1 && observer.settled == 1);
    LOKA_VERIFY(fixture.lines.at(0).value.equals(String("abxcd")));
    fixture.matches();
    LOKA_VERIFY(EditorAccess::restores(*fixture.context) == 1 && probe.sets == 1 && !probe.restoredInsideNotification);
    LOKA_VERIFY(!SendMessageW(fixture.context->hwnd(), EM_CANUNDO, 0, 0));
    fixture.type(L'y');
    LOKA_VERIFY(fixture.lines.at(0).value.equals(String("abxycd")) && observer.notifications == 2);
  }
}
void testWin32TextEditorFailedReplacementRetries()
{
  for (int failure = 0; failure < 3; ++failure)
  {
    Fixture fixture(256);
    Probe probe(fixture.context->hwnd());
    const loka::win32::testing::TextEditorSetFailure failures[] = {
        loka::win32::testing::TEXT_EDITOR_SET_REFUSED,
        loka::win32::testing::TEXT_EDITOR_SET_TRUNCATED,
        loka::win32::testing::TEXT_EDITOR_SET_FALSE_AFTER_DELIVERY};
    loka::win32::testing::failTextEditorSets(failures[failure], failure == 0 ? 2 : 1);
    const Snapshot before(fixture);
    {
      StateTrackerGuard guard(&fixture.tracker);
      fixture.request.set(LineCursor(fixture.lines.at(1).id, 1));
    }
    fixture.type(L'\r');
    before.unchanged(fixture);
    LOKA_VERIFY(fixture.request.get().isNone());
    LOKA_VERIFY(EditorAccess::pending(*fixture.context));
    LOKA_VERIFY(GetWindowLongPtrW(fixture.context->hwnd(), GWL_STYLE) & ES_READONLY);
    LOKA_VERIFY(EditorAccess::restores(*fixture.context) == 1 && !probe.restoredInsideNotification);
    fixture.type(L'x');
    before.unchanged(fixture);
    // Explicitly dispatch each timer turn; no nested pump inside owner apply.
    SendMessageW(fixture.context->hwnd(), WM_TIMER, 853, 0);
    LOKA_VERIFY(EditorAccess::restores(*fixture.context) == 2);
    if (failure == 0)
    {
      LOKA_VERIFY(EditorAccess::pending(*fixture.context));
      SendMessageW(fixture.context->hwnd(), WM_TIMER, 853, 0);
      LOKA_VERIFY(EditorAccess::restores(*fixture.context) == 3);
    }
    LOKA_VERIFY(!EditorAccess::pending(*fixture.context));
    LOKA_VERIFY(!(GetWindowLongPtrW(fixture.context->hwnd(), GWL_STYLE) & ES_READONLY));
    fixture.matches();
    fixture.type(L'x');
    LOKA_VERIFY(fixture.lines.at(0).value.equals(String("abxcd")));
  }
}
void testWin32TextEditorLayoutDpiAndRetirement()
{
  PlainOnlyHighlighter highlighter;
  Fixture fixture;
  fixture.node->props.highlighter(highlighter);
  fixture.context->onPropsApplied();
  fixture.type(L'x');
  LOKA_VERIFY(highlighter.calls == 0);
  HWND child = fixture.context->hwnd();
  const LONG_PTR style = GetWindowLongPtrW(child, GWL_STYLE);
  const LONG_PTR required = ES_MULTILINE | ES_WANTRETURN | ES_AUTOVSCROLL | ES_AUTOHSCROLL | WS_VSCROLL;
  LOKA_VERIFY((style & required) == required);
  LayoutState bounds;
  bounds.x = 10;
  bounds.y = 20;
  bounds.width = 300;
  bounds.height = 140;
  bounds.spacing = 4;
  // The leaf ends at its own bottom; spacing belongs to the container (#950).
  LOKA_VERIFY(fixture.context->layout(&fixture.controller, bounds) == 160 && bounds.height == 140);
  RECT rect;
  LOKA_VERIFY(GetWindowRect(child, &rect) && rect.bottom - rect.top == 140);
  const HFONT font = reinterpret_cast<HFONT>(SendMessageW(child, WM_GETFONT, 0, 0));
  LOKA_VERIFY(font);
  fixture.controller.updateDisplayScale(loka::win32::Win32DisplayScale(144, RailMetrics()));
  const HFONT nextFont = reinterpret_cast<HFONT>(SendMessageW(child, WM_GETFONT, 0, 0));
  LOKA_VERIFY(nextFont && nextFont != font);
  const PaintQuery query = {Win32RetirableContext::paintScope(), PLACEMENT_ELIGIBLE};
  const PaintAnswer answer = fixture.context->queryPaintDamage(query);
  LOKA_VERIFY(answer.kind == PAINT_ANSWER_NATIVE_SCHEDULED);
  {
    Probe probe(child);
    loka::win32::testing::failTextEditorSets(loka::win32::testing::TEXT_EDITOR_SET_REFUSED, 1);
    fixture.type(static_cast<wchar_t>(0xff21));
    LOKA_VERIFY(EditorAccess::pending(*fixture.context));
    const Snapshot before(fixture);
    NotifySubtreeNodeDetached(fixture.node);
    const unsigned beforeDetachSets = probe.sets;
    SendMessageW(child, WM_TIMER, 853, 0);
    SendMessageW(child, WM_CHAR, 'x', 1);
    before.unchanged(fixture);
    LOKA_VERIFY(probe.sets == beforeDetachSets);
    delete fixture.node;
    fixture.node = 0;
    fixture.context = 0;
    LOKA_VERIFY(!Win32TextEditorContext::fromWindow(child));
    fixture.controller.drainNativeRetirements();
    LOKA_VERIFY(!IsWindow(child) && !GetWindow(fixture.host, GW_CHILD));
  }
}
