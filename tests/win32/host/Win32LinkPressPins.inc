namespace
{
  using namespace loka::app::scene;
  using namespace loka::core;
  using namespace win32_host;

  struct PressFixture
  {
    Win32ScenePlatformController controller;
    HostWindow root, other;
    PushStateTracker tracker;
    MutableState<LinkTarget> result;
    NodeState<LinkTarget> box;
    MutableState<AttributedString> text;
    AttributedTextNode node;
    Win32AttributedTextContext *context;
    int writes;
    void (*observer)(PressFixture &);
    bool readable;
    explicit PressFixture(const AttributedString &value = Linked("ab", Bold, LinkTarget(String("x"))))
        : result(LinkTarget()),
          box(&result, &tracker),
          text(value),
          node(AttributedText(&text).activatedLink(box).props),
          context(0),
          writes(0),
          observer(0),
          readable(true)
    {
      root.data = other.data = 0;
      root.proc = other.proc = 0;
      root.parent = other.parent = 0;
      controller.rootHwnd_ = &root;
      tracker.addState(&result);
      result.bind(&changed, this, false);
      context = new Win32AttributedTextContext(&controller, &root, 0, 0, 100, 100, &node);
      node.setPropsTypeId(AttributedTextProps::staticTypeId());
      node.setContext(context);
      rebuild();
    }
    ~PressFixture()
    {
      pressHook.call = 0;
      loka::testing::failCaptureReleases(0);
      loka::testing::failCaptureAcquisitions(0);
      loka::testing::failCancelReleases(0);
      if (captureOwner)
        LOKA_VERIFY(ReleaseCapture());
      result.unbind(&changed, this);
    }
    HWND hwnd() const
    {
      return context->paintHwnd();
    }
    void paint()
    {
      const RECT seat = {0, 0, 100, 100};
      FittingAccess::place(*context, 0, 0, 100, 100);
      paintClip = seat;
      FittingAccess::draw(*context, &hwnd()->dc, seat);
    }
    void rebuild()
    {
      LayoutState layout;
      layout.width = 100;
      layout.inputs = NODE_DIRTY_MYSELF;
      context->layout(&controller, layout);
      paint();
    }
    void down(int x = 0)
    {
      SendMessageW(hwnd(), WM_LBUTTONDOWN, 0, static_cast<unsigned short>(x));
    }
    void up(int x = 0)
    {
      SendMessageW(hwnd(), WM_LBUTTONUP, 0, static_cast<unsigned short>(x));
    }
    static void changed(void *data)
    {
      PressFixture &f = *static_cast<PressFixture *>(data);
      ++f.writes;
      LOKA_VERIFY(f.controller.borrowPhase().open());
      if (f.observer)
        f.observer(f);
    }
  };
  void pressRebuild(void *data)
  {
    static_cast<PressFixture *>(data)->rebuild();
  }
  void pressDifferentRebuild(void *data)
  {
    PressFixture &f = *static_cast<PressFixture *>(data);
    f.text.set(Linked("changed", Bold, LinkTarget(String("y"))));
    f.rebuild();
  }
  void pressSteal(void *data)
  {
    SetCapture(&static_cast<PressFixture *>(data)->other);
  }
  void pressReturn(void *data)
  {
    PressFixture &f = *static_cast<PressFixture *>(data);
    SetCapture(&f.other);
    SetCapture(f.hwnd());
  }
  void pressCancel(void *data)
  {
    modalCancel(static_cast<PressFixture *>(data)->hwnd());
  }
  void pressPartial(void *data)
  {
    PressFixture &f = *static_cast<PressFixture *>(data);
    const RECT seat = {0, 0, 100, 100};
    paintClip.right = 90;
    FittingAccess::draw(*f.context, &f.hwnd()->dc, seat);
  }
  void pressFailedPaint(void *data)
  {
    refuseOutput = outputAttempts + 1;
    static_cast<PressFixture *>(data)->paint();
    refuseOutput = 0;
  }
  void pressResize(void *data)
  {
    PressFixture &f = *static_cast<PressFixture *>(data);
    const RECT narrow = {0, 0, 80, 100};
    FittingAccess::place(*f.context, 0, 0, 80, 100);
    paintClip = narrow;
    FittingAccess::draw(*f.context, &f.hwnd()->dc, narrow);
  }
  void pressDetach(void *data)
  {
    PressFixture &f = *static_cast<PressFixture *>(data);
    f.context->onFactChanged(NODE_FACT_ATTACHED, NODE_FACT_DETACHED_RETAINED);
  }
  void pressNested(void *data)
  {
    PressFixture &f = *static_cast<PressFixture *>(data);
    const int gets = captureGets, sets = captureSets, releasesBefore = captureReleases;
    const int rects = clientRectCalls, dcs = dcCalls;
    f.down();
    f.up();
    f.up();
    LOKA_VERIFY(gets == captureGets && sets == captureSets && releasesBefore == captureReleases);
    LOKA_VERIFY(rects == clientRectCalls && dcs == dcCalls);
  }
  void pressObserver(PressFixture &f)
  {
    f.observer = 0;
    f.down();
    f.up();
  }
  void pressObserverProps(PressFixture &f)
  {
    AttributedText replacement(&f.text);
    replacement.activatedLink(f.box);
    LOKA_VERIFY(replacement.applyPropsToNode(&f.node));
  }
  void pinLinkPressProcedure()
  {
    fittingPin("press_ordinary_self_notification_and_orphan");
    {
      PressFixture f;
      f.down();
      SetCapture(f.hwnd());
      f.up();
      LOKA_VERIFY(f.writes == 1 && !GetCapture());
      const int rects = clientRectCalls, dcs = dcCalls;
      f.up();
      LOKA_VERIFY(f.writes == 1 && rects == clientRectCalls && dcs == dcCalls);
    }
    fittingPin("press_release_refusal_then_new_down");
    {
      PressFixture f;
      f.down();
      loka::testing::failCaptureReleases(1);
      f.up();
      LOKA_VERIFY(f.writes == 0 && GetCapture() == f.hwnd());
      f.down();
      f.up();
      LOKA_VERIFY(f.writes == 1);
    }
    fittingPin("press_stolen_capture_is_not_released");
    {
      PressFixture f;
      f.down();
      menuCapture(&f.other);
      const int before = captureReleases;
      f.up();
      LOKA_VERIFY(f.writes == 0 && GetCapture() == &f.other && before == captureReleases);
    }
    const PressPhase phases[] = {PRESS_SET_CAPTURE, PRESS_CLIENT_RECT, PRESS_GET_DC, PRESS_RELEASE_CAPTURE};
    void (*actions[])(void *) = {pressRebuild,
                                 pressSteal,
                                 pressReturn,
                                 pressCancel,
                                 pressPartial,
                                 pressFailedPaint,
                                 pressNested,
                                 pressDetach,
                                 pressResize,
                                 pressDifferentRebuild};
    const char *names[] = {"rebuild",
                           "steal",
                           "return",
                           "cancel",
                           "partial",
                           "failed_paint",
                           "nested",
                           "detach",
                           "resize",
                           "different_rebuild"};
    for (unsigned phase = 0; phase < 4; ++phase)
      for (unsigned action = 0; action < 10; ++action)
      {
        std::printf("press_phase_%u_%s\n", phase, names[action]);
        std::fflush(stdout);
        PressFixture f;
        if (phase)
          f.down();
        pressHook.arm(phases[phase], actions[action], &f);
        if (!phase)
        {
          f.down();
          if (action == 5 || action == 8)
            f.paint();
          f.up();
        }
        else
          f.up();
        const bool delivers =
            action == 4 || action == 6 || (action == 2 && phase != 0) || (phase == 3 && (action == 1 || action == 3));
        // GetClientRect runs before reading geometry: a new complete seat is valid.
        const bool resizedBeforeRead = phase == 1 && action == 8;
        LOKA_VERIFY(f.writes == (delivers || resizedBeforeRead ? 1 : 0));
      }
    fittingPin("cancel_before_take_default_release_refused");
    {
      PressFixture f;
      f.down();
      loka::testing::failCancelReleases(1);
      modalCancel(f.hwnd());
      LOKA_VERIFY(GetCapture() == f.hwnd());
      f.up();
      LOKA_VERIFY(f.writes == 0);
    }
    fittingPin("cancel_default_releases");
    {
      PressFixture f;
      f.down();
      modalCancel(f.hwnd());
      LOKA_VERIFY(!GetCapture());
      f.up();
      LOKA_VERIFY(f.writes == 0);
    }
    fittingPin("press_cancel_after_take_default_refused_delivers");
    {
      PressFixture f;
      f.down();
      loka::testing::failCancelReleases(1);
      pressHook.arm(PRESS_GET_DC, pressCancel, &f);
      f.up();
      LOKA_VERIFY(f.writes == 1);
    }
    fittingPin("press_prefilter_cancel_starts_press");
    {
      PressFixture f;
      pressHook.arm(PRESS_GET_DC, pressCancel, &f);
      f.down();
      f.up();
      LOKA_VERIFY(f.writes == 1);
    }
    fittingPin("down_loss_then_capture_return");
    {
      PressFixture f;
      pressHook.arm(PRESS_SET_CAPTURE, pressReturn, &f);
      f.down();
      LOKA_VERIFY(!GetCapture());
      f.up();
      LOKA_VERIFY(f.writes == 0);
    }
    fittingPin("press_acquire_refused");
    {
      PressFixture f;
      loka::testing::failCaptureAcquisitions(1);
      const int before = captureReleases;
      f.down();
      LOKA_VERIFY(!GetCapture() && captureReleases == before);
      SetCapture(f.hwnd());
      f.up();
      LOKA_VERIFY(f.writes == 0);
    }
    fittingPin("press_cleanup_repeated_refusal_orphan_and_miss");
    for (int miss = 0; miss < 2; ++miss)
    {
      PressFixture f;
      SetCapture(f.hwnd());
      loka::testing::failCaptureReleases(2);
      for (int attempt = 0; attempt < 3; ++attempt)
      {
        const int before = captureReleases;
        if (miss)
          f.down(-1);
        else
          f.up();
        LOKA_VERIFY(captureReleases == before + 1 && f.writes == 0);
        LOKA_VERIFY(GetCapture() == (attempt < 2 ? f.hwnd() : 0));
      }
      f.down();
      f.up();
      LOKA_VERIFY(f.writes == 1);
    }
    fittingPin("press_observer_new_press_and_props");
    {
      PressFixture f;
      f.observer = pressObserver;
      f.down();
      f.up();
      LOKA_VERIFY(f.writes == 2);
      f.observer = pressObserverProps;
      f.down();
      f.up();
      LOKA_VERIFY(f.writes == 3);
    }
    fittingPin("press_separated_runs_same_target");
    {
      const LinkTarget x(String("x"));
      PressFixture f(Linked("a", Bold, x) + Styled("b", Bold) + Linked("c", Bold, x));
      f.down();
      f.up(8);
      LOKA_VERIFY(f.writes == 0);
      f.down(8);
      f.up(8);
      LOKA_VERIFY(f.writes == 1);
    }
    fittingPin("press_no_hit_release");
    {
      PressFixture f;
      f.down();
      f.up(-1);
      LOKA_VERIFY(f.writes == 0 && !GetCapture());
    }
    fittingPin("press_stale_candidate_overwritten");
    {
      PressFixture f;
      f.down();
      f.down();
      f.up();
      LOKA_VERIFY(f.writes == 1);
    }
    fittingPin("press_rebuild_cancels_but_next_down_starts");
    {
      PressFixture f;
      f.down();
      f.rebuild();
      f.up();
      LOKA_VERIFY(f.writes == 0);
      f.down();
      f.rebuild();
      f.down();
      f.up();
      LOKA_VERIFY(f.writes == 1);
    }
  }
} // namespace
namespace
{
  void pressUnreadable(void *data)
  {
    static_cast<PressFixture *>(data)->readable = false;
  }
  void pressCheckRetired(void *data)
  {
    FittingAccess::expectInvalidated(*static_cast<PressFixture *>(data)->context, false);
  }
  void pinLinkPressHistory()
  {
    fittingPin("down_run_contracts_then_recovers");
    for (int phase = 0; phase < 3; ++phase)
    {
      PressFixture f;
      const LinkTarget x(String("x")), otherX(hitString(f.readable, "x"));
      f.text.set(Linked("a", Bold, x) + Linked("b", Italic, otherX));
      f.rebuild();
      if (phase == 0)
        pressHook.arm(PRESS_SET_CAPTURE, pressUnreadable, &f);
      f.down();
      if (phase == 1)
        f.readable = false;
      if (phase == 2)
      {
        f.readable = false;
        f.readable = true;
      }
      if (phase == 0)
        f.readable = true;
      f.up();
      LOKA_VERIFY(f.writes == (phase == 2 ? 1 : 0));
    }
    const LinkTarget x(String("x")), y(String("y"));
    const AttributedString a = Linked("ab", Bold, x) + Linked("cd", Bold, y);
    const AttributedString b = Linked("zz", Bold, x) + Linked("cd", Bold, y);
    const AttributedString a2 = Linked("a", Bold, x) + Linked("b", Bold, x) + Linked("cd", Bold, y);
    fittingPin("press_A_B_A_and_A2_with_without_rebuild");
    for (int build = 0; build < 2; ++build)
      for (int split = 0; split < 2; ++split)
      {
        PressFixture f(a);
        f.down(8);
        f.text.set(b);
        if (build)
          f.rebuild();
        f.text.set(split ? a2 : a);
        if (build)
          f.rebuild();
        f.up(8);
        LOKA_VERIFY(f.writes == (!build && !split ? 1 : 0));
      }
    fittingPin("press_forced_equal_notification_with_without_layout");
    for (int build = 0; build < 2; ++build)
    {
      PressFixture f;
      f.down();
      f.text.set(f.text.get(), true);
      if (build)
        f.rebuild();
      f.up();
      LOKA_VERIFY(f.writes == (build ? 0 : 1));
    }
    fittingPin("press_each_rebuild_entrance");
    for (int path = 0; path < 8; ++path)
    {
      PressFixture f;
      if (path == 7)
      {
        f.controller.ensureDisplayFont();
        f.rebuild();
      }
      f.down();
      switch (path)
      {
      case 0:
        f.context->onPropsApplied();
        break;
      case 1:
        f.controller.textEnvironment_.changed();
        break;
      case 2:
        SendMessageW(f.hwnd(), WM_SETFONT, 0, 0);
        break;
      case 3:
        f.rebuild();
        break;
      case 4:
        failDC = true;
        f.rebuild();
        failDC = false;
        break;
      case 5:
        f.controller.updateDisplayScale(loka::win32::Win32DisplayScale(144));
        break;
      case 6:
        failMetrics = true;
        f.rebuild();
        LOKA_VERIFY(!FittingAccess::table(*f.context).valid());
        failMetrics = false;
        break;
      case 7:
        f.controller.applyDisplayFontToNativeSubtree(f.controller.displayFont_);
        break;
      }
      FittingAccess::expectInvalidated(*f.context, false);
      f.rebuild();
      f.up();
      LOKA_VERIFY(f.writes == 0);
    }
    fittingPin("press_retained_detach_reattach_draw_without_layout");
    {
      PressFixture f;
      f.down();
      NotifySubtreeNodeDetached(&f.node);
      LifecycleFactTestAccess::DeliverFacts(&f.node);
      NotifySubtreeNodeAttached(&f.node);
      LifecycleFactTestAccess::DeliverFacts(&f.node);
      f.paint();
      f.up();
      LOKA_VERIFY(f.writes == 0);
      f.down();
      f.up();
      LOKA_VERIFY(f.writes == 1);
    }
    fittingPin("press_retired_without_capture_notification");
    {
      PressFixture f;
      f.down();
      pressHook.arm(PRESS_HIDE, pressCheckRetired, &f);
      LifecycleFactTestAccess::MarkSubtreeRetired(&f.node);
      LifecycleFactTestAccess::DeliverFacts(&f.node);
      LOKA_VERIFY(!f.context->paintHwnd() && !FittingAccess::table(*f.context).valid());
      LOKA_VERIFY(f.writes == 0);
    }
    fittingPin("press_box_only_replacement_cancels");
    {
      PressFixture f;
      MutableState<LinkTarget> replacement;
      f.tracker.addState(&replacement);
      const NodeState<LinkTarget> box(&replacement, &f.tracker);
      f.down();
      AttributedText props(&f.text);
      props.activatedLink(box);
      LOKA_VERIFY(props.applyPropsToNode(&f.node));
      FittingAccess::expectInvalidated(*f.context, false);
      f.rebuild();
      f.up();
      LOKA_VERIFY(f.writes == 0 && replacement.get() == LinkTarget());
      f.tracker.removeState(&replacement);
    }
    fittingPin("press_translation_and_complete_same_size_repaint");
    {
      PressFixture f;
      f.down();
      f.root.rect.left = 10;
      f.root.rect.top = 20;
      f.paint();
      f.up();
      LOKA_VERIFY(f.writes == 1);
    }
    fittingPin("press_preparation_refusal_and_unknown_presentation");
    {
      PressFixture f;
      failDC = true;
      f.down();
      failDC = false;
      f.up();
      LOKA_VERIFY(f.writes == 0);
      f.down();
      SendMessageW(f.hwnd(), WM_SIZE, 0, 0);
      f.up();
      LOKA_VERIFY(f.writes == 0);
    }
  }
} // namespace
namespace
{
  void pressCheckHidden(void *data)
  {
    PressFixture &f = *static_cast<PressFixture *>(data);
    FittingAccess::expectInvalidated(*f.context, f.observer != 0);
  }
  void pressDetachWithCheck(void *data)
  {
    PressFixture &f = *static_cast<PressFixture *>(data);
    pressHook.arm(PRESS_HIDE, pressCheckHidden, &f);
    pressDetach(data);
  }
  void pinLinkPressDoor2()
  {
    fittingPin("door2_invalidates_before_hide");
    for (int active = 0; active < 2; ++active)
    {
      PressFixture f;
      if (active)
      {
        f.observer = pressObserver;
        pressHook.arm(PRESS_SET_CAPTURE, pressDetachWithCheck, &f);
        f.down();
      }
      else
      {
        f.down();
        pressDetachWithCheck(&f);
      }
      f.up();
      LOKA_VERIFY(f.writes == 0);
    }
  }
} // namespace

