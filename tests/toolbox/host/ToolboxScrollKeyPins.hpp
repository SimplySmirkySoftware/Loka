#include "app/nodes/controls/TextEditorRequests.hpp"
/** View-key pins share the bar fixture and exercise production input bodies. */
namespace
{
  void ScrollKeyCodes()
  {
    for (int initial = SCROLL_PAGE_UP; initial <= SCROLL_END; ++initial)
    {
      unsigned accepted = 0;
      for (unsigned code = 0; code < 256; ++code)
      {
        ToolboxScrollKey key = static_cast<ToolboxScrollKey>(initial);
        const bool found = QueryToolboxScrollKey(static_cast<unsigned char>(code), key);
        LOKA_VERIFY(found == (code == 0x74 || code == 0x79 || code == 0x73 || code == 0x77));
        if (!found)
          LOKA_VERIFY(key == initial);
        else
        {
          ++accepted;
          LOKA_VERIFY(key
                      == (code == 0x74   ? SCROLL_PAGE_UP
                          : code == 0x79 ? SCROLL_PAGE_DOWN
                          : code == 0x73 ? SCROLL_HOME
                                         : SCROLL_END));
        }
      }
      LOKA_VERIFY(accepted == 4);
    }
  }

  void ScrollKeyView()
  {
    Fixture f(20);
    f.controller.activateEditControl(0);
    TERec &te = **f.te();
    ControlRef bar = f.controller.scrollControl(0).control;
    for (unsigned selection = 0; selection < 2; ++selection)
    {
      te.selEnd = te.selStart + static_cast<short>(selection * 2);
      const short start = te.selStart, end = te.selEnd;
      Snapshot before(f);
      for (int origin = 0; origin <= 15; ++origin)
        for (int direction = 0; direction < 2; ++direction)
        {
          LOKA_VERIFY(f.context->scrollInput(origin) == EDITOR_OK);
          const Rect initial = te.destRect;
          LOKA_VERIFY(f.context->scrollInput(origin + (direction ? 4 : -4)) == EDITOR_OK);
          const Rect expected = te.destRect;
          const short expectedValue = bar->value;
          te.destRect = initial;
          ToolboxTextEditorAccess::sync(*f.context);
          LOKA_VERIFY(ToolboxInputDoor::scrollKey(f.controller, direction ? SCROLL_PAGE_DOWN : SCROLL_PAGE_UP));
          LOKA_VERIFY(EqualRect(&expected, &te.destRect) && bar->value == expectedValue);
          LOKA_VERIFY(te.selStart == start && te.selEnd == end);
          before.unchanged(f);
        }
      LOKA_VERIFY(ToolboxInputDoor::scrollKey(f.controller, SCROLL_HOME));
      LOKA_VERIFY(bar->value == 0 && te.destRect.top == f.context->geometry().text.top);
      LOKA_VERIFY(ToolboxInputDoor::scrollKey(f.controller, SCROLL_END));
      LOKA_VERIFY(bar->value == 15 && f.context->geometry().text.top - te.destRect.top == 206);
      for (int repeat = 0; repeat < 3; ++repeat)
        LOKA_VERIFY(ToolboxInputDoor::scrollKey(f.controller, SCROLL_PAGE_DOWN));
      LOKA_VERIFY(bar->value == 15 && f.context->geometry().text.top - te.destRect.top == 206);
      LOKA_VERIFY(te.selStart == start && te.selEnd == end);
      before.unchanged(f);
    }
  }

  Fixture *scrollReentry = 0;
  void ReenterScroll(TEHandle)
  {
    toolbox_host::afterKey = 0;
    LOKA_VERIFY(scrollReentry->controller.borrowPhase().open());
    LOKA_VERIFY(scrollReentry->context->scrollKey(SCROLL_END) == EDITOR_REENTRANT);
    LOKA_VERIFY(ToolboxTextEditorAccess::reconciling(*scrollReentry->context));
  }

  void ScrollKeyAdmission()
  {
    Fixture f(20);
    const Rect before = (**f.te()).destRect;
    LOKA_VERIFY(!ToolboxInputDoor::scrollKey(f.controller, SCROLL_END));
    f.controller.activateEditControl(0);
    f.controller.editControls_[0].editor = 0;
    LOKA_VERIFY(!ToolboxInputDoor::scrollKey(f.controller, SCROLL_END));
    f.controller.editControls_[0].editor = f.context;
    TEHandle native = f.controller.editControls_[0].te;
    f.controller.editControls_[0].te = 0;
    LOKA_VERIFY(!ToolboxInputDoor::scrollKey(f.controller, SCROLL_END));
    f.controller.editControls_[0].te = native;
    LOKA_VERIFY(EqualRect(&before, &(**f.te()).destRect));
    ToolboxTextEditorAccess::projecting(*f.context, true);
    LOKA_VERIFY(f.context->scrollKey(SCROLL_END) == EDITOR_REENTRANT);
    LOKA_VERIFY(ToolboxTextEditorAccess::isProjecting(*f.context));
    LOKA_VERIFY(ToolboxInputDoor::scrollKey(f.controller, SCROLL_END));
    ToolboxTextEditorAccess::projecting(*f.context, false);
    scrollReentry = &f;
    toolbox_host::afterKey = &ReenterScroll;
    const unsigned restores = f.restores();
    LOKA_VERIFY(ToolboxInputDoor::keyDown(f.controller, 'x'));
    LOKA_VERIFY(f.restores() == restores + 1 && !f.controller.borrowPhase().open());
    scrollReentry = 0;
    ToolboxTextEditorAccess::unavailable(*f.context);
    LOKA_VERIFY(f.context->scrollKey(SCROLL_END) == EDITOR_UNAVAILABLE);
    LOKA_VERIFY(ToolboxInputDoor::scrollKey(f.controller, SCROLL_END));
  }

