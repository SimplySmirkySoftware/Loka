#include "ToolboxHost.hpp"
#include "ToolboxInputDoor.hpp"
#include "ToolboxScrollViewDecisions.hpp"
#include "support/TestVerify.hpp"
#include <cstring>
#include <cstdio>
using namespace loka::app;
using namespace loka::app::scene;
using namespace loka::core;
namespace
{
  const Rect barRect = {0, 0, 120, 16};
  const Point center = {60, 8};
  struct Changes
  {
    unsigned count;
    Changes()
        : count(0)
    {
    }
    static void changed(void *data)
    {
      ++static_cast<Changes *>(data)->count;
    }
  };
  struct Fixture
  {
    ToolboxWindow window;
    HostPortScope port;
    ToolboxScenePlatformController controller;
    PushStateTracker tracker;
    MutableState<int> value;
    NodeState<int> seat;
    EmitterState change;
    Changes notices;
    Fixture()
        : port(window.window()),
          controller(&window),
          value(10),
          seat(&value, &tracker)
    {
      tracker.addState(&value);
      change.bind(&Changes::changed, &notices, false);
      LOKA_VERIFY(controller.ensureScrollBarControl(
          controller.allocateControlId(),
          barRect,
          ScrollBarProps().value(seat).range(0, 100).lineStep(2).pageStep(15).onChange(&change),
          NATIVE_HINT_DEFAULT));
    }
    ~Fixture()
    {
      change.unbind(&Changes::changed, &notices);
    }
    ControlRef control()
    {
      return controller.scrollControl(0).control;
    }
  };
  void appGesture(const char *which)
  {
    Fixture f;
    ControlRef c = f.control();
    LOKA_VERIFY(c->owner == f.window.window() && c->visible && c->hilite == 0);
    LOKA_VERIFY(c->minimum == 0 && c->maximum == 100 && c->value == 10);
    LOKA_VERIFY(EqualRect(&c->rect, &barRect));
    const bool page = std::strcmp(which, "app-page") == 0;
    const bool off = std::strcmp(which, "app-off-arrow") == 0;
    c->tracking = toolbox_host::TrackScript(page ? 23 : off ? 21 : 129, page ? 23 : 0, 90);
    c->tracking.callbacks.assign(3, page ? 23 : 21);
    LOKA_VERIFY(f.controller.handleControlClick(center));
    const int expected = page ? 55 : off ? 16 : 10;
    LOKA_VERIFY(f.value.get() == expected && c->value == expected);
    LOKA_VERIFY(f.notices.count == (page || off ? 1u : 0u));
    LOKA_VERIFY(f.controller.pendingDirtyRects_.size() == (page || off ? 1u : 0u));
  }
  void nativeHit()
  {
    Fixture f;
    ControlRef c = f.control(), found = 0;
    LOKA_VERIFY(FindControl(center, f.window.window(), &found) == 129 && found == c);
    const Point outside = {60, 20};
    LOKA_VERIFY(FindControl(outside, f.window.window(), &found) == 0 && !found);
    Str255 title = {0};
    ControlRef front = NewControl(f.window.window(), &barRect, title, true, 0, 0, 100, 16, 0);
    LOKA_VERIFY(FindControl(center, f.window.window(), &found) == 129 && found == front);
    DisposeControl(front);
    LOKA_VERIFY(FindControl(center, f.window.window(), &found) == 129 && found == c);
    // System 7 MAME maciix probe, 2026-10-17: hilite 255 and invisible
    // controls return part 0 and NULL, so the inactive swallowing branch is unreachable.
    LOKA_VERIFY(f.controller.ensureScrollBarControl(f.controller.scrollControl(0).resourceId,
                                                    barRect,
                                                    ScrollBarProps().value(f.seat).range(10, 10),
                                                    NATIVE_HINT_DEFAULT));
    LOKA_VERIFY(!f.controller.scrollControl(0).active && c->hilite == 255);
    const unsigned before = toolbox_host::tracks;
    LOKA_VERIFY(FindControl(center, f.window.window(), &found) == 0 && !found);
    LOKA_VERIFY(!f.controller.handleControlClick(center));
    LOKA_VERIFY(f.controller.ensureScrollBarControl(f.controller.scrollControl(0).resourceId,
                                                    barRect,
                                                    ScrollBarProps().value(f.seat).range(0, 100),
                                                    NATIVE_HINT_DEFAULT));
    HideControl(c);
    LOKA_VERIFY(FindControl(center, f.window.window(), &found) == 0 && !found);
    LOKA_VERIFY(!f.controller.handleControlClick(center));
    LOKA_VERIFY(toolbox_host::tracks == before && f.notices.count == 0);
  }
  void thumbAction(ControlRef c, ControlPartCode)
  {
    SetControlValue(c, 99);
  }
  void setters()
  {
    Fixture f;
    ControlRef c = f.control();
    c->calls.clear();
    SetControlMinimum(c, 20);
    LOKA_VERIFY(GetControlMinimum(c) == 20 && GetControlValue(c) == 20);
    SetControlMaximum(c, 60);
    SetControlValue(c, 90);
    LOKA_VERIFY(GetControlMaximum(c) == 60 && GetControlValue(c) == 60);
    SetControlValue(c, 0);
    LOKA_VERIFY(GetControlValue(c) == 20);
    MoveControl(c, 30, 40);
    SizeControl(c, 18, 90);
    const Rect moved = {40, 30, 130, 48};
    LOKA_VERIFY(EqualRect(&c->rect, &moved));
    const char *operations[] = {"minimum", "maximum", "value", "value", "move", "size"};
    LOKA_VERIFY(c->calls.size() == 6);
    for (unsigned i = 0; i < 6; ++i)
    {
      LOKA_VERIFY(c->calls[i].operation == operations[i]);
      LOKA_VERIFY(c->calls[i].currentPort == f.window.window() && c->calls[i].painted);
    }
    c->tracking = toolbox_host::TrackScript(129, 129, 40);
    c->tracking.callbacks.assign(3, 23);
    const std::size_t before = c->calls.size();
    LOKA_VERIFY(TrackControl(c, center, thumbAction) == 129);
    LOKA_VERIFY(c->value == 40 && c->calls.size() == before + 1);
    c->tracking.release = 0;
    c->tracking.finalValue = 60;
    LOKA_VERIFY(TrackControl(c, center, thumbAction) == 0 && c->value == 40);
  }
  void lifetime()
  {
    Fixture f;
    const short id = f.controller.scrollControl(0).resourceId;
    ControlRef original = f.control();
    f.controller.destroyScrollBarControl(id, NATIVE_HINT_DEFAULT);
    LOKA_VERIFY(!original->visible && f.controller.scrollControls().empty());
    LOKA_VERIFY(f.controller.retiredScrollBarControls_.size() == 1);
    LOKA_VERIFY(f.controller.retiredScrollBarControls_[0].handle == original);
    LOKA_VERIFY(f.window.port.native.controls.size() == 1);
    const short reused = f.controller.allocateControlId();
    LOKA_VERIFY(reused == id && f.controller.allocateControlId() == id + 1);
    f.controller.flushControls();
    LOKA_VERIFY(f.controller.retiredScrollBarControls_.empty());
    LOKA_VERIFY(f.controller.ensureScrollBarControl(
        reused, barRect, ScrollBarProps().value(f.seat).range(0, 200), NATIVE_HINT_DEFAULT));
    LOKA_VERIFY(f.control() == original && f.control()->maximum == 200 && f.control()->visible);
    f.controller.destroyScrollBarControl(reused, NATIVE_HINT_EAGER_RELEASE);
    f.controller.flushControls();
    LOKA_VERIFY(f.window.port.native.controls.empty());
  }
  void clearControls()
  {
    Fixture f;
    ScrollViewNode scroll((ScrollViewProps()));
    const Rect viewport = {20, 20, 100, 180};
    f.controller.ensureViewportScrollBarControl(viewport, &scroll, 300, 64, 0, ToolboxScrollViewMetrics(50, 0), true);
    LOKA_VERIFY(f.controller.scrollControls().size() == 3);
    const short vertical = f.controller.viewportScroll().barId[SCROLL_BAR_VERTICAL];
    const short horizontal = f.controller.viewportScroll().barId[SCROLL_BAR_HORIZONTAL];
    f.controller.clearControls();
    LOKA_VERIFY(f.controller.scrollControls().empty());
    LOKA_VERIFY(f.controller.retiredScrollBarControls_.size() == 3);
    for (std::size_t i = 0; i < f.window.port.native.controls.size(); ++i)
      LOKA_VERIFY(!f.window.port.native.controls[i]->visible);
    LOKA_VERIFY(f.controller.allocateControlId() == horizontal);
    LOKA_VERIFY(f.controller.allocateControlId() == vertical);
    // The app id remains its context's responsibility; bulk clear releases only viewport ids.
    LOKA_VERIFY(f.controller.allocateControlId() == horizontal + 1);
    f.controller.clearControls();
    LOKA_VERIFY(f.controller.retiredScrollBarControls_.size() == 3);
  }
  void viewportGesture(const char *which)
  {
    ToolboxWindow window;
    HostPortScope port(window.window());
    ToolboxScenePlatformController controller(&window);
    PushStateTracker tracker;
    MutableState<Frame> value(Frame(7, 8, 90, 80));
    tracker.addState(&value);
    NodeState<Frame> seat(&value, &tracker);
    ScrollViewNode scroll(ScrollViewProps().viewport(seat).contentInsets(ScrollViewContentInsets::of(12, 3)));
    const Rect viewport = {0, 0, 80, 100};
    controller.ensureViewportScrollBarControl(
        viewport, &scroll, 300 + 12 + 3, 80, scroll.requestedOrigin().y, ToolboxScrollViewMetrics(0, 0), false);
    ControlRef c = controller.scrollControl(0).control;
    LOKA_VERIFY(c->value == 20 && c->maximum == 235);
    Changes notices;
    value.bind(&Changes::changed, &notices, false);
    const bool page = std::strcmp(which, "viewport-page") == 0;
    const bool off = std::strcmp(which, "viewport-off-arrow") == 0;
    c->tracking = toolbox_host::TrackScript(page ? 23 : off ? 21 : 129, page ? 23 : 0, 90);
    c->tracking.callbacks.assign(2, page ? 23 : 21);
    const Point point = {40, 90};
    LOKA_VERIFY(controller.handleControlClick(point));
    LOKA_VERIFY(value.get().y == (page ? 166 : off ? 10 : 8));
    LOKA_VERIFY(value.get().x == 7 && value.get().width == 90 && value.get().height == 80);
    LOKA_VERIFY(notices.count == (page || off ? 1u : 0u));
    LOKA_VERIFY(controller.pendingDirtyRects_.size() == (page || off ? 1u : 0u));
    value.unbind(&Changes::changed, &notices);
    controller.ensureViewportScrollBarControl(viewport, &scroll, 80, 80, 0, ToolboxScrollViewMetrics(0, 0), false);
    LOKA_VERIFY(!controller.scrollControl(0).active && c->hilite == 255);
    // The same System 7 MAME probe applies to viewport-owned bars.
    LOKA_VERIFY(!controller.handleControlClick(point));
    controller.ensureViewportScrollBarControl(viewport, &scroll, 315, 80, 20, ToolboxScrollViewMetrics(0, 0), false);
    HideControl(c);
    LOKA_VERIFY(!controller.handleControlClick(point));
  }
  void ports()
  {
    Fixture f;
    ControlRef c = f.control();
    ToolboxWindow other;
    HostPortScope otherPort(other.window());
    const Rect empty = {0, 0, 0, 0};
    ClipRect(&empty);
    SetControlValue(c, 12);
    const toolbox_host::ControlCall &call = c->calls.back();
    LOKA_VERIFY(call.currentPort == other.window() && EmptyRect(&call.currentClip));
    LOKA_VERIFY(!EmptyRect(&call.ownerClip) && call.painted);
    RgnHandle scratch = NewRgn();
    GetClip(scratch);
    LOKA_VERIFY(EmptyRect(&(**scratch).rgnBBox));
    SetPort(f.window.window());
    SetClip(scratch);
    SetControlValue(c, 14);
    LOKA_VERIFY(!c->calls.back().painted && EmptyRect(&c->calls.back().ownerClip));
    f.window.port.native.update.clear();
    HideControl(c);
    LOKA_VERIFY(f.window.port.native.update.size() == 1);
    LOKA_VERIFY(EqualRect(&f.window.port.native.update[0], &barRect));
    LOKA_VERIFY(other.port.native.update.empty());
    HideControl(c);
    LOKA_VERIFY(f.window.port.native.update.size() == 1);
    DisposeRgn(scratch);
  }
  void teMovement()
  {
    ToolboxWindow window;
    ToolboxEditTextContext context;
    ToolboxScenePlatformController controller(&window);
    MutableState<String> text(String("abcd"));
    TEHandle te = controller.ensureEditTextControl(&context, barRect, &text, NATIVE_HINT_DEFAULT).te;
    LOKA_VERIFY(te);
    controller.activateEditControl(0);
    ToolboxInputDoor::idleTextEdits(controller);
    LOKA_VERIFY((**te).idleCalls == 1 && EqualRect(&(**te).destRect, &barRect));
    const HostTECompletion key = {{-10, 0, 110, 16}, 2, 2};
    const HostTECompletion click = {{-40, 0, 80, 16}, 0, 4};
    const HostTECompletion idle = {{-50, 0, 70, 16}, 0, 4};
    (**te).keyCompletions.push_back(key);
    (**te).clickCompletions.push_back(click);
    (**te).idleCompletions.push_back(idle);
    TEKey(29, te);
    LOKA_VERIFY(EqualRect(&(**te).destRect, &key.destRect) && (**te).selStart == 2);
    (**te).keyCompletions.push_back(key);
    TEKey('x', te);
    LOKA_VERIFY(EqualRect(&(**te).destRect, &key.destRect) && (**te).selStart == 2);
    TEClick(center, true, te);
    LOKA_VERIFY(EqualRect(&(**te).destRect, &click.destRect) && (**te).selStart == 0 && (**te).selEnd == 4);
    ToolboxInputDoor::idleTextEdits(controller);
    LOKA_VERIFY(EqualRect(&(**te).destRect, &idle.destRect) && (**te).idleCalls == 2);
    (**te).lineHeight = 9;
    (**te).nLines = 2;
    (**te).lineStarts[0] = 0;
    (**te).lineStarts[1] = 2;
    (**te).lineStarts[2] = 4;
    ToolboxInputDoor::idleTextEdits(controller);
    LOKA_VERIFY((**te).lineHeight == 9 && (**te).nLines == 2 && (**te).lineStarts[1] == 2);
    controller.activateEditControl(controller.editControls_.size());
    ToolboxInputDoor::idleTextEdits(controller);
    LOKA_VERIFY((**te).idleCalls == 3);
  }
} // namespace
int main(int argc, char **argv)
{
  LOKA_VERIFY(argc == 2);
  std::printf("[pin] %s\n", argv[1]);
  if (std::strncmp(argv[1], "app-", 4) == 0)
    appGesture(argv[1]);
  else if (std::strncmp(argv[1], "viewport-", 9) == 0)
    viewportGesture(argv[1]);
  else if (std::strcmp(argv[1], "native-hit") == 0)
    nativeHit();
  else if (std::strcmp(argv[1], "setters") == 0)
    setters();
  else if (std::strcmp(argv[1], "lifetime") == 0)
    lifetime();
  else if (std::strcmp(argv[1], "clear") == 0)
    clearControls();
  else if (std::strcmp(argv[1], "ports") == 0)
    ports();
  else if (std::strcmp(argv[1], "te-movement") == 0)
    teMovement();
  else
    LOKA_VERIFY(false);
  return 0;
}
