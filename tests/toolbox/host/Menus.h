#ifndef LOKA_HOST_MENUS_H
#define LOKA_HOST_MENUS_H
#include "Controls.h"
namespace toolbox_host
{
  extern unsigned menuDraws, menuClears, menuDisposes, menuValueWrites;
  extern unsigned menuInitializations;
  extern std::vector<MenuHandle> installedMenus;
  /** Immutable call facts captured at the fake Toolbox boundary. */
  struct ItemCmdCall
  {
    MenuHandle menu;
    short item, cmd;
  };
  extern std::vector<ItemCmdCall> itemCmdCalls, itemMarkCalls;
  struct MenuAllocation { MenuHandle handle; unsigned disposals; };
  struct MenuInsertion { MenuHandle handle; short mode; };
  extern std::vector<MenuAllocation> menuAllocations;
  extern std::vector<MenuInsertion> menuInsertions;
  namespace testing
  {
    void failMenuCreations(unsigned count);
    void failMenuCreationCall(unsigned call);
    void failSeparatorAppends(unsigned count);
    void failPlaceholderAppends(unsigned count);
    void failItemInserts(unsigned count);
  }
}
enum { kInsertHierarchicalMenu = -1, hMenuCmd = 27 };
typedef short CharParameter;
inline short CountMenuItems(MenuHandle menu) { return static_cast<short>(menu->items.size()); }
inline void DeleteMenuItem(MenuHandle menu, short item) { menu->items.erase(menu->items.begin() + item - 1); }
inline void EnableItem(MenuHandle, short) { ++toolbox_host::menuValueWrites; }
inline void DisableItem(MenuHandle, short) { ++toolbox_host::menuValueWrites; }
inline void CheckItem(MenuHandle, short, bool) { ++toolbox_host::menuValueWrites; }
inline void ClearMenuBar() { ++toolbox_host::menuClears; toolbox_host::installedMenus.clear(); }
void InitMenus();
inline void DrawMenuBar() { ++toolbox_host::menuDraws; }
inline void AppendResMenu(MenuHandle menu, unsigned long)
{
  menu->items.push_back("-");
  menu->items.push_back("Desk accessory");
}
inline void SetItemCmd(MenuHandle menu, short item, short cmd)
{
  const toolbox_host::ItemCmdCall call = {menu, item, cmd};
  toolbox_host::itemCmdCalls.push_back(call);
}
inline void SetItemMark(MenuHandle menu, short item, CharParameter mark)
{
  const toolbox_host::ItemCmdCall call = {menu, item, mark};
  toolbox_host::itemMarkCalls.push_back(call);
}
inline MenuHandle GetMenuHandle(short) { return 0; }
inline void GetMenuItemText(MenuHandle, short, unsigned char *text) { text[0] = 0; }
#endif
