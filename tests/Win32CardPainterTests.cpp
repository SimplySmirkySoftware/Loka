#include "Win32CardPainterTests.hpp"
#include "Win32CardPainter.hpp"
#include "app/nodes/controls/Card.hpp"
#include "support/TestVerify.hpp"
#include <cstring>

namespace
{
  COLORREF ground(int x, int y)
  {
    return ((x / 5 + y / 7) & 1) ? RGB(81, 137, 193) : RGB(209, 111, 57);
  }

  /** A fresh device-pixel surface; no window or desktop pixels enter the oracle. */
  class CardDib
  {
  public:
    CardDib(int width, int height, int originX = 0, int originY = 0)
        : dc(CreateCompatibleDC(NULL)),
          bitmap(0),
          old(0)
    {
      BITMAPINFO info;
      ZeroMemory(&info, sizeof(info));
      info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
      info.bmiHeader.biWidth = width;
      info.bmiHeader.biHeight = -height;
      info.bmiHeader.biPlanes = 1;
      info.bmiHeader.biBitCount = 32;
      info.bmiHeader.biCompression = BI_RGB;
      void *bits = 0;
      bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, NULL, 0);
      LOKA_VERIFY(dc && bitmap && bits);
      old = SelectObject(dc, bitmap);
      LOKA_VERIFY(old && old != HGDI_ERROR);
      for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
          LOKA_VERIFY(SetPixelV(dc, x, y, ground(x + originX, y + originY)));
    }
    ~CardDib()
    {
      SelectObject(dc, old);
      DeleteObject(bitmap);
      DeleteDC(dc);
    }
    HDC dc;

  private:
    HBITMAP bitmap;
    HGDIOBJ old;
    CardDib(const CardDib &);
    CardDib &operator=(const CardDib &);
  };
} // namespace

void testWin32CardPainterPixels()
{
  using namespace loka::app;
  using namespace loka::win32;
  using loka::core::Frame;
  const UINT dpis[] = {96, 120, 144, 168, 192};
  for (int metricsIndex = 0; metricsIndex < 2; ++metricsIndex)
    for (int dpiIndex = 0; dpiIndex < 5; ++dpiIndex)
      for (int sizeIndex = 0; sizeIndex < 4; ++sizeIndex)
      {
        const RailMetrics metrics(Ratio(), metricsIndex ? Ratio(3, 2) : Ratio());
        const Win32DisplayScale scale(dpis[dpiIndex], metrics);
        // Translated origin and asymmetric allowances exercise absolute-edge rounding.
        const int width = (sizeIndex & 1) ? 14 : 77;
        const int height = (sizeIndex & 2) ? 14 : 57;
        const Frame wholeBody(11, 17, width, height);
        const Frame bands[] = {Frame(11, 17, width - 3, 3),
                               Frame(11, 20, 3, height),
                               Frame(11 + width - 3, 17, 6, height - 3),
                               Frame(14, 17 + height - 3, width, 6)};
        CardDib reference(320, 320);
        CardDib split(320, 320);
        // Q1 limits the whole reference to the outer frame. Exclude the
        // content seat below: no band owns those pixels, including arc corners.
        const RECT outer = scale.projectFrame(Frame(11, 17, width + 3, height + 3)).r;
        const RECT seat = scale.projectFrame(Frame(14, 20, width - 6, height - 6)).r;
        const int savedReference = SaveDC(reference.dc);
        LOKA_VERIFY(savedReference != 0);
        LOKA_VERIFY(IntersectClipRect(reference.dc, outer.left, outer.top, outer.right, outer.bottom) != ERROR);
        LOKA_VERIFY(ExcludeClipRect(reference.dc, seat.left, seat.top, seat.right, seat.bottom) != ERROR);
        PaintCardInk(reference.dc, wholeBody, CardStyle(), scale, 0, 0, 0, 0);
        LOKA_VERIFY(RestoreDC(reference.dc, savedReference));
        for (int role = 0; role < CARD_BAND_COUNT; ++role)
        {
          const RECT native = scale.projectFrame(bands[role]).r;
          const int nativeWidth = static_cast<int>(native.right - native.left);
          const int nativeHeight = static_cast<int>(native.bottom - native.top);
          CardDib strip(nativeWidth, nativeHeight, native.left, native.top);
          const COLORREF oldText = SetTextColor(strip.dc, RGB(12, 34, 56));
          const COLORREF oldBack = SetBkColor(strip.dc, RGB(78, 90, 12));
          POINT oldOrigin;
          LOKA_VERIFY(SetBrushOrgEx(strip.dc, 5, 7, &oldOrigin));
          PaintCardBandInk(
              strip.dc, bands[role], CardBandProps(static_cast<CardBandRole>(role)), scale, native.left, native.top);
          LOKA_VERIFY(GetTextColor(strip.dc) == RGB(12, 34, 56) && GetBkColor(strip.dc) == RGB(78, 90, 12));
          POINT restored;
          LOKA_VERIFY(GetBrushOrgEx(strip.dc, &restored) && restored.x == 5 && restored.y == 7);
          SetTextColor(strip.dc, oldText);
          SetBkColor(strip.dc, oldBack);
          LOKA_VERIFY(BitBlt(split.dc, native.left, native.top, nativeWidth, nativeHeight, strip.dc, 0, 0, SRCCOPY));
        }
        int ink = 0;
        for (int y = 0; y < 320; ++y)
          for (int x = 0; x < 320; ++x)
          {
            const COLORREF expected = GetPixel(reference.dc, x, y);
            const COLORREF actual = GetPixel(split.dc, x, y);
            LOKA_VERIFY(actual == expected);
            LOKA_VERIFY(actual == RGB(0, 0, 0) || actual == ground(x, y));
            if (actual == RGB(0, 0, 0))
              ++ink;
          }
        LOKA_VERIFY(ink > 10);
        for (int y = static_cast<int>(seat.top); y < seat.bottom; ++y)
          for (int x = static_cast<int>(seat.left); x < seat.right; ++x)
            LOKA_VERIFY(GetPixel(split.dc, x, y) == ground(x, y));
        // Straight-side checks need space between top and bottom corner quadrants.
        if (height == 14)
          continue;
        const RECT body = scale.projectFrame(wholeBody).r;
        const int middle = static_cast<int>((body.top + body.bottom) / 2);
        const int pen = scale.projectLength(1);
        for (int x = static_cast<int>(body.left); x < body.left + pen; ++x)
          LOKA_VERIFY(GetPixel(split.dc, x, middle) == RGB(0, 0, 0));
        LOKA_VERIFY(GetPixel(split.dc, body.left + pen, middle) == ground(body.left + pen, middle));
        // Shadow truth on a non-white ground, away from arcs and the body border.
        const int shadowX = static_cast<int>(body.right + 1);
        for (int y = middle; y < middle + 8; ++y)
        {
          const COLORREF expected = ((shadowX + y) & 1) ? RGB(0, 0, 0) : ground(shadowX, y);
          LOKA_VERIFY(GetPixel(split.dc, shadowX, y) == expected);
        }
      }
}
