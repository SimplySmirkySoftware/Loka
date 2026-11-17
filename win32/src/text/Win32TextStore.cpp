#include "Win32TextStore.hpp"
#include "Win32TextParking.hpp"
#include "../context/Win32FocusParticipant.hpp"
#include "../Win32ScenePlatformController.hpp"
#include <cassert>
#include "app/text/TextStoreSource.hpp"
#include <imm.h>
#include <climits>
#include <new>

using namespace loka::app;
using namespace loka::app::scene;
namespace
{
  /** Temporary movable buffer. Only release transfers ownership to EDIT. */
  class BirthBuffer
  {
  public:
    BirthBuffer()
        : handle_(0),
          size_(0),
          capacity_(0)
    {
    }
    ~BirthBuffer()
    {
      if (this->handle_)
        LocalFree(this->handle_);
    }
    TextStoreBirth append(unsigned int scalar, bool separator)
    {
      if (!scalar)
        return TEXT_STORE_REFUSED_NOT_REPRESENTABLE;
      const unsigned long units = separator || scalar > 0xffff ? 2 : 1;
      if (this->size_ > static_cast<unsigned long>(INT_MAX) - units - 1)
        return TEXT_STORE_REFUSED_CAPACITY;
      const unsigned long needed = this->size_ + units + 1;
      if (needed > this->capacity_)
      {
        const unsigned long capacity = needed < 256                                        ? 256
                                       : needed <= static_cast<unsigned long>(INT_MAX) / 2 ? needed * 2
                                                                                           : needed;
        if (capacity > static_cast<SIZE_T>(-1) / sizeof(wchar_t))
          return TEXT_STORE_REFUSED_CAPACITY;
#ifdef TEST_BUILD
        if (loka::win32::testing::refuseTextLocalAllocation())
          return TEXT_STORE_REFUSED_ALLOCATION;
#endif
        HLOCAL fresh = this->handle_ ? LocalReAlloc(this->handle_, capacity * sizeof(wchar_t), LMEM_MOVEABLE)
                                     : LocalAlloc(LMEM_MOVEABLE, capacity * sizeof(wchar_t));
        if (!fresh)
          return TEXT_STORE_REFUSED_ALLOCATION;
        this->handle_ = fresh;
        this->capacity_ = capacity;
      }
      wchar_t *bytes = static_cast<wchar_t *>(LocalLock(this->handle_));
      if (!bytes)
        return TEXT_STORE_REFUSED_ALLOCATION;
      if (separator)
      {
        bytes[this->size_++] = L'\r';
        bytes[this->size_++] = L'\n';
      }
      else if (scalar > 0xffff)
      {
        scalar -= 0x10000;
        bytes[this->size_++] = static_cast<wchar_t>(0xd800 + (scalar >> 10));
        bytes[this->size_++] = static_cast<wchar_t>(0xdc00 + (scalar & 0x3ff));
      }
      else
        bytes[this->size_++] = static_cast<wchar_t>(scalar);
      bytes[this->size_] = 0;
      LocalUnlock(this->handle_);
      return TEXT_STORE_BORN;
    }
    TextStoreBirth finish()
    {
      if (this->handle_)
        return TEXT_STORE_BORN;
#ifdef TEST_BUILD
      if (loka::win32::testing::refuseTextLocalAllocation())
        return TEXT_STORE_REFUSED_ALLOCATION;
#endif
      this->handle_ = LocalAlloc(LMEM_MOVEABLE | LMEM_ZEROINIT, sizeof(wchar_t));
      return this->handle_ ? TEXT_STORE_BORN : TEXT_STORE_REFUSED_ALLOCATION;
    }
    unsigned long size() const
    {
      return this->size_;
    }
    HLOCAL release()
    {
      HLOCAL result = this->handle_;
      this->handle_ = 0;
      return result;
    }

