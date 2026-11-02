#ifndef LOKA_TOOLBOX_TEXT_CAPACITY_HPP
#define LOKA_TOOLBOX_TEXT_CAPACITY_HPP
#include "app/TextCapacity.hpp"
#include <climits>
/** Rail representation limit; applications choose a smaller memory profile. */
inline loka::app::TextCapacity ToolboxTextCapacity()
{
#if LOKA_TOOLBOX_UNICODE
  // CR is one unit; each UTF-8 scalar has U <= B, so 2U <= 2B <= INT_MAX - 1.
  return loka::app::TextCapacity::of(65535, INT_MAX / 2);
#else
  return loka::app::TextCapacity::of(256, 8192);
#endif
}
#endif
