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
    int x, y;
    RECT clip;
    COLORREF ink;
  };
  SpanNativeCall spanExtents[128], spanDraws[128];
  unsigned spanExtentCount, spanDrawCount;
  HDC spanTraceDC;
  unsigned spanRefuseOutput, spanOutputSuccesses;

  BOOL WINAPI recordSpanExtent(HDC dc, LPCWSTR text, int count, int limit, LPINT fit, LPINT advances, LPSIZE size)
  {
    if (dc != spanTraceDC)
      return spanExtentNative(dc, text, count, limit, fit, advances, size);
    LOKA_VERIFY(spanExtentCount < 128);
    SpanNativeCall &call = spanExtents[spanExtentCount++];
    call.text.assign(text, count);
    call.font = GetCurrentObject(dc, OBJ_FONT);
    return spanExtentNative(dc, text, count, limit, fit, advances, size);
  }
  BOOL WINAPI
  recordSpanDraw(HDC dc, int x, int y, UINT options, const RECT *rect, LPCWSTR text, UINT count, const INT *advances)
  {
    if (dc != spanTraceDC)
      return spanDrawNative(dc, x, y, options, rect, text, count, advances);
    LOKA_VERIFY(spanDrawCount < 128);
    SpanNativeCall &call = spanDraws[spanDrawCount++];
    call.text.assign(text, count);
    call.font = GetCurrentObject(dc, OBJ_FONT);
    call.x = x;
    call.y = y;
    LOKA_VERIFY(rect && options == ETO_CLIPPED);
    call.clip = *rect;
    call.ink = GetTextColor(dc);
    if (spanDrawCount == spanRefuseOutput)
      return FALSE;
    const BOOL result = spanDrawNative(dc, x, y, options, rect, text, count, advances);
    if (result)
      ++spanOutputSuccesses;
    return result;
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
    spanTraceDC = dc;
    spanRefuseOutput = spanOutputSuccesses = 0;
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
          LOKA_VERIFY(spanDraws[0].y == seat.top + table.lines().line(0).metrics.ascent);
          LOKA_VERIFY(spanDraws[1].y == spanDraws[0].y);
          LOKA_VERIFY(EqualRect(&spanDraws[0].clip, &seat) && EqualRect(&spanDraws[1].clip, &seat));
          LOKA_VERIFY(spanDraws[0].ink == GetSysColor(COLOR_BTNTEXT) && spanDraws[1].ink == spanDraws[0].ink);
          std::printf("linked boundary align=%u delta=%d kept=%d width=%d x=%d dots-x=%d\n",
                      a,
                      delta,
                      kept,
                      table.width(),
                      offset,
                      offset + keptWidth);
        }
      // Coordinate discriminator: both clips admit the full line but differ
      // in width. Placement must still use the identical nonzero-origin seat.
      std::puts("vm_clip_width_independence");
      const int lineWidth = table.lines().line(0).width;
      const int seatWidth = lineWidth + 40, deltaWidth = 13;
      LOKA_VERIFY(deltaWidth > 0 && lineWidth < seatWidth - deltaWidth && seatWidth + 7 < 400);
      RECT pairSeat = {7, 9, 7 + seatWidth, 80};
      for (int narrow = 0; narrow < 2; ++narrow)
      {
        SelectClipRgn(dc, NULL);
        LOKA_VERIFY(IntersectClipRect(dc, 7, 9, pairSeat.right - narrow * deltaWidth, 80) != ERROR);
        RECT beforeClip, afterClip;
        LOKA_VERIFY(GetClipBox(dc, &beforeClip) != ERROR);
        LOKA_VERIFY(beforeClip.right - beforeClip.left == seatWidth - narrow * deltaWidth);
        spanDrawCount = 0;
        LOKA_VERIFY(table.draw(dc, pairSeat, BlockStyle().align(TEXT_ALIGN_RIGHT)));
        LOKA_VERIFY(spanDrawCount == 2 && spanDraws[0].x == 7 + seatWidth - lineWidth);
        LOKA_VERIFY(spanDraws[1].x == spanDraws[0].x + firstWidth);
        LOKA_VERIFY(spanDraws[0].y == 9 + table.lines().line(0).metrics.ascent);
        LOKA_VERIFY(EqualRect(&spanDraws[0].clip, &pairSeat) && EqualRect(&spanDraws[1].clip, &pairSeat));
        LOKA_VERIFY(spanDraws[0].ink == GetSysColor(COLOR_BTNTEXT));
        LOKA_VERIFY(GetClipBox(dc, &afterClip) != ERROR && EqualRect(&beforeClip, &afterClip));
      }
      LOKA_VERIFY(dotsWidth > 1 && lineWidth < seatWidth);
      SelectClipRgn(dc, NULL);
      LOKA_VERIFY(IntersectClipRect(dc, 7, 9, 7 + dotsWidth - 1, 80) != ERROR);
      spanDrawCount = spanExtentCount = 0;
      LOKA_VERIFY(table.draw(dc, pairSeat, BlockStyle().truncation(TEXT_TRUNCATION_ELLIPSIS)));
      LOKA_VERIFY(spanDrawCount == 2 && spanExtentCount == 0);
      LOKA_VERIFY(spanDraws[0].text == L"MMMM" && spanDraws[1].text == L"WWWW");

      // Separate pixel evidence. Clear unrestricted, clip through visible ink,
      // verify that the region survives draw, then inspect the entire bitmap.
      std::puts("vm_pixel_clip");
      SelectClipRgn(dc, NULL);
      RECT surface = {0, 0, 400, 80};
      LOKA_VERIFY(FillRect(dc, &surface, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH))));
      spanDrawCount = 0;
      LOKA_VERIFY(table.draw(dc, pairSeat, BlockStyle()));
      int firstInk = 400, lastInk = -1;
      for (int py = 0; py < 80; ++py)
        for (int px = 0; px < 400; ++px)
          if (GetPixel(dc, px, py) != RGB(255, 255, 255))
          {
            if (px < firstInk)
              firstInk = px;
            if (px > lastInk)
              lastInk = px;
          }
      LOKA_VERIFY(firstInk < lastInk);
      const int cut = firstInk + (lastInk - firstInk) / 2 + 1;
      LOKA_VERIFY(FillRect(dc, &surface, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH))));
      LOKA_VERIFY(IntersectClipRect(dc, 0, 0, cut, 80) != ERROR);
      RECT clipBefore, clipAfter;
      LOKA_VERIFY(GetClipBox(dc, &clipBefore) != ERROR);
      spanDrawCount = 0;
      LOKA_VERIFY(table.draw(dc, pairSeat, BlockStyle()));
      LOKA_VERIFY(GetClipBox(dc, &clipAfter) != ERROR && EqualRect(&clipBefore, &clipAfter));
      SelectClipRgn(dc, NULL);
      bool clippedInk = false;
      for (int py = 0; py < 80; ++py)
        for (int px = 0; px < 400; ++px)
        {
          const COLORREF pixel = GetPixel(dc, px, py);
          LOKA_VERIFY(pixel != CLR_INVALID);
          if (px >= cut)
            LOKA_VERIFY(pixel == RGB(255, 255, 255));
          else if (pixel != RGB(255, 255, 255))
            clippedInk = true;
        }
      LOKA_VERIFY(clippedInk);

      std::puts("vm_hidden_terminal_font_and_narrow_dots");
      const TextStyle terminalStyle = FontSize<24>() + Italic;
      HFONT terminalFont = controller.textFont(terminalStyle).font();
      LOKA_VERIFY(terminalFont && terminalFont != font);
      SelectObject(dc, terminalFont);
      const int terminalDots = spanNativeWidth(dc, L"...", 3);
      SelectObject(dc, font);
      const int mWidth = spanNativeWidth(dc, L"M", 1);
      const BlockStyle ellipsis = BlockStyle().truncation(TEXT_TRUNCATION_ELLIPSIS);
      spanExtentCount = 0;
      LOKA_VERIFY(
          table.build(Styled("MMMM", style) + Styled("WWWWWWWW", terminalStyle), ellipsis, 400, dc, controller));
      pairSeat.right = pairSeat.left + mWidth + terminalDots;
      LOKA_VERIFY(table.lines().line(0).width > pairSeat.right - pairSeat.left);
      spanDrawCount = spanExtentCount = 0;
      LOKA_VERIFY(table.draw(dc, pairSeat, ellipsis));
      LOKA_VERIFY(spanDrawCount == 2 && spanDraws[0].text == L"M" && spanDraws[1].text == L"...");
      LOKA_VERIFY(spanDraws[1].font == terminalFont && spanDraws[1].x == pairSeat.left + mWidth);
      for (int less = 0; less < 2; ++less)
      {
        LOKA_VERIFY(terminalDots > 1);
        pairSeat.right = pairSeat.left + terminalDots - less;
        spanDrawCount = spanExtentCount = 0;
        LOKA_VERIFY(table.draw(dc, pairSeat, ellipsis));
        LOKA_VERIFY(spanDrawCount == 1 && spanDraws[0].text == L"...");
        LOKA_VERIFY(spanDraws[0].font == terminalFont && spanDraws[0].x == pairSeat.left);
        LOKA_VERIFY(EqualRect(&spanDraws[0].clip, &pairSeat));
      }

      std::puts("vm_surrogate_batch_boundary");
      spanExtentCount = 0;
      const std::string longBody = std::string(8191, 'a') + "\xf0\x9f\x98\x80z";
      LOKA_VERIFY(table.build(Styled(String(longBody), style), BlockStyle(), 400, dc, controller));
      pairSeat.right = 390;
      spanDrawCount = 0;
      LOKA_VERIFY(table.draw(dc, pairSeat, BlockStyle()));
      LOKA_VERIFY(spanDrawCount == 2 && spanDraws[0].text == std::wstring(8191, L'a'));
      LOKA_VERIFY(spanDraws[1].text.size() == 3 && spanDraws[1].text[0] == 0xd83d && spanDraws[1].text[1] == 0xde00);
      const int firstBatchWidth = spanNativeWidth(dc, spanDraws[0].text.c_str(), 8191);
      LOKA_VERIFY(spanDraws[1].x == pairSeat.left + firstBatchWidth);
      LOKA_VERIFY(spanDraws[0].y == spanDraws[1].y && spanDraws[0].font == font && spanDraws[1].font == font);

      std::puts("vm_second_batch_refuses");
      spanExtentCount = 0;
      LOKA_VERIFY(table.build(Styled(String(std::string(16385, 'a')), style), BlockStyle(), 400, dc, controller));
      SelectObject(dc, oldFont);
      SetTextAlign(dc, TA_RIGHT | TA_TOP);
      SetBkMode(dc, OPAQUE);
      SetTextColor(dc, RGB(1, 2, 3));
      LOKA_VERIFY(GetClipBox(dc, &clipBefore) != ERROR);
      spanDrawCount = spanOutputSuccesses = 0;
      spanRefuseOutput = 2;
      LOKA_VERIFY(!table.draw(dc, pairSeat, BlockStyle()));
      spanRefuseOutput = 0;
      LOKA_VERIFY(spanDrawCount == 2 && spanOutputSuccesses == 1);
      LOKA_VERIFY(spanDraws[0].text.size() == 8192 && spanDraws[1].text.size() == 8192);
      LOKA_VERIFY(GetCurrentObject(dc, OBJ_FONT) == oldFont && GetTextColor(dc) == RGB(1, 2, 3));
      LOKA_VERIFY(GetTextAlign(dc) == (TA_RIGHT | TA_TOP) && GetBkMode(dc) == OPAQUE);
      LOKA_VERIFY(GetClipBox(dc, &clipAfter) != ERROR && EqualRect(&clipBefore, &clipAfter));
    }
    spanTraceDC = NULL;
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
