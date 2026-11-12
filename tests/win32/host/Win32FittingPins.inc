namespace loka
{
  namespace testing
  {
    class Win32AttributedTextAccess
    {
    public:
      static void
      present(Win32AttributedTextContext &context, const loka::app::AttributedString &value, const RECT &seat)
      {
        context.presented_.commit(Win32PresentedText(value, seat), context.paintScope());
      }
      static bool eligibleWithoutRows(Win32AttributedTextContext &context, const LinkHit &hit)
      {
        loka::app::TextLineBreaker *rows = context.table_.lines_;
        context.table_.lines_ = 0;
        const bool eligible = context.linkHitEligible(hit);
        context.table_.lines_ = rows;
        return eligible;
      }
      static HitAnswer prepare(Win32AttributedTextContext &context, POINT point, LinkHit &out)
      {
        return context.prepareLinkHit(point, out);
      }
      static bool eligible(const Win32AttributedTextContext &context, const LinkHit &hit)
      {
        return context.linkHitEligible(hit);
      }
      static void draw(Win32AttributedTextContext &context, HDC dc, const RECT &seat)
      {
        context.draw(dc, seat);
      }
      static void place(Win32AttributedTextContext &context, int x, int y, int w, int h)
      {
        context.relayout(x, y, w, h);
      }
      static void refuseSecondProbe(Win32AttributedTextTable &table)
      {
        table.advances_[2] = -1;
      }
      static const Win32AttributedTextTable &table(const Win32AttributedTextContext &context)
      {
        return context.table_;
      }
      static std::size_t spans(const Win32AttributedTextTable &table)
      {
        return table.spans_.size();
      }
    };
  } // namespace testing
} // namespace loka
namespace
{
  using namespace loka::app;
  using loka::core::String;
  typedef loka::testing::Win32AttributedTextAccess FittingAccess;

