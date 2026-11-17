#include "Win32TextStoreTests.hpp"
#include "Win32ScenePlatformController.hpp"
#include "text/Win32TextParking.hpp"
#include "support/Win32TextStoreAccess.hpp"
#include "support/TextStoreConformance.hpp"
#include "support/TestVerify.hpp"
#include "app/text/TextStoreSource.hpp"
#include <cstdio>
#include <cstring>
#include <string>

namespace
{
  using namespace loka::app;
  using namespace loka::app::scene;
  using namespace loka::win32::testing;
  TextReplacement replacement(const std::string &body, TextPlacement placement = TextPlacement::Start())
  {
    loka::core::resource::Blob bytes = loka::core::resource::Blob::Create();
    LOKA_VERIFY(bytes.tryAssign(reinterpret_cast<const unsigned char *>(body.data()), body.size()));
    bytes.sealBytes();
    TextReplacement result;
    LOKA_VERIFY(TextReplacement::FromUtf8(bytes, placement, result) == TEXT_DOCUMENT_OK);
    return result;
  }
  struct Fixture
  {
    HWND root;
    Win32ScenePlatformController controller;
    Fixture()
        : root(CreateWindowExW(
              0, L"STATIC", L"store tests", WS_OVERLAPPED, 0, 0, 320, 240, 0, 0, GetModuleHandleW(0), 0)),
          controller(root, loka::win32::Win32DisplayScale(96, RailMetrics()))
    {
      LOKA_VERIFY(this->root);
    }
    ~Fixture()
    {
      this->controller.destroy();
      DestroyWindow(this->root);
    }
    TextStoreRow *birth(const TextReplacement &value, TextStoreReading &reading, TextStoreRow *old = 0)
    {
      TextStoreSource source(value);
      TextStoreRow *fresh = 0;
      LOKA_VERIFY(this->controller.createTextStore(source, old, fresh, reading) == TEXT_STORE_BORN);
      LOKA_VERIFY(fresh);
      return fresh;
    }
  };
  std::wstring native(HWND edit)
  {
    const int length = GetWindowTextLengthW(edit);
    std::wstring text(length + 1, L'\0');
    LOKA_VERIFY(GetWindowTextW(edit, &text[0], length + 1) == length);
    text.resize(length);
    return text;
  }
  /** Test-only subclass witnesses notification silence and child-before-parent destruction. */
  struct Messages
  {
    HWND parking, edit;
    WNDPROC previous;
    unsigned commands, changes, destroys;
    Messages(HWND parent, HWND child)
        : parking(parent),
          edit(child),
          previous(0),
          commands(0),
          changes(0),
          destroys(0)
    {
      SetWindowLongPtrW(parent, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
      this->previous =
          reinterpret_cast<WNDPROC>(SetWindowLongPtrW(parent, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&procedure)));
      LOKA_VERIFY(this->previous);
    }
    ~Messages()
    {
      if (IsWindow(this->parking))
      {
        SetWindowLongPtrW(this->parking, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(this->previous));
        SetWindowLongPtrW(this->parking, GWLP_USERDATA, 0);
      }
    }
    static LRESULT CALLBACK procedure(HWND hwnd, UINT message, WPARAM wp, LPARAM lp)
    {
      Messages &self = *reinterpret_cast<Messages *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
      if (message == WM_COMMAND)
      {
        ++self.commands;
        if (HIWORD(wp) == EN_CHANGE)
          ++self.changes;
      }
      if (message == WM_DESTROY)
      {
        LOKA_VERIFY(!IsWindow(self.edit));
        ++self.destroys;
      }
      return CallWindowProcW(self.previous, hwnd, message, wp, lp);
    }
  };
  /** Observe the detach line from the real EDIT destruction path. */
  struct EditRetirement
  {
    HWND edit;
    WNDPROC previous;
    unsigned destroys;
    explicit EditRetirement(HWND hwnd)
        : edit(hwnd),
          previous(0),
          destroys(0)
    {
      LOKA_VERIFY(SetPropW(hwnd, L"LOKA_TEST_TEXT_RETIRE", this));
      this->previous =
          reinterpret_cast<WNDPROC>(SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&procedure)));
      LOKA_VERIFY(this->previous);
    }
    ~EditRetirement()
    {
      if (IsWindow(this->edit))
      {
        SetWindowLongPtrW(this->edit, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(this->previous));
        RemovePropW(this->edit, L"LOKA_TEST_TEXT_RETIRE");
      }
    }
    static LRESULT CALLBACK procedure(HWND hwnd, UINT message, WPARAM wp, LPARAM lp)
    {
      EditRetirement &self = *static_cast<EditRetirement *>(GetPropW(hwnd, L"LOKA_TEST_TEXT_RETIRE"));
      if (message == WM_DESTROY)
      {
        LOKA_VERIFY(!GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        LOKA_VERIFY(IsWindow(GetParent(hwnd)));
        ++self.destroys;
      }
      WNDPROC previous = self.previous;
      if (message == WM_NCDESTROY)
        RemovePropW(hwnd, L"LOKA_TEST_TEXT_RETIRE");
      return CallWindowProcW(previous, hwnd, message, wp, lp);
    }
  };
  unsigned injectedBirthMarks = 0;
  LRESULT CALLBACK injectBirthMark(int code, WPARAM wp, LPARAM lp)
  {
    if (code >= 0)
    {
      const CWPSTRUCT &call = *reinterpret_cast<const CWPSTRUCT *>(lp);
      if (call.message == EM_SETSEL && GetWindowLongPtrW(call.hwnd, GWLP_USERDATA))
      {
        ++injectedBirthMarks;
        SendMessageW(GetParent(call.hwnd), WM_COMMAND, MAKEWPARAM(0, EN_CHANGE), reinterpret_cast<LPARAM>(call.hwnd));
        SendMessageW(call.hwnd, EM_SETMODIFY, TRUE, 0);
      }
    }
    return CallNextHookEx(0, code, wp, lp);
  }
  void chunkResult(Fixture &f,
                   TextStoreRow &row,
                   unsigned long from,
                   unsigned long to,
                   unsigned long capacity,
                   TextChunkResult expected,
                   unsigned long next,
                   const std::string &bytes)
  {
    char buffer[64];
    unsigned long written = 99, offset = from;
    LOKA_VERIFY(f.controller.readTextChunk(row, offset, to, buffer, capacity, written) == expected);
    LOKA_VERIFY(offset == next && written == bytes.size());
    if (written)
      LOKA_VERIFY(std::memcmp(buffer, bytes.data(), written) == 0);
  }
} // namespace
void testWin32TextStoreBirthAndRead()
{
  Fixture f;
  TextStoreReading reading;
  TextStoreRow *old = f.birth(replacement("old"), reading);
  HWND oldEdit = TextStoreAccess::edit(*old);
  Messages messages(GetParent(oldEdit), oldEdit);
  const unsigned long offsets[] = {0, 1, 2, 3, 4, 5, 6, 99};
  const unsigned long expected[] = {0, 1, 3, 3, 3, 3, 5, 6};
  for (unsigned i = 0; i < sizeof offsets / sizeof offsets[0]; ++i)
  {
    TextStoreRow *row =
        f.birth(replacement("a\n\xf0\x9f\x98\x80z", TextPlacement::AtUtf8Offset(offsets[i])), reading, old);
    HWND edit = TextStoreAccess::edit(*row);
    LOKA_VERIFY(native(edit) == L"a\r\n\xd83d\xde00z");
    LOKA_VERIFY(reading.caret() == expected[i] && reading.anchor() == expected[i]);
    TextChangedRange range;
    LOKA_VERIFY(reading.queryRange(range) && range.start() == 0 && range.length() == 0 && range.insertedLength() == 6);
    LOKA_VERIFY(!reading.changed() && !reading.marked());
    LOKA_VERIFY(!SendMessageW(edit, EM_CANUNDO, 0, 0));
    LOKA_VERIFY(!f.controller.peekTextStore(*row));
    LOKA_VERIFY(native(oldEdit) == L"old");
    row->retire();
  }
  LOKA_VERIFY(messages.commands == 0);
  SendMessageW(oldEdit, EM_SETSEL, 0, 2);
  LOKA_VERIFY(f.controller.readTextStore(*old, reading));
  LOKA_VERIFY(reading.anchor() == 0 && reading.caret() == 2 && !reading.changed());
  SendMessageW(oldEdit, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L"x"));
  LOKA_VERIFY(messages.changes > 0); // Positive control for notification silence.
  LOKA_VERIFY(f.controller.peekTextStore(*old) && f.controller.peekTextStore(*old));
  LOKA_VERIFY(f.controller.readTextStore(*old, reading) && reading.changed());
  TextChangedRange range;
  LOKA_VERIFY(!reading.queryRange(range));
  LOKA_VERIFY(!f.controller.peekTextStore(*old));
  SendMessageW(oldEdit, EM_SETMODIFY, TRUE, 0);
  LOKA_VERIFY(f.controller.peekTextStore(*old));
  LOKA_VERIFY(f.controller.readTextStore(*old, reading) && reading.changed());
  LOKA_VERIFY(!SendMessageW(oldEdit, EM_GETMODIFY, 0, 0));
  LOKA_VERIFY(f.controller.applyTextRequest(*old, 1));
  const unsigned before = messages.commands;
  f.controller.commitMarkedText(*old);
  LOKA_VERIFY(f.controller.readTextStore(*old, reading));
  LOKA_VERIFY(reading.caret() == 1 && reading.anchor() == 1 && !reading.marked() && !reading.changed());
  LOKA_VERIFY(messages.commands == before);
  injectedBirthMarks = 0;
  HHOOK hook = SetWindowsHookExW(WH_CALLWNDPROC, &injectBirthMark, 0, GetCurrentThreadId());
  LOKA_VERIFY(hook);
  TextStoreRow *injected = f.birth(replacement("marked during birth"), reading);
  LOKA_VERIFY(UnhookWindowsHookEx(hook));
  LOKA_VERIFY(injectedBirthMarks == 1 && !reading.changed() && !reading.marked());
  LOKA_VERIFY(!f.controller.peekTextStore(*injected));
  TextStoreSource source(TextReplacement::Empty());
  LOKA_VERIFY(f.controller.applyTextSplice(*old, 0, 0, source, reading) == TEXT_SPLICE_UNAVAILABLE);
}
void testWin32TextStoreChunks()
{
  Fixture f;
  TextStoreReading reading;
  TextStoreRow *row = f.birth(TextReplacement::Empty(), reading);
  HWND edit = TextStoreAccess::edit(*row);
  LOKA_VERIFY(SetWindowTextW(edit, L"a\r\nb\r\n\xd83d\xde00\x2028"));
  chunkResult(f, *row, 0, kTextNativeEnd, 64, TEXT_CHUNK_PROGRESS, 9, "a\nb\n\xf0\x9f\x98\x80\xe2\x80\xa8");
  chunkResult(f, *row, 9, kTextNativeEnd, 1, TEXT_CHUNK_END, 9, "");
  chunkResult(f, *row, 6, 8, 3, TEXT_CHUNK_NO_ROOM, 6, "");
  chunkResult(f, *row, 6, 8, 4, TEXT_CHUNK_PROGRESS, 8, "\xf0\x9f\x98\x80");
  chunkResult(f, *row, 0, 6, 3, TEXT_CHUNK_PROGRESS, 4, "a\nb");
  chunkResult(f, *row, 1, 3, 1, TEXT_CHUNK_PROGRESS, 3, "\n");
  chunkResult(f, *row, 2, 3, 4, TEXT_CHUNK_REFUSED_RANGE, 2, "");
  chunkResult(f, *row, 0, 2, 4, TEXT_CHUNK_REFUSED_RANGE, 0, "");
  chunkResult(f, *row, 7, 8, 4, TEXT_CHUNK_REFUSED_RANGE, 7, "");
  chunkResult(f, *row, 0, 7, 4, TEXT_CHUNK_REFUSED_RANGE, 0, "");
  chunkResult(f, *row, 4, 3, 4, TEXT_CHUNK_REFUSED_RANGE, 4, "");
  chunkResult(f, *row, 0, 10, 4, TEXT_CHUNK_REFUSED_RANGE, 0, "");
  chunkResult(f, *row, 10, kTextNativeEnd, 4, TEXT_CHUNK_REFUSED_RANGE, 10, "");
  chunkResult(f, *row, 0, 0, 0, TEXT_CHUNK_REFUSED_RANGE, 0, "");
  LOKA_VERIFY(SetWindowTextW(edit, L"ab\xd800"));
  chunkResult(f, *row, 0, kTextNativeEnd, 64, TEXT_CHUNK_REFUSED_MALFORMED, 0, "");
  chunkResult(f, *row, 0, kTextNativeEnd, 2, TEXT_CHUNK_PROGRESS, 2, "ab");
  chunkResult(f, *row, 2, kTextNativeEnd, 4, TEXT_CHUNK_REFUSED_MALFORMED, 2, "");
  LOKA_VERIFY(SetWindowTextW(edit, L"\xdc00"));
  chunkResult(f, *row, 0, kTextNativeEnd, 4, TEXT_CHUNK_REFUSED_MALFORMED, 0, "");
  LOKA_VERIFY(SetWindowTextW(edit, L"a\rb\nc\r\r\n"));
  chunkResult(f, *row, 0, kTextNativeEnd, 64, TEXT_CHUNK_PROGRESS, 8, "a\nb\nc\n\n");
  row->retire();
  chunkResult(f, *row, 0, kTextNativeEnd, 4, TEXT_CHUNK_REFUSED_RESOURCE, 0, "");
}
void testWin32TextStoreLimitsAndEol()
{
  Fixture f;
  TextStoreReading reading;
  TextStoreRow *row = f.birth(replacement(std::string(70000, 'a'), TextPlacement::End()), reading);
  HWND edit = TextStoreAccess::edit(*row);
  LOKA_VERIFY(reading.caret() == 70000);
  LOKA_VERIFY(static_cast<unsigned long>(SendMessageW(edit, EM_GETLIMITTEXT, 0, 0)) > 70000);
  SendMessageW(edit, WM_CHAR, L'b', 1);
  LOKA_VERIFY(GetWindowTextLengthW(edit) == 70001);
  LOKA_VERIFY(f.controller.readTextStore(*row, reading));
  LOKA_VERIFY(reading.caret() == 70001 && reading.anchor() == 70001 && reading.changed());
  LOKA_VERIFY(SetWindowTextW(edit, L"a\nb"));
  // Capability witness is independent of the production style request: deleting
  // that request must fail on a capable EDIT, not silently select the old-OS branch.
  HWND control = CreateWindowExW(
      0, L"EDIT", L"", WS_CHILD | ES_MULTILINE, 0, 0, 320, 240, GetParent(edit), 0, GetModuleHandleW(0), 0);
  LOKA_VERIFY(control);
  SendMessageW(control, 0x150a /* EM_SETEXTENDEDSTYLE */, 3, 3);
  const bool allows = (SendMessageW(control, 0x150b /* EM_GETEXTENDEDSTYLE */, 0, 0) & 3) == 3;
  LOKA_VERIFY(((SendMessageW(edit, 0x150b, 0, 0) & 3) == 3) == allows);
  LOKA_VERIFY(DestroyWindow(control));
  LOKA_VERIFY(SetWindowPos(edit, 0, 0, 0, 320, 240, SWP_NOZORDER | SWP_NOACTIVATE));
  LOKA_VERIFY(SendMessageW(edit, EM_GETLINECOUNT, 0, 0) == (allows ? 2 : 1));
  std::printf("[skip] ALLOWEOL %s branch requires the other EDIT configuration (run v6 and user32)\n",
              allows ? "unsupported" : "supported");
  for (unsigned long i = 0; i <= 3; ++i)
  {
    LOKA_VERIFY(f.controller.applyTextRequest(*row, i));
    LOKA_VERIFY(f.controller.readTextStore(*row, reading));
    LOKA_VERIFY(reading.caret() == i && reading.anchor() == i);
  }
  SendMessageW(edit, EM_SETSEL, 1, 3);
  LOKA_VERIFY(f.controller.readTextStore(*row, reading));
  LOKA_VERIFY(reading.anchor() == 1 && reading.caret() == 3);
  chunkResult(f, *row, 0, kTextNativeEnd, 64, TEXT_CHUNK_PROGRESS, 3, "a\nb");
}
void testWin32TextStoreFailuresAndRetirement()
{
  Fixture f;
  TextStoreReading reading;
  failTextParkingCreations(1);
  TextStoreSource empty(TextReplacement::Empty());
  TextStoreRow *fresh = 0;
  LOKA_VERIFY(f.controller.createTextStore(empty, 0, fresh, reading) == TEXT_STORE_REFUSED_UNSUPPORTED && !fresh);
  TextStoreRow *old = f.birth(replacement("old"), reading);
  HWND edit = TextStoreAccess::edit(*old), parking = GetParent(edit);
  Messages messages(parking, edit);
  SendMessageW(edit, EM_SETSEL, 1, 2);
  SendMessageW(edit, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L"X"));
  const std::wstring before = native(edit);
  const LRESULT undo = SendMessageW(edit, EM_CANUNDO, 0, 0);
  typedef void (*Fail)(unsigned);
  const Fail failures[] = {failTextLocalAllocations, failTextEditCreations, failTextDpiQualifications};
  for (unsigned i = 0; i < 3; ++i)
  {
    failures[i](2);
    for (unsigned repeat = 0; repeat < 2; ++repeat)
    {
      TextStoreSource source(replacement("new"));
      fresh = old;
      LOKA_VERIFY(f.controller.createTextStore(source, old, fresh, reading)
                  == (i == 0 ? TEXT_STORE_REFUSED_ALLOCATION : TEXT_STORE_REFUSED_UNSUPPORTED));
      LOKA_VERIFY(!fresh && native(edit) == before && f.controller.peekTextStore(*old));
      LOKA_VERIFY(SendMessageW(edit, EM_CANUNDO, 0, 0) == undo);
      DWORD from = 0, to = 0;
      SendMessageW(edit, EM_GETSEL, reinterpret_cast<WPARAM>(&from), reinterpret_cast<LPARAM>(&to));
      LOKA_VERIFY(from == 2 && to == 2);
    }
  }
  TextStoreSource nul(replacement(std::string("a\0b", 3)));
  LOKA_VERIFY(f.controller.createTextStore(nul, old, fresh, reading) == TEXT_STORE_REFUSED_NOT_REPRESENTABLE && !fresh);
  LOKA_VERIFY(f.controller.readTextStore(*old, reading));
  old->retire();
  old->retire();
  LOKA_VERIFY(!GetWindowLongPtrW(edit, GWLP_USERDATA));
  SendMessageW(parking, WM_COMMAND, MAKEWPARAM(0, EN_CHANGE), reinterpret_cast<LPARAM>(edit));
  LOKA_VERIFY(!TextStoreAccess::marked(*old));
  const unsigned commands = messages.commands;
  LOKA_VERIFY(IsWindow(edit));
  f.controller.drainNativeRetirements();
  LOKA_VERIFY(!IsWindow(edit) && IsWindow(parking) && messages.commands == commands);
  f.controller.destroy();
  LOKA_VERIFY(!IsWindow(parking) && messages.destroys == 1 && messages.commands == commands);
  f.controller.destroy();
}
void testWin32TextStoreDpiQualification()
{
  typedef HANDLE(WINAPI * GetContext)(HWND);
  typedef HANDLE(WINAPI * SetContext)(HANDLE);
  typedef BOOL(WINAPI * Equal)(HANDLE, HANDLE);
  HMODULE user = GetModuleHandleW(L"user32.dll");
  GetContext get = reinterpret_cast<GetContext>(GetProcAddress(user, "GetWindowDpiAwarenessContext"));
  SetContext set = reinterpret_cast<SetContext>(GetProcAddress(user, "SetThreadDpiAwarenessContext"));
  Equal equal = reinterpret_cast<Equal>(GetProcAddress(user, "AreDpiAwarenessContextsEqual"));
  if (!get || !set || !equal)
  {
    std::printf("[skip] DPI negative control needs Windows 10 1607 APIs\n");
    return;
  }
  Fixture f;
  Win32TextParking parking;
  HANDLE saved = set(reinterpret_cast<HANDLE>(static_cast<INT_PTR>(-1)));
  LOKA_VERIFY(saved && parking.ensure());
  set(saved);
  HWND witness = CreateWindowExW(
      0, L"EDIT", L"", WS_CHILD | ES_MULTILINE, 0, 0, 0, 0, parking.window(), 0, GetModuleHandleW(0), 0);
  LOKA_VERIFY(witness);
  const bool differs = !equal(get(witness), get(parking.window()));
  DestroyWindow(witness);
  if (!differs)
  {
    std::printf("[skip] DPI negative control: native child inherited parking awareness; no real mismatch\n");
    return;
  }
  TextStoreSource source(TextReplacement::Empty());
  TextStoreRow *fresh = 0;
  TextStoreReading reading;
  LOKA_VERIFY(Win32TextStore::create(parking, f.controller, source, fresh, reading) == TEXT_STORE_REFUSED_UNSUPPORTED);
  LOKA_VERIFY(!fresh);
}

