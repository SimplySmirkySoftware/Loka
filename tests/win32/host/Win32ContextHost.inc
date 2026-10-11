#include "platform/Win32DisplayScale.cpp"
#include "platform/Win32DisplayFont.cpp"
#include "context/Win32RetirableContext.cpp"
#include "context/Win32AttributedTextContext.cpp"
#include "context/Win32TextContext.cpp"
#include "context/Win32RectSurfaceContext.cpp"
#include <algorithm>

namespace win32_host
{
  int failFontAllocations = 0, fontAllocations = 0, drawMeasures = 0;
  int styleReads = 0, fontReads = 0, positions = 0;
  bool failDC = false, failMetrics = false, failConversion = false;
  std::vector<HWND> windows;
  WNDPROC attributedProc = 0, rectSurfaceProc = 0;
  std::vector<HBRUSH> fills;
} // namespace win32_host
// Deliberate platform-seam twin: keep this sequence in step with
// Win32ScenePlatformController::updateDisplayScale / ensureDisplayFont and
// applyDisplayFontToNativeSubtree in win32/src/Win32ScenePlatformController.cpp.
// Windows pins cover production ordering; this host pins context reactions.
namespace
{
  struct DisplayFontReplacement
  {
    const loka::win32::Win32DisplayFont &previous;
    const loka::win32::Win32DisplayFont &next;
  };

  BOOL CALLBACK ApplyDisplayFont(HWND hwnd, LPARAM fontValue)
  {
    const DisplayFontReplacement &fonts = *reinterpret_cast<const DisplayFontReplacement *>(fontValue);
    const HFONT previous = reinterpret_cast<HFONT>(SendMessageW(hwnd, WM_GETFONT, 0, 0));
    SendMessageW(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(fonts.previous.replacementFor(previous, fonts.next)), TRUE);
    return TRUE;
  }

} // namespace

void Win32ScenePlatformController::updateDisplayScale(const loka::win32::Win32DisplayScale &displayScale)
{
  this->displayScale_ = loka::win32::Win32DisplayScale(displayScale.dpi(), this->railMetrics_);
  this->textEnvironment_.changed();
  this->ensureDisplayFont();
}

void Win32ScenePlatformController::ensureDisplayFont()
{
  if (this->displayFont_.matches(this->displayScale_))
  {
    return;
  }
  loka::win32::Win32DisplayFont replacement;
  if (replacement.create(this->displayScale_))
  {
    this->textEnvironment_.changed();
    this->applyDisplayFontToNativeSubtree(replacement);
    this->displayFont_.swap(replacement);
  }
}

void Win32ScenePlatformController::applyDisplayFontToNativeSubtree(const loka::win32::Win32DisplayFont &replacement)
{
  if (!this->rootHwnd_ || !replacement.get())
  {
    return;
  }
  const DisplayFontReplacement fonts = {this->displayFont_, replacement};
  EnumChildWindows(this->rootHwnd_, &ApplyDisplayFont, reinterpret_cast<LPARAM>(&fonts));
}

