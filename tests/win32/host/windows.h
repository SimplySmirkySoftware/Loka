#ifndef LOKA_TEST_GDI_WINDOWS_H
#define LOKA_TEST_GDI_WINDOWS_H
#include <cstddef>
#include <string>
#include <vector>
typedef unsigned long COLORREF;
#define COLOR_WINDOW 5
#define COLOR_WINDOWTEXT 8
#define COLOR_BTNFACE 15
#define COLOR_BTNTEXT 18
COLORREF GetSysColor(int);
struct HostDC;
COLORREF SetTextColor(HostDC *, COLORREF);
typedef int BOOL;
typedef unsigned int UINT;
// Host wchar_t holds the same UTF-16 unit values; this fixture is not an ABI test.
typedef wchar_t WCHAR;
struct HostFont
{
  int ascent, descent, leading, advance;
};
typedef HostFont *HFONT;
typedef void *HGDIOBJ;
struct HostDC
{
  HFONT font;
  UINT alignment;
  int background;
  COLORREF textColor;
};
typedef HostDC *HDC;
struct RECT
{
  int left, top, right, bottom;
};
struct SIZE
{
  int cx, cy;
};
struct TEXTMETRICW
{
  int tmAscent, tmDescent, tmExternalLeading, tmHeight;
};
#define FALSE 0
#define TRUE 1
#define OBJ_FONT 6
#define HGDI_ERROR reinterpret_cast<HGDIOBJ>(-1)
#define TRANSPARENT 1
#define TA_BASELINE 24
#define TA_LEFT 0
#define ETO_CLIPPED 4
HGDIOBJ GetCurrentObject(HDC, UINT);
HGDIOBJ SelectObject(HDC, HGDIOBJ);
BOOL GetTextMetricsW(HDC, TEXTMETRICW *);
BOOL GetTextExtentExPointW(HDC, const WCHAR *, int, int, int *, int *, SIZE *);
UINT GetTextAlign(HDC);
UINT SetTextAlign(HDC, UINT);
int SetBkMode(HDC, int);
BOOL ExtTextOutW(HDC, int, int, UINT, const RECT *, const WCHAR *, UINT, const int *);
namespace win32_host
{
  struct Draw
  {
    int x, y;
    HFONT font;
    COLORREF textColor;
    std::wstring units;
  };
  extern int selections, measures, metrics;
  extern bool failMeasure;
  extern std::vector<Draw> draws;
  void reset();
} // namespace win32_host
#endif

