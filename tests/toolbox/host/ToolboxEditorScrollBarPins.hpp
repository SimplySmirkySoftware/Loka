/** Host-only pins use the production ledger, completion and retirement procedures. */
#include "context/ToolboxRibbonGroupEdgeContext.hpp"
namespace
{
  void BarMetrics()
  {
    Fixture f(20);
    TERec &te = **f.te();
    ControlRef bar = f.controller.scrollControl(0).control;
    const Rect text = f.context->geometry().text;
    LOKA_VERIFY(text.right == 192 && text.bottom - text.top == 74);
    LOKA_VERIFY(bar->maximum == 15 && bar->value == 0 && bar->visible);
    const short selection = te.selStart;
    const unsigned restores = f.restores();
    const unsigned damage = toolbox_host::invalidations;
    LOKA_VERIFY(f.context->scrollInput(15) == EDITOR_OK);
    LOKA_VERIFY(toolbox_host::invalidations > damage);
    LOKA_VERIFY(text.top - te.destRect.top == 206 && bar->value == 15);
    LOKA_VERIFY(te.selStart == selection && f.restores() == restores);
    LOKA_VERIFY(f.context->scrollInput(2) == EDITOR_OK);
    LOKA_VERIFY(text.top - te.destRect.top == 28 && bar->value == 2);
    const unsigned unchangedDamage = toolbox_host::invalidations;
    LOKA_VERIFY(f.context->scrollInput(2) == EDITOR_OK);
    LOKA_VERIFY(toolbox_host::invalidations == unchangedDamage);
    OffsetRect(&te.destRect, 0, -3);
    ToolboxTextEditorAccess::sync(*f.context);
    LOKA_VERIFY(text.top - te.destRect.top == 31 && bar->value == 2);
    // A clipped native view never changes the unclipped capacity or origin.
    te.viewRect.top += 14;
    te.viewRect.bottom -= 14;
    ToolboxTextEditorAccess::sync(*f.context);
    LOKA_VERIFY(bar->maximum == 15 && bar->value == 2);
    te.nLines = 2;
    ToolboxTextEditorAccess::sync(*f.context);
    LOKA_VERIFY(te.destRect.top == text.top && bar->maximum == 0 && bar->hilite == 255);
    te.lineHeight = 75;
    ToolboxTextEditorAccess::sync(*f.context);
    LOKA_VERIFY(!bar->visible);
    te.lineHeight = 14;
    te.nLines = 3000;
    ToolboxTextEditorAccess::sync(*f.context);
    LOKA_VERIFY(!bar->visible); // Conservative N*L bound, before native narrowing.
    te.nLines = 20;
    ToolboxTextEditorAccess::sync(*f.context);
    LOKA_VERIFY(bar->visible);
    ++toolbox_host::refuseReads;
    ToolboxTextEditorAccess::sync(*f.context);
    LOKA_VERIFY(!bar->visible);
  }
  void BarGeometry()
  {
    const Rect port = {0, 0, 260, 360}, sizeBox = {245, 345, 260, 360};
    const ToolboxTextEditorRects flush = ToolboxTextEditorGeometry(port, port, true, sizeBox);
    LOKA_VERIFY(flush.frame.left == -1 && flush.frame.top == -1 && flush.frame.right == 361
                && flush.frame.bottom == 261);
    LOKA_VERIFY(flush.bar.left == 345 && flush.bar.bottom == 246);
    LOKA_VERIFY(flush.text.left == 2 && flush.text.top == 2 && flush.text.right == 343);
    const Rect inset = {20, 10, 100, 210};
    const ToolboxTextEditorRects ordinary = ToolboxTextEditorGeometry(inset, port, true, sizeBox);
    LOKA_VERIFY(EqualRect(&inset, &ordinary.frame) && ordinary.bar.left == 194);
    const Rect limit = {SHRT_MIN, SHRT_MIN, SHRT_MIN + 80, SHRT_MIN + 200};
    const ToolboxTextEditorRects refused = ToolboxTextEditorGeometry(limit, limit, true, Rect());
    LOKA_VERIFY(EmptyRect(&refused.text) && EmptyRect(&refused.bar));
    const Rect coversBar = {0, 194, 120, 210};
    const ToolboxTextEditorRects trimmed = ToolboxTextEditorGeometry(inset, port, true, coversBar);
    LOKA_VERIFY(EmptyRect(&trimmed.bar));
  }
  void BarTerminalLines()
  {
    Fixture empty(1, "");
    TERec &te = **empty.te();
    te.lineHeight = 74;
    te.nLines = 0;
    ToolboxTextEditorAccess::sync(*empty.context);
    LOKA_VERIFY(empty.controller.scrollControl(0).control->maximum == 0);
    Fixture trailing(2, "");
    TERec &end = **trailing.te();
    end.lineHeight = 74;
    end.nLines = 1;
    end.lineStarts[0] = 0;
    ToolboxTextEditorAccess::sync(*trailing.context);
    LOKA_VERIFY(trailing.controller.scrollControl(0).control->maximum == 1);
    end.nLines = 2;
    end.lineStarts[1] = end.teLength;
    ToolboxTextEditorAccess::sync(*trailing.context);
    LOKA_VERIFY(trailing.controller.scrollControl(0).control->maximum == 1);
  }
  void BarRefusal()
  {
    Fixture f(20);
    TERec &te = **f.te();
    LayoutState state;
    state.x = 10;
    state.y = -32760;
    state.width = 200;
    state.height = 80;
    f.context->layout(&f.controller, state);
    const Rect before = te.destRect;
    const int sets = toolbox_host::sets;
    const unsigned restores = f.restores();
    LOKA_VERIFY(f.context->scrollInput(2) == EDITOR_OK);
    LOKA_VERIFY(EqualRect(&before, &te.destRect));
    LOKA_VERIFY(toolbox_host::sets == sets && f.restores() == restores);
    LOKA_VERIFY(f.controller.scrollControl(0).control->value == 0);
    ToolboxTextEditorAccess::busy(*f.context, true);
    const std::size_t calls = f.controller.scrollControl(0).control->calls.size();
    ToolboxTextEditorAccess::sync(*f.context);
    LOKA_VERIFY(calls == f.controller.scrollControl(0).control->calls.size());
    LOKA_VERIFY(f.context->scrollInput(2) == EDITOR_REENTRANT);
    ToolboxTextEditorAccess::busy(*f.context, false);
  }
  void BarPlacement()
  {
    Fixture f(20);
    ControlRef bar = f.controller.scrollControl(0).control;
    f.controller.scrollControl(0).usedThisFrame = false;
    HideControl(bar);
    ToolboxTextEditorAccess::sync(*f.context);
    LOKA_VERIFY(!bar->visible);
    f.context->onPropsApplied();
    LOKA_VERIFY(!bar->visible);
    const std::size_t rows = f.controller.scrollControls().size();
    LOKA_VERIFY(!f.controller.presentEditorScrollBar(30000, ToolboxEditorScrollBarPresentation(5, 2, 1)));
    LOKA_VERIFY(f.controller.scrollControls().size() == rows);
    f.context->render(&f.controller);
    LOKA_VERIFY(bar->visible);
    f.controller.scrollControl(0).usedThisFrame = false;
    SetRect(&f.controller.projectionClip, 10, 20, 100, 100);
    HostPortScope port(f.window.window());
    const Rect clip = f.window.port.native.clip;
    f.context->render(&f.controller);
    LOKA_VERIFY(!bar->visible && EqualRect(&clip, &f.window.port.native.clip));
    const unsigned invalidations = toolbox_host::invalidations;
    for (unsigned i = 0; i < 5; ++i)
    {
      ToolboxTextEditorAccess::sync(*f.context);
      f.context->render(&f.controller);
    }
    LOKA_VERIFY(!bar->visible && toolbox_host::invalidations == invalidations);
  }
  void BarPortIdle()
  {
    Fixture f(20);
    ToolboxWindow other;
    HostPortScope port(other.window());
    f.window.scenePlatformController_ = &f.controller;
    f.controller.activateEditControl(0);
    ControlRef bar = f.controller.scrollControl(0).control;
    TERec &te = **f.te();
    HostTECompletion completion = {te.destRect, te.selStart, te.selEnd};
    OffsetRect(&completion.destRect, 0, -28);
    te.idleCompletions.push_back(completion);
    bar->calls.clear();
    const unsigned invalidations = toolbox_host::invalidations;
    f.window.idleControls(ACTIVATION_FOREGROUND);
    LOKA_VERIFY(bar->value == 2 && toolbox_host::invalidations > invalidations);
    LOKA_VERIFY(!bar->calls.empty());
    for (std::size_t i = 0; i < bar->calls.size(); ++i)
      LOKA_VERIFY(bar->calls[i].currentPort == f.window.window() && !bar->calls[i].painted);
    GrafPtr current;
    GetPort(&current);
    LOKA_VERIFY(current == other.window());
    bar->calls.clear();
    const unsigned directDamage = toolbox_host::invalidations;
    LOKA_VERIFY(f.controller.presentEditorScrollBar(ToolboxTextEditorAccess::barId(*f.context),
                                                    ToolboxEditorScrollBarPresentation(15, 4, 4)));
    LOKA_VERIFY(toolbox_host::invalidations > directDamage);
    for (std::size_t i = 0; i < bar->calls.size(); ++i)
      LOKA_VERIFY(bar->calls[i].currentPort == f.window.window() && !bar->calls[i].painted);
    ToolboxTextEditorAccess::sync(*f.context);
    bar->calls.clear();
    f.window.idleControls(ACTIVATION_FOREGROUND);
    LOKA_VERIFY(bar->calls.empty());
    completion.destRect = te.destRect;
    OffsetRect(&completion.destRect, 0, -14);
    te.keyCompletions.push_back(completion);
    LOKA_VERIFY(f.context->key(28) == EDITOR_OK);
    LOKA_VERIFY(bar->value == 3);
    for (std::size_t i = 0; i < bar->calls.size(); ++i)
      LOKA_VERIFY(bar->calls[i].currentPort == f.window.window() && !bar->calls[i].painted);
  }
  void BarGesture()
  {
    Fixture f(20);
    ControlRef bar = f.controller.scrollControl(0).control;
    TERec &te = **f.te();
    const short start = te.selStart, end = te.selEnd;
    const unsigned restores = f.restores();
    const Point point = {50, 200};
    bar->tracking = toolbox_host::TrackScript(23, 0, 0);
    bar->tracking.callbacks.assign(2, 23);
    f.controller.publishFixturePressOrder();
    LOKA_VERIFY(!ToolboxInputDoor::mouseDown(f.controller, point));
    LOKA_VERIFY(bar->value == 8 && f.context->geometry().text.top - te.destRect.top == 112);
    LOKA_VERIFY(te.selStart == start && te.selEnd == end && f.restores() == restores);
    LOKA_VERIFY(!f.controller.editControls_.focused());
    OffsetRect(&te.destRect, 0, -3);
    const Rect before = te.destRect;
    bar->tracking = toolbox_host::TrackScript(129, 0, 12);
    f.controller.publishFixturePressOrder();
    LOKA_VERIFY(!ToolboxInputDoor::mouseDown(f.controller, point));
    LOKA_VERIFY(EqualRect(&before, &te.destRect));
    bar->tracking = toolbox_host::TrackScript(129, 129, bar->value);
    f.controller.publishFixturePressOrder();
    LOKA_VERIFY(!ToolboxInputDoor::mouseDown(f.controller, point));
    LOKA_VERIFY(EqualRect(&before, &te.destRect));
  }
  void BarInactiveHit()
  {
    Fixture f;
    ControlRef bar = f.controller.scrollControl(0).control;
    const Point point = {50, 200};
    const short selection = (**f.te()).selStart;
    for (unsigned i = 0; i < 2; ++i)
    {
      if (i)
        HideControl(bar);
      f.controller.publishFixturePressOrder();
      LOKA_VERIFY(!ToolboxInputDoor::mouseDown(f.controller, point));
      f.controller.publishFixturePressOrder();
      LOKA_VERIFY(!ToolboxInputDoor::mouseDown(f.controller, point));
      LOKA_VERIFY(!f.controller.editControls_.focused());
      LOKA_VERIFY(!(**f.te()).active && (**f.te()).selStart == selection);
      LOKA_VERIFY(!PtInRect(point, &f.controller.editControls_[0].placement.visible()));
    }
  }
  void BarClippedHit()
  {
    Fixture f;
    // Render under a viewport clip, then pop it before native input arrives.
    SetRect(&f.controller.projectionClip, 10, 20, 210, 60);
    f.context->render(&f.controller);
    SetRect(&f.controller.projectionClip, -30000, -30000, 30000, 30000);
    ToolboxScrollBarLedger::ScrollBarControlBinding &row = f.controller.scrollControl(0);
    LOKA_VERIFY(row.usedThisFrame && row.editor.editor() == f.context);
    Rect visible;
    LOKA_VERIFY(row.placement.queryVisible(visible));
    LOKA_VERIFY(visible.bottom == 60 && row.control->hilite == 255);
    const Point outside = {75, 200}, inside = {50, 200};
    const Rect column = f.context->geometry().bar;
    LOKA_VERIFY(EqualRect(&row.control->rect, &column));
    LOKA_VERIFY(EqualRect(&row.placement.full(), &column));
    LOKA_VERIFY(PtInRect(outside, &column) && !PtInRect(outside, &visible));
    LOKA_VERIFY(PtInRect(inside, &visible));
    TERec &te = **f.te();
    const short selection = te.selStart;
    HostTECompletion click = {te.destRect, 0, 0};
    te.clickCompletions.push_back(click);
    f.controller.publishFixturePressOrder();
    LOKA_VERIFY(!ToolboxInputDoor::mouseDown(f.controller, outside));
    f.controller.publishFixturePressOrder();
    LOKA_VERIFY(!ToolboxInputDoor::mouseDown(f.controller, inside));
    f.controller.publishFixturePressOrder();
    LOKA_VERIFY(!ToolboxInputDoor::mouseDown(f.controller, inside));
    LOKA_VERIFY(te.clickCompletions.size() == 1 && te.selStart == selection);
    LOKA_VERIFY(!f.controller.editControls_.focused() && !te.active);
    LOKA_VERIFY(!PtInRect(outside, &f.controller.editControls_[0].placement.visible()));
    LOKA_VERIFY(!PtInRect(inside, &f.controller.editControls_[0].placement.visible()));
    row.usedThisFrame = false;
    f.controller.publishFixturePressOrder();
    LOKA_VERIFY(!ToolboxInputDoor::mouseDown(f.controller, inside));
    row.usedThisFrame = true;
    row.editor.revoke();
    f.controller.publishFixturePressOrder();
    LOKA_VERIFY(!ToolboxInputDoor::mouseDown(f.controller, inside));
  }
  ToolboxScenePlatformController *retiringController = 0;
  void VerifyRevokedBeforeHide(ControlRef control)
  {
    const std::vector<ToolboxScrollBarLedger::ScrollBarControlBinding> &rows = retiringController->scrollControls();
    for (std::size_t i = 0; i < rows.size(); ++i)
      if (rows[i].control == control)
        LOKA_VERIFY(!rows[i].editor.editor());
  }
  void BarLifetime(const char *which)
  {
    Fixture f(20);
    const short id = ToolboxTextEditorAccess::barId(*f.context);
    ControlRef original = f.controller.scrollControl(0).control;
    if (std::strcmp(which, "bar-bulk") == 0)
    {
      retiringController = &f.controller;
      toolbox_host::beforeHide = &VerifyRevokedBeforeHide;
      f.controller.clearControls();
      toolbox_host::beforeHide = 0;
      retiringController = 0;
      LOKA_VERIFY(f.controller.scrollControls().empty() && !original->visible);
      f.controller.flushControls();
      LOKA_VERIFY(f.controller.allocateControlId() != id);
      f.context->render(&f.controller);
      LOKA_VERIFY(ToolboxTextEditorAccess::barId(*f.context) == id);
      LOKA_VERIFY(f.controller.scrollControl(0).editor.editor() == f.context);
      return;
    }
    if (std::strcmp(which, "bar-retained") == 0)
      f.context->onFactChanged(NODE_FACT_ATTACHED, NODE_FACT_DETACHED_RETAINED);
    else
      f.controller.retireTextEditorControl(f.context, NATIVE_HINT_DEFAULT);
    LOKA_VERIFY(!f.controller.scrollControl(0).editor.editor() && !original->visible);
    LOKA_VERIFY(ToolboxTextEditorAccess::barId(*f.context) == id);
    LOKA_VERIFY(f.controller.allocateControlId() != id);
    f.context->invalidateNativePresentation();
    LOKA_VERIFY(f.commands.post(EditorCommand(EditorCommand::PAGE_DOWN)) == POST_ACCEPTED);
    ShowControl(original);
    original->tracking = toolbox_host::TrackScript(129, 129, 5);
    const Point stalePoint = {50, 200};
    f.controller.publishFixturePressOrder();
    LOKA_VERIFY(!ToolboxInputDoor::mouseDown(f.controller, stalePoint));
    LOKA_VERIFY(!f.commands.state()->get().isNone());
    HideControl(original);
    LOKA_VERIFY(!f.controller.presentEditorScrollBar(id, ToolboxEditorScrollBarPresentation(5, 2, 1)));
    f.context->onFactChanged(NODE_FACT_DETACHED_RETAINED, NODE_FACT_ATTACHED);
    f.context->render(&f.controller);
    LOKA_VERIFY(f.controller.scrollControl(0).control == original);
    LOKA_VERIFY(f.controller.scrollControl(0).editor.editor() == f.context);
    f.context->onFactChanged(NODE_FACT_ATTACHED, NODE_FACT_DETACHED_RETAINED);
    f.controller.clearControls(); // Reserved ID with neither native row materialized.
    f.context->onFactChanged(NODE_FACT_DETACHED_RETAINED, NODE_FACT_RETIRED);
    f.context->onFactChanged(NODE_FACT_RETIRED, NODE_FACT_RETIRED);
    LOKA_VERIFY(f.controller.allocateControlId() == id);
    LOKA_VERIFY(f.controller.allocateControlId() != id);
  }
  void BarReplay(bool composition)
  {
    Fixture f(20);
    HostPortScope port(f.window.window());
    f.controller.rootNode_ = &f.node;
    f.controller.renderContext = f.context;
    RibbonGroupEdgeNode edgeNode((RibbonGroupEdgeProps()));
    ToolboxRibbonGroupEdgeContext edge(&edgeNode, &f.controller);
    if (composition)
      edge.readLifecycleFactOnAttach();
    f.window.scenePlatformController_ = &f.controller;
    f.controller.activateEditControl(0);
    ControlRef bar = f.controller.scrollControl(0).control;
    HostTECompletion completion = {(**f.te()).destRect, (**f.te()).selStart, (**f.te()).selEnd};
    OffsetRect(&completion.destRect, 0, -28);
    (**f.te()).idleCompletions.push_back(completion);
    f.window.idleControls(ACTIVATION_FOREGROUND);
    LOKA_VERIFY(bar->value == 2);
    const unsigned renders = f.controller.debugStats_.totalRenderCalls;
    const int updates = toolbox_host::updates;
    bar->calls.clear();
    toolbox_host::paintOperations.clear();
    ToolboxInputDoor::renderDirty(f.controller, f.context->chromeRect());
    LOKA_VERIFY(toolbox_host::updates > updates && bar->calls.back().operation == "draw");
    // Composition replay erases the dirty rect first; the editor adds no
    // ground of its own, because the text fills its view (#1429).
    const std::size_t replayErase = composition ? 1 : 0;
    LOKA_VERIFY(toolbox_host::paintOperations.size() == 3 + replayErase);
    LOKA_VERIFY(!composition || toolbox_host::paintOperations[0] == "erase");
    LOKA_VERIFY(toolbox_host::paintOperations[replayErase] == "frame");
    LOKA_VERIFY(toolbox_host::paintOperations[replayErase + 1] == "text");
    LOKA_VERIFY(toolbox_host::paintOperations[replayErase + 2] == "bar");
    LOKA_VERIFY(f.controller.debugStats_.totalRenderCalls == renders + (composition ? 1 : 0));
    toolbox_host::failSets = 1;
    LOKA_VERIFY(f.context->paste("", 8192 + 1) == EDITOR_CAPACITY);
    f.context->render(&f.controller);
    LOKA_VERIFY(!bar->visible && f.controller.scrollControl(0).usedThisFrame);
    f.window.idleControls(ACTIVATION_FOREGROUND);
    LOKA_VERIFY(bar->visible);
    const unsigned recoveredRenders = f.controller.debugStats_.totalRenderCalls;
    ToolboxInputDoor::renderDirty(f.controller, f.context->chromeRect());
    LOKA_VERIFY(f.controller.debugStats_.totalRenderCalls == recoveredRenders + (composition ? 1 : 0));
    const unsigned stableDamage = toolbox_host::invalidations;
    for (unsigned i = 0; i < 5; ++i)
      ToolboxInputDoor::renderDirty(f.controller, f.context->chromeRect());
    LOKA_VERIFY(toolbox_host::invalidations == stableDamage);
    f.controller.rootNode_ = 0;
    f.controller.renderContext = 0;
  }
  void BarCreationRefusal()
  {
    toolbox_host::refuseControls = 1;
    Fixture f(20);
    const short id = ToolboxTextEditorAccess::barId(*f.context);
    LOKA_VERIFY(id && f.controller.scrollControls().empty());
    ToolboxTextEditorAccess::sync(*f.context);
    LOKA_VERIFY(f.controller.scrollControls().empty());
    f.context->onFactChanged(NODE_FACT_ATTACHED, NODE_FACT_RETIRED);
    LOKA_VERIFY(f.controller.allocateControlId() == id);
    LOKA_VERIFY(f.controller.allocateControlId() != id);
    toolbox_host::failNew = 1;
    Fixture noTE;
    LOKA_VERIFY(!noTE.te() && noTE.controller.scrollControls().size() == 1);
    LOKA_VERIFY(!noTE.controller.scrollControl(0).editor.editor()
                && !noTE.controller.scrollControl(0).control->visible);
    noTE.context->render(&noTE.controller);
    LOKA_VERIFY(noTE.te() && noTE.controller.scrollControl(0).editor.editor() == noTE.context);
    const short appId = noTE.controller.allocateControlId();
    const Rect appRect = {0, 0, 80, 16};
    LOKA_VERIFY(noTE.controller.ensureScrollBarControl(appId, appRect, ScrollBarProps(), 0, NATIVE_HINT_DEFAULT));
    LOKA_VERIFY(!noTE.controller.scrollControl(1).editor.editor());
    noTE.controller.destroyScrollBarControl(appId, NATIVE_HINT_DEFAULT);
  }
  bool RunBarPin(const char *which)
  {
    if (std::strcmp(which, "bar-creation") == 0)
      BarCreationRefusal();
    else if (std::strcmp(which, "bar-geometry") == 0)
      BarGeometry();
    else if (std::strcmp(which, "bar-replay-retained") == 0)
      BarReplay(false);
    else if (std::strcmp(which, "bar-replay-composition") == 0)
      BarReplay(true);
    else if (std::strcmp(which, "bar-metrics") == 0)
      BarMetrics();
    else if (std::strcmp(which, "bar-terminal") == 0)
      BarTerminalLines();
    else if (std::strcmp(which, "bar-refusal") == 0)
      BarRefusal();
    else if (std::strcmp(which, "bar-placement") == 0)
      BarPlacement();
    else if (std::strcmp(which, "bar-port-idle") == 0)
      BarPortIdle();
    else if (std::strcmp(which, "bar-gesture") == 0)
      BarGesture();
    else if (std::strcmp(which, "bar-clipped-hit") == 0)
      BarClippedHit();
    else if (std::strcmp(which, "bar-inactive") == 0)
      BarInactiveHit();
    else if (std::strcmp(which, "bar-bulk") == 0 || std::strcmp(which, "bar-retained") == 0
             || std::strcmp(which, "bar-unused") == 0)
      BarLifetime(which);
    else
      return false;
    return true;
  }
} // namespace
