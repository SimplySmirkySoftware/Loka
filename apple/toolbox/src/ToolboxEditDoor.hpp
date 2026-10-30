#ifndef LOKA_TOOLBOX_EDIT_DOOR_HPP
#define LOKA_TOOLBOX_EDIT_DOOR_HPP

#include "ToolboxNativeDoorScope.hpp"
#include "ToolboxNativePlacement.hpp"

/** One ordinary TextEdit native interval. Placement is copied before entry;
    restoration never borrows a row that publication could retire or relocate.
    A fully visible field selects its owner without allocating a region. */
class ToolboxEditDoor
{
public:
  ToolboxEditDoor(GrafPtr owner, ToolboxNativePlacement placement)
      : scope_(owner, placement.visible(), !EqualRect(&placement.full(), &placement.visible()))
  {
  }

private:
  ToolboxEditDoor(const ToolboxEditDoor &);
  ToolboxEditDoor &operator=(const ToolboxEditDoor &);
  ToolboxNativeDoorScope scope_;
};

#endif