#ifndef LOKA_TEST_HOST_WINDOWS_CONTEXTS
#define LOKA_TEST_HOST_WINDOWS_CONTEXTS
#include <cstring>
#include <cwchar>
#include <cstdlib>
#include <stdint.h>
typedef unsigned long DWORD;
typedef intptr_t LONG_PTR;
typedef intptr_t LPARAM;
typedef uintptr_t WPARAM;
typedef intptr_t LRESULT;
typedef void *HINSTANCE;
typedef void *HMENU;
typedef void *HCURSOR;
typedef void *HBRUSH;
typedef void *HBITMAP;
typedef void *HMODULE;
typedef void *HMONITOR;
typedef void *PVOID;
typedef long HRESULT;
typedef int64_t LONGLONG;
typedef void (*FARPROC)();
typedef const wchar_t *LPCWSTR;
typedef RECT *LPRECT;
struct POINT { int x, y; };
struct HostWindow;
typedef HostWindow *HWND;
#define CALLBACK
#define WINAPI
typedef LRESULT (*WNDPROC)(HWND, UINT, WPARAM, LPARAM);
struct HostWindow
{
  RECT rect;
  HostDC dc;
  LONG_PTR data, style;
  WNDPROC proc;
  HWND parent;
  std::wstring text;
};
struct WNDCLASSW
{
  UINT style;
  WNDPROC lpfnWndProc;
  HINSTANCE hInstance;
  HCURSOR hCursor;
  LPCWSTR lpszClassName;
  HBRUSH hbrBackground;
};
struct CREATESTRUCTW { void *lpCreateParams; };
struct PAINTSTRUCT { int unused; };
#define WS_CHILD 1
#define WS_VISIBLE 2
#define SS_LEFT 0
#define SS_CENTER 1
#define SS_RIGHT 2
#define SS_LEFTNOWORDWRAP 12
#define SS_TYPEMASK 31
#define SS_EDITCONTROL 8192
#define SS_NOPREFIX 128
#define SS_ENDELLIPSIS 16384
#define GWL_STYLE -16
#define GWLP_USERDATA -21
#define WM_SETFONT 48
#define WM_GETFONT 49
#define WM_NCCREATE 129
#define WM_SIZE 5
#define WM_ERASEBKGND 20
#define WM_PAINT 15
#define SW_SHOW 5
#define SW_HIDE 0
#define CS_HREDRAW 2
#define CS_VREDRAW 1
#define IDC_ARROW L"arrow"
#define WHITE_BRUSH 0
#define BLACK_BRUSH 4
#define NULL_BRUSH 5
#define SRCCOPY 0x00CC0020
#define SIMPLEREGION 2
#define DT_LEFT 0
#define DT_NOPREFIX 2048
#define DT_CALCRECT 1024
#define DT_WORDBREAK 16
#define DT_EDITCONTROL 8192
#define ZeroMemory(p,n) std::memset(p,0,n)
#define SetWindowLongPtr SetWindowLongPtrW
#define GetWindowLongPtr GetWindowLongPtrW
#define MONITOR_DEFAULTTONEAREST 2
#define FAILED(x) ((x)<0)
#define LOGPIXELSX 88
#define FW_BOLD 700
#define SPI_GETNONCLIENTMETRICS 41
struct LOGFONTW { int lfHeight, lfWidth, lfWeight; unsigned char lfItalic; };
struct NONCLIENTMETRICSW { UINT cbSize; LOGFONTW lfMessageFont; int iPaddedBorderWidth; };
HDC GetDC(HWND);
int ReleaseDC(HWND, HDC);
LONG_PTR GetWindowLongPtrW(HWND,int);
LONG_PTR SetWindowLongPtrW(HWND,int,LONG_PTR);
LRESULT SendMessageW(HWND,UINT,WPARAM,LPARAM);
BOOL GetClientRect(HWND,RECT *);
BOOL GetWindowRect(HWND,RECT *);
BOOL IsRectEmpty(const RECT *);
BOOL InvalidateRect(HWND,const RECT *,BOOL);
BOOL ShowWindow(HWND,int);
HINSTANCE GetModuleHandleW(LPCWSTR);
HCURSOR LoadCursorW(HINSTANCE,LPCWSTR);
int RegisterClassW(const WNDCLASSW *);
LRESULT DefWindowProcW(HWND,UINT,WPARAM,LPARAM);
HDC BeginPaint(HWND,PAINTSTRUCT *);
BOOL EndPaint(HWND,const PAINTSTRUCT *);
int GetClipBox(HDC,RECT *);
BOOL EqualRect(const RECT *,const RECT *);
int FillRect(HDC,const RECT *,HBRUSH);
HBRUSH GetSysColorBrush(int);
HDC CreateCompatibleDC(HDC);
HBITMAP CreateCompatibleBitmap(HDC,int,int);
BOOL DeleteDC(HDC);
BOOL BitBlt(HDC,int,int,int,int,HDC,int,int,DWORD);
HGDIOBJ GetStockObject(int);
int DrawTextW(HDC,LPCWSTR,int,RECT *,UINT);
int GetWindowTextLengthW(HWND);
int GetWindowTextW(HWND,wchar_t *,int);
BOOL SetWindowTextW(HWND,LPCWSTR);
HWND GetParent(HWND);
int MapWindowPoints(HWND,HWND,POINT *,UINT);
BOOL EnumChildWindows(HWND,BOOL (*)(HWND,LPARAM),LPARAM);
HMODULE LoadLibraryExW(LPCWSTR,void *,DWORD);
BOOL FreeLibrary(HMODULE);
FARPROC GetProcAddress(HMODULE,const char *);
HMONITOR MonitorFromWindow(HWND,DWORD);
int GetDeviceCaps(HDC,int);
int MulDiv(int,int,int);
BOOL AdjustWindowRectEx(RECT *,DWORD,BOOL,DWORD);
BOOL SystemParametersInfoW(UINT,UINT,PVOID,UINT);
HFONT CreateFontIndirectW(const LOGFONTW *);
BOOL DeleteObject(HGDIOBJ);
namespace win32_host
{
  extern int failFontAllocations, fontAllocations, drawMeasures, styleReads, fontReads, positions;
  extern bool failDC, failMetrics, failConversion;
  extern std::vector<HWND> windows;
}
#endif