#include "Win32Window.hpp"
#include "platform/null/NullPlatformContext.hpp"
#include "support/WindowAdmissionTestApp.hpp"
#include "support/TextDocumentAccess.hpp"
#include "testing/scene/SceneTestFlow.hpp"
#include "app/nodes/boundary/StdComposition.hpp"
#include "core/util/StateTrackerGuard.hpp"
namespace
{
  unsigned rootDeaths = 0;
  class StoreRoot : public BoundaryNodeFor<StoreRoot>
  {
  public:
    TextDocument text;
    explicit StoreRoot(const BoundaryPropsFor<StoreRoot> &p)
        : BoundaryNodeFor<StoreRoot>(p)
    {
      this->document(this->text);
    }
    virtual ~StoreRoot()
    {
      // Scene destroys the platform before reclaiming the logical root.
      LOKA_VERIFY(!loka::app::testing::TextDocumentAccess::registration(this->text)->store());
      ++rootDeaths;
    }
    virtual void composeNode(NodeComposition &) {}
  };
} // namespace
void testWin32TextStoreWindowTeardown()
{
  typedef loka::dsl::testing::SceneTestAccess SceneAccess;
  for (unsigned external = 0; external < 2; ++external)
  {
    const unsigned deaths = rootDeaths;
    NullPlatformContext context;
    WindowProps props;
    props.frame(40, 40, 257, 163).visible(true).scene(new Scene(Boundary<StoreRoot>()));
    Win32Window window(&context, props);
    WindowAdmissionTestApp app(window);
    app.flush();
    app.reconcileAtCompletion();
    StoreRoot &root = *static_cast<StoreRoot *>(SceneAccess::rootBoundary(*window.scene()));
    LOKA_VERIFY(root.text.fact()->get().state() == TEXT_DOCUMENT_READY);
    TextStoreRow *row = loka::app::testing::TextDocumentAccess::registration(root.text)->store();
    HWND edit = TextStoreAccess::edit(*row), parking = GetParent(edit);
    LOKA_VERIFY(IsWindow(edit) && IsWindow(parking));
    LOKA_VERIFY(root.text.replace(replacement("a\n\xf0\x9f\x98\x80", TextPlacement::End())) == POST_ACCEPTED);
    app.reconcileAtCompletion();
    unsigned long length = 0;
    LOKA_VERIFY(root.text.queryLength(length) && length == 6);
    LOKA_VERIFY(root.text.fact()->get().textRevision() == 1);
    row = loka::app::testing::TextDocumentAccess::registration(root.text)->store();
    HWND freshEdit = TextStoreAccess::edit(*row);
    LOKA_VERIFY(freshEdit != edit && native(edit).empty());
    window.drainNativeRetirements();
    LOKA_VERIFY(!IsWindow(edit) && IsWindow(freshEdit));
    Messages messages(parking, freshEdit);
    if (external)
      LOKA_VERIFY(DestroyWindow(window.hwnd()));
    else
    {
      {
        loka::core::StateTrackerGuard guard(window.getTracker());
        window.visibilityState().set(false);
      }
      app.flush();
    }
    LOKA_VERIFY(rootDeaths == deaths + 1);
    LOKA_VERIFY(!IsWindow(freshEdit) && !IsWindow(parking));
    LOKA_VERIFY(messages.commands == 0 && messages.destroys == 1);
  }
}

