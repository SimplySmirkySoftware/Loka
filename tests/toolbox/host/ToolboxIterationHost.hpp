#ifndef LOKA_TEST_TOOLBOX_ITERATION_HOST_HPP
#define LOKA_TEST_TOOLBOX_ITERATION_HOST_HPP
#include <cassert>
#include <string>
#include <vector>
#include "ToolboxActivationPhase.hpp"
#include "ToolboxTwoByteKey.hpp"
#include "ToolboxTabKey.hpp"
#include "ToolboxScrollKey.hpp"
#include "ToolboxIdleClock.hpp"

namespace iteration_host
{
  static std::vector<std::string> trace;
  static void note(const char *s)
  {
    trace.push_back(s);
  }
} // namespace iteration_host
using iteration_host::note;
struct Point
{
  short v, h;
};
struct Rect
{
  short top, left, bottom, right;
};
struct NativeWindow
{
};
typedef NativeWindow *WindowPtr;
typedef char *Ptr;
struct EventRecord
{
  unsigned short what;
  unsigned long message, when;
  Point where;
  unsigned short modifiers;
};
enum
{
  nullEvent = 0,
  mouseDown = 1,
  keyDown = 3,
  autoKey = 5,
  updateEvt = 6,
  activateEvt = 8,
  osEvt = 15,
  suspendResumeMessage = 1,
  resumeFlag = 1,
  activeFlag = 1,
  cmdKey = 256,
  shiftKey = 512,
  charCodeMask = 255,
  keyCodeMask = 65280,
  inMenuBar = 1,
  inContent = 3,
  inDrag = 4,
  inGrow = 5,
  inGoAway = 6,
  smRoman = 0,
  smKeyScript = 22,
  smFirstByte = 1,
  smLastByte = 2
};
namespace iteration_host
{
  static WindowPtr front = 0, target = 0;
  static short part = inContent, script = 0;
  static long menuChoice = 0;
  static unsigned long ticks = 60;
  static bool acceptKey = false;
} // namespace iteration_host
struct QD
{
  struct Bits
  {
    Rect bounds;
  } screenBits;
};
static QD qd;
static WindowPtr FrontWindow()
{
  return iteration_host::front;
}
static short FindWindow(Point, WindowPtr *out)
{
  note("find-window");
  *out = iteration_host::target;
  return iteration_host::part;
}
static long MenuSelect(Point)
{
  note("menu-select");
  return iteration_host::menuChoice;
}
static long MenuKey(char)
{
  note("menu-key");
  return iteration_host::menuChoice;
}
static void HiliteMenu(short)
{
  note("hilite-menu");
}
static bool TrackGoAway(WindowPtr, Point)
{
  note("track-go-away");
  return true;
}
static void SelectWindow(WindowPtr w)
{
  note("select-window");
  iteration_host::front = w;
}
static void DragWindow(WindowPtr, Point, Rect *)
{
  note("drag-window");
}
static void HiliteWindow(WindowPtr, bool on)
{
  note(on ? "hilite-on" : "hilite-off");
}
static void DrawMenuBar()
{
  note("draw-menu-bar");
}
static void SysBeep(short)
{
  note("beep");
}
static unsigned long TickCount()
{
  note("tick");
  return iteration_host::ticks;
}
static long GetScriptManagerVariable(short)
{
  note("script");
  return iteration_host::script;
}
static short CharacterByteType(Ptr p, short offset, short)
{
  return offset ? (static_cast<unsigned char>(p[1]) == 0x40 ? smLastByte : 0)
                : (static_cast<unsigned char>(p[0]) == 0x81 ? smFirstByte : 0);
}
namespace loka
{
  namespace core
  {
    class Operation
    {
    public:
      Operation()
      {
        note("operation");
      }
      ~Operation()
      {
        note("end-operation");
      }
    };
  } // namespace core
  namespace toolbox
  {
    inline void QuitIfOutOfMemoryReserveSpent()
    {
      note("oom");
    }
  } // namespace toolbox
} // namespace loka
class ToolboxWindow;
class Window
{
public:
  virtual ~Window() {}
  virtual ToolboxWindow *asToolboxWindow() = 0;
  Window *asWindow()
  {
    return this;
  }
};
class ToolboxScenePlatformController
{
public:
  void noteWindowUpdateEvtDraw()
  {
    note("update-evidence");
  }
};
class ToolboxWindow : public Window
{
  NativeWindow native_;
  ToolboxScenePlatformController controller_;

public:
  virtual ToolboxWindow *asToolboxWindow()
  {
    return this;
  }
  WindowPtr window()
  {
    return &this->native_;
  }
  ToolboxScenePlatformController *scenePlatformController()
  {
    return &this->controller_;
  }
  void idleControls(ActivationPhase phase)
  {
    note(phase == ACTIVATION_FOREGROUND ? "idle-foreground" : "idle-background");
  }
  void drawForUpdate()
  {
    note("draw-update");
  }
  void handleMouseDown(Point, const EventRecord &)
  {
    note("mouse");
  }
  void storeCurrentNativeContentFrame()
  {
    note("store-frame");
  }
  void handleGrow(Point)
  {
    note("grow");
  }
  void invalidateGrowIcon()
  {
    note("grow-damage");
  }
  bool offerKeyEvent(const EventRecord &)
  {
    note("offer-key");
    return false;
  }
  bool handleScrollKey(ToolboxScrollKey)
  {
    note("scroll-key");
    return true;
  }
  bool handleTabKey(ToolboxTabDirection)
  {
    note("tab-key");
    return true;
  }
  bool handleKeyDown(char)
  {
    note("key");
    return iteration_host::acceptKey;
  }
  void requestDeferredDebugDump()
  {
    note("request-diagnostic");
  }
  void flushDeferredDebugDump()
  {
    note("diagnostic-tail");
  }
};
class AppComponentGroup
{
public:
  typedef std::vector<Window *> Components;
  Components components;
  Components getComponents() const
  {
    return this->components;
  }
};
static ToolboxWindow *FindToolboxWindow(AppComponentGroup *group, WindowPtr target)
{
  if (!group || !target)
    return 0;
  const AppComponentGroup::Components components = group->getComponents();
  for (std::size_t i = 0; i < components.size(); ++i)
  {
    ToolboxWindow *w = components[i]->asToolboxWindow();
    if (w && w->window() == target)
      return w;
  }
  return 0;
}
class ToolboxApp
{
public:
  struct Cursor
  {
    void reconcile()
    {
      note("cursor-reconcile");
    }
    void assertIdle()
    {
      note("cursor-idle");
    }
  } cursorOwner_;
  AppComponentGroup *group_;
  Window *active_;
  ActivationPhase activationPhase_;
  bool menuBarDrawDeferred_;
  ToolboxTwoByteKey twoByteKey_;
  explicit ToolboxApp(AppComponentGroup &g)
      : group_(&g),
        active_(0),
        activationPhase_(ACTIVATION_FOREGROUND),
        menuBarDrawDeferred_(false)
  {
  }
  Window *activeWindow()
  {
    return this->active_;
  }
  void setActiveWindow(Window *w)
  {
    note("set-active");
    this->active_ = w;
  }
  void sampleHover(bool)
  {
    note("hover");
  }
  void handleMenuSelection(short, short)
  {
    note("menu-command");
  }
  bool closeAsked(Window *)
  {
    note("close-asked");
    return false;
  }
  void requestWindowClose(Window *)
  {
    note("close-request");
  }
  bool handleKeyPress(char)
  {
    note("app-key");
    return true;
  }
  bool consumeIdle(double elapsed, double &out)
  {
    note("consume-idle");
    out = elapsed;
    return true;
  }
  void handleIdle(double)
  {
    note("app-idle");
  }
  void present(ActivationPhase, loka::core::Operation &)
  {
    note("present");
  }
  void iterate(ToolboxIdleClock &, bool, const EventRecord &);
};
#include "ToolboxIteration.cpp"
#endif