  private:
    BirthBuffer(const BirthBuffer &);
    BirthBuffer &operator=(const BirthBuffer &);
    HLOCAL handle_;
    unsigned long size_, capacity_;
  };
  bool equalDpi(HWND edit, HWND parking)
  {
#ifdef TEST_BUILD
    if (loka::win32::testing::refuseTextDpiQualification())
      return false;
#endif
    // HANDLE keeps the dynamic seam buildable with the XP SDK contract.
    typedef HANDLE(WINAPI * GetContext)(HWND);
    typedef BOOL(WINAPI * EqualContexts)(HANDLE, HANDLE);
    HMODULE user = GetModuleHandleW(L"user32.dll");
    GetContext get = reinterpret_cast<GetContext>(GetProcAddress(user, "GetWindowDpiAwarenessContext"));
    EqualContexts equal = reinterpret_cast<EqualContexts>(GetProcAddress(user, "AreDpiAwarenessContextsEqual"));
    return !get || !equal || equal(get(edit), get(parking));
  }
  bool high(wchar_t c)
  {
    return c >= 0xd800 && c <= 0xdbff;
  }
  bool low(wchar_t c)
  {
    return c >= 0xdc00 && c <= 0xdfff;
  }
  bool canonical(const wchar_t *text, unsigned long length, unsigned long offset)
  {
    return offset <= length
           && (!offset || offset == length
               || !((text[offset - 1] == L'\r' && text[offset] == L'\n')
                    || (high(text[offset - 1]) && low(text[offset]))));
  }
  /** Locks only while chunk reads native memory; all exits unlock. */
  class LockedText
  {
  public:
    explicit LockedText(HLOCAL handle)
        : handle_(handle),
          text_(handle ? static_cast<const wchar_t *>(LocalLock(handle)) : 0)
    {
    }
    ~LockedText()
    {
      if (this->text_)
        LocalUnlock(this->handle_);
    }
    const wchar_t *get() const
    {
      return this->text_;
    }