  void ScrollKeyRepair()
  {
    Fixture f(20);
    f.controller.activateEditControl(0);
    LOKA_VERIFY(f.lines.update(f.lines.at(0).id, String("owner")) == EDIT_OK);
    const int sets = toolbox_host::sets;
    LOKA_VERIFY(ToolboxInputDoor::scrollKey(f.controller, SCROLL_END));
    LOKA_VERIFY(toolbox_host::sets > sets && f.native().substr(0, 5) == "owner");
    (**f.te()).selStart = 0;
    (**f.te()).selEnd = 0;
    LOKA_VERIFY(ToolboxInputDoor::scrollKey(f.controller, SCROLL_HOME));
    LOKA_VERIFY((**f.te()).selStart == 2);
    // A pending request settles inside the borrow, once, at input completion.
    DoorSettlementObserver observer(f);
    f.cursor.state()->bind(&DoorSettlementObserver::report, &observer, false);
    {
      StateTrackerGuard guard(&f.tracker);
      f.request.set(LineCursor(f.lines.at(1).id, 1));
    }
    LOKA_VERIFY(ToolboxInputDoor::scrollKey(f.controller, SCROLL_END));
    f.cursor.state()->unbind(&DoorSettlementObserver::report, &observer);
    LOKA_VERIFY(observer.reports == 1 && !f.controller.borrowPhase().open());
  }

  void ScrollKeyNoop()
  {
    for (unsigned kind = 0; kind < 3; ++kind)
    {
      Fixture f(kind == 0 ? 0 : kind == 1 ? 1 : 2, "");
      f.controller.activateEditControl(0);
      const Rect before = (**f.te()).destRect;
      for (int key = SCROLL_PAGE_UP; key <= SCROLL_END; ++key)
      {
        LOKA_VERIFY(f.context->scrollKey(static_cast<ToolboxScrollKey>(key)) == EDITOR_OK);
        LOKA_VERIFY(EqualRect(&before, &(**f.te()).destRect));
      }
      loka::app::testing::SettleTrace<EditorCommand, LineCursor> &trace =
          loka::app::testing::SettleTrace<EditorCommand, LineCursor>::instance();
      trace.clear();
      for (unsigned pending = 0; pending < 4; ++pending)
        LOKA_VERIFY(f.commands.post(EditorCommand(EditorCommand::PAGE_DOWN)) == POST_ACCEPTED);
      LOKA_VERIFY(ToolboxInputDoor::scrollKey(f.controller, SCROLL_END));
      LOKA_VERIFY(trace.size() == 1 && trace.at(0).stimulus == SETTLE_INPUT && trace.at(0).count == 2);
      LOKA_VERIFY(!f.commands.state()->get().isNone());
    }
    Fixture f(20);
    const Rect before = (**f.te()).destRect;
    ++toolbox_host::refuseReads;
    LOKA_VERIFY(f.context->scrollKey(SCROLL_END) == EDITOR_OK);
    LOKA_VERIFY(EqualRect(&before, &(**f.te()).destRect));
    LayoutState state;
    state.x = 10;
    state.y = -32760;
    state.width = 200;
    state.height = 80;
    f.context->layout(&f.controller, state);
    const Rect unrepresentable = (**f.te()).destRect;
    LOKA_VERIFY(f.context->scrollKey(SCROLL_END) == EDITOR_OK);
    LOKA_VERIFY(EqualRect(&unrepresentable, &(**f.te()).destRect));
  }

  bool RunScrollKeyPin(const char *name)
  {
    if (std::strcmp(name, "scroll-codes") == 0)
      ScrollKeyCodes();
    else if (std::strcmp(name, "scroll-view") == 0)
      ScrollKeyView();
    else if (std::strcmp(name, "scroll-admission") == 0)
      ScrollKeyAdmission();
    else if (std::strcmp(name, "scroll-repair") == 0)
      ScrollKeyRepair();
    else if (std::strcmp(name, "scroll-noop") == 0)
      ScrollKeyNoop();
    else
      return false;
    return true;
  }
} // namespace
