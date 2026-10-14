#ifndef LOKA_TESTS_SUPPORT_MENU_STRUCTURE_HPP
#define LOKA_TESTS_SUPPORT_MENU_STRUCTURE_HPP

#include "app/Menu.hpp"

namespace loka
{
  namespace app
  {
    namespace testing
    {
      /** Test-only definition comparison, including endpoint identities and
          every menu/item attribute. Production projection compares sources. */
      inline bool EqualsStructure(const MenuItemDefinition &value, const MenuItemDefinition &other)
      {
        if (value.isSeparator != other.isSeparator)
          return false;
        if (!value.title.equals(other.title))
          return false;
        if (value.titleState != other.titleState)
          return false;
        if (value.enabledState != other.enabledState)
          return false;
        if (value.enabledValue_ != other.enabledValue_)
          return false;
        if (value.hasEnabledValue_ != other.hasEnabledValue_)
          return false;
        if (value.onClickState != other.onClickState)
          return false;
        if (value.action != other.action)
          return false;
        if (value.hasShortcut != other.hasShortcut)
          return false;
        if (value.shortcutKey != other.shortcutKey)
          return false;
        if (value.hasAttr_ != other.hasAttr_)
          return false;
        if (!(value.attr_ == other.attr_))
          return false;
        if (value.children_.count() != other.children_.count())
          return false;
        const MenuItemDefinition *left = value.children_.head();
        const MenuItemDefinition *right = other.children_.head();
        while (left && right)
        {
          if (!EqualsStructure(*left, *right))
            return false;
          left = left->nextInComposition;
          right = right->nextInComposition;
        }
        return true;
      }

      inline bool EqualsStructure(const MenuDefinition &value, const MenuDefinition &other)
      {
        if (value.isAppMenu != other.isAppMenu)
          return false;
        if (!value.title.equals(other.title))
          return false;
        if (value.items_.count() != other.items_.count())
          return false;
        const MenuItemDefinition *left = value.items_.head();
        const MenuItemDefinition *right = other.items_.head();
        while (left && right)
        {
          if (!EqualsStructure(*left, *right))
            return false;
          left = left->nextInComposition;
          right = right->nextInComposition;
        }
        return true;
      }

      inline bool EqualsStructure(const MenuBarDefinition &value, const MenuBarDefinition &other)
      {
        if (value.menus_.count() != other.menus_.count())
          return false;
        const MenuDefinition *left = value.menus_.head();
        const MenuDefinition *right = other.menus_.head();
        while (left && right)
        {
          if (!EqualsStructure(*left, *right))
            return false;
          left = left->nextInComposition;
          right = right->nextInComposition;
        }
        return true;
      }
    } // namespace testing
  } // namespace app
} // namespace loka

#endif // LOKA_TESTS_SUPPORT_MENU_STRUCTURE_HPP
