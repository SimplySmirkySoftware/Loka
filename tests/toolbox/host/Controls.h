#ifndef LOKA_HOST_CONTROLS_H
#define LOKA_HOST_CONTROLS_H
#include "TextEdit.h"
#include <string>
#include <vector>
struct HostControl { short value; };
typedef HostControl *ControlRef;
typedef short ControlPartCode;
typedef void (*ControlActionUPP)(ControlRef, ControlPartCode);
enum { kControlIndicatorPart = 129 };
namespace toolbox_host
{
  extern ControlRef hitControl;
  extern short trackedValue;
  extern short popupItem;
  extern unsigned tracks;
  Rect currentClip();
  struct ControlCalls
  {
    unsigned values, hilites, shows, draws;
    Rect valueClip, hiliteClip, showClip, drawClip;
    ControlCalls() : values(0), hilites(0), shows(0), draws(0),
        valueClip(), hiliteClip(), showClip(), drawClip() {}
  };
  extern ControlCalls controlCalls;
}
inline short GetControlValue(ControlRef c) { return c->value; }
inline void SetControlValue(ControlRef c, short value)
{
  ++toolbox_host::controlCalls.values;
  toolbox_host::controlCalls.valueClip = toolbox_host::currentClip();
  c->value = value;
}
inline void HiliteControl(ControlRef, short)
{
  ++toolbox_host::controlCalls.hilites;
  toolbox_host::controlCalls.hiliteClip = toolbox_host::currentClip();
}
inline void ShowControl(ControlRef)
{
  ++toolbox_host::controlCalls.shows;
  toolbox_host::controlCalls.showClip = toolbox_host::currentClip();
}
inline void Draw1Control(ControlRef)
{
  ++toolbox_host::controlCalls.draws;
  toolbox_host::controlCalls.drawClip = toolbox_host::currentClip();
}
inline void HideControl(ControlRef) {}
inline short FindControl(Point, GrafPtr, ControlRef *out)
{ *out = toolbox_host::hitControl; return *out ? kControlIndicatorPart : 0; }
inline short TrackControl(ControlRef c, Point, ControlActionUPP)
{ ++toolbox_host::tracks; c->value = toolbox_host::trackedValue; return kControlIndicatorPart; }
#ifdef LOKA_HOST_CONTROL_WIDTH
namespace toolbox_host { extern Rect controlRect; extern std::vector<std::string> controlTitles; }
struct HostQD { int gray; };
extern HostQD qd;
inline void PenPat(const int *) {}
inline void SetControlTitle(ControlRef, const unsigned char *text)
{ toolbox_host::controlTitles.push_back(std::string(reinterpret_cast<const char *>(text + 1), text[0])); }
inline void LineTo(short, short) {}
#endif
/** Native call ledger: Pascal payloads are copied at the OS boundary. */
struct HostMenu { std::vector<std::string> items; };
typedef HostMenu *MenuHandle;
namespace toolbox_host
{
  extern std::vector<std::string> menuTitles, menuAppends, menuInserts, menuSets, disposedMenuItems;
}
inline std::string HostPascalBytes(const unsigned char *text)
{ return std::string(reinterpret_cast<const char *>(text + 1), text[0]); }
inline MenuHandle NewMenu(short, const unsigned char *title)
{ toolbox_host::menuTitles.push_back(HostPascalBytes(title)); return new HostMenu; }
inline void AppendMenu(MenuHandle menu, const unsigned char *text)
{
  const std::string bytes = HostPascalBytes(text);
  toolbox_host::menuAppends.push_back(bytes);
  // Classic AppendMenu treats semicolons and Return as item separators.
  std::size_t start = 0;
  for (std::size_t i = 0; i <= bytes.size(); ++i)
    if (i == bytes.size() || bytes[i] == ';' || bytes[i] == '\r')
    { menu->items.push_back(bytes.substr(start, i - start)); start = i + 1; }
}
inline void InsertMenuItem(MenuHandle menu, const unsigned char *text, short after)
{
  toolbox_host::menuInserts.push_back(HostPascalBytes(text));
  menu->items.insert(menu->items.begin() + after, HostPascalBytes(text));
}
#ifdef LOKA_HOST_MENU_ATTACHMENT
namespace toolbox_host { extern void (*afterMenuSet)(); }
#endif
inline void SetMenuItemText(MenuHandle menu, short item, const unsigned char *text)
{
  toolbox_host::menuSets.push_back(HostPascalBytes(text));
#ifdef LOKA_HOST_MENU_ATTACHMENT
  if (toolbox_host::afterMenuSet) toolbox_host::afterMenuSet();
#endif
  if (item > 0 && static_cast<std::size_t>(item) <= menu->items.size())
    menu->items[item - 1] = HostPascalBytes(text);
}
#ifdef LOKA_HOST_MENU_ATTACHMENT
namespace toolbox_host { extern std::vector<MenuHandle> installedMenus; extern unsigned menuDisposes; }
inline void InsertMenu(MenuHandle menu, short) { toolbox_host::installedMenus.push_back(menu); }
#else
inline void InsertMenu(MenuHandle, short) {}
#endif
inline void LocalToGlobal(Point *) {}
inline long PopUpMenuSelect(MenuHandle, short, short, short) { return toolbox_host::popupItem; }
inline void DeleteMenu(short) {}
inline void DisposeMenu(MenuHandle menu)
{
#ifdef LOKA_HOST_MENU_ATTACHMENT
  ++toolbox_host::menuDisposes;
#endif
  toolbox_host::disposedMenuItems = menu->items; delete menu;
}
#endif
