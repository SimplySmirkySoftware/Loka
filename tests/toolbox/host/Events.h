#ifndef LOKA_HOST_EVENTS_H
#define LOKA_HOST_EVENTS_H
#include "TextEdit.h"
#include <vector>
enum
{
  nullEvent = 0,
  keyDown = 3,
  autoKey = 5
};
/** Host event value carried unchanged through the editor seat input door. */
struct EventRecord
{
  unsigned short what;
  unsigned long message;
  unsigned long when;
  Point where;
  unsigned short modifiers;
};
namespace toolbox_host
{
  /** Native-only mouse samples; no callback or event dispatch in tracking. */
  struct MouseScript
  {
    std::vector<Point> held;
    Point release;
    unsigned calls;
    MouseScript()
        : release(),
          calls(0)
    {
    }
  };
  extern MouseScript mouse;
  extern unsigned testControls;
  // Keep the unused native poll out of the DSL Button type's global lookup.
  bool Button();
} // namespace toolbox_host
bool WaitMouseUp();
void GetMouse(Point *);
#endif