  void fittingPin(const char *name)
  {
    std::printf("%s\n", name);
    std::fflush(stdout);
  }
  void
  fittingDraw(std::size_t index, const std::wstring &units, int x, int y, HFONT font, COLORREF ink, const RECT &clip)
  {
    LOKA_VERIFY(index < win32_host::draws.size());
    const win32_host::Draw &draw = win32_host::draws[index];
    LOKA_VERIFY(draw.units == units && draw.x == x && draw.y == y);
    LOKA_VERIFY(draw.font == font && draw.textColor == ink && EqualRect(&draw.clip, &clip));
  }
  void fittingRestored(const HostDC &dc, HFONT original, const RECT &clip, int width)
  {
    LOKA_VERIFY(dc.font == original && dc.alignment == 2 && dc.background == 2 && dc.textColor == 0x010203);
    LOKA_VERIFY(clip.left == 7 && clip.top == 11 && clip.right == 7 + width && clip.bottom == 200);
  }
  void fittingTrace(const win32_host::Event *expected, std::size_t count)
  {
    LOKA_VERIFY(win32_host::events.size() == count);
    for (std::size_t i = 0; i < count; ++i)
    {
      LOKA_VERIFY(win32_host::events[i].kind == expected[i].kind);
      LOKA_VERIFY(win32_host::events[i].object == expected[i].object);
      LOKA_VERIFY(win32_host::events[i].value == expected[i].value);
    }
  }
  void pinFitting()
  {
    Win32ScenePlatformController controller;
    HostFont original = {7, 1, 0, 3};
    HostDC dc = {&original, 2, 2, 0x010203};
    HFONT a = controller.fallbackFont(Bold), b = controller.fallbackFont(Italic);
    const COLORREF ink = GetSysColor(COLOR_BTNTEXT);
    const BlockStyle dots = BlockStyle().truncation(TEXT_TRUNCATION_ELLIPSIS);
    const LinkTarget x(String("x")), y(String("y"));
    Win32AttributedTextTable table;
    RECT clip = {7, 11, 39, 200};
    const AttributedString mixed = Styled("ab", Bold) + Styled("cdefgh", Italic);
    fittingPin("host_complete_ellipsis_trace / host_hidden_terminal_font");
    LOKA_VERIFY(table.build(mixed, dots, 32, &dc, controller));
    win32_host::reset();
    LOKA_VERIFY(table.draw(&dc, clip, dots));
    LOKA_VERIFY(win32_host::draws.size() == 2);
    fittingDraw(0, L"ab", 7, 31, a, ink, clip);
    fittingDraw(1, L"...", 15, 31, b, ink, clip);
    const win32_host::Event exact[] = {{'B', 0, TRANSPARENT},
                                       {'A', 0, TA_BASELINE | TA_LEFT},
                                       {'S', b, 0},
                                       {'M', b, 3},
                                       {'C', 0, ink},
                                       {'S', a, 0},
                                       {'O', a, 2},
                                       {'C', 0, ink},
                                       {'S', b, 0},
                                       {'C', 0, ink},
                                       {'S', b, 0},
                                       {'O', b, 3},
                                       {'C', 0, 0x010203},
                                       {'A', 0, 2},
                                       {'B', 0, 2},
                                       {'S', &original, 0}};
    fittingTrace(exact, sizeof(exact) / sizeof(exact[0]));
    fittingRestored(dc, &original, clip, 32);
    fittingPin("host_complete_multifragment_trace");
    win32_host::reset();
    LOKA_VERIFY(table.draw(&dc, clip, BlockStyle()));
    const win32_host::Event full[] = {{'B', 0, TRANSPARENT},
                                      {'A', 0, TA_BASELINE | TA_LEFT},
                                      {'C', 0, ink},
                                      {'S', a, 0},
                                      {'O', a, 2},
                                      {'C', 0, ink},
                                      {'S', b, 0},
                                      {'O', b, 6},
                                      {'C', 0, 0x010203},
                                      {'A', 0, 2},
                                      {'B', 0, 2},
                                      {'S', &original, 0}};
    fittingTrace(full, sizeof(full) / sizeof(full[0]));
    fittingDraw(0, L"ab", 7, 31, a, ink, clip);
    fittingDraw(1, L"cdefgh", 15, 31, b, ink, clip);

    fittingPin("host_truncated_alignment_slack");
    const TextAlign alignments[] = {TEXT_ALIGN_LEFT, TEXT_ALIGN_CENTER, TEXT_ALIGN_RIGHT};
    for (int mode = 0; mode < 3; ++mode)
    {
      clip.right = 42; // 35px seat, 32px painted: positive slack.
      win32_host::reset();
      LOKA_VERIFY(table.draw(&dc, clip, BlockStyle(dots).align(alignments[mode])));
      const int offset = mode == 0 ? 0 : mode == 1 ? 1 : 3;
      fittingDraw(0, L"ab", 7 + offset, 31, a, ink, clip);
      fittingDraw(1, L"...", 15 + offset, 31, b, ink, clip);
      clip.right = 77;
      win32_host::reset();
      LOKA_VERIFY(table.draw(&dc, clip, BlockStyle().align(alignments[mode])));
      fittingDraw(0, L"ab", 7 + (mode == 0 ? 0 : mode == 1 ? 7 : 14), 31, a, ink, clip);
      fittingDraw(1, L"cdefgh", 15 + (mode == 0 ? 0 : mode == 1 ? 7 : 14), 31, b, ink, clip);
    }
    fittingPin("host_narrow_equal_zero_seat");
    for (int width = 23; width <= 24; ++width)
    {
      clip.right = 7 + width;
      win32_host::reset();
      LOKA_VERIFY(table.draw(&dc, clip, dots));
      LOKA_VERIFY(win32_host::draws.size() == 1);
      fittingDraw(0, L"...", 7, 31, b, ink, clip);
    }
    clip.right = 7;
    win32_host::reset();
    LOKA_VERIFY(table.draw(&dc, clip, dots));
    LOKA_VERIFY(win32_host::draws.size() == 2 && win32_host::measures == 0);
    fittingDraw(0, L"ab", 7, 31, a, ink, clip);
    fittingDraw(1, L"cdefgh", 15, 31, b, ink, clip);

    fittingPin("host_whitespace_lines");
    LOKA_VERIFY(table.build(Styled(" \tab \n\nx\r\ny", Bold), BlockStyle(), 100, &dc, controller));
    clip.right = 107;
    win32_host::reset();
    LOKA_VERIFY(table.draw(&dc, clip, BlockStyle()));
    LOKA_VERIFY(table.lines().lineCount() == 4 && win32_host::draws.size() == 3);
    fittingDraw(0, L" \tab ", 7, 21, a, ink, clip);
    fittingDraw(1, L"x", 7, 47, a, ink, clip);
    fittingDraw(2, L"y", 7, 60, a, ink, clip);
    clip.right = 29;
    win32_host::reset();
    LOKA_VERIFY(table.draw(&dc, clip, dots));
    // The first line is only 20px and remains whole in a 22px seat.
    fittingDraw(0, L" \tab ", 7, 21, a, ink, clip);
    clip.right = 25;
    win32_host::reset();
    LOKA_VERIFY(table.draw(&dc, clip, dots));
    fittingDraw(0, L" ", 7, 21, a, ink, clip);
    fittingDraw(1, L"...", 11, 21, a, ink, clip);

    LOKA_VERIFY(table.build(Styled(" \tab cdef", Bold), dots, 22, &dc, controller));
    clip.right = 29;
    win32_host::reset();
    LOKA_VERIFY(table.draw(&dc, clip, dots));
    fittingDraw(0, L" \t", 7, 21, a, ink, clip);
    fittingDraw(1, L"...", 15, 21, a, ink, clip);
    // A narrow line still advances by its nonzero leading before the next.
    LOKA_VERIFY(table.build(Styled("abcdefgh\nx", Bold), dots, 11, &dc, controller));
    clip.right = 18;
    win32_host::reset();
    LOKA_VERIFY(table.draw(&dc, clip, dots));
    fittingDraw(0, L"...", 7, 21, a, ink, clip);
    fittingDraw(1, L"x", 7, 34, a, ink, clip);

    fittingPin("host_linked_cutoff");
    LOKA_VERIFY(table.build(Linked("ab", Bold, x) + Linked("cdefgh", Bold, y), dots, 20, &dc, controller));
    clip.right = 27;
    win32_host::reset();
    LOKA_VERIFY(table.draw(&dc, clip, dots));
    LOKA_VERIFY(win32_host::draws.size() == 2);
    fittingDraw(0, L"ab", 7, 21, a, ink, clip);
    fittingDraw(1, L"...", 15, 21, a, ink, clip);

    fittingPin("host_probe_refusal_keeps_prefix");
    LOKA_VERIFY(table.build(Styled("abcdefgh", Bold), dots, 20, &dc, controller));
    FittingAccess::refuseSecondProbe(table);
    win32_host::reset();
    LOKA_VERIFY(table.draw(&dc, clip, dots));
    LOKA_VERIFY(win32_host::draws.size() == 2);
    fittingDraw(0, L"a", 7, 21, a, ink, clip);
    fittingDraw(1, L"...", 11, 21, a, ink, clip);
    fittingRestored(dc, &original, clip, 20);

    fittingPin("host_accepted_range_exact_recipe");
    LOKA_VERIFY(table.build(Linked("ab", Bold, x) + Linked("cdefgh", Bold, y), dots, 20, &dc, controller));
    TextFragment &invalidEnd = const_cast<TextFragment &>(table.lines().fragment(0));
    invalidEnd.end = 9;
    win32_host::reset();
    LOKA_VERIFY(!table.draw(&dc, clip, dots));
    LOKA_VERIFY(win32_host::draws.empty() && win32_host::outputAttempts == 0);
    fittingRestored(dc, &original, clip, 20);

    // Earlier and later short lines make the failure boundary observable.
    const char *refusals[] = {"host_dots_measure_refuses",
                              "host_fit_terminal_select_refuses",
                              "host_accepted_range_refuses",
                              "host_zero_unit_select_refuses",
                              "host_dots_select_refuses",
                              "host_dots_output_refuses",
                              "host_second_batch_refuses"};
    for (int refusal = 0; refusal < 7; ++refusal)
    {
      fittingPin(refusals[refusal]);
      AttributedString value = Styled("z\n", Bold) + mixed + Styled("\nq", Bold);
      int width = 32;
      if (refusal == 2)
      {
        value = Styled("z\n", Bold) + Linked("ab", Bold, x) + Linked("cdefgh", Bold, y) + Styled("\nq", Bold);
        width = 20;
      }
      if (refusal == 6)
        value = Styled(String(std::string("z\n") + std::string(16385, 'a') + "\nq"), Bold);
      const BlockStyle block = refusal == 6 ? BlockStyle() : dots;
      LOKA_VERIFY(table.build(value, block, width, &dc, controller));
      if (refusal == 2)
      {
        // Offset variant of the Gate 1 recipe: 2 preceding units; end exceeds
        // the entire table, while the first fragment's cached width still fits.
        TextFragment &fragment =
            const_cast<TextFragment &>(table.lines().fragment(table.lines().line(1).firstFragment));
        fragment.end = 99;
      }
      clip.right = 7 + width;
      win32_host::reset();
      win32_host::failMeasure = refusal == 0;
      win32_host::refuseSelection = refusal == 1 ? 2 : refusal == 3 ? 4 : refusal == 4 ? 5 : 0;
      win32_host::refuseOutput = refusal == 5 ? 3 : refusal == 6 ? 3 : 0;
      LOKA_VERIFY(!table.draw(&dc, clip, block));
      const bool prefix = refusal >= 3;
      LOKA_VERIFY(win32_host::draws.size() == (prefix ? 2u : 1u));
      LOKA_VERIFY(win32_host::outputAttempts == (refusal >= 5 ? 3 : prefix ? 2 : 1));
      fittingDraw(0, L"z", 7, 21, a, ink, clip);
      if (prefix)
        fittingDraw(1, refusal == 6 ? std::wstring(8192, L'a') : L"ab", 7, refusal == 6 ? 34 : 44, a, ink, clip);
      if (refusal == 1)
        LOKA_VERIFY(win32_host::measures == 0);
      fittingRestored(dc, &original, clip, width);
      win32_host::reset();
    }
    fittingPin("host_surrogate_batch_boundary");
    const std::string longText = std::string(8191, 'a') + "\xf0\x9f\x98\x80z";
    LOKA_VERIFY(table.build(Styled(String(longText), Bold), BlockStyle(), 100, &dc, controller));
    clip.right = 107;
    win32_host::reset();
    LOKA_VERIFY(table.draw(&dc, clip, BlockStyle()));
    LOKA_VERIFY(win32_host::draws.size() == 2);
    fittingDraw(0, std::wstring(8191, L'a'), 7, 21, a, ink, clip);
    std::wstring tail;
    tail.push_back(0xd83d);
    tail.push_back(0xde00);
    tail.push_back(L'z');
    fittingDraw(1, tail, 7 + 8191 * 4, 21, a, ink, clip);
    fittingRestored(dc, &original, clip, 100);
    win32_host::reset();
  }
} // namespace
