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

    /** Clone base and replace equal-title menus in place with overlay menus;
        append overlay-only menus in order. Null means both absent or clone
        refusal; callers distinguish by input presence and keep installed truth.
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
