/** Characterization of the pre-extraction custom rows. Keep unchanged across extraction. */
#include "ToolboxHost.hpp"
#include "context/ToolboxTextContext.hpp"
#include "ToolboxInputDoor.hpp"
#include "ToolboxBuiltInSupport.hpp"
#include <cstring>
using namespace loka::app;
using namespace loka::app::scene;
using namespace loka::core;
namespace
{
  const Rect area = {0, 0, 100, 100};
  const Point point = {20, 20};
  struct Fixture
  {
    ToolboxWindow window;
    HostPortScope port;
    ToolboxScenePlatformController controller;
    MutableState<String> text;
    MutableState<bool> enabled;
    MutableState<int> selection;
    EmitterState click;
    loka::Vector<String> items;
    ButtonNode button;
    CellNode cell;
    PopupMenuNode popup;
    EditTextNode edit;
    ToolboxButtonContext buttonContext;
    ToolboxCellContext cellContext;
    ToolboxPopupMenuContext popupContext;
    ToolboxEditTextContext editContext;
    Fixture()
        : port(window.window()),
          controller(&window),
          text(String("row")),
          enabled(true),
          selection(0),
          button(ButtonProps()),
          cell(CellProps().text(&text).onClick(&click)),
          popup(PopupMenuProps()),
          edit(EditTextProps()),
          buttonContext(&button, &controller),
          cellContext(&cell, &controller),
          popupContext(&popup, &controller),
          editContext(&edit, &controller)
    {
      this->buttonContext.setOwner(&this->button);
      this->cellContext.setOwner(&this->cell);
      this->popupContext.setOwner(&this->popup);
      this->editContext.setOwner(&this->edit);
      ClipRect(&area);
      this->controller.projectionClip = area;
      LOKA_VERIFY(this->controller.projectionParentScopes_.resetRoot(this->window.window(), Frame(0, 0, 100, 100)));
      this->items.push_back(String("item"));
      this->buttonContext.updateRect(area);
      this->cellContext.updateRect(area);
      this->cellContext.updateData(&this->text);
      this->popupContext.updateRect(area, 12);
      this->popupContext.updateData(&this->items, &this->selection, WriteSeat<int>(), 0, &this->enabled);
      this->editContext.updateRect(area, area, 0, 12);
    }
    NodeContext *context(int kind)
    {
      switch (kind)
      {
      case 0:
        return &this->buttonContext;
      case 1:
        return &this->cellContext;
      case 2:
        return &this->popupContext;
      }
      return &this->editContext;
    }
    void record(int kind, Fixture &owner, const Rect &rect = area)
    {
      switch (kind)
      {
      case 0:
        this->controller.recordButtonHit(rect, &owner.click, &owner.enabled, 0, &owner.buttonContext);
        break;
      case 1:
        this->controller.recordCellHit(rect, &owner.click, 0, &owner.cellContext, &owner.text);
        break;
      case 2:
        this->controller.recordPopupHit(rect, &owner.enabled, &owner.popupContext);
        this->controller.reachPopupHit(&owner.popupContext);
        break;
      case 3:
        this->controller.recordEditHit(rect, &owner.text, 0, &owner.editContext);
        break;
      }
    }
    void expect(NodeContext *context, PressRow::Kind kind, std::size_t index)
    {
      PressRow row;
      LOKA_VERIFY(this->controller.selectedPress(point, row) == context);
      if (context)
      {
        LOKA_VERIFY(row.kind == kind);
        LOKA_VERIFY(row.index == index);
      }
    }
  };
  const PressRow::Kind kinds[] = {PressRow::BUTTON_HIT, PressRow::CELL_HIT, PressRow::POPUP_HIT, PressRow::EDIT_HIT};
  void recordPins()
  {
    for (int kind = 0; kind != 4; ++kind)
    {
      Fixture f, other;
      f.controller.pressLedger_.begin();
      f.record(kind, other);
      f.record(kind, f);
      f.controller.pressLedger_.commit();
      // Popup reaches the first matching context; two different contexts distinguish indices.
      f.expect(f.context(kind), kinds[kind], 1);
      LOKA_VERIFY(kind == 0 || kind == 2 ? f.controller.boundEnabled.size() == 2 : f.controller.boundText.size() == 2);
    }
  }
  void retirePins()
  {
    for (int kind = 0; kind != 4; ++kind)
      for (int pattern = 0; pattern != 3; ++pattern)
      {
        Fixture f, removed, survivor;
        f.controller.pressLedger_.begin();
        if (pattern != 0)
          f.record(kind, survivor);
        f.record(kind, removed);
        if (pattern == 2)
          f.record(kind, removed);
        f.record(kind, f);
        f.controller.pressLedger_.commit();
        // Populated staging references the same payload rows as LIVE.
        f.controller.publishFixturePressOrder();
        f.controller.pressLedger_.begin();
        for (std::size_t i = 0; i < static_cast<std::size_t>(pattern == 0 ? 2 : pattern == 1 ? 3 : 4); ++i)
          f.controller.pressLedger_.append(kinds[kind], i);
        f.controller.retireNodeContext(removed.context(kind), NATIVE_HINT_DEFAULT);
        f.expect(f.context(kind), kinds[kind], pattern == 0 ? 0 : 1);
        f.controller.pressLedger_.commit();
        f.expect(f.context(kind), kinds[kind], pattern == 0 ? 0 : 1);
        f.controller.retireNodeContext(f.context(kind), NATIVE_HINT_DEFAULT);
        f.expect(pattern == 0 ? 0 : survivor.context(kind), kinds[kind], 0);
      }
  }
  void releasePins()
  {
    for (int kind = 0; kind != 4; ++kind)
    {
      Fixture f;
      f.record(kind, f);
      f.record(kind, f);
      f.controller.retireNodeContext(f.context(kind), NATIVE_HINT_DEFAULT);
      if (kind == 0 || kind == 2)
      {
        LOKA_VERIFY(f.controller.unboundEnabled.size() == 2);
        LOKA_VERIFY(f.controller.unboundEnabled[0] == &f.enabled);
      }
      else
      {
        LOKA_VERIFY(f.controller.unboundText.size() == 2);
        LOKA_VERIFY(f.controller.unboundText[0] == &f.text);
      }
    }
    Fixture f;
    f.record(0, f);
    f.record(2, f);
    f.controller.retireNodeContext(&f.buttonContext, NATIVE_HINT_DEFAULT);
    LOKA_VERIFY(f.controller.unboundEnabled.empty());
    f.controller.retireNodeContext(&f.popupContext, NATIVE_HINT_DEFAULT);
    LOKA_VERIFY(f.controller.unboundEnabled.size() == 1);
    f.record(1, f);
    f.record(3, f);
    f.controller.retireNodeContext(&f.editContext, NATIVE_HINT_DEFAULT);
    LOKA_VERIFY(f.controller.unboundText.empty());
    f.controller.retireNodeContext(&f.cellContext, NATIVE_HINT_DEFAULT);
    LOKA_VERIFY(f.controller.unboundText.size() == 1);
    f.controller.recordButtonHit(area, 0, 0, 0, &f.buttonContext);
    f.controller.recordEditHit(area, 0, 0, &f.editContext);
    f.controller.retireNodeContext(&f.buttonContext, NATIVE_HINT_DEFAULT);
    f.controller.retireNodeContext(&f.editContext, NATIVE_HINT_DEFAULT);
    LOKA_VERIFY(f.controller.unboundEnabled.size() == 1);
    LOKA_VERIFY(f.controller.unboundText.size() == 1);
    // TextHit's live source, rather than a stored candidate, keeps a shared subscription alive.
    TextNode label(TextProps().text(&f.text));
    ToolboxTextContext labelContext(&label, &f.controller);
    labelContext.updateData(&f.text);
    f.controller.recordTextHit(area, 0, 12, &f.text, 0, false, 100, &labelContext);
    f.record(3, f);
    f.controller.retireNodeContext(&f.editContext, NATIVE_HINT_DEFAULT);
    LOKA_VERIFY(f.controller.unboundText.size() == 1);
    f.controller.retireNodeContext(&labelContext, NATIVE_HINT_DEFAULT);
    LOKA_VERIFY(f.controller.unboundText.size() == 2);
    // Native users are considered after every custom kind has removed its rows.
    ToolboxScenePlatformController::ButtonControlBinding native =
        ToolboxScenePlatformController::ButtonControlBinding();
    native.enabled = &f.enabled;
    f.controller.buttonControls_.push_back(native);
    f.record(0, f);
    f.controller.retireNodeContext(&f.buttonContext, NATIVE_HINT_DEFAULT);
    LOKA_VERIFY(f.controller.unboundEnabled.size() == 1);
    f.controller.buttonControls_.clear();
    Fixture other;
    f.controller.ensureEditTextControl(&other.editContext, area, &f.text, NATIVE_HINT_DEFAULT);
    LOKA_VERIFY(f.controller.editControls_.size() == 1);
    f.record(3, f);
    f.controller.retireNodeContext(&f.editContext, NATIVE_HINT_DEFAULT);
    LOKA_VERIFY(f.controller.unboundText.size() == 2);
    f.controller.retireNodeContext(&other.editContext, NATIVE_HINT_DEFAULT);
  }
  void popupPins()
  {
    Fixture f;
    f.controller.pressLedger_.begin();
    f.controller.recordPopupHit(area, &f.enabled, &f.popupContext);
    f.controller.pressLedger_.commit();
    f.expect(0, PressRow::POPUP_HIT, 0);
    LOKA_VERIFY(f.controller.boundEnabled.size() == 1);
    f.controller.pressLedger_.begin();
    const Rect miss = {200, 200, 300, 300};
    f.controller.recordPopupHit(miss, &f.enabled, &f.popupContext);
    f.controller.reachPopupHit(&f.popupContext);
    f.controller.reachPopupHit(&f.popupContext);
    f.controller.pressLedger_.commit();
    f.expect(&f.popupContext, PressRow::POPUP_HIT, 0);
    LOKA_VERIFY(f.controller.boundEnabled.size() == 1);
  }
  class SweepContext : public ToolboxButtonContext
  {
    Fixture &fixture_;