namespace
{
  void pressRebuildNested(void *data)
  {
    pressRebuild(data);
    pressNested(data);
  }
  void pressTakeOwn(void *data)
  {
    SetCapture(static_cast<PressFixture *>(data)->hwnd());
  }
  void pinLinkPressContinuations()
  {
    fittingPin("press_rebuild_then_nested_and_later_click");
    for (int up = 0; up < 2; ++up)
    {
      PressFixture f;
      if (up)
        f.down();
      pressHook.arm(up ? PRESS_RELEASE_CAPTURE : PRESS_SET_CAPTURE, pressRebuildNested, &f);
      if (up)
        f.up();
      else
      {
        f.down();
        f.up();
      }
      LOKA_VERIFY(f.writes == 0);
      f.down();
      f.up();
      LOKA_VERIFY(f.writes == 1);
    }
    fittingPin("press_K1_foreign_controller_capture_after_release");
    {
      PressFixture f, foreign;
      f.down();
      pressHook.arm(PRESS_RELEASE_CAPTURE, pressTakeOwn, &foreign);
      f.up();
      LOKA_VERIFY(f.writes == 1 && GetCapture() == foreign.hwnd());
      foreign.up();
      LOKA_VERIFY(foreign.writes == 0);
    }
    fittingPin("press_owned_capture_miss_and_preparation_refusal");
    for (int refused = 0; refused < 2; ++refused)
    {
      PressFixture f;
      f.down();
      const int before = captureReleases;
      failDC = refused != 0;
      f.down(refused ? 0 : -1);
      failDC = false;
      LOKA_VERIFY(captureReleases == before + 1 && !GetCapture());
      f.up();
      LOKA_VERIFY(f.writes == 0);
    }
  }
} // namespace

