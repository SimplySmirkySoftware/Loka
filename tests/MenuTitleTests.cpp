#include "support/MenuStructure.hpp"
#include "MenuTitleTests.hpp"
#include "app/Menu.hpp"
#include "platform/null/NullMenuAttachment.hpp"
#include "core/util/StateTrackerGuard.hpp"
#include "support/TestVerify.hpp"

using namespace loka::app;
using namespace loka::core;

namespace
{
  class TitleState : public MutableState<String>
  {
  public:
    explicit TitleState(const char *value) : MutableState<String>(String::Literal(value)) {}
    size_t subscriptions() const { return this->deferredHandlers.size(); }
  };
}

void testMenuTitleDefinitionCopiesAndSelectsState()
{
  TitleState first("Alpha"), second("Beta");
  MenuItemDefinition original = MenuItem("Fallback").text(&first);
  MenuItemDefinition copy(original), assigned;
  assigned = original;
  loka::core::OwnedDef<MenuItemDefinition> cloned(original.clone());
  LOKA_VERIFY(copy.titleState == &first && assigned.titleState == &first);
  LOKA_VERIFY(cloned.isSet() && cloned->titleState == &first);
  LOKA_VERIFY(loka::app::testing::EqualsStructure(original, copy) && loka::app::testing::EqualsStructure(original, assigned));
  copy.text(&second);
  LOKA_VERIFY(!loka::app::testing::EqualsStructure(original, copy));
  copy.text(static_cast<State<String> *>(0));
  LOKA_VERIFY(!copy.titleState && copy.title.equals(String::Literal("Fallback")));
  copy.text(&first).text("Constant");
  LOKA_VERIFY(!copy.titleState && copy.title.equals(String::Literal("Constant")));
  copy.text(&first).text(String::Literal("Value"));
  LOKA_VERIFY(!copy.titleState && copy.title.equals(String::Literal("Value")));
  MenuItemDefinition differentFallback = MenuItem("Other").text(&first);
  LOKA_VERIFY(!loka::app::testing::EqualsStructure(original, differentFallback));
}

void testNullMenuTitleFollowsStateAndDisconnects()
{
  TitleState state("Alpha");
  PushStateTracker tracker;
  tracker.addState(&state);
  NullMenuAttachment attachment;
  MenuBarDefinition bar;
  bar << (Menu("File") << MenuItem("Fallback").text(&state) << MenuItem("Constant"));
  LOKA_VERIFY(attachment.open(bar));
  LOKA_VERIFY(state.subscriptions() == 1);
  LOKA_VERIFY(attachment.title(1).equals(String::Literal("Alpha")));
  LOKA_VERIFY(attachment.title(2).equals(String::Literal("Constant")));
  {
    StateTrackerGuard guard(&tracker);
    state.set(String::Literal("Beta"));
  }
  LOKA_VERIFY(attachment.title(1).equals(String::Literal("Beta")));
  attachment.disconnect();
  LOKA_VERIFY(state.subscriptions() == 0);
  {
    StateTrackerGuard guard(&tracker);
    state.set(String::Literal("Gamma"));
  }
  LOKA_VERIFY(!attachment.connected());
}

void testMenuStructureComparisonSeesItemsOfEveryMenu()
{
  MenuBarDefinition before;
  before << (Menu("File") << MenuItem("Open"))
         << (Menu("Edit") << MenuItem("Option"));
  LOKA_VERIFY(loka::app::testing::EqualsStructure(before, before));
  MenuBarDefinition extraMenu(before);
  extraMenu << Menu("Help");
  LOKA_VERIFY(!loka::app::testing::EqualsStructure(before, extraMenu));
  LOKA_VERIFY(!loka::app::testing::EqualsStructure(extraMenu, before));
  MenuBarDefinition extraItem(before);
  *extraItem.menuAt(1) << MenuItem("Extra");
  LOKA_VERIFY(!loka::app::testing::EqualsStructure(before, extraItem));
  LOKA_VERIFY(!loka::app::testing::EqualsStructure(extraItem, before));
  MenuBarDefinition extraChild(before);
  *extraChild.menuAt(1)->itemsHead() << MenuItem("Child");
  LOKA_VERIFY(!loka::app::testing::EqualsStructure(before, extraChild));
  LOKA_VERIFY(!loka::app::testing::EqualsStructure(extraChild, before));
  for (unsigned change = 0; change != 3; ++change)
  {
    MenuBarDefinition after;
    MenuDefinition second = Menu("Edit");
    if (change == 0)
      second << MenuItem("Option").attr(MenuItemAttr().checked(true));
    else if (change == 1)
      second << MenuItem("Option").enabled(false);
    else
      second << MenuItem("Renamed");
    after << (Menu("File") << MenuItem("Open")) << second;
    LOKA_VERIFY(!loka::app::testing::EqualsStructure(before, after));
    LOKA_VERIFY(!loka::app::testing::EqualsStructure(after, before));
  }
}