void testWin32TextStoreControllerDestructor()
{
  HWND root = CreateWindowExW(0, L"STATIC", L"destructor", WS_OVERLAPPED, 0, 0, 320, 240, 0, 0, GetModuleHandleW(0), 0);
  LOKA_VERIFY(root);
  EditRetirement *editWitness = 0;
  Messages *parkingWitness = 0;
  TextStoreLink link;
  {
    Win32ScenePlatformController controller(root, loka::win32::Win32DisplayScale(96, RailMetrics()));
    TextStoreSource source(replacement("live until controller destructor"));
    TextStoreRow *row = 0;
    TextStoreReading reading;
    LOKA_VERIFY(controller.createTextStore(source, 0, row, reading) == TEXT_STORE_BORN);
    row->connect(link);
    HWND edit = TextStoreAccess::edit(*row);
    editWitness = new EditRetirement(edit);
    parkingWitness = new Messages(GetParent(edit), edit);
    // No destroy() call: virtual retirement must occur in the derived destructor body.
  }
  LOKA_VERIFY(!link.row());
  LOKA_VERIFY(editWitness->destroys == 1 && parkingWitness->destroys == 1);
  LOKA_VERIFY(parkingWitness->commands == 0);
  delete editWitness;
  delete parkingWitness;
  LOKA_VERIFY(DestroyWindow(root));
}
namespace
{
  class BirthController : public Win32ScenePlatformController
  {
  public:
    TextStoreReading birthReading;
    TextStoreRow *fresh;
    explicit BirthController(HWND root)
        : Win32ScenePlatformController(root, loka::win32::Win32DisplayScale(96, RailMetrics())),
          fresh(0)
    {
    }
    virtual TextStoreBirth
    createTextStore(TextStoreSource &source, TextStoreRow *old, TextStoreRow *&out, TextStoreReading &reading)
    {
      const TextStoreBirth result = Win32ScenePlatformController::createTextStore(source, old, out, reading);
      if (result == TEXT_STORE_BORN)
      {
        this->fresh = out;
        this->birthReading = reading;
      }
      return result;
    }
  };
  /** Fixture window borrows a real rail so the shared conformance driver uses App completion. */
  class BorrowedStoreWindow : public Window
  {
  public:
    BorrowedStoreWindow(PlatformContext *context, const WindowProps &props, BirthController &controller)
        : Window(context, props)
    {
      LOKA_VERIFY(this->scene()->mount(&controller));
      loka::dsl::testing::SceneTestAccess::updateAttached(*this->scene(), true);
    }
    virtual ~BorrowedStoreWindow()
    {
      this->unmountSceneForTeardown(*this->scene());
    }

