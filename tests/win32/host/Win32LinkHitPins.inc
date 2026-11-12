namespace
{
  int expectHit(const Win32AttributedTextTable &table,
                HDC dc,
                const RECT &seat,
                const BlockStyle &block,
                int x,
                int y,
                HitAnswer expected,
                std::size_t first = 0,
                std::size_t last = 0)
  {
    const POINT point = {x, y};
    LinkHit hit = {91, 92, 93, 94};
    const int measuresBefore = win32_host::measures;
    const HitAnswer answer = table.hitAt(dc, seat, block, point, hit);
    const int hitMeasures = win32_host::measures - measuresBefore;
    LOKA_VERIFY(answer == expected);
    if (answer == HIT_LINK)
      LOKA_VERIFY(hit.firstSegment == first && hit.lastSegment == last && hit.seatWidth == seat.right - seat.left
                  && hit.seatHeight == seat.bottom - seat.top);
    else
      LOKA_VERIFY(hit.firstSegment == 91 && hit.lastSegment == 92 && hit.seatWidth == 93 && hit.seatHeight == 94);
    // Independent oracle: recorded native output, not the fitted-row walker.
    // These recipes use Bold throughout; Red identifies the sole non-link span.
    win32_host::reset();
    LOKA_VERIFY(table.draw(dc, seat, block));
    bool paintedLink = false;
    for (std::size_t i = 0; i < win32_host::draws.size(); ++i)
    {
      const win32_host::Draw &draw = win32_host::draws[i];
      int width = 0;
      for (std::size_t unit = 0; unit < draw.units.size(); ++unit)
        if (draw.units[unit] < 0xdc00 || draw.units[unit] > 0xdfff)
          width += draw.font->advance;
      const int top = draw.y - draw.font->ascent;
      const bool inside = x >= seat.left && x < seat.right && y >= seat.top && y < seat.bottom && x >= draw.x
                          && x < draw.x + width && y >= top && y < draw.y + draw.font->descent;
      if (inside && draw.units != L"..." && draw.textColor != RGB(0xC4, 0x2B, 0x1C))
        paintedLink = true;
    }
    LOKA_VERIFY(paintedLink == (answer == HIT_LINK));
    return hitMeasures;
  }

  void pinLinkHitGeometry()
  {
    Win32ScenePlatformController controller;
    HostFont original = {7, 1, 0, 3};
    HostDC dc = {&original, 2, 2, 0x010203};
    const LinkTarget x(String("x")), y(String("y"));
    Win32AttributedTextTable table;
    RECT seat = {7, 11, 107, 111};
    const BlockStyle plain;
    fittingPin("host_hit_link_nonlink_link_half_open");
    LOKA_VERIFY(table.build(
        Linked("a", Bold, x) + Styled("b", Bold + Red) + Linked("c", Bold, y), plain, 100, &dc, controller));
    expectHit(table, &dc, seat, plain, 7, 11, HIT_LINK);
    expectHit(table, &dc, seat, plain, 10, 22, HIT_LINK);
    expectHit(table, &dc, seat, plain, 11, 11, HIT_NONE);
    expectHit(table, &dc, seat, plain, 14, 11, HIT_NONE);
    expectHit(table, &dc, seat, plain, 15, 11, HIT_LINK, 2, 2);
    expectHit(table, &dc, seat, plain, 19, 11, HIT_NONE);
    expectHit(table, &dc, seat, plain, 7, 23, HIT_NONE);
    expectHit(table, &dc, seat, plain, 7, 110, HIT_NONE);
    expectHit(table, &dc, seat, plain, 107, 11, HIT_NONE);
    expectHit(table, &dc, seat, plain, 7, 111, HIT_NONE);
    expectHit(table, &dc, seat, plain, 6, 11, HIT_NONE);
    fittingPin("host_hit_alignment_draw_agreement");
    const TextAlign alignments[] = {TEXT_ALIGN_LEFT, TEXT_ALIGN_CENTER, TEXT_ALIGN_RIGHT};
    for (int mode = 0; mode < 3; ++mode)
    {
      const BlockStyle block = BlockStyle().align(alignments[mode]);
      win32_host::reset();
      LOKA_VERIFY(table.draw(&dc, seat, block));
      const int left = win32_host::draws.front().x;
      expectHit(table, &dc, seat, block, left - 1, 11, HIT_NONE);
      expectHit(table, &dc, seat, block, left, 11, HIT_LINK);
      expectHit(table, &dc, seat, block, left + 4, 11, HIT_NONE);
      expectHit(table, &dc, seat, block, left + 8, 11, HIT_LINK, 2, 2);
      expectHit(table, &dc, seat, block, left + 12, 11, HIT_NONE);
    }
    fittingPin("host_hit_leading_empty_row_and_partial_clip");
    LOKA_VERIFY(table.build(Linked("a\n\nb", Bold, x), plain, 100, &dc, controller));
    expectHit(table, &dc, seat, plain, 7, 23, HIT_NONE);
    expectHit(table, &dc, seat, plain, 7, 24, HIT_NONE);
    expectHit(table, &dc, seat, plain, 7, 37, HIT_LINK);
    seat.bottom = 39;
    expectHit(table, &dc, seat, plain, 7, 38, HIT_LINK);
    expectHit(table, &dc, seat, plain, 7, 39, HIT_NONE);
    fittingPin("host_hit_dots_clipped_tail_only_hit_row_fitted");
    const BlockStyle dots = BlockStyle().truncation(TEXT_TRUNCATION_ELLIPSIS);
    LOKA_VERIFY(table.build(Linked("abcdefgh\nabcdefgh\nabcdefgh", Bold, x), dots, 20, &dc, controller));
    seat.right = 27;
    seat.bottom = 111;
    win32_host::reset();
    LOKA_VERIFY(expectHit(table, &dc, seat, dots, 7, 24, HIT_LINK) == 1);
    LOKA_VERIFY(dc.font == &original);
    expectHit(table, &dc, seat, dots, 15, 24, HIT_NONE);
    expectHit(table, &dc, seat, dots, 27, 24, HIT_NONE);
    seat.right = 18;
    expectHit(table, &dc, seat, dots, 7, 11, HIT_NONE);
    seat.right = 7;
    expectHit(table, &dc, seat, dots, 7, 11, HIT_NONE);
    seat.right = 6;
    expectHit(table, &dc, seat, dots, 7, 11, HIT_NONE);
    seat.right = 107;
    seat.bottom = 11;
    expectHit(table, &dc, seat, dots, 7, 11, HIT_NONE);
    seat.bottom = 10;
    expectHit(table, &dc, seat, dots, 7, 11, HIT_NONE);
    seat.bottom = 111;
    fittingPin("host_hit_contributor_range");
    LOKA_VERIFY(table.build(Linked("a", Bold, x) + Linked("", Bold, y) + Linked("b", Bold, x) + Linked("", Bold, y),
                            plain,
                            100,
                            &dc,
                            controller));
    expectHit(table, &dc, seat, plain, 7, 11, HIT_LINK, 0, 2);
    expectHit(table, &dc, seat, plain, 11, 11, HIT_LINK, 0, 2);
    fittingPin("host_hit_surrogate_batch_boundary");
    const std::string text = std::string(8191, 'a') + "\xf0\x9f\x98\x80z";
    LOKA_VERIFY(table.build(Linked(String(text), Bold, x), plain, 32767, &dc, controller));
    seat.right = 40000;
    expectHit(table, &dc, seat, plain, 7 + 8191 * 4, 11, HIT_LINK);
    expectHit(table, &dc, seat, plain, 7 + 8192 * 4 - 1, 11, HIT_LINK);
    fittingPin("host_hit_accepted_width_refuses_and_invalid_table");
    LOKA_VERIFY(table.build(Linked("ab", Bold, x), plain, 100, &dc, controller));
    FittingAccess::refuseSecondProbe(table);
    const POINT point = {7, 11};
    LinkHit refused = {91, 92, 93, 94};
    win32_host::reset();
    LOKA_VERIFY(table.hitAt(&dc, seat, plain, point, refused) == HIT_REFUSED);
    LOKA_VERIFY(refused.firstSegment == 91 && refused.lastSegment == 92 && refused.seatWidth == 93
                && refused.seatHeight == 94);
    LOKA_VERIFY(win32_host::selections == 0 && dc.font == &original);
    table.clear();
    LOKA_VERIFY(table.hitAt(&dc, seat, plain, point, refused) == HIT_REFUSED);
    LOKA_VERIFY(refused.firstSegment == 91 && refused.lastSegment == 92 && refused.seatWidth == 93
                && refused.seatHeight == 94);
    win32_host::reset();
  }
} // namespace
namespace
{
  void pinLinkHitCleanup()
  {
    using namespace win32_host;
    using namespace loka::app::scene;
    Win32ScenePlatformController controller;
    HostWindow root;
    controller.rootHwnd_ = &root;
    const LinkTarget target(String("target"));
    AttributedTextNode node(
        (AttributedText(Linked("abcdefgh", Bold, target)) + BlockStyle().truncation(TEXT_TRUNCATION_ELLIPSIS)).props);
    Win32AttributedTextContext *context = new Win32AttributedTextContext(&controller, &root, 0, 0, 20, 100, &node);
    node.setContext(context);
    LayoutState layout;
    layout.width = 20;
    context->layout(&controller, layout);
    HWND window = context->paintHwnd();
    HostFont borrowedFont = {7, 1, 0, 3};
    HFONT original = &borrowedFont;
    window->dc.font = original;
    const POINT point = {0, 0};
    const char *names[] = {"host_cleanup_success",
                           "host_cleanup_client_rect",
                           "host_cleanup_getdc",
                           "host_cleanup_select",
                           "host_cleanup_extent",
                           "host_cleanup_first_restore",
                           "host_cleanup_both_restores",
                           "host_cleanup_release",
                           "host_cleanup_snapshot",
                           "host_cleanup_first_restore_release",
                           "host_cleanup_both_restores_release"};
    for (int injection = 0; injection < 11; ++injection)
    {
      fittingPin(names[injection]);
      reset();
      acquisitions = releaseAttempts = releases = restoreAttempts = 0;
      restoreFont = original;
      refusedRestores = injection == 5 || injection == 9 ? 1 : injection == 6 || injection == 10 ? 2 : 0;
      refuseClientRect = injection == 1;
      failDC = injection == 2;
      refuseSelection = injection == 3 ? 1 : 0;
      failMeasure = injection == 4;
      refuseRelease = injection == 7 || injection == 9 || injection == 10;
      refuseSnapshot = injection == 8;
      LinkHit hit = {91, 92, 93, 94};
      LOKA_VERIFY(FittingAccess::prepare(*context, point, hit) == (injection ? HIT_REFUSED : HIT_LINK));
      const int acquired = injection == 1 || injection == 2 ? 0 : 1;
      LOKA_VERIFY(acquisitions == acquired && releaseAttempts == acquired);
      LOKA_VERIFY(releases == (refuseRelease ? 0 : acquired));
      LOKA_VERIFY(borrowedDCs[&window->dc] == (refuseRelease ? 1 : 0));
      const int restores = !acquired || injection == 8 ? 0 : injection == 5 || injection == 6 || injection >= 9 ? 2 : 1;
      LOKA_VERIFY(restoreAttempts == restores);
      LOKA_VERIFY(window->dc.font == (injection == 6 || injection == 10 ? controller.fallbackFont(Bold) : original));
      if (injection)
        LOKA_VERIFY(hit.firstSegment == 91 && hit.lastSegment == 92 && hit.seatWidth == 93 && hit.seatHeight == 94);
      // Repair only after observing the deliberately withheld API effects.
      refuseClientRect = failDC = failMeasure = refuseRelease = refuseSnapshot = false;
      refusedRestores = 0;
      restoreFont = 0;
      window->dc.font = original;
      if (borrowedDCs[&window->dc])
        LOKA_VERIFY(ReleaseDC(window, &window->dc));
    }
    fittingPin("host_table_cleanup_publication_is_independent_of_context");
    const int tableInjections[] = {0, 3, 4, 5, 6, 8};
    const RECT tableSeat = {0, 0, 20, 100};
    for (unsigned i = 0; i < sizeof(tableInjections) / sizeof(tableInjections[0]); ++i)
    {
      const int injection = tableInjections[i];
      reset();
      restoreFont = original;
      restoreAttempts = 0;
      refusedRestores = injection == 5 ? 1 : injection == 6 ? 2 : 0;
      refuseSelection = injection == 3 ? 1 : 0;
      failMeasure = injection == 4;
      refuseSnapshot = injection == 8;
      LinkHit direct = {91, 92, 93, 94};
      LOKA_VERIFY(FittingAccess::table(*context).hitAt(&window->dc, tableSeat, node.props.blockStyle_, point, direct)
                  == (injection ? HIT_REFUSED : HIT_LINK));
      if (injection)
        LOKA_VERIFY(direct.firstSegment == 91 && direct.lastSegment == 92 && direct.seatWidth == 93
                    && direct.seatHeight == 94);
      LOKA_VERIFY(restoreAttempts == (injection == 8 ? 0 : injection == 5 || injection == 6 ? 2 : 1));
      LOKA_VERIFY(window->dc.font == (injection == 6 ? controller.fallbackFont(Bold) : original));
      refuseSnapshot = failMeasure = false;
      refusedRestores = 0;
      restoreFont = 0;
      window->dc.font = original;
    }
    fittingPin("host_cleanup_no_font_work_without_ellipsis_or_hit_row");
    node.props.blockStyle_ = BlockStyle();
    reset();
    restoreAttempts = 0;
    restoreFont = original;
    LinkHit hit;
    LOKA_VERIFY(FittingAccess::prepare(*context, point, hit) == HIT_LINK);
    LOKA_VERIFY(selections == 0 && restoreAttempts == 0);
    const POINT outside = {-1, 0};
    LOKA_VERIFY(FittingAccess::prepare(*context, outside, hit) == HIT_NONE);
    LOKA_VERIFY(selections == 0 && restoreAttempts == 0);
    restoreFont = 0;
    reset();
  }
} // namespace
namespace
{
  class HitReadableString : public loka::platform::String
  {
  public:
    HitReadableString(const bool &readable, const char *text)
        : readable_(readable),
          text_(text)
    {
    }
    virtual bool appendUtf8(std::string &out) const
    {
      if (!this->readable_)
        return false;
      out += this->text_;
      return true;
    }

