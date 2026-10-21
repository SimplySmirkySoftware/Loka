#include "support/MenuStructure.hpp"
#include "MenuCloneRefusalTests.hpp"
#include "app/Menu.hpp"
#include "support/TestVerify.hpp"

using namespace loka::app;
using namespace loka::core;

namespace
{
  void fillBar(MenuBarDefinition &bar)
  {
    bar << (Menu("Source") << (MenuItem("Parent") << MenuItem("Grandchild"))
                          << MenuItem("Sibling"));
  }
}

void testMenuCloneRefusesGrandchild()
{
  MenuBarDefinition bar;
  fillBar(bar);
  loka::app::testing::failMenuItemDefinitionClones(1, 1);
  OwnedDef<MenuBarDefinition> copy(bar.clone());
  loka::app::testing::failMenuItemDefinitionClones(0);
  LOKA_VERIFY(!copy.isSet());
  LOKA_VERIFY(bar.menuAt(0)->itemsHead()->childrenCount() == 1);
}

void testMenuAssignmentPreservesDestination()
{
  MenuBarDefinition source;
  fillBar(source);
  MenuDefinition destination("Old");
  destination.appMenu(true) << MenuItem("Keep");
  MenuDefinition before(destination);
  loka::app::testing::failMenuItemDefinitionClones(1, 1);
  destination = *source.menuAt(0);
  loka::app::testing::failMenuItemDefinitionClones(0);
  LOKA_VERIFY(destination.title.equals(String::Literal("Old")));
  LOKA_VERIFY(destination.itemsCount() == 1);
  LOKA_VERIFY(loka::app::testing::EqualsStructure(destination, before));
}

void testMenuMergeRefusesNestedClone()
{
  MenuBarDefinition base, overlay;
  base << (Menu("Base") << MenuItem("Keep"));
  fillBar(overlay);
  // Base item and overlay parent succeed; the overlay grandchild refuses.
  loka::app::testing::failMenuItemDefinitionClones(1, 2);
  OwnedDef<MenuBarDefinition> merged(MergeMenuBars(&base, &overlay));
  loka::app::testing::failMenuItemDefinitionClones(0);
  LOKA_VERIFY(!merged.isSet());
  LOKA_VERIFY(base.menuAt(0)->itemsCount() == 1);
  LOKA_VERIFY(overlay.menuAt(0)->itemsHead()->childrenCount() == 1);
}

void testMenuClonePreservesStructure()
{
  MenuBarDefinition bar;
  fillBar(bar);
  OwnedDef<MenuBarDefinition> copy(bar.clone());
  LOKA_VERIFY(copy.isSet());
  LOKA_VERIFY(loka::app::testing::EqualsStructure(*copy, bar));
  LOKA_VERIFY(copy->menuAt(0) != bar.menuAt(0));
  LOKA_VERIFY(copy->menuAt(0)->itemsHead()->childrenHead() !=
              bar.menuAt(0)->itemsHead()->childrenHead());
  MenuBarDefinition assigned;
  assigned = bar;
  LOKA_VERIFY(loka::app::testing::EqualsStructure(assigned, bar));
  MenuDefinition menu;
  menu = *bar.menuAt(0);
  LOKA_VERIFY(loka::app::testing::EqualsStructure(menu, *bar.menuAt(0)));
  MenuItemDefinition item;
  item = *menu.itemsHead();
  LOKA_VERIFY(loka::app::testing::EqualsStructure(item, *menu.itemsHead()));
}

