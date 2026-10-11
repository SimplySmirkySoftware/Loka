#include "Win32TextHost.hpp"
#include "context/Win32AttributedTextTable.hpp"
#include "support/TestVerify.hpp"
#include "support/LokaAllocFailure.hpp"
#include <cstdio>
#include <climits>
#include "Win32ContextHost.inc"

namespace win32_host
{
  int selections = 0, measures = 0, metrics = 0;
  bool failMeasure = false;
  std::vector<Draw> draws;
  void reset()
  {
    selections = measures = metrics = 0;
    failMeasure = false;
    draws.clear();
  }
} // namespace win32_host
HGDIOBJ GetCurrentObject(HDC dc, UINT)
{
  return dc->font;
}
HGDIOBJ SelectObject(HDC dc, HGDIOBJ font)
{
  ++win32_host::selections;
  HFONT old = dc->font;
  dc->font = static_cast<HFONT>(font);
  return old;
}
BOOL GetTextMetricsW(HDC dc, TEXTMETRICW *out)
{
  ++win32_host::metrics;
  if (win32_host::failMetrics) return FALSE;
  out->tmHeight = dc->font->ascent + dc->font->descent;
  out->tmAscent = dc->font->ascent;
  out->tmDescent = dc->font->descent;
  out->tmExternalLeading = dc->font->leading;
  return TRUE;
}
BOOL GetTextExtentExPointW(HDC dc, const WCHAR *text, int count, int maximum, int *fit, int *dx, SIZE *size)
{
  LOKA_VERIFY(maximum == INT_MAX && !fit);
  ++win32_host::measures;
  if (win32_host::failMeasure)
    return FALSE;
  int width = 0;
  for (int i = 0; i < count; ++i)
  {
    if (text[i] < 0xdc00 || text[i] > 0xdfff)
      width += dc->font->advance;
    if (dx)
      dx[i] = width;
  }
  size->cx = width;
  size->cy = dc->font->ascent + dc->font->descent;
  return TRUE;
}
UINT GetTextAlign(HDC dc)
{
  return dc->alignment;
}
UINT SetTextAlign(HDC dc, UINT align)
{
  UINT old = dc->alignment;
  dc->alignment = align;
  return old;
}
int SetBkMode(HDC dc, int mode)
{
  int old = dc->background;
  dc->background = mode;
  return old;
}
COLORREF GetSysColor(int index)
{
  // Distinct test-only roles discriminate a wrong index, unlike light themes.
  LOKA_VERIFY(index == COLOR_BTNTEXT || index == COLOR_WINDOWTEXT);
  return index == COLOR_BTNTEXT ? 0x123456 : 0x654321;
}
COLORREF SetTextColor(HDC dc, COLORREF color)
{
  const COLORREF old = dc->textColor;
  dc->textColor = color;
  return old;
}
BOOL ExtTextOutW(HDC dc, int x, int y, UINT flags, const RECT *, const WCHAR *text, UINT count, const int *dx)
{
  LOKA_VERIFY(flags == ETO_CLIPPED && !dx && count <= 8192);
  LOKA_VERIFY(dc->alignment == (TA_BASELINE | TA_LEFT) && dc->background == TRANSPARENT);
  LOKA_VERIFY(!count || text[0] < 0xdc00 || text[0] > 0xdfff);
  LOKA_VERIFY(!count || text[count - 1] < 0xd800 || text[count - 1] > 0xdbff);
  win32_host::Draw draw;
  draw.x = x;
  draw.y = y;
  draw.font = dc->font;
  draw.textColor = dc->textColor;
  draw.units.assign(text, count);
  win32_host::draws.push_back(draw);
  return TRUE;
}
#include "Win32MeasurementPins.inc"