HDC GetDC(HWND h)
{
  return win32_host::failDC || !h ? 0 : &h->dc;
}
int ReleaseDC(HWND, HDC)
{
  return 1;
}
LONG_PTR GetWindowLongPtrW(HWND h, int key)
{
  if (key == GWL_STYLE)
  {
    ++win32_host::styleReads;
    return h->style;
  }
  return h->data;
}
LONG_PTR SetWindowLongPtrW(HWND h, int key, LONG_PTR value)
{
  LONG_PTR &dest = key == GWL_STYLE ? h->style : h->data;
  LONG_PTR old = dest;
  dest = value;
  return old;
}
LRESULT SendMessageW(HWND h, UINT message, WPARAM w, LPARAM l)
{
  if (message == WM_GETFONT)
  {
    ++win32_host::fontReads;
    return reinterpret_cast<LRESULT>(h->dc.font);
  }
  if (message == WM_SETFONT)
    h->dc.font = reinterpret_cast<HFONT>(w);
  return h->proc ? h->proc(h, message, w, l) : 0;
}
BOOL GetClientRect(HWND h, RECT *r)
{
  *r = h->rect;
  r->right -= r->left;
  r->bottom -= r->top;
  r->left = r->top = 0;
  return TRUE;
}
BOOL GetWindowRect(HWND h, RECT *r)
{
  *r = h->rect;
  return TRUE;
}
BOOL IsRectEmpty(const RECT *r)
{
  return r->right <= r->left || r->bottom <= r->top;
}
BOOL InvalidateRect(HWND, const RECT *, BOOL)
{
  return TRUE;
}
BOOL ShowWindow(HWND, int)
{
  return TRUE;
}
HINSTANCE GetModuleHandleW(LPCWSTR)
{
  return 0;
}
HCURSOR LoadCursorW(HINSTANCE, LPCWSTR)
{
  return 0;
}
int RegisterClassW(const WNDCLASSW *wc)
{
  if (std::wcscmp(wc->lpszClassName, L"LOKA_RECT_SURFACE") == 0)
    win32_host::rectSurfaceProc = wc->lpfnWndProc;
  else
    win32_host::attributedProc = wc->lpfnWndProc;
  return 1;
}
LRESULT DefWindowProcW(HWND, UINT, WPARAM, LPARAM)
{
  return 0;
}
HDC BeginPaint(HWND h, PAINTSTRUCT *)
{
  return &h->dc;
}
BOOL EndPaint(HWND, const PAINTSTRUCT *)
{
  return TRUE;
}
int GetClipBox(HDC, RECT *r)
{
  r->left = r->top = 0;
  r->right = r->bottom = 100;
  return SIMPLEREGION;
}
BOOL EqualRect(const RECT *a, const RECT *b)
{
  return std::memcmp(a, b, sizeof(*a)) == 0;
}
int FillRect(HDC, const RECT *, HBRUSH brush)
{
  win32_host::fills.push_back(brush);
  return 1;
}
HGDIOBJ GetStockObject(int index)
{
  return reinterpret_cast<HGDIOBJ>(static_cast<intptr_t>(index + 1));
}
HBRUSH GetSysColorBrush(int index)
{
  return reinterpret_cast<HBRUSH>(static_cast<intptr_t>(index + 100));
}
// Exercise the real RectSurface direct-target fallback; native buffering and
// pixels remain VM pins. These deliberately refuse allocation, not paint.
HDC CreateCompatibleDC(HDC) { return 0; }
HBITMAP CreateCompatibleBitmap(HDC, int, int) { return 0; }
BOOL DeleteDC(HDC) { return TRUE; }
BOOL BitBlt(HDC, int, int, int, int, HDC, int, int, DWORD)
{
  LOKA_VERIFY(false && "buffered drawing is outside this host fixture");
  return FALSE;
}
int DrawTextW(HDC dc, LPCWSTR text, int, RECT *r, UINT flags)
{
  ++win32_host::drawMeasures;
  if (win32_host::failMeasure)
    return 0;
  int width = static_cast<int>(std::wcslen(text)) * dc->font->advance;
  int lines = 1;
  if ((flags & DT_WORDBREAK) && r->right > 0)
    lines = (width + r->right - 1) / r->right;
  else
    for (const wchar_t *unit = text; *unit; ++unit)
      if (*unit == L'\n')
        ++lines;
  r->bottom = (lines ? lines : 1) * (dc->font->ascent + dc->font->descent + dc->font->leading);
  return r->bottom;
}
int GetWindowTextLengthW(HWND h)
{
  return static_cast<int>(h->text.size());
}
int GetWindowTextW(HWND h, wchar_t *out, int count)
{
  const int length = std::min(count - 1, static_cast<int>(h->text.size()));
  std::wmemcpy(out, h->text.c_str(), length);
  out[length] = 0;
  return length;
}
BOOL SetWindowTextW(HWND h, LPCWSTR text)
{
  h->text = text;
  return TRUE;
}
HWND GetParent(HWND h)
{
  return h->parent;
}
int MapWindowPoints(HWND, HWND, POINT *, UINT)
{
  return 0;
}
BOOL EnumChildWindows(HWND root, BOOL (*callback)(HWND, LPARAM), LPARAM value)
{
  for (std::size_t i = 0; i < win32_host::windows.size(); ++i)
    if (win32_host::windows[i]->parent == root)
      callback(win32_host::windows[i], value);
  return TRUE;
}
HMODULE LoadLibraryExW(LPCWSTR, void *, DWORD)
{
  return 0;
}
BOOL FreeLibrary(HMODULE)
{
  return TRUE;
}
FARPROC GetProcAddress(HMODULE, const char *)
{
  return 0;
}
HMONITOR MonitorFromWindow(HWND, DWORD)
{
  return 0;
}
int GetDeviceCaps(HDC, int)
{
  return 96;
}
int MulDiv(int value, int numerator, int denominator)
{
  const int product = value * numerator;
  return (product >= 0 ? product + denominator / 2 : product - denominator / 2) / denominator;
}
BOOL AdjustWindowRectEx(RECT *, DWORD, BOOL, DWORD)
{
  return TRUE;
}
BOOL SystemParametersInfoW(UINT, UINT, PVOID out, UINT)
{
  NONCLIENTMETRICSW *metrics = static_cast<NONCLIENTMETRICSW *>(out);
  metrics->lfMessageFont.lfHeight = -12;
  return TRUE;
}
HFONT CreateFontIndirectW(const LOGFONTW *font)
{
  ++win32_host::fontAllocations;
  if (win32_host::failFontAllocations > 0)
  {
    --win32_host::failFontAllocations;
    return 0;
  }
  HostFont *result = new HostFont;
  result->ascent = std::abs(font->lfHeight);
  result->descent = 2;
  result->leading = 1;
  result->advance = std::max(1, result->ascent / 3);
  return result;
}
BOOL DeleteObject(HGDIOBJ object)
{
  delete static_cast<HFONT>(object);
  return TRUE;
}
void Win32ScenePlatformController::positionNativeWindow(HWND h, const loka::win32::NativeRect &r)
{
  ++win32_host::positions;
  h->rect = r.r;
}
HWND Win32ScenePlatformController::createNativeChildWindow(DWORD,
                                                           LPCWSTR name,
                                                           LPCWSTR,
                                                           DWORD style,
                                                           const loka::win32::NativeRect &rect,
                                                           HWND parent,
                                                           HMENU,
                                                           HINSTANCE,
                                                           void *parameter)
{
  HWND h = new HostWindow;
  h->rect = rect.r;
  h->dc.font = this->displayFont();
  h->dc.alignment = 0;
  h->dc.background = 0;
  h->dc.textColor = 0;
  h->data = 0;
  h->style = style;
  h->parent = parent;
  h->proc = std::wcscmp(name, L"STATIC") == 0 ? 0 :
      (std::wcscmp(name, L"LOKA_RECT_SURFACE") == 0 ? win32_host::rectSurfaceProc : win32_host::attributedProc);
  win32_host::windows.push_back(h);
  if (h->proc)
  {
    CREATESTRUCTW create = {parameter};
    h->proc(h, WM_NCCREATE, 0, reinterpret_cast<LPARAM>(&create));
  }
  return h;
}
void Win32ScenePlatformController::queueNativeRetirement(HWND h)
{
  win32_host::windows.erase(std::find(win32_host::windows.begin(), win32_host::windows.end(), h));
  delete h;
}
namespace loka
{
  namespace win32
  {
    bool MaterializeWideString(const loka::core::String &value, std::wstring &out)
    {
      if (win32_host::failConversion)
        return false;
      const loka::core::StringBuffer buffer = value.bufferWithEncoding(loka::core::StringEncodingUtf16);
      out.clear();
      for (std::size_t i = 0; i < buffer.length(); ++i)
        out.push_back(static_cast<wchar_t>(buffer.characterAt(i)));
      return true;
    }
  } // namespace win32
} // namespace loka
