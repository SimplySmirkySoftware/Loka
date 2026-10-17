#ifndef LOKA_HOST_CONTROLS_H
#define LOKA_HOST_CONTROLS_H
#include "TextEdit.h"
#include <string>
#include <vector>
struct HostControl;
typedef HostControl *ControlRef;
typedef short ControlPartCode;
typedef void (*ControlActionUPP)(ControlRef, ControlPartCode);
#ifndef pascal
#define pascal
#endif
enum { kControlIndicatorPart = 129 };
namespace toolbox_host
{
  /** One gesture; callbacks are delivered only for non-thumb presses. */
  struct TrackScript
  {
    short press, release, finalValue;
    std::vector<short> callbacks;
    TrackScript(short part = kControlIndicatorPart, short up = kControlIndicatorPart, short value = 0)
        : press(part),
          release(up),
          finalValue(value)
    {
    }
  };
  struct ControlCall
  {
    std::string operation;
    GrafPtr currentPort;
    Rect currentClip, ownerClip;
    bool painted;
  };
  extern short popupItem;
  extern unsigned tracks;
  extern unsigned refuseControls;
  extern void (*beforeHide)(ControlRef);
  Rect currentClip();
  struct ControlCalls
  {
    unsigned values, hilites, shows, draws, hides;
    Rect valueClip, hiliteClip, showClip, drawClip;
    ControlRef lastHidden;
    ControlCalls()
        : values(0),
          hilites(0),
          shows(0),
          draws(0),
          hides(0),
          valueClip(),
          hiliteClip(),
          showClip(),
          drawClip(),
          lastHidden(0)
    {
    }
  };
  extern ControlCalls controlCalls;
}
/** The fake OS owns heap controls; window control lists borrow them. */
struct HostControl
{
  short value, minimum, maximum, hilite;
  GrafPtr owner;
  Rect rect;
  bool visible;
  bool &contrlVis;
  HostControl &operator*()
  {
    return *this;
  }
  toolbox_host::TrackScript tracking;
  std::vector<toolbox_host::ControlCall> calls;
  HostControl(short initial = 0)
      : value(initial),
        minimum(0),
        maximum(32767),
        hilite(0),
        owner(0),
        rect(),
        visible(false),
        contrlVis(visible)
  {
  }
};
ControlRef NewControl(GrafPtr, const Rect *, const unsigned char *, bool, short, short, short, short, long);
void DisposeControl(ControlRef);
void MoveControl(ControlRef, short, short);
void SizeControl(ControlRef, short, short);
void SetControlMinimum(ControlRef, short);
void SetControlMaximum(ControlRef, short);
inline short GetControlMinimum(ControlRef c)
{
  return c->minimum;
}
inline short GetControlMaximum(ControlRef c)
{
  return c->maximum;
}
inline short GetControlValue(ControlRef c)
{
  return c->value;
}
inline ControlActionUPP NewControlActionUPP(ControlActionUPP action)
{
  return action;
}
void SetControlValue(ControlRef, short);
void HiliteControl(ControlRef, short);
void ShowControl(ControlRef);
void Draw1Control(ControlRef);
void HideControl(ControlRef);
short FindControl(Point, GrafPtr, ControlRef *);
short TrackControl(ControlRef, Point, ControlActionUPP);
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