namespace
{
  void partialDamage(PressFixture &f, bool known)
  {
    LOKA_VERIFY(FittingAccess::known(*f.context) == known);
    const PaintQuery query = {Win32RetirableContext::paintScope(), PLACEMENT_ELIGIBLE};
    const PaintAnswer answer = f.context->queryPaintDamage(query);
    if (known)
      LOKA_VERIFY(answer.kind == PAINT_ANSWER_EXACT && answer.damage.width == 0 && answer.damage.height == 0);
    else
      LOKA_VERIFY(answer.kind == PAINT_ANSWER_REFUSED && answer.reason == PAINT_REFUSED_HISTORY_UNKNOWN);
  }
  void drawPartial(PressFixture &f, const RECT &seat)
  {
    paintClip = seat;
    --paintClip.right;
    const int before = outputAttempts;
    FittingAccess::draw(*f.context, &f.hwnd()->dc, seat);
    LOKA_VERIFY(outputAttempts > before);
  }
  void pinLinkPressPartialPresentation()
  {
    const RECT seat = {0, 0, 100, 100};
    fittingPin("partial_same_keeps");
    {
      PressFixture f;
      f.down();
      drawPartial(f, seat);
      partialDamage(f, true);
      const POINT point = {0, 0};
      LinkHit hit = LinkHit();
      LOKA_VERIFY(FittingAccess::prepare(*f.context, point, hit) == HIT_LINK);
      LOKA_VERIFY(FittingAccess::eligible(*f.context, hit));
      f.up();
      LOKA_VERIFY(f.writes == 1);
    }
    fittingPin("partial_after_unknown_stays_unknown");
    {
      PressFixture f;
      f.down();
      SendMessageW(f.hwnd(), WM_SIZE, 0, 0);
      drawPartial(f, seat);
      partialDamage(f, false);
      f.up();
      LOKA_VERIFY(f.writes == 0);
    }
    fittingPin("partial_failed_draw_unknown");
    {
      PressFixture f;
      f.down();
      refuseOutput = outputAttempts + 1;
      drawPartial(f, seat);
      refuseOutput = 0;
      partialDamage(f, false);
      f.up();
      LOKA_VERIFY(f.writes == 0);
      f.paint();
      partialDamage(f, true);
      refuseOutput = outputAttempts + 1;
      const int before = outputAttempts;
      FittingAccess::draw(*f.context, &f.hwnd()->dc, seat);
      LOKA_VERIFY(outputAttempts > before);
      refuseOutput = 0;
      partialDamage(f, false);
      drawPartial(f, seat);
      partialDamage(f, false);
    }
    fittingPin("partial_other_value_unknown");
    {
      PressFixture f;
      const AttributedString a = f.text.get();
      f.text.set(Linked("different", Bold, LinkTarget(String("y"))));
      f.rebuild();
      // Predicate isolation: production rebuild invalidates A before drawing B.
      FittingAccess::present(*f.context, a, seat);
      LOKA_VERIFY(FittingAccess::known(*f.context));
      drawPartial(f, seat);
      partialDamage(f, false);
    }
    fittingPin("partial_other_seat_unknown");
    for (int dimension = 0; dimension < 2; ++dimension)
    {
      PressFixture f;
      RECT different = seat;
      if (dimension)
        --different.bottom;
      else
        --different.right;
      // Predicate isolation: do not let relayout/WM_SIZE invalidate the old fact.
      LOKA_VERIFY(FittingAccess::known(*f.context));
      drawPartial(f, different);
      partialDamage(f, false);
    }
  }
} // namespace