  public:
    explicit SweepContext(Fixture &f)
        : ToolboxButtonContext(&f.button, &f.controller),
          fixture_(f)
    {
    }
    virtual short layout(IPlatformController *, LayoutState &)
    {
      return 100;
    }
    virtual void render(IPlatformController *)
    {
      for (int kind = 0; kind != 4; ++kind)
        this->fixture_.record(kind, this->fixture_);
    }
  };
  void sweepPins()
  {
    Fixture f;
    LOKA_VERIFY(RegisterToolboxBuiltInSupport(f.controller));
    f.button.setContext(new SweepContext(f));
    f.controller.rootNode_ = &f.button;
    for (int pass = 0; pass != 3; ++pass)
    {
      ToolboxInputDoor::render(f.controller);
      LOKA_VERIFY(f.controller.buttonHits().size() == 1);
      LOKA_VERIFY(f.controller.cellHits().size() == 1);
      f.expect(&f.editContext, PressRow::EDIT_HIT, 0);
    }
    f.controller.rootNode_ = 0;
    ToolboxInputDoor::render(f.controller);
    LOKA_VERIFY(f.controller.buttonHits().size() == 1);
    LOKA_VERIFY(f.controller.cellHits().size() == 1);
    f.expect(&f.editContext, PressRow::EDIT_HIT, 0);
    f.controller.retireNodeContext(&f.editContext, NATIVE_HINT_DEFAULT);
    f.expect(&f.popupContext, PressRow::POPUP_HIT, 0);
    f.controller.retireNodeContext(&f.popupContext, NATIVE_HINT_DEFAULT);
    f.expect(&f.cellContext, PressRow::CELL_HIT, 0);
    f.controller.retireNodeContext(&f.cellContext, NATIVE_HINT_DEFAULT);
    f.expect(&f.buttonContext, PressRow::BUTTON_HIT, 0);
  }

} // namespace
int main(int argc, char **argv)
{
  LOKA_VERIFY(argc == 2);
  if (!std::strcmp(argv[1], "record"))
    recordPins();
  else if (!std::strcmp(argv[1], "retire"))
    retirePins();
  else if (!std::strcmp(argv[1], "release"))
    releasePins();
  else if (!std::strcmp(argv[1], "popup"))
    popupPins();
  else if (!std::strcmp(argv[1], "sweep"))
    sweepPins();
  else
    LOKA_VERIFY(false);
}
