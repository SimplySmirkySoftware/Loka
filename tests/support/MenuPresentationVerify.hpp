#ifndef LOKA_TESTS_SUPPORT_MENU_PRESENTATION_VERIFY_HPP
#define LOKA_TESTS_SUPPORT_MENU_PRESENTATION_VERIFY_HPP

#include "MenuStructure.hpp"
#include "app/core/AppConfigurable.hpp"

namespace loka
{
  namespace testing
  {
    inline void ComposeMenuBar(AppConfigurable &config, app::MenuBarDefinition &bar)
    {
      app::MenuComposition composition(&bar);
      config.composeDefaultMenu(composition);
      composition.finish();
    }

    inline void ClearMenuItemDriving(app::MenuItemDefinition *item)
    {
      for (; item; item = item->nextInComposition)
      {
        item->onClickState = 0;
        if (item->titleState)
        {
          item->title = item->titleState->get();
          item->titleState = 0;
        }
        ClearMenuItemDriving(item->childrenHead());
      }
    }

    /** Compares visible menu declarations while ignoring owner-specific
        callback identities; menu driving is outside this fidelity check. */
    inline bool MenuPresentationsEqual(const app::MenuBarDefinition &left, const app::MenuBarDefinition &right)
    {
      app::MenuBarDefinition leftPresentation(left);
      app::MenuBarDefinition rightPresentation(right);
      for (app::MenuDefinition *menu = leftPresentation.menusHead(); menu; menu = menu->nextInComposition)
      {
        ClearMenuItemDriving(menu->itemsHead());
      }
      for (app::MenuDefinition *menu = rightPresentation.menusHead(); menu; menu = menu->nextInComposition)
      {
        ClearMenuItemDriving(menu->itemsHead());
      }
      return loka::app::testing::EqualsStructure(leftPresentation, rightPresentation);
    }
  } // namespace testing
} // namespace loka

#endif // LOKA_TESTS_SUPPORT_MENU_PRESENTATION_VERIFY_HPP
