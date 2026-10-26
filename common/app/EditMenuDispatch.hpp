#ifndef LOKA_APP_EDIT_MENU_DISPATCH_HPP
#define LOKA_APP_EDIT_MENU_DISPATCH_HPP
#include "app/core/Window.hpp"

namespace loka
{
  namespace app
  {
    /** One synchronous offer per menu hit. The binding is copied before the offer;
        no attachment row or borrowed context is read after potentially retiring input. */
    inline void dispatchEditMenuCommand(Window *window, EditVerb verb, loka::core::EmitterState *binding)
    {
      if (window && window->handleEditCommand(verb))
        return;
      if (binding)
        binding->emit();
    }
  } // namespace app
} // namespace loka
#endif
