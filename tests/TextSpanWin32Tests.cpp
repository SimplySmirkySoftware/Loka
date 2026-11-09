#include "TextSpanWin32Tests.hpp"
#include <cstdio>

#if defined(_WIN32)
#include <climits>
#include <string>
#include "support/TestVerify.hpp"
#include "support/Win32ImportRefusal.hpp"
#include "Win32ScenePlatformController.hpp"
#include "context/Win32AttributedTextTable.hpp"

namespace
{
  typedef BOOL(WINAPI *SpanExtentFn)(HDC, LPCWSTR, int, int, LPINT, LPINT, LPSIZE);
  typedef BOOL(WINAPI *SpanDrawFn)(HDC, int, int, UINT, const RECT *, LPCWSTR, UINT, const INT *);
  SpanExtentFn spanExtentNative;
  SpanDrawFn spanDrawNative;
  struct SpanNativeCall
  {
    std::wstring text;
    HGDIOBJ font;
    int x;
  };
  SpanNativeCall spanExtents[8], spanDraws[8];
  unsigned spanExtentCount, spanDrawCount;

  BOOL WINAPI recordSpanExtent(HDC dc, LPCWSTR text, int count, int limit, LPINT fit, LPINT advances, LPSIZE size)
  {
    LOKA_VERIFY(spanExtentCount < 8);
    SpanNativeCall &call = spanExtents[spanExtentCount++];
    call.text.assign(text, count);
    call.font = GetCurrentObject(dc, OBJ_FONT);
    return spanExtentNative(dc, text, count, limit, fit, advances, size);
  }
  BOOL WINAPI
  recordSpanDraw(HDC dc, int x, int y, UINT options, const RECT *rect, LPCWSTR text, UINT count, const INT *advances)
  {
    LOKA_VERIFY(spanDrawCount < 8);
    SpanNativeCall &call = spanDraws[spanDrawCount++];
    call.text.assign(text, count);
    call.font = GetCurrentObject(dc, OBJ_FONT);
    call.x = x;
    return spanDrawNative(dc, x, y, options, rect, text, count, advances);
  }
  int spanNativeWidth(HDC dc, const wchar_t *text, int count)
  {
    SIZE size;
    LOKA_VERIFY(spanExtentNative(dc, text, count, INT_MAX, NULL, NULL, &size));
    return size.cx;
  }
} // namespace
#endif

void testWin32LinkedSpanCharacterization()
{
#if defined(_WIN32)
  using namespace loka::app;
  using loka::core::String;
  spanExtentNative =
      reinterpret_cast<SpanExtentFn>(GetProcAddress(GetModuleHandleW(L"gdi32.dll"), "GetTextExtentExPointW"));
  spanDrawNative = reinterpret_cast<SpanDrawFn>(GetProcAddress(GetModuleHandleW(L"gdi32.dll"), "ExtTextOutW"));
  LOKA_VERIFY(spanExtentNative && spanDrawNative);
  HWND host =
      CreateWindowExW(0, L"STATIC", L"linked-span", WS_POPUP, 0, 0, 400, 80, NULL, NULL, GetModuleHandleW(NULL), NULL);
  LOKA_VERIFY(host != NULL);
  {
    Win32ScenePlatformController controller(host, loka::win32::Win32DisplayScale(96, RailMetrics()));
    HDC screen = GetDC(host);
    LOKA_VERIFY(screen != NULL);
    HDC dc = CreateCompatibleDC(screen);
    HBITMAP bitmap = CreateCompatibleBitmap(screen, 400, 80);
    LOKA_VERIFY(dc && bitmap);
    HGDIOBJ oldBitmap = SelectObject(dc, bitmap);
    const TextStyle style = FontSize<12>();
    HFONT font = controller.textFont(style).font();
    HGDIOBJ oldFont = SelectObject(dc, font);
    const int firstWidth = spanNativeWidth(dc, L"MMMM", 4);
    const int secondWidth = spanNativeWidth(dc, L"WWWW", 4);
    const int dotsWidth = spanNativeWidth(dc, L"...", 3);
    LOKA_VERIFY(secondWidth > dotsWidth + 1);
    const AttributedString value =
        Linked("MMMM", style, LinkTarget(String("first"))) + Linked("WWWW", style, LinkTarget(String("second")));
    Win32AttributedTextTable table;
    {
      Win32ImportRefusal extentHook(
          "gdi32.dll", "GetTextExtentExPointW", reinterpret_cast<ULONG_PTR>(&recordSpanExtent));
      Win32ImportRefusal drawHook("gdi32.dll", "ExtTextOutW", reinterpret_cast<ULONG_PTR>(&recordSpanDraw));
      spanExtentCount = spanDrawCount = 0;
      LOKA_VERIFY(table.build(value, BlockStyle(), 400, dc, controller));
      LOKA_VERIFY(spanExtentCount == 2 && spanExtents[0].text == L"MMMM" && spanExtents[1].text == L"WWWW");
      LOKA_VERIFY(spanExtents[0].font == font && spanExtents[1].font == font);
      LOKA_VERIFY(table.width() == firstWidth + secondWidth && table.lines().line(0).fragmentCount == 2);
      RECT seat = {0, 0, 400, 80};
      LOKA_VERIFY(table.draw(dc, seat, BlockStyle()));
      LOKA_VERIFY(spanDrawCount == 2 && spanDraws[0].text == L"MMMM" && spanDraws[1].text == L"WWWW");
      LOKA_VERIFY(spanDraws[0].x == 0 && spanDraws[1].x == firstWidth);
      LOKA_VERIFY(spanDraws[0].font == font && spanDraws[1].font == font);
      const TextAlign aligns[] = {TEXT_ALIGN_LEFT, TEXT_ALIGN_CENTER, TEXT_ALIGN_RIGHT};
      for (unsigned a = 0; a < 3; ++a)
        for (int delta = -1; delta <= 1; ++delta)
        {
          seat.right = firstWidth + dotsWidth + delta;
          int kept = 4;
          while (kept && spanNativeWidth(dc, L"MMMM", kept) > firstWidth + delta)
            --kept;
          const int keptWidth = spanNativeWidth(dc, L"MMMM", kept);
          const int slack = seat.right - keptWidth - dotsWidth;
          const int offset = a == 0 ? 0 : (a == 1 ? slack / 2 : slack);
          spanExtentCount = spanDrawCount = 0;
          const BlockStyle block = BlockStyle().truncation(TEXT_TRUNCATION_ELLIPSIS).align(aligns[a]);
          LOKA_VERIFY(table.draw(dc, seat, block));
          LOKA_VERIFY(spanExtentCount == 1 && spanExtents[0].text == L"...");
          LOKA_VERIFY(spanDrawCount == 2 && spanDraws[0].text == std::wstring(L"MMMM", kept));
          LOKA_VERIFY(spanDraws[1].text == L"..." && spanDraws[0].x == offset && spanDraws[1].x == offset + keptWidth);
          LOKA_VERIFY(spanDraws[0].font == font && spanDraws[1].font == font);
          std::printf("linked boundary align=%u delta=%d kept=%d width=%d x=%d dots-x=%d\n",
                      a,
                      delta,
                      kept,
                      table.width(),
                      offset,
                      offset + keptWidth);
        }
    }
    SelectObject(dc, oldFont);
    SelectObject(dc, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(dc);
    ReleaseDC(host, screen);
  }
  DestroyWindow(host);
#else
  std::puts("[skip] Win32 linked-span GDI characterization requires Windows");
#endif
}