void testMenuCloneFailCounts()
{
  MenuItemDefinition item("Item");
  MenuDefinition menu("Menu");
  MenuBarDefinition bar;
  bar << menu;
  loka::app::testing::failMenuItemDefinitionClones(2);
  OwnedDef<MenuItemDefinition> first(item.clone()), second(item.clone()), third(item.clone());
  loka::app::testing::failMenuItemDefinitionClones(0);
  LOKA_VERIFY(!first.isSet() && !second.isSet() && third.isSet());
  loka::app::testing::failMenuItemDefinitionClones(2, 1);
  OwnedDef<MenuItemDefinition> allowed(item.clone()), refusedFirst(item.clone()),
      refusedSecond(item.clone()), recovered(item.clone());
  loka::app::testing::failMenuItemDefinitionClones(0);
  LOKA_VERIFY(allowed.isSet() && !refusedFirst.isSet() && !refusedSecond.isSet() && recovered.isSet());
  loka::app::testing::failMenuDefinitionClones(2);
  OwnedDef<MenuBarDefinition> barFirst(bar.clone()), barSecond(bar.clone()), barThird(bar.clone());
  loka::app::testing::failMenuDefinitionClones(0);
  LOKA_VERIFY(!barFirst.isSet() && !barSecond.isSet() && barThird.isSet());
  loka::app::testing::failMenuBarDefinitionClones(2);
  OwnedDef<MenuBarDefinition> firstBar(bar.clone()), secondBar(bar.clone()), thirdBar(bar.clone());
  loka::app::testing::allowMenuBarDefinitionClones();
  LOKA_VERIFY(!firstBar.isSet() && !secondBar.isSet() && thirdBar.isSet());
}

void testMenuItemAssignmentPreservesDestination()
{
  MenuItemDefinition source("New");
  source.enabled(false).shortcut('N') << MenuItem("First") << MenuItem("Second");
  MenuItemDefinition destination("Old");
  destination << MenuItem("Keep");
  MenuItemDefinition before(destination);
  MenuItemDefinition link("Link");
  destination.nextInComposition = &link;
  loka::app::testing::failMenuItemDefinitionClones(1, 1);
  destination = source;
  loka::app::testing::failMenuItemDefinitionClones(0);
  LOKA_VERIFY(destination.title.equals(String::Literal("Old")));
  LOKA_VERIFY(destination.childrenCount() == 1);
  LOKA_VERIFY(loka::app::testing::EqualsStructure(destination, before));
  LOKA_VERIFY(destination.nextInComposition == &link);
}

void testMenuBarAssignmentPreservesDestination()
{
  MenuBarDefinition source, destination;
  fillBar(source);
  destination << (Menu("Old") << MenuItem("Keep"));
  MenuBarDefinition before(destination);
  loka::app::testing::failMenuDefinitionClones(1);
  destination = source;
  loka::app::testing::failMenuDefinitionClones(0);
  LOKA_VERIFY(loka::app::testing::EqualsStructure(destination, before));
}

void testMenuCopyConstructorRefusalLeavesEmpty()
{
  MenuItemDefinition item("Parent");
  item << MenuItem("First") << MenuItem("Second");
  loka::app::testing::failMenuItemDefinitionClones(1, 1);
  MenuItemDefinition itemCopy(item);
  loka::app::testing::failMenuItemDefinitionClones(0);
  LOKA_VERIFY(itemCopy.childrenCount() == 0);
  MenuDefinition menu("Menu");
  menu << item << MenuItem("Other");
  loka::app::testing::failMenuItemDefinitionClones(1, 1);
  MenuDefinition menuCopy(menu);
  loka::app::testing::failMenuItemDefinitionClones(0);
  LOKA_VERIFY(menuCopy.itemsCount() == 0);
  MenuBarDefinition bar;
  bar << menu << Menu("Other");
  loka::app::testing::failMenuDefinitionClones(1);
  MenuBarDefinition barCopy(bar);
  loka::app::testing::failMenuDefinitionClones(0);
  LOKA_VERIFY(barCopy.menusCount() == 0);
}