  private:
    LockedText(const LockedText &);
    LockedText &operator=(const LockedText &);
    HLOCAL handle_;
    const wchar_t *text_;
  };
} // namespace
TextStoreBirth Win32TextStore::create(Win32TextParking &parking,
                                      IPlatformController &controller,
                                      TextStoreSource &source,
                                      TextStoreRow *old,
                                      TextStoreRow *&fresh,
                                      TextStoreReading &reading)
{
  fresh = 0;
  if (!parking.ensure())
    return TEXT_STORE_REFUSED_UNSUPPORTED;
  BirthBuffer buffer;
  const TextPlacement placement = source.requestedPlacement();
  unsigned long caret = 0;
  unsigned int scalar = 0;
  bool separator = false;
  while (source.next(scalar, separator))
  {
    TextStoreBirth result = buffer.append(scalar, separator);
    if (result != TEXT_STORE_BORN)
      return result;
    // Interior UTF-8 offsets round down; past-end offsets clamp to the end.
    if (placement.kind() == TextPlacement::END
        || (placement.kind() == TextPlacement::AT_UTF8_OFFSET && source.logicalOffset() <= placement.utf8Offset()))
      caret = buffer.size();
  }
  TextStoreBirth result = buffer.finish();
  if (result != TEXT_STORE_BORN)
    return result;
  Win32TextStore *row = new (std::nothrow) Win32TextStore(parking.ledger(), controller);
  if (!row)
    return TEXT_STORE_REFUSED_ALLOCATION;
#ifdef TEST_BUILD
  if (!loka::win32::testing::refuseTextEditCreation())
#endif
    row->edit_ = CreateWindowExW(0,
                                 L"EDIT",
                                 L"",
                                 WS_CHILD | WS_TABSTOP | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN,
                                 0,
                                 0,
                                 0,
                                 0,
                                 parking.window(),
                                 0,
                                 GetModuleHandleW(0),
                                 0);
  const HWND edit = row->edit_;
  if (!edit || !equalDpi(edit, parking.window()))
  {
    row->retire();
    return TEXT_STORE_REFUSED_UNSUPPORTED;
  }
  // P3-1 (page 3): both user32 and v6 EDIT accept ownership, clear undo,
  // and send no EN_CHANGE/EN_UPDATE. The displaced empty buffer is ours.
  const UINT setExtendedStyle = 0x150a;       // EM_SETEXTENDEDSTYLE, also with older SDKs.
  SendMessageW(edit, setExtendedStyle, 3, 3); // ES_EX_ALLOWEOL_ALL
  SendMessageW(edit, EM_SETLIMITTEXT, 0, 0);
  HLOCAL previous = reinterpret_cast<HLOCAL>(SendMessageW(edit, EM_GETHANDLE, 0, 0));
  SendMessageW(edit, EM_SETHANDLE, reinterpret_cast<WPARAM>(buffer.release()), 0);
  if (previous)
    LocalFree(previous);
  SetWindowLongPtrW(edit, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(row));
  SendMessageW(edit, EM_SETSEL, caret, caret);
  SendMessageW(edit, EM_SETMODIFY, FALSE, 0);
  if (!row->matches(edit))
  {
    row->retire();
    return TEXT_STORE_REFUSED_UNSUPPORTED;
  }
  Win32TextStore *previousRow = static_cast<Win32TextStore *>(old);
#ifdef LOKA_LIFECYCLE_AUDIT
  assert(!previousRow || previousRow->viewAttached() == (GetParent(previousRow->edit_) != parking.window()));
#endif
  if (previousRow && previousRow->viewAttached())
  {
    if (!row->prepareReplacement(*previousRow))
    {
      row->detach(parking.window());
      row->retire();
      return TEXT_STORE_REFUSED_UNSUPPORTED;
    }
    const bool hadFocus = GetFocus() == previousRow->edit_;
    transferView(*previousRow, *row);
    if (GetWindowLongW(previousRow->edit_, GWL_STYLE) & WS_VISIBLE)
      Win32ScenePlatformController::swapNativeReplacement(edit, previousRow->edit_);
    previousRow->detach(parking.window());
    if (hadFocus)
      SetFocus(edit);
  }
  row->changes_.take();
  const TextChangedRange range(0, 0, buffer.size());
  reading = TextStoreReading(false, &range, caret, caret, false);
  fresh = row;
  return TEXT_STORE_BORN;
}
Win32TextStore::~Win32TextStore()
{
  if (this->edit_)
    DestroyWindow(this->edit_);
}
bool Win32TextStore::matches(HWND edit) const
{
  return edit && this->edit_ == edit
         && reinterpret_cast<Win32TextStore *>(GetWindowLongPtrW(edit, GWLP_USERDATA)) == this;
}
void Win32TextStore::stopMarks()
{
  const HWND edit = this->edit_;
  if (edit)
    SetWindowLongPtrW(edit, GWLP_USERDATA, 0);
}
bool Win32TextStore::read(TextStoreReading &out)
{
  const HWND edit = this->edit_;
  if (!this->matches(edit) || GetWindowThreadProcessId(edit, 0) != GetCurrentThreadId())
    return false;
  const bool modified = SendMessageW(edit, EM_GETMODIFY, 0, 0) != 0;
  DWORD anchor = 0, caret = 0;
  SendMessageW(edit, EM_GETSEL, reinterpret_cast<WPARAM>(&anchor), reinterpret_cast<LPARAM>(&caret));
  bool marked = false;
  if (GetFocus() == edit)
  {
    HIMC context = ImmGetContext(edit);
    if (context)
    {
      marked = ImmGetCompositionStringW(context, GCS_COMPSTR, 0, 0) != 0;
      ImmReleaseContext(edit, context);
    }
  }
  if (!this->matches(edit))
    return false;
  if (modified)
    this->changes_.noteEdit(0);
  SendMessageW(edit, EM_SETMODIFY, FALSE, 0);
  if (!this->matches(edit))
    return false;
  const TextStoreReading note = this->changes_.take();
  // EDIT reports sorted endpoints: low is anchor, high is caret (Null's convention).
  out = TextStoreReading(note.changed(), 0, caret, anchor, marked);
  return true;
}
bool Win32TextStore::pending()
{
  const HWND edit = this->edit_;
  if (!this->matches(edit))
    return true;
  const bool modified = SendMessageW(edit, EM_GETMODIFY, 0, 0) != 0;
  return !this->matches(edit) || this->changes_.pending() || modified;
}
bool Win32TextStore::apply(unsigned long target)
{
  const HWND edit = this->edit_;
  if (!this->matches(edit) || target > static_cast<unsigned long>(INT_MAX))
    return false;
  SendMessageW(edit, EM_SETSEL, target, target);
  return this->matches(edit);
}
TextChunkResult Win32TextStore::chunk(
    unsigned long &offset, unsigned long end, char *out, unsigned long capacity, unsigned long &written)
{
  written = 0;
  if (!capacity || !out)
    return TEXT_CHUNK_REFUSED_RANGE;
  const HWND edit = this->edit_;
  if (!this->matches(edit))
    return TEXT_CHUNK_REFUSED_RESOURCE;
  const unsigned long length = static_cast<unsigned long>(GetWindowTextLengthW(edit));
  const HLOCAL handle = reinterpret_cast<HLOCAL>(SendMessageW(edit, EM_GETHANDLE, 0, 0));
  if (!this->matches(edit))
    return TEXT_CHUNK_REFUSED_RESOURCE;
  LockedText locked(handle);
  const wchar_t *text = locked.get();
  if (!text)
    return TEXT_CHUNK_REFUSED_RESOURCE;
  if (end == kTextNativeEnd)
    end = length;
  if (offset > end || !canonical(text, length, offset) || !canonical(text, length, end))
    return TEXT_CHUNK_REFUSED_RANGE;
  unsigned long cursor = offset, count = 0;
  while (cursor < end)
  {
    unsigned int scalar = text[cursor];
    unsigned long units = 1;
    if (high(text[cursor]))
    {
      if (cursor + 1 >= end || !low(text[cursor + 1]))
        return TEXT_CHUNK_REFUSED_MALFORMED;
      scalar = 0x10000 + ((scalar - 0xd800) << 10) + (text[cursor + 1] - 0xdc00);
      units = 2;
    }
    else if (low(text[cursor]))
      return TEXT_CHUNK_REFUSED_MALFORMED;
    else if (scalar == '\r')
    {
      scalar = '\n';
      if (cursor + 1 < end && text[cursor + 1] == L'\n')
        units = 2;
    }
    const unsigned long bytes = scalar < 0x80 ? 1 : scalar < 0x800 ? 2 : scalar < 0x10000 ? 3 : 4;
    if (bytes > capacity - count)
      break;
    if (bytes == 1)
      out[count] = static_cast<char>(scalar);
    else
    {
      const unsigned int prefix = bytes == 2 ? 0xc0 : bytes == 3 ? 0xe0 : 0xf0;
      out[count] = static_cast<char>(prefix | (scalar >> (6 * (bytes - 1))));
      for (unsigned long i = 1; i < bytes; ++i)
        out[count + i] = static_cast<char>(0x80 | ((scalar >> (6 * (bytes - i - 1))) & 0x3f));
    }
    count += bytes;
    cursor += units;
    if (count == capacity)
      break;
  }
  if (!count)
    return cursor == end ? TEXT_CHUNK_END : TEXT_CHUNK_NO_ROOM;
  offset = cursor;
  written = count;
  return TEXT_CHUNK_PROGRESS;
}

