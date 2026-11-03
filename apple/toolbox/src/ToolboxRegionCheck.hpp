#ifndef LOKA_TOOLBOX_REGION_CHECK_HPP
#define LOKA_TOOLBOX_REGION_CHECK_HPP
#include <Quickdraw.h>
#include <MacMemory.h>
/** Check immediately after each region operation, including reads of installed clips. */
inline bool ToolboxRegionOK(RgnHandle region)
{
  return MemError() == 0 && region && *region && (**region).rgnSize >= 10;
}
#endif