void testMenuListCopyCommitsOnlyCompletedChain()
{
  loka::dsl::CompositionList<MenuItemDefinition> source, destination;
  const MenuItemDefinition first("First"), second("Second"), old("Old");
  LOKA_VERIFY(source.appendClone(first));
  LOKA_VERIFY(source.appendClone(second));
  LOKA_VERIFY(destination.appendClone(old));
  MenuItemDefinition *oldHead = destination.head();
  loka::app::testing::failMenuItemDefinitionClones(1, 1);
  const bool copied = destination.copyFrom(source);
  loka::app::testing::failMenuItemDefinitionClones(0);
  LOKA_VERIFY(!copied);
  LOKA_VERIFY(destination.head() == oldHead && destination.count() == 1);
  LOKA_VERIFY(destination.head()->title.equals(String::Literal("Old")));
  LOKA_VERIFY(destination.copyFrom(source));
  LOKA_VERIFY(destination.count() == 2);
  LOKA_VERIFY(loka::app::testing::EqualsStructure(*destination.head(), first));
  LOKA_VERIFY(loka::app::testing::EqualsStructure(*destination.head()->nextInComposition, second));
  LOKA_VERIFY(destination.appendClone(old));
  LOKA_VERIFY(destination.count() == 3);
  LOKA_VERIFY(loka::app::testing::EqualsStructure(*destination.head()->nextInComposition->nextInComposition, old));
  loka::app::testing::failMenuItemDefinitionClones(1);
  LOKA_VERIFY(destination.copyFrom(destination));
  loka::app::testing::failMenuItemDefinitionClones(0);
  LOKA_VERIFY(destination.count() == 3);
}

void testMenuSelfAssignmentDoesNotClone()
{
  MenuBarDefinition bar;
  fillBar(bar);
  MenuBarDefinition before(bar);
  MenuBarDefinition &sameBar = bar;
  MenuDefinition &menu = *bar.menuAt(0);
  MenuDefinition &sameMenu = menu;
  MenuItemDefinition &item = *menu.itemsHead();
  MenuItemDefinition &sameItem = item;
  loka::app::testing::failMenuItemDefinitionClones(1);
  loka::app::testing::failMenuDefinitionClones(1);
  bar = sameBar;
  menu = sameMenu;
  item = sameItem;
  OwnedDef<MenuItemDefinition> refusedItem(item.clone());
  OwnedDef<MenuDefinition> refusedMenu(menu.clone());
  loka::app::testing::failMenuItemDefinitionClones(0);
  loka::app::testing::failMenuDefinitionClones(0);
  LOKA_VERIFY(!refusedItem.isSet() && !refusedMenu.isSet());
  LOKA_VERIFY(loka::app::testing::EqualsStructure(bar, before));
}

void testMergeAppMenuRefusalIsAtomic()
{
  MenuBarDefinition base, overlay;
  base << (AppMenu() << MenuItem("Quit").actionType(MENU_ACTION_QUIT_APP));
  overlay << (AppMenu() << MenuItem("Winner").actionType(MENU_ACTION_QUIT_APP) << MenuItem("Other")
                        << (MenuItem("Parent") << MenuItem("Child")));
  const MenuBarDefinition beforeBase(base), beforeOverlay(overlay);
  // Clone order: base Quit, winning Quit, kept other, parent, child.
  for (int item = 0; item < 5; ++item)
  {
    loka::app::testing::failMenuItemDefinitionClones(1, item);
    OwnedDef<MenuBarDefinition> refused(MergeMenuBars(&base, &overlay));
    loka::app::testing::failMenuItemDefinitionClones(0);
    LOKA_VERIFY(!refused.isSet());
    LOKA_VERIFY(loka::app::testing::EqualsStructure(base, beforeBase));
    LOKA_VERIFY(loka::app::testing::EqualsStructure(overlay, beforeOverlay));
    OwnedDef<MenuBarDefinition> retry(MergeMenuBars(&base, &overlay));
    LOKA_VERIFY(retry.isSet() && retry->menuAt(0)->itemsCount() == 3);
  }
  // An empty base makes the overlay container the next menu clone.
  MenuBarDefinition empty;
  for (int source = 0; source < 2; ++source)
  {
    loka::app::testing::failMenuDefinitionClones(1);
    OwnedDef<MenuBarDefinition> refused(MergeMenuBars(source ? &empty : &base, &overlay));
    loka::app::testing::failMenuDefinitionClones(0);
    LOKA_VERIFY(!refused.isSet());
    LOKA_VERIFY(loka::app::testing::EqualsStructure(base, beforeBase));
    LOKA_VERIFY(loka::app::testing::EqualsStructure(overlay, beforeOverlay));
    OwnedDef<MenuBarDefinition> retry(MergeMenuBars(source ? &empty : &base, &overlay));
    LOKA_VERIFY(retry.isSet() && retry->menuAt(0)->itemsCount() == 3);
  }
}
