#include "app/MenuComposition.hpp"
#include "app/Menu.hpp"

namespace loka
{
  namespace app
  {
    MenuComposition::~MenuComposition()
    {
      // list_ cleans up automatically
    }

    void MenuComposition::declare(const MenuDefinition &menu)
    {
      if (bar_)
      {
        list_.appendOwned(new MenuDefinition(menu));
      }
    }

    void MenuComposition::declare(const MenuBarDefinition &bar)
    {
      if (!bar_)
        return;
      loka::dsl::CompositionCursor<MenuDefinition> it(bar.menusHead(), bar.menusCount());
      for (MenuDefinition *menu = it.next(); menu; menu = it.next())
      {
        declare(*menu);
      }
    }

    void MenuComposition::finish()
    {
      if (!bar_ || list_.count() == 0)
        return;
      bar_->clearMenus();
      list_.detachTo(bar_->menus_);
    }

    MenuComposition &MenuComposition::operator<<(const MenuDefinition &menu)
    {
      declare(menu);
      return *this;
    }

    MenuComposition &MenuComposition::operator<<(const MenuBarDefinition &bar)
    {
      declare(bar);
      return *this;
    }

    MenuBarDefinition *MergeMenuBars(const MenuBarDefinition *base, const MenuBarDefinition *overlay)
    {
      if (!base)
        return overlay ? overlay->clone() : 0;
      loka::core::OwnedDef<MenuBarDefinition> merged(base->clone());
      if (!merged.isSet() || !overlay)
        return merged.take();
      for (const MenuDefinition *menu = overlay->menusHead(); menu; menu = menu->nextInComposition)
      {
        loka::core::OwnedDef<MenuDefinition> replacement(menu->clone());
        if (!replacement.isSet())
          return 0;
        MenuDefinition *match = merged->menusHead();
        while (match && !match->title.equals(menu->title))
          match = match->nextInComposition;
        if (match)
        {
          if (!merged->menus_.replace(match, replacement.get()))
            return 0;
          replacement.take();
          delete match;
        }
        else
          merged->menus_.appendOwned(replacement.take());
      }
      return merged.take();
    }
  } // namespace app
} // namespace loka