static void pinPlainWrappedLines()
{
  using namespace loka::app;
  using namespace loka::app::scene;
  using namespace loka::core;
  Win32ScenePlatformController controller;
  HostWindow root;
  controller.rootHwnd_ = &root;
  PushStateTracker tracker;
  MutableState<String> live(String::Literal("ab cdef"));
  tracker.addState(&live);
  TextProps props(&live);
  props.blockStyle_.wrap(TEXT_WRAP_CHAR);
  TextNode node(props);
  node.setContext(new Win32TextContext(&controller, &root, 0, 0, 100, 20, &node));
  HWND window = win32_host::windows.back();
  LayoutState state;
  state.width = static_cast<short>(controller.displayFont()->advance * 4);
  Win32TextContext *context = static_cast<Win32TextContext *>(node.getContext());
  context->layout(&controller, state);
  LOKA_VERIFY(window->text == L"ab c\ndef");
  LOKA_VERIFY((window->style & SS_TYPEMASK) == SS_LEFTNOWORDWRAP);
  node.props.blockStyle_.wrap(TEXT_WRAP_WORD);
  context->onPropsApplied();
  context->layout(&controller, state);
  LOKA_VERIFY(window->text == L"ab \ncdef");
  node.props.blockStyle_.wrap(TEXT_WRAP_CHAR);
  context->onPropsApplied();
  context->layout(&controller, state);
  {
    StateTrackerGuard guard(&tracker);
    live.set(String::Literal("xy zwvu"));
  }
  state.inputs = NODE_DIRTY_NONE;
  context->layout(&controller, state);
  LOKA_VERIFY(window->text == L"xy z\nwvu");
  {
    StateTrackerGuard guard(&tracker);
    live.set(String::Literal("xy zwvu"), true);
  }
  context->layout(&controller, state);
  const PaintQuery query = {Win32RetirableContext::paintScope(), PLACEMENT_ELIGIBLE};
  LOKA_VERIFY(context->queryPaintDamage(query).kind == PAINT_ANSWER_EXACT);
}

static void pinAttributedTextGroundText()
{
  using namespace loka::app;
  using namespace loka::app::scene;
  Win32ScenePlatformController controller;
  HostWindow root;
  controller.rootHwnd_ = &root;
  AttributedTextProps props(Styled("MMMMMMMMMMMM", Bold));
  props.blockStyle_.wrap(TEXT_WRAP_NONE).truncation(TEXT_TRUNCATION_ELLIPSIS);
  AttributedTextNode node(props);
  node.setContext(new Win32AttributedTextContext(&controller, &root, 0, 0, 32, 24, &node));
  HWND child = win32_host::windows.back();
  LayoutState state;
  state.width = 32;
  node.layout(&controller, state);
  const COLORREF sentinel = 0x010203;
  child->dc.textColor = sentinel;
  win32_host::reset();
  SendMessageW(child, WM_PAINT, 0, 0);
  LOKA_VERIFY(win32_host::draws.size() >= 2);
  LOKA_VERIFY(win32_host::draws.front().units != L"...");
  LOKA_VERIFY(win32_host::draws.back().units == L"...");
  for (std::size_t i = 0; i < win32_host::draws.size(); ++i)
    LOKA_VERIFY(win32_host::draws[i].textColor == GetSysColor(COLOR_BTNTEXT));
  LOKA_VERIFY(child->dc.textColor == sentinel);
  std::printf("AttributedText runs/ellipsis use WINDOW text and restore DC color\n");
}

