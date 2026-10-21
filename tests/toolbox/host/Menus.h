#ifndef LOKA_HOST_MENUS_H
#define LOKA_HOST_MENUS_H
#include "Controls.h"
namespace toolbox_host
{
  extern std::vector<std::string> resourceMenuItems;
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
  extern std::vector<ItemCmdCall> itemCmdCalls, itemMarkCalls, enabledCalls, checkedCalls;
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
inline void EnableItem(MenuHandle menu, short item)
{
  ++toolbox_host::menuValueWrites;
  const toolbox_host::ItemCmdCall call = {menu, item, 1};
  toolbox_host::enabledCalls.push_back(call);
}
inline void DisableItem(MenuHandle menu, short item)
{
  ++toolbox_host::menuValueWrites;
  const toolbox_host::ItemCmdCall call = {menu, item, 0};
  toolbox_host::enabledCalls.push_back(call);
}
inline void CheckItem(MenuHandle menu, short item, bool checked)
{
  ++toolbox_host::menuValueWrites;
  const toolbox_host::ItemCmdCall call = {menu, item, static_cast<short>(checked)};
  toolbox_host::checkedCalls.push_back(call);
}
inline void ClearMenuBar() { ++toolbox_host::menuClears; toolbox_host::installedMenus.clear(); toolbox_host::menuColorEntries = 0; }
typedef int *MCTableHandle;
inline MCTableHandle GetMCInfo() { return new int(toolbox_host::menuColorEntries); }
inline void SetMCInfo(MCTableHandle table) { toolbox_host::menuColorEntries = *table; }
inline void DisposeMCInfo(MCTableHandle table) { delete table; }
void InitMenus();
inline void DrawMenuBar() { ++toolbox_host::menuDraws; }
inline void AppendResMenu(MenuHandle menu, unsigned long)
{
  menu->items.insert(menu->items.end(), toolbox_host::resourceMenuItems.begin(), toolbox_host::resourceMenuItems.end());
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
inline MenuHandle GetMenuHandle(short id)
{
  for (size_t i = 0; i < toolbox_host::installedMenus.size(); ++i)
    if (toolbox_host::installedMenus[i]->id == id) return toolbox_host::installedMenus[i];
  return 0;
}
inline void GetMenuItemText(MenuHandle menu, short item, unsigned char *text)
{
  const std::string &value = menu->items[item - 1];
  text[0] = static_cast<unsigned char>(value.size());
  for (size_t i = 0; i < value.size(); ++i) text[i + 1] = value[i];
}
#endif
