#ifndef SMIRKYCARD_CARD_WINDOW_PROPS_HPP
#define SMIRKYCARD_CARD_WINDOW_PROPS_HPP

#include "app/core/Window.hpp"

namespace smirkycard
{
  /** Shared card window geometry; each card declares its own interior padding. */
  inline WindowProps CardWindowProps()
  {
    return WindowProps().frame(60, 60, 420, 340).contentMargin(0);
  }
} // namespace smirkycard
#endif