  private:
    const bool &readable_;
    const char *text_;
  };
  String hitString(const bool &readable, const char *text)
  {
    return String::FromPlatform(
        loka::core::Managed<loka::platform::String>::Wrap(new HitReadableString(readable, text)));
  }
  void pinLinkHitEligibility()
  {
    using namespace loka::app::scene;
    Win32ScenePlatformController controller;
    HostWindow root;
    controller.rootHwnd_ = &root;
    const LinkTarget x(String("x")), y(String("y"));
    const AttributedString value = Linked("ab", Bold, x);
    loka::core::MutableState<AttributedString> live(value);
    AttributedTextNode node(AttributedText(&live).props);
    Win32AttributedTextContext *context = new Win32AttributedTextContext(&controller, &root, 0, 0, 100, 100, &node);
    node.setContext(context);
    LayoutState layout;
    layout.width = 100;
    context->layout(&controller, layout);
    FittingAccess::place(*context, 0, 0, 100, 100);
    const RECT seat = {0, 0, 100, 100};
    const POINT point = {0, 0};
    LinkHit hit;
    LOKA_VERIFY(FittingAccess::prepare(*context, point, hit) == HIT_LINK);
    fittingPin("host_eligible_unpainted_partial_failed_complete");
    LOKA_VERIFY(!FittingAccess::eligible(*context, hit));
    RECT partial = {0, 0, 90, 100};
    FittingAccess::draw(*context, &context->paintHwnd()->dc, partial);
    LOKA_VERIFY(!FittingAccess::eligible(*context, hit));
    win32_host::reset();
    win32_host::refuseOutput = 1;
    FittingAccess::draw(*context, &context->paintHwnd()->dc, seat);
    LOKA_VERIFY(!FittingAccess::eligible(*context, hit));
    win32_host::reset();
    FittingAccess::draw(*context, &context->paintHwnd()->dc, seat);
    LOKA_VERIFY(FittingAccess::eligible(*context, hit));
    fittingPin("host_eligible_ancestor_translation_and_fresh_hit");
    root.rect.left = 30;
    root.rect.top = 40;
    LOKA_VERIFY(FittingAccess::eligible(*context, hit));
    LinkHit fresh;
    LOKA_VERIFY(FittingAccess::prepare(*context, point, fresh) == HIT_LINK && FittingAccess::eligible(*context, fresh));
    fittingPin("host_eligible_relayout_without_then_with_repaint");
    FittingAccess::place(*context, 20, 30, 100, 100);
    LOKA_VERIFY(!FittingAccess::eligible(*context, hit));
    FittingAccess::draw(*context, &context->paintHwnd()->dc, seat);
    LOKA_VERIFY(FittingAccess::eligible(*context, hit));
    fittingPin("host_eligible_size_changed_then_changed_back");
    FittingAccess::place(*context, 20, 30, 80, 100);
    win32_host::paintClip.right = 80;
    RECT narrow = {0, 0, 80, 100};
    FittingAccess::draw(*context, &context->paintHwnd()->dc, narrow);
    LOKA_VERIFY(!FittingAccess::eligible(*context, hit));
    FittingAccess::place(*context, 20, 30, 100, 100);
    win32_host::paintClip = seat;
    FittingAccess::draw(*context, &context->paintHwnd()->dc, seat);
    LOKA_VERIFY(FittingAccess::eligible(*context, hit));
    fittingPin("host_eligible_equal_size_message_without_then_with_repaint");
    SendMessageW(context->paintHwnd(), WM_SIZE, 0, 0);
    LOKA_VERIFY(!FittingAccess::eligible(*context, hit));
    FittingAccess::draw(*context, &context->paintHwnd()->dc, seat);
    LOKA_VERIFY(FittingAccess::eligible(*context, hit));
    fittingPin("host_eligible_unchanged_repaint");
    FittingAccess::draw(*context, &context->paintHwnd()->dc, seat);
    LOKA_VERIFY(FittingAccess::eligible(*context, hit));
    fittingPin("host_eligible_seat_width_height_and_range");
    LinkHit changed = hit;
    changed.seatWidth = 99;
    LOKA_VERIFY(!FittingAccess::eligible(*context, changed));
    changed = hit;
    changed.seatHeight = 99;
    LOKA_VERIFY(!FittingAccess::eligible(*context, changed));
    changed = hit;
    changed.lastSegment = 1;
    LOKA_VERIFY(!FittingAccess::eligible(*context, changed));
    changed = hit;
    changed.firstSegment = 9;
    LOKA_VERIFY(!FittingAccess::eligible(*context, changed));
    fittingPin("host_eligible_presented_segmentation_table_current");
    const AttributedString split = Linked("a", Bold, x) + Linked("b", Bold, x);
    FittingAccess::present(*context, split, seat);
    LOKA_VERIFY(!FittingAccess::eligible(*context, hit));
    FittingAccess::present(*context, value, seat);
    loka::core::MutableState<AttributedString> replacement(split);
    node.props.text_ = &replacement;
    LOKA_VERIFY(!FittingAccess::eligible(*context, hit));
    node.props.text_ = &live;
    LOKA_VERIFY(FittingAccess::eligible(*context, hit));
    fittingPin("host_eligible_detached_other_context_invalid_table_retired");
    NotifySubtreeNodeDetached(&node);
    LOKA_VERIFY(!FittingAccess::eligible(*context, hit));
    NotifySubtreeNodeAttached(&node);
    LOKA_VERIFY(FittingAccess::eligible(*context, hit));
    Win32AttributedTextContext other(&controller, &root, 0, 0, 100, 100, &node);
    other.setOwner(&node);
    other.layout(&controller, layout);
    FittingAccess::place(other, 0, 0, 100, 100);
    FittingAccess::draw(other, &other.paintHwnd()->dc, seat);
    LOKA_VERIFY(!FittingAccess::eligible(other, hit));
    other.onFactChanged(NODE_FACT_ATTACHED, NODE_FACT_RETIRED);
    LOKA_VERIFY(!FittingAccess::eligibleWithoutRows(*context, hit));
    LifecycleFactTestAccess::MarkSubtreeRetired(&node);
    LifecycleFactTestAccess::DeliverFacts(&node);
    LOKA_VERIFY(!FittingAccess::eligible(*context, hit));
  }
  void pinLinkHitRunCoverage()
  {
    using namespace loka::app::scene;
    for (int refusal = 0; refusal < 2; ++refusal)
    {
      fittingPin(refusal ? "host_eligible_distinct_targets_stop_equal"
                         : "host_eligible_empty_middle_conversion_refuses");
      bool readable = true;
      const LinkTarget x(hitString(readable, "x")), other(hitString(readable, "x")), y(String("y"));
      const AttributedString value =
          refusal ? Linked("a", Bold, x) + Linked("b", Bold, other)
                  : Linked("a", Bold, x) + Linked(hitString(readable, ""), Bold, y) + Linked("b", Bold, x);
      Win32ScenePlatformController controller;
      HostWindow root;
      controller.rootHwnd_ = &root;
      AttributedTextNode node(AttributedText(value).props);
      Win32AttributedTextContext *context = new Win32AttributedTextContext(&controller, &root, 0, 0, 100, 100, &node);
      node.setContext(context);
      LayoutState layout;
      layout.width = 100;
      context->layout(&controller, layout);
      FittingAccess::place(*context, 0, 0, 100, 100);
      const RECT seat = {0, 0, 100, 100};
      FittingAccess::draw(*context, &context->paintHwnd()->dc, seat);
      const POINT point = {0, 0};
      LinkHit hit;
      LOKA_VERIFY(FittingAccess::prepare(*context, point, hit) == HIT_LINK);
      LOKA_VERIFY(hit.firstSegment == 0 && hit.lastSegment == (refusal ? 1u : 2u));
      LOKA_VERIFY(FittingAccess::eligible(*context, hit));
      readable = false;
      LOKA_VERIFY(!FittingAccess::eligible(*context, hit));
    }
  }
} // namespace
