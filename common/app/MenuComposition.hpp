#ifndef LOKA_APP_MENU_COMPOSITION_HPP
#define LOKA_APP_MENU_COMPOSITION_HPP

#include <cstddef>
#include "dsl/composition/CompositionList.hpp"

namespace loka
{
  namespace app
  {
    struct MenuDefinition;
    struct MenuBarDefinition;
    class MenuComposition;

    /** Build a new bar from base and overlay. An overlay ordinary menu replaces
        the first base ordinary menu with an equal title in place, otherwise it
        is appended in order. App containers (isAppMenu, title not consulted)
        fold into one, at the first container's slot and with its title, even
        when only one source is given; an empty container still claims the
        slot. Childless About and Quit items keep the slot of their first
        occurrence and the complete payload of their last; every other item,
        separators and parents included, keeps encounter order. Null means
        both absent or clone refusal; callers distinguish by input presence
        and keep installed truth.
        The caller owns the result; borrowed item endpoints retain their owner. */
    MenuBarDefinition *MergeMenuBars(const MenuBarDefinition *base, const MenuBarDefinition *overlay);

    class MenuComposition
    {
    public:
      explicit MenuComposition(MenuBarDefinition *bar)
          : bar_(bar),
            list_()
      {
      }
      ~MenuComposition();

      void declare(const MenuDefinition &menu);
      void declare(const MenuBarDefinition &bar);
      void finish();

      MenuComposition &operator<<(const MenuDefinition &menu);
      MenuComposition &operator<<(const MenuBarDefinition &bar);

    private:
      MenuBarDefinition *bar_;
      loka::dsl::CompositionList<MenuDefinition> list_;
    };

  } // namespace app
} // namespace loka

#endif // LOKA_APP_MENU_COMPOSITION_HPP