  protected:
    virtual bool hasLiveScenePlatform() const
    {
      return true;
    }
  };
} // namespace
void testWin32TextStoreConformance()
{
  HWND hwnd =
      CreateWindowExW(0, L"STATIC", L"conformance", WS_OVERLAPPED, 0, 0, 320, 240, 0, 0, GetModuleHandleW(0), 0);
  LOKA_VERIFY(hwnd);
  {
    BirthController controller(hwnd);
    NullPlatformContext context;
    WindowProps props;
    props.scene(new Scene(Boundary<StoreRoot>()));
    BorrowedStoreWindow window(&context, props, controller);
    WindowAdmissionTestApp app(window);
    app.flush();
    app.reconcileAtCompletion();
    StoreRoot &root = *static_cast<StoreRoot *>(loka::dsl::testing::SceneTestAccess::rootBoundary(*window.scene()));
    TextStoreRow *old = loka::app::testing::TextDocumentAccess::registration(root.text)->store();
    LOKA_VERIFY(old);
    loka::app::testing::checkTextStoreConformance(
        controller, app, root.text, *old, replacement("a\n\xf0\x9f\x98\x80", TextPlacement::End()), 5, 5);
    controller.drainNativeRetirements();
    LOKA_VERIFY(loka::app::testing::TextStoreTestAccess::retiredCount(*controller.fresh) == 0);
  }
  LOKA_VERIFY(DestroyWindow(hwnd));
}
