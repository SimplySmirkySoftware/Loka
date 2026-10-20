#ifndef LOKA_HOST_MENUS_H
#define LOKA_HOST_MENUS_H
#include "Controls.h"
namespace toolbox_host
{
  extern unsigned menuDraws, menuClears, menuDisposes, menuValueWrites;
  extern unsigned menuInitializations;
  // Entries in the fake menu color information table; ClearMenuBar deletes them.
  extern int menuColorEntries;
  extern std::vector<MenuHandle> installedMenus;
  /** Immutable call facts captured at the fake Toolbox boundary. */
  struct ItemCmdCall
  {
    MenuHandle menu;
    short item, cmd;
  };
  extern std::vector<ItemCmdCall> itemCmdCalls;
}
enum { kInsertHierarchicalMenu = -1, hMenuCmd = 27 };
typedef short CharParameter;
inline short CountMenuItems(MenuHandle menu) { return static_cast<short>(menu->items.size()); }
inline void DeleteMenuItem(MenuHandle menu, short item) { menu->items.erase(menu->items.begin() + item - 1); }
inline void EnableItem(MenuHandle, short) { ++toolbox_host::menuValueWrites; }
inline void DisableItem(MenuHandle, short) { ++toolbox_host::menuValueWrites; }
inline void CheckItem(MenuHandle, short, bool) { ++toolbox_host::menuValueWrites; }
inline void ClearMenuBar() { ++toolbox_host::menuClears; toolbox_host::installedMenus.clear(); toolbox_host::menuColorEntries = 0; }
typedef int *MCTableHandle;
inline MCTableHandle GetMCInfo() { return new int(toolbox_host::menuColorEntries); }
inline void SetMCInfo(MCTableHandle table) { toolbox_host::menuColorEntries = *table; }
inline void DisposeMCInfo(MCTableHandle table) { delete table; }
void InitMenus();
inline void DrawMenuBar() { ++toolbox_host::menuDraws; }
inline void AppendResMenu(MenuHandle, unsigned long) {}
inline void SetItemCmd(MenuHandle menu, short item, short cmd)
{
  const toolbox_host::ItemCmdCall call = {menu, item, cmd};
  toolbox_host::itemCmdCalls.push_back(call);
}
inline void SetItemMark(MenuHandle, short, CharParameter) {}
inline MenuHandle GetMenuHandle(short) { return 0; }
inline void GetMenuItemText(MenuHandle, short, unsigned char *text) { text[0] = 0; }
#endif