static void pinRectSurfaceGroundBrushes()
{
  using namespace loka::app;
  using namespace loka::app::scene;
  Win32ScenePlatformController controller;
  HostWindow root;
  controller.rootHwnd_ = &root;
  RectSurfaceModel value;
  value.rectCount = 1;
  value.rects[0] = RectSprite(8, 8, 16, 16);
  loka::core::MutableState<RectSurfaceModel> model(value);
  RectSurfaceNode node(RectSurfaceProps().model(&model));
  node.setContext(new Win32RectSurfaceContext(&controller, &root, 0, 0, 100, 60, &node));
  HWND child = win32_host::windows.back();
  win32_host::fills.clear();
  SendMessageW(child, WM_PAINT, 0, 0);
  LOKA_VERIFY(win32_host::fills.size() == 2);
  // System and stock brush identities differ even on a white/black theme.
  LOKA_VERIFY(win32_host::fills[0] == GetSysColorBrush(COLOR_WINDOW));
  LOKA_VERIFY(win32_host::fills[1] == GetSysColorBrush(COLOR_WINDOWTEXT));
  std::printf("RectSurface direct-target ground/sprite use system brushes\n");
}

int main(int argc, char **)
{
  pinAttributedTextGroundText();
  pinRectSurfaceGroundBrushes();
  if (argc > 1)
  {
    pinPlainWrappedLines();
    pinMeasurement(true);
    pinMeasurement(false);
    return 0;
  }
  using namespace loka::app;
  using namespace loka::core::testing;
  Win32ScenePlatformController controller;
  HostFont original = {7, 1, 0, 3};
  HostDC dc = {&original, 2, 2, 0};
  failLokaAllocRaw("Win32AttributedText", "Break", 0);
  {
    Win32AttributedTextTable table;
    const AttributedString value = Styled("a ab", FontSize<12>() + Bold) + Styled("cd", FontSize<24>() + Italic);
    const BlockStyle word = BlockStyle().wrap(TEXT_WRAP_WORD);
    win32_host::reset();
    LOKA_VERIFY(table.build(value, word, 30, &dc, controller));
    LOKA_VERIFY(table.lines().lineCount() == 2 && table.lines().line(1).fragmentCount == 2);
    LOKA_VERIFY(win32_host::selections == 3 && win32_host::measures == 2 && win32_host::metrics == 2);
    LOKA_VERIFY(dc.font == &original);
    std::printf("measure: SelectObject(font)=3 GetTextExtentExPointW=2 GetTextMetricsW=2; probes=0 GDI\n");
    RECT rect = {5, 7, 35, 100};
    LOKA_VERIFY(table.draw(&dc, rect, word));
    LOKA_VERIFY(win32_host::draws.size() == 3);
    LOKA_VERIFY(win32_host::draws[0].y == 17 && win32_host::draws[1].y == 40 && win32_host::draws[2].y == 40);
    LOKA_VERIFY(win32_host::draws[2].x == 13);
    LOKA_VERIFY(dc.font == &original && dc.alignment == 2 && dc.background == 2);
    // Different styles resolving to one descriptor measure metrics just once.
    win32_host::reset();
    LOKA_VERIFY(table.build(Styled("a", Bold) + Styled("b", FontSize<12>()), word, 30, &dc, controller));
    LOKA_VERIFY(win32_host::metrics == 1 && win32_host::measures == 2);
    // Adjacent equal styles are a single native span regardless of segmentation.
    win32_host::reset();
    LOKA_VERIFY(table.build(Styled("a", Bold) + Styled("b", Bold), word, 30, &dc, controller));
    LOKA_VERIFY(win32_host::measures == 1 && win32_host::selections == 2);
    const AttributedString unicode = Styled("\xef\xbc\xa1\xf0\x9f\x98\x80", Bold);
    const BlockStyle characters = BlockStyle().wrap(TEXT_WRAP_CHAR);
    win32_host::reset();
    LOKA_VERIFY(table.build(unicode, characters, 4, &dc, controller));
    LOKA_VERIFY(table.lines().lineCount() == 2);
    LOKA_VERIFY(table.draw(&dc, rect, characters));
    LOKA_VERIFY(win32_host::draws[0].units[0] == 0xff21);
    LOKA_VERIFY(win32_host::draws[1].units.size() == 2);
    LOKA_VERIFY(win32_host::draws[1].units[0] == 0xd83d && win32_host::draws[1].units[1] == 0xde00);
    win32_host::reset();
    const BlockStyle dots = BlockStyle().wrap(TEXT_WRAP_NONE).truncation(TEXT_TRUNCATION_ELLIPSIS);
    LOKA_VERIFY(table.build(value, dots, 30, &dc, controller));
    LOKA_VERIFY(table.draw(&dc, rect, dots));
    LOKA_VERIFY(win32_host::draws.back().units == L"...");
    LOKA_VERIFY(win32_host::draws.back().font == controller.textFont(Italic));
    LOKA_VERIFY(win32_host::draws[0].units == L"a");
    // The native prefix is 4px plus 24px of terminal-run dots, not the
    // breaker's overflowing width. Each physical alignment uses those 28px.
    const TextAlign alignments[] = {TEXT_ALIGN_LEFT, TEXT_ALIGN_CENTER, TEXT_ALIGN_RIGHT};
    for (int a = 0; a < 3; ++a)
    {
      win32_host::reset();
      LOKA_VERIFY(table.draw(&dc, rect, BlockStyle(dots).align(alignments[a])));
      LOKA_VERIFY(win32_host::draws[0].x == 5 + a);
      LOKA_VERIFY(win32_host::draws.back().x == 9 + a);
      LOKA_VERIFY(win32_host::measures == 1); // Dots only; prefix table is reused.
    }
    for (int a = 0; a < 3; ++a)
    {
      const BlockStyle aligned = BlockStyle().align(alignments[a]);
      LOKA_VERIFY(table.build(Styled("ab\nx", Bold), aligned, 30, &dc, controller));
      win32_host::reset();
      LOKA_VERIFY(table.draw(&dc, rect, aligned));
      LOKA_VERIFY(win32_host::draws.size() == 2);
      LOKA_VERIFY(win32_host::draws[0].x == 5 + (a == 0 ? 0 : a == 1 ? 11 : 22));
      LOKA_VERIFY(win32_host::draws[1].x == 5 + (a == 0 ? 0 : a == 1 ? 13 : 26));
      LOKA_VERIFY(win32_host::measures == 0);
      LOKA_VERIFY(table.build(Styled("abcdefghij", Bold), aligned, 30, &dc, controller));
      win32_host::reset();
      LOKA_VERIFY(table.draw(&dc, rect, aligned));
      LOKA_VERIFY(win32_host::draws[0].x == 5); // Unfitted overflow stays left.
    }
    failLokaAllocRaw("Win32AttributedText", "Break", 1);
    LOKA_VERIFY(!table.build(value, word, 30, &dc, controller));
    LOKA_VERIFY(!table.valid() && dc.font == &original);
    LOKA_VERIFY(table.build(value, word, 30, &dc, controller));
    win32_host::failMeasure = true;
    LOKA_VERIFY(!table.build(value, word, 30, &dc, controller));
    LOKA_VERIFY(!table.valid() && dc.font == &original);
    win32_host::reset();
    // Force heap table allocations; fail each allocation until one full build succeeds.
    std::string longText(9000, 'x');
    const AttributedString longValue = Styled(loka::core::String(longText), Bold);
    bool completed = false;
    for (int failure = 1; failure < 16 && !completed; ++failure)
    {
      failLokaAllocRaw("TextLineBreaker", "Table", failure);
      completed = table.build(longValue, BlockStyle(), 0, &dc, controller);
      if (!completed)
        LOKA_VERIFY(!table.valid());
    }
    LOKA_VERIFY(completed);
    failLokaAllocRaw("TextLineBreaker", "Table", 0);
    LOKA_VERIFY(table.draw(&dc, rect, BlockStyle()));
    LOKA_VERIFY(win32_host::draws.size() == 2);
  }
  LOKA_VERIFY(lokaAllocRawLive() == 0);
  allowLokaAllocRaw();
  pinMeasurement(true);
  pinMeasurement(false);
  LOKA_VERIFY(win32_host::windows.empty());
  return 0;
}