bool Win32TextStore::attach(HWND view, NodeContext &context, const TextViewPresentation &presentation)
{
  if (!presentation.wrap() || !equalDpi(this->edit_, view))
    return false;
  if (!Win32FocusParticipant::attach(this->edit_, &context))
    return false;
  if (!SetParent(this->edit_, view))
  {
    Win32FocusParticipant::detach(this->edit_);
    return false;
  }
  static_cast<Win32ScenePlatformController &>(this->controller()).requestRelayout();
  return true;
}
void Win32TextStore::detach(HWND parking)
{
  Win32FocusParticipant::detach(this->edit_);
  ShowWindow(this->edit_, SW_HIDE);
  const HWND previous = SetParent(this->edit_, parking);
#ifdef LOKA_LIFECYCLE_AUDIT
  assert(previous && "qualified EDIT must return to its live parking window");
#endif
  (void)previous;
  SendMessageW(this->edit_, WM_SETFONT, 0, FALSE);
}
void Win32TextStore::present(const TextViewPresentation &presentation)
{
#ifdef LOKA_LIFECYCLE_AUDIT
  assert(presentation.wrap() && !(GetWindowLongW(this->edit_, GWL_STYLE) & ES_AUTOHSCROLL));
#endif
  Win32ScenePlatformController &controller = static_cast<Win32ScenePlatformController &>(this->controller());
  this->present(presentation, controller.fallbackFont(presentation.style()));
}
void Win32TextStore::present(const TextViewPresentation &presentation, HFONT font)
{
  Win32ScenePlatformController &controller = static_cast<Win32ScenePlatformController &>(this->controller());
  SendMessageW(this->edit_,
               WM_SETFONT,
               reinterpret_cast<WPARAM>(font),
               (GetWindowLongW(this->edit_, GWL_STYLE) & WS_VISIBLE) != 0);
  SendMessageW(this->edit_, EM_SETREADONLY, presentation.readOnly(), 0);
  const int margin = controller.displayScale().nativeLength(0, presentation.margin()).px;
  SendMessageW(this->edit_, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(margin, margin));
}
bool Win32TextStore::prepareReplacement(const Win32TextStore &old)
{
  const HWND view = GetParent(old.edit_);
#ifdef TEST_BUILD
  if (loka::win32::testing::refuseTextViewPreparation(loka::win32::testing::TEXT_VIEW_DPI))
    return false;
#endif
  if (!equalDpi(this->edit_, view))
    return false;
#ifdef TEST_BUILD
  if (loka::win32::testing::refuseTextViewPreparation(loka::win32::testing::TEXT_VIEW_PROPERTY))
    return false;
#endif
  if (!Win32FocusParticipant::attach(this->edit_, Win32FocusParticipant::read(old.edit_)))
    return false;
#ifdef TEST_BUILD
  if (loka::win32::testing::refuseTextViewPreparation(loka::win32::testing::TEXT_VIEW_PARENT))
    return false;
#endif
  if (!SetParent(this->edit_, view))
    return false;
  SendMessageW(this->edit_, WM_SETFONT, SendMessageW(old.edit_, WM_GETFONT, 0, 0), FALSE);
  if (!SendMessageW(this->edit_, EM_SETREADONLY, (GetWindowLongW(old.edit_, GWL_STYLE) & ES_READONLY) != 0, 0))
    return false;
  SendMessageW(
      this->edit_, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, SendMessageW(old.edit_, EM_GETMARGINS, 0, 0));
#ifdef TEST_BUILD
  if (loka::win32::testing::refuseTextViewPreparation(loka::win32::testing::TEXT_VIEW_PRESENTATION))
    return false;
#endif
#ifdef TEST_BUILD
  if (loka::win32::testing::refuseTextViewPreparation(loka::win32::testing::TEXT_VIEW_POSITION))
    return false;
#endif
  return Win32ScenePlatformController::positionNativeReplacement(this->edit_, old.edit_);
}
