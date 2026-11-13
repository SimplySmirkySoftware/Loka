/** Additional reader and real layout/render coverage for the extracted stores. */
#include "ToolboxInputDoor.hpp"
#include "ToolboxEnabledChangeDispatch.hpp"
#include "ToolboxBuiltInSupport.hpp"
#include "ToolboxPlatformLayoutHandlers.hpp"
#include "ToolboxNodeDispatch.hpp"
using namespace loka::app;
namespace press_rows_behavior
{
  class OverlapButton : public ToolboxButtonContext
  {
    const bool custom_;

  public:
    OverlapButton(ButtonNode &node, ToolboxScenePlatformController &controller, bool custom)
        : ToolboxButtonContext(&node, &controller),
          custom_(custom)
    {
      this->updateData(String("button"), 0, 0, 0, 0);
    }
    virtual short layout(IPlatformController *, LayoutState &state)
    {
      const Rect rect = {0, 0, 120, 200};
      this->updateRect(rect);
      state.y = 120;
      return 200;
    }
    virtual void render(IPlatformController *platform)
    {
      if (!this->custom_)
      {
        ToolboxButtonContext::render(platform);
        return;
      }
      const Rect rect = {0, 0, 120, 200};
      static_cast<ToolboxScenePlatformController *>(platform)->recordButtonHit(rect, 0, 0, 0, this);
    }
  };
  template <class NodeType, class Props> class PlacedNode : public NodeType
  {
  public:
    explicit PlacedNode(const Props &props)
        : NodeType(props)
    {
    }
    virtual short layoutProjected(IPlatformController *controller, LayoutState &state)
    {
      return this->getContext()->layout(controller, state);
    }
  };
  void popupComposition()
  {
    for (int last = 0; last != 3; ++last)
    {
      ToolboxWindow window;
      HostPortScope port(window.window());
      ToolboxScenePlatformController controller(&window);
      LOKA_VERIFY(RegisterToolboxBuiltInSupport(controller));
      LOKA_VERIFY(RegisterToolboxPlatformLayoutHandlers(controller.gapLayoutRegistry));
      MutableState<int> selection(0);
      const char *items[] = {"one"};
      ZStackNode stack((ZStackProps()));
      ButtonNode *custom = new PlacedNode<ButtonNode, ButtonProps>(ButtonProps().text("custom"));
      ButtonNode *native = new PlacedNode<ButtonNode, ButtonProps>(ButtonProps().text("native"));
      PopupMenuNode *popup = new PlacedNode<PopupMenuNode, PopupMenuProps>(
          PopupMenuProps().items(items, 1).selectedIndex(WriteSeat<int>(&selection)));
      custom->setContext(new OverlapButton(*custom, controller, true));
      native->setContext(new OverlapButton(*native, controller, false));
      popup->setContext(new ToolboxPopupMenuContext(popup, &controller));
      Node *nodes[] = {custom, native, popup};
      for (int i = 1; i <= 3; ++i)
        stack.addChild(nodes[(last + i) % 3]);
      NotifySubtreeNodeAttached(&stack);
      controller.rootNode_ = &stack;
      ToolboxInputDoor::render(controller);
      ToolboxPopupMenuContext *popupContext = static_cast<ToolboxPopupMenuContext *>(popup->getContext());
      const Rect rect = popupContext->rect();
      const Point point = {static_cast<short>(rect.top + 2), static_cast<short>(rect.left + 2)};
      PressRow row;
      NodeContext *actual = controller.selectedPress(point, row);
      LOKA_VERIFY(actual == nodes[last]->getContext());
      const PressRow::Kind kinds[] = {PressRow::BUTTON_HIT, PressRow::NATIVE_BUTTON, PressRow::POPUP_HIT};
      LOKA_VERIFY(row.kind == kinds[last]);
      controller.pressLedger_.begin();
      LayoutState repeat;
      repeat.x = rect.left;
      repeat.y = rect.top;
      repeat.width = rect.right - rect.left;
      repeat.lineHeight = 14;
      // A second layout stores the same context again; render still reaches index zero.
      popupContext->layout(&controller, repeat);
      const std::size_t bindings = controller.boundEnabled.size();
      const Rect previousClip = controller.projectionClip;
      const Rect miss = {20000, 20000, 20100, 20100};
      controller.projectionClip = miss;
      repeat.y = rect.top;
      popupContext->layout(&controller, repeat);
      controller.projectionClip = previousClip;
      LOKA_VERIFY(controller.boundEnabled.size() == bindings);
      popupContext->render(&controller);
      popupContext->render(&controller);
      controller.pressLedger_.commit();
      LOKA_VERIFY(controller.selectedPress(point, row) == popupContext);
      LOKA_VERIFY(row.index == 0);
      controller.clearControls();
      controller.rootNode_ = 0;
    }
  }
  void retainedProps()
  {
    ToolboxWindow window;
    HostPortScope port(window.window());
    ToolboxScenePlatformController controller(&window);
    MutableState<bool> oldEnabled(true), newEnabled(false);
    MutableState<String> oldText(String("old")), newText(String("new"));
    ButtonNode button(ButtonProps().enabled(&oldEnabled));
    CellNode cell(CellProps().text(&oldText));
    PopupMenuNode popup(PopupMenuProps().enabled(&oldEnabled));
    EditTextNode edit((EditTextProps()));
    button.setContext(new ToolboxButtonContext(&button, &controller));
    cell.setContext(new ToolboxCellContext(&cell, &controller));
    popup.setContext(new ToolboxPopupMenuContext(&popup, &controller));
    edit.setContext(new ToolboxEditTextContext(&edit, &controller));
    ToolboxButtonContext *b = static_cast<ToolboxButtonContext *>(button.getContext());
    ToolboxCellContext *c = static_cast<ToolboxCellContext *>(cell.getContext());
    ToolboxPopupMenuContext *p = static_cast<ToolboxPopupMenuContext *>(popup.getContext());
    ToolboxEditTextContext *e = static_cast<ToolboxEditTextContext *>(edit.getContext());
    const Rect rect = {0, 0, 100, 100};
    c->updateData(&oldText);
    controller.recordButtonHit(rect, 0, &oldEnabled, 0, b);
    controller.recordCellHit(rect, 0, 0, c, &oldText);
    controller.recordPopupHit(rect, &oldEnabled, p);
    controller.recordEditHit(rect, &oldText, 0, e);
    button.props.enabled_ = &newEnabled;
    cell.props.text_ = &newText;
    popup.props.enabled_ = &newEnabled;
    c->updateData(&newText);
    e->updateData(&newText);
    controller.refreshContextProps(&button);
    controller.refreshContextProps(&cell);
    controller.refreshContextProps(&popup);
    controller.refreshContextProps(&edit);
    LOKA_VERIFY(controller.buttonHits()[0].enabled == &newEnabled);
    LOKA_VERIFY(controller.cellHits()[0].text == &newText);
    controller.unboundEnabled.clear();
    controller.unboundText.clear();
    controller.retireNodeContext(b, NATIVE_HINT_DEFAULT);
    controller.retireNodeContext(c, NATIVE_HINT_DEFAULT);
    LOKA_VERIFY(controller.unboundEnabled.empty() && controller.unboundText.empty());
    controller.retireNodeContext(p, NATIVE_HINT_DEFAULT);
    controller.retireNodeContext(e, NATIVE_HINT_DEFAULT);
    LOKA_VERIFY(controller.unboundEnabled.size() == 1 && controller.unboundEnabled[0] == &newEnabled);
    LOKA_VERIFY(controller.unboundText.size() == 1 && controller.unboundText[0] == &newText);
    // Null candidates in the other two kinds also skip the final unbind phase.
    controller.recordCellHit(rect, 0, 0, c, 0);
    controller.recordPopupHit(rect, 0, p);
    controller.retireNodeContext(c, NATIVE_HINT_DEFAULT);
    controller.retireNodeContext(p, NATIVE_HINT_DEFAULT);
    LOKA_VERIFY(controller.unboundText.size() == 1);
    LOKA_VERIFY(controller.unboundEnabled.size() == 1);
  }
  void cellDirtyReplay()
  {
    ToolboxWindow window;
    HostPortScope port(window.window());
    ToolboxScenePlatformController controller(&window);
    CellNode cell(CellProps().text("replay"));
    ToolboxCellContext *context = new ToolboxCellContext(&cell, &controller);
    cell.setContext(context);
    LayoutState seat;
    seat.x = 10;
    seat.y = 10;
    seat.width = 100;
    seat.height = 20;
    seat.lineHeight = 14;
    context->layout(&controller, seat);
    context->render(&controller);
    LOKA_VERIFY(controller.cellHits().size() == 1);
    const Rect rect = controller.cellHits()[0].rect;
    controller.rootNode_ = &cell;
    controller.forceFullRedraw_ = false;
    toolbox_host::draws.clear();
    for (unsigned pass = 0; pass != 3; ++pass)
    {
      ToolboxInputDoor::renderDirty(controller, rect);
      LOKA_VERIFY(controller.cellHits().size() == 1);
      LOKA_VERIFY(toolbox_host::draws.size() == pass + 1);
    }
    controller.rootNode_ = 0;
  }
  void mixedKinds()
  {
    for (unsigned removed = 0; removed != 4; ++removed)
    {
      ToolboxWindow window;
      HostPortScope port(window.window());
      ToolboxScenePlatformController controller(&window);
      MutableState<String> text(String("text"));
      MutableState<int> selected(0);
      EmitterState click;
      loka::Vector<String> items;
      items.push_back(String("item"));
      ButtonNode button((ButtonProps()));
      CellNode cell(CellProps().text(&text).onClick(&click));
      PopupMenuNode popup((PopupMenuProps()));
      EditTextNode edit((EditTextProps()));
      ToolboxButtonContext *b = new ToolboxButtonContext(&button, &controller);
      ToolboxCellContext *c = new ToolboxCellContext(&cell, &controller);
      ToolboxPopupMenuContext *p = new ToolboxPopupMenuContext(&popup, &controller);
      ToolboxEditTextContext *e = new ToolboxEditTextContext(&edit, &controller);
      button.setContext(b);
      cell.setContext(c);
      popup.setContext(p);
      edit.setContext(e);
      const Rect broad = {0, 0, 100, 100};
      b->updateRect(broad);
      c->updateRect(broad);
      c->updateData(&text);
      p->updateRect(broad, 12);
      p->updateData(&items, &selected, WriteSeat<int>(&selected), 0, 0);
      e->updateRect(broad, broad, 0, 12);
      const Rect rects[] = {{0, 0, 20, 20}, {0, 20, 20, 40}, {0, 40, 20, 60}, {0, 60, 20, 80}};
      controller.recordButtonHit(rects[0], 0, 0, 0, b);
      controller.recordCellHit(rects[1], &click, 0, c, &text);
      controller.recordPopupHit(rects[2], 0, p);
      controller.reachPopupHit(p);
      controller.recordEditHit(rects[3], &text, 0, e);
      controller.pressLedger_.commit();
      const PressRow::Kind kinds[] = {
          PressRow::BUTTON_HIT, PressRow::CELL_HIT, PressRow::POPUP_HIT, PressRow::EDIT_HIT};
      NodeContext *contexts[] = {b, c, p, e};
      for (unsigned i = 0; i != 4; ++i)
        controller.pressLedger_.append(kinds[i], 0);
      controller.retireNodeContext(contexts[removed], NATIVE_HINT_DEFAULT);
      for (unsigned phase = 0; phase != 2; ++phase)
      {
        for (unsigned i = 0; i != 4; ++i)
        {
          const Point point = {5, static_cast<short>(i * 20 + 5)};
          PressRow row;
          LOKA_VERIFY(controller.selectedPress(point, row) == (i == removed ? 0 : contexts[i]));
          if (i != removed)
            LOKA_VERIFY(row.kind == kinds[i] && row.index == 0);
        }
        controller.pressLedger_.commit();
      }
    }
  }
} // namespace press_rows_behavior
