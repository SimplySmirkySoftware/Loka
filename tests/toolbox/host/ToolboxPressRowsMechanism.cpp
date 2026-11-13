#include "ToolboxHost.hpp"
#include "ToolboxPressRows.inc"
using namespace loka::app::scene;
using namespace loka::core;
namespace
{
  struct OrderedHit
  {
    NodeContext *context;
    State<bool> *state;
    static void release(const OrderedHit &hit, ToolboxReleasedStates &released)
    {
      released.enableds.push_back(hit.state);
    }
  };
  void chainOrder()
  {
    MutableState<bool> a(true), b(true), c(true), d(true);
    NodeContext context;
    ToolboxReleasedStates released;
    ToolboxPressLedger ledger;
    {
      ToolboxPressStoreChain chain;
      ToolboxPressRows<OrderedHit, PressRow::BUTTON_HIT> first(chain);
      ToolboxPressRows<OrderedHit, PressRow::CELL_HIT> second(chain);
      ToolboxPressRows<OrderedHit, PressRow::POPUP_HIT> third(chain);
      ToolboxPressRows<OrderedHit, PressRow::EDIT_HIT> fourth(chain);
      const OrderedHit hits[] = {{&context, &a}, {&context, &b}, {&context, &c}, {&context, &d}};
      // Recording order differs from enrollment order.
      fourth.record(hits[3], ledger);
      second.record(hits[1], ledger);
      first.record(hits[0], ledger);
      third.record(hits[2], ledger);
      chain.retire(&context, ledger, released);
      LOKA_VERIFY(released.enableds.size() == 4);
      LOKA_VERIFY(released.enableds[0] == &a);
      LOKA_VERIFY(released.enableds[1] == &b);
      LOKA_VERIFY(released.enableds[2] == &c);
      LOKA_VERIFY(released.enableds[3] == &d);
      first.store(hits[0]);
      second.store(hits[1]);
      third.store(hits[2]);
      fourth.store(hits[3]);
      chain.clear();
      LOKA_VERIFY(first.empty() && second.empty() && third.empty() && fourth.empty());
      LOKA_VERIFY(released.enableds.size() == 4);
      first.store(hits[0]);
      // Destruction leaves an expired, non-owning chain; no destructor traverses it.
    }
    LOKA_VERIFY(released.enableds.size() == 4);
  }
  void clearOwnership()
  {
    ToolboxWindow window;
    HostPortScope port(window.window());
    ToolboxScenePlatformController controller(&window);
    loka::app::ButtonNode button((loka::app::ButtonProps()));
    ToolboxButtonContext context(&button, &controller);
    context.setOwner(&button);
    const Rect rect = {0, 0, 100, 100};
    const Point point = {20, 20};
    context.updateRect(rect);
    controller.recordButtonHit(rect, 0, 0, 0, &context);
    controller.pressLedger_.commit();
    const ToolboxHitLedger::ButtonHit saved = controller.buttonHits()[0];
    controller.hitLedger_.clearPressRows();
    LOKA_VERIFY(controller.buttonHits().empty());
    LOKA_VERIFY(controller.unboundEnabled.empty());
    controller.installHit(saved);
    PressRow row;
    LOKA_VERIFY(controller.selectedPress(point, row) == &context);
    LOKA_VERIFY(row.kind == PressRow::BUTTON_HIT && row.index == 0);
  }
} // namespace
#include "ToolboxPressRowsBehaviorPins.hpp"
namespace press_rows_behavior
{
  struct EnabledSink
  {
    unsigned calls;
    EnabledSink()
        : calls(0)
    {
    }
    void apply(ToolboxHitLedger::ButtonHit &hit)
    {
      ++this->calls;
      hit.rect.right = 77;
    }
  };
  void readers()
  {
    ToolboxPressStoreChain chain;
    ToolboxPressRows<ToolboxHitLedger::ButtonHit, PressRow::BUTTON_HIT> rows(chain);
    MutableState<bool> enabled(true), other(false);
    ToolboxHitLedger::ButtonHit hit = ToolboxHitLedger::ButtonHit();
    hit.enabled = &enabled;
    rows.store(hit);
    rows.store(hit);
    rows[1].enabled = &other;
    EnabledSink sink;
    LOKA_VERIFY(ApplyToolboxEnabledChangeToBindings(rows.begin(), rows.end(), &enabled, sink, &EnabledSink::apply));
    LOKA_VERIFY(sink.calls == 1);
    const ToolboxPressRows<ToolboxHitLedger::ButtonHit, PressRow::BUTTON_HIT> &view = rows;
    LOKA_VERIFY(view[0].rect.right == 77);
    LOKA_VERIFY(view.begin()->enabled == &enabled);
    LOKA_VERIFY(view.end() - view.begin() == 2);
    LOKA_VERIFY(&view[1] == &rows[1]);
  }
} // namespace press_rows_behavior
int main()
{
  chainOrder();
  clearOwnership();
  press_rows_behavior::readers();
  press_rows_behavior::popupComposition();
  press_rows_behavior::retainedProps();
  press_rows_behavior::cellDirtyReplay();
  press_rows_behavior::mixedKinds();
}
