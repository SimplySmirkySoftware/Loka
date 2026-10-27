#ifndef LOKA_TOOLBOX_TEXT_CAPACITY_HPP
#define LOKA_TOOLBOX_TEXT_CAPACITY_HPP
#include "app/TextCapacity.hpp"
/** TE storage remains fixed until the separate native-capacity work. */
inline loka::app::TextCapacity ToolboxTextCapacity()
{
  return loka::app::TextCapacity::of(256, 8192);
}
#endif
