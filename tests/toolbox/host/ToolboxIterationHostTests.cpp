#include "ToolboxIterationHost.hpp"
#include <cstdio>
#include <cstdlib>
#include "support/TestVerify.hpp"

static void expect(ToolboxApp &app, ToolboxIdleClock &clock, EventRecord event, const char *expected)
{
  iteration_host::trace.clear();
  app.iterate(clock, event.what != nullEvent, event);
  std::string actual;
  for (std::size_t i = 0; i < iteration_host::trace.size(); ++i)
  {
    if (i)
      actual += ",";
    actual += iteration_host::trace[i];
  }
  if (actual != expected)
  {
    std::fprintf(stderr, "iteration trace mismatch\nexpected: %s\nactual: %s\n", expected, actual.c_str());
    std::abort();
  }
  std::printf("%s\n", actual.c_str());
}
int main()
{
  ToolboxWindow window;
  AppComponentGroup group;
  group.components.push_back(&window);
  ToolboxApp app(group);
  app.active_ = &window;
  iteration_host::front = iteration_host::target = window.window();
  ToolboxIdleClock clock(0);
  EventRecord event = {nullEvent, 0, 0, {0, 0}, 0};
#define HEAD "operation,idle-foreground,hover,"
#define TAIL "tick,consume-idle,app-idle,present,cursor-idle,oom,end-operation"
  expect(app, clock, event, HEAD "tick,consume-idle,app-idle,present,cursor-idle,diagnostic-tail,oom,end-operation");
  event.what = keyDown;
  event.message = 'a';
  expect(app, clock, event, HEAD "script,offer-key,key,app-key," TAIL);
  event.modifiers = cmdKey;
  iteration_host::menuChoice = 0x10001;
  expect(app, clock, event, HEAD "script,menu-key,menu-command,hilite-menu," TAIL);
  event.what = autoKey;
  expect(app, clock, event, HEAD "script," TAIL);
  event.what = keyDown;
  event.modifiers = 0;
  event.message = 0x81;
  iteration_host::script = 1;
  expect(app, clock, event, HEAD "script,offer-key," TAIL);
  LOKA_VERIFY(app.twoByteKey_.holdsLead());
  event.what = updateEvt;
  event.message = reinterpret_cast<unsigned long>(window.window());
  expect(app, clock, event, HEAD "update-evidence,draw-update," TAIL);
  LOKA_VERIFY(app.twoByteKey_.holdsLead());
  event.what = keyDown;
  event.message = 0x40;
  expect(app, clock, event, HEAD "script,offer-key,beep," TAIL);
  LOKA_VERIFY(!app.twoByteKey_.holdsLead());
  event.what = mouseDown;
  expect(app, clock, event, HEAD "find-window,mouse,cursor-reconcile," TAIL);
  iteration_host::front = 0;
  expect(app, clock, event, HEAD "find-window,select-window," TAIL);
  iteration_host::part = inMenuBar;
  expect(app, clock, event, HEAD "find-window,menu-select,cursor-reconcile,menu-command,hilite-menu," TAIL);
  iteration_host::part = inGoAway;
  expect(app, clock, event, HEAD "find-window,track-go-away,close-asked,close-request," TAIL);
  event.what = activateEvt;
  event.message = reinterpret_cast<unsigned long>(window.window());
  expect(app, clock, event, HEAD "grow-damage," TAIL);
  event.modifiers = activeFlag;
  expect(app, clock, event, HEAD "set-active,grow-damage," TAIL);
  event.what = osEvt;
  event.modifiers = 0;
  event.message = 1UL << 24;
  expect(app, clock, event, "operation,cursor-reconcile,idle-background,hilite-off,grow-damage," TAIL);
  app.menuBarDrawDeferred_ = true;
  event.message |= resumeFlag;
  expect(
      app, clock, event, "operation,cursor-reconcile,idle-foreground,hover,hilite-on,grow-damage,draw-menu-bar," TAIL);
  LOKA_VERIFY(!app.menuBarDrawDeferred_);
  ToolboxIdleClock wrap(100);
  LOKA_VERIFY(wrap.elapsedSeconds(99) == 0.0);
  LOKA_VERIFY(wrap.elapsedSeconds(159) == 1.0);
  return 0;
}
