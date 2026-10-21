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

    namespace
    {
      // Consume completed clones; the candidate owns every retained item.
      bool FoldAppItems(MenuDefinition &app, loka::dsl::CompositionList<MenuItemDefinition> &items)
      {
        while (items.head())
        {
          loka::core::OwnedDef<MenuItemDefinition> item(items.head());
          items.remove(item.get());
          MenuItemDefinition *match = 0;
          if (!item->hasChildren() && !item->isSeparator
              && (item->action == MENU_ACTION_ABOUT_APP || item->action == MENU_ACTION_QUIT_APP))
          {
            match = app.itemsHead();
            while (match && (match->hasChildren() || match->isSeparator || match->action != item->action))
              match = match->nextInComposition;
          }
          if (match)
          {
            if (!app.items_.replace(match, item.get()))
              return false;
            item.take();
            delete match;
          }
          else
            app.items_.appendOwned(item.take());
        }
        return true;
      }
    } // namespace

    MenuBarDefinition *MergeMenuBars(const MenuBarDefinition *base, const MenuBarDefinition *overlay)
    {
      if (!base && !overlay)
        return 0;
      loka::core::OwnedDef<MenuBarDefinition> merged((base ? base : overlay)->clone());
      if (!merged.isSet())
        return 0;
      loka::dsl::CompositionList<MenuDefinition> initial;
      merged->menus_.detachTo(initial);
      MenuDefinition *app = 0;
      const MenuDefinition *nextOverlay = base && overlay ? overlay->menusHead() : 0;
      while (initial.head() || nextOverlay)
      {
        const bool fromInitial = initial.head() != 0;
        loka::core::OwnedDef<MenuDefinition> menu;
        if (fromInitial)
        {
          menu.reset(initial.head());
          initial.remove(menu.get());
        }
        else
        {
          menu.reset(nextOverlay->clone());
          if (!menu.isSet())
            return 0;
          nextOverlay = nextOverlay->nextInComposition;
        }
        if (menu->isAppMenu)
        {
          loka::dsl::CompositionList<MenuItemDefinition> items;
          menu->items_.detachTo(items);
          if (!app)
          {
            app = menu.get();
            merged->menus_.appendOwned(menu.take());
          }
          if (!FoldAppItems(*app, items))
            return 0;
        }
        else
        {
          MenuDefinition *match = fromInitial ? 0 : merged->menusHead();
          while (match && (match->isAppMenu || !match->title.equals(menu->title)))
            match = match->nextInComposition;
          if (match)
          {
            if (!merged->menus_.replace(match, menu.get()))
              return 0;
            menu.take();
            delete match;
          }
          else
            merged->menus_.appendOwned(menu.take());
        }
      }
      return merged.take();
    }
  } // namespace app
} // namespace loka
