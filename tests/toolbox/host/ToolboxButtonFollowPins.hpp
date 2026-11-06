#include "ToolboxButtonFollow.hpp"
#include "ToolboxButtonMoveCapability.hpp"
#include "toolbox/ToolboxButtonFollowAccess.hpp"
#include <Gestalt.h>
namespace button_follow
{
  using namespace loka::testing;
  ToolboxPresentPlan planFor(const Rect &domain, int dv, const Rect *preserve = 0)
  {
    ToolboxPresentObservation observation;
    observation.noteViewportMove(domain, domain, 0, -dv, ToolboxViewportContentResult(), TOOLBOX_BASELINE_VALID);
    ToolboxPresentInputs inputs;
    if (preserve)
    {
      const ToolboxForegroundUnits::Entry entry = {*preserve, TOOLBOX_SCROLL_PRESERVE_DURING_SCROLL};
      inputs.foreground = ToolboxForegroundUnits(&entry, 1, Rect(), false);
    }
    observation.complete(preserve ? 1 : 0, inputs);
    return ToolboxPresentPlan(observation);
  }
  void gate()
  {
    const long versions[] = {0x0710, 0x0711, 0x0755, 0x0800, 0x0710, 0x0710, 0x0710};
    for (unsigned i = 0; i < 7; ++i)
    {
      ResetSilentButtonMoveCapability();
      toolbox_host::systemVersion = versions[i];
      toolbox_host::systemVersionError = i == 6 ? -1 : noErr;
      toolbox_host::appearanceError = i == 4 ? noErr : i == 5 ? -1 : gestaltUndefSelectorErr;
      toolbox_host::systemVersionQueries = toolbox_host::appearanceQueries = 0;
      LOKA_VERIFY(ToolboxCanSilentlyMoveButton() == (i == 0));
      const unsigned queries = toolbox_host::appearanceQueries;
      for (unsigned repeat = 0; repeat < 5; ++repeat)
        LOKA_VERIFY(ToolboxCanSilentlyMoveButton() == (i == 0));
      LOKA_VERIFY(toolbox_host::systemVersionQueries == 1 && toolbox_host::appearanceQueries == queries);
    }
  }
  struct Observe : SilentButtonMoveObserver
  {
    unsigned visits;
    Rect oldRect;
    Observe()
        : visits(0),
          oldRect()
    {
    }
    virtual void before(ControlRef control)
    {
      LOKA_VERIFY(control->contrlVis == 0x7f);
      this->oldRect = control->rect;
    }
    virtual void after(ControlRef control)
    {
      LOKA_VERIFY(control->contrlVis == 0x7f);
      LOKA_VERIFY(!EqualRect(&this->oldRect, &control->rect));
      ++this->visits;
    }
  };
  class RefuseCollection : public loka::platform::String
  {
  public:
    mutable unsigned reads;
    const unsigned refusal;
    explicit RefuseCollection(unsigned n)
        : reads(0),
          refusal(n)
    {
    }
    virtual bool appendUtf8(std::string &out) const
    {
      if (++this->reads == this->refusal)
        return false;
      out += "Different";
      return true;
    }
  };
  void face()
  {
    ToolboxWindow window;
    HostPortScope selected(window.window());
    const Rect rect = {10, 10, 30, 110};
    Str255 title = {0};
    ControlRef control = NewControl(window.window(), &rect, title, true, 0, 0, 1, 0, 0);
    std::string installed = "Same";
    MutableState<bool> enabled(true);
    for (unsigned i = 0; i < 6; ++i)
    {
      installed = "Same";
      control->hilite = i == 2 ? 255 : i == 3 ? 1 : 0;
      String label = i == 1 ? String::Literal("Different") : String::Literal("Same");
      if (i >= 4)
        label = String::FromPlatform(Managed<loka::platform::String>::Wrap(new RefuseCollection(i - 3)));
      control->calls.clear();
      bool changed = false;
      const bool result = ReconcileToolboxButtonControl(control, label, &enabled, installed, &changed);
      LOKA_VERIFY(result == (i < 4));
      LOKA_VERIFY(changed == (i > 0 && i < 4));
      if (i >= 4)
        LOKA_VERIFY(control->calls.empty() && installed == "Same" && control->hilite == 0);
      else
      {
        LOKA_VERIFY(control->calls.back().operation == "hilite");
        LOKA_VERIFY(control->calls.size() == (i == 1 ? 2u : 1u));
        if (i == 1)
          LOKA_VERIFY(control->calls[0].operation == "title" && installed == "Different");
      }
    }
    bool changed = true;
    LOKA_VERIFY(!ReconcileToolboxButtonControl(0, String::Literal("Same"), &enabled, installed, &changed) && changed);
    DisposeControl(control);
  }
  void run(const char *mode)
  {
    if (std::strcmp(mode, "FollowFace") == 0)
    {
      face();
      return;
    }
    if (std::strcmp(mode, "FollowGate") == 0)
    {
      gate();
      return;
    }
    // Every case exercises the production ensure branch, including its pure predicate.
    const std::string kind(mode + 6);
    ToolboxWindow window;
    HostPortScope selected(window.window());
    ToolboxScenePlatformController controller(&window);
    ButtonNode node(ButtonProps().text("Follow"));
    ToolboxButtonContext context(&node, &controller);
    const short id = controller.allocateControlId();
    Rect before = {70, 30, 80, 100};
    int dv = kind == "Down" ? 16 : -16;
    if (kind == "Edge")
      before.top = 16, before.bottom = 26;
    Rect after = before;
    OffsetRect(&after, kind == "Horizontal" ? 1 : 0, dv);
    Rect domain = {0, 0, 240, 360};
    Rect preserve = {70, 100, 80, 150}; // Touching is safe, overlapping is not.
    if (kind == "Below")
      preserve.top = 40, preserve.bottom = 54;
    if (kind == "Preserve")
      preserve = after;
    if (kind == "Domain")
      domain.top = 55;
    if (kind == "Translation")
      ++after.bottom;
    const ToolboxPresentPlan plan = planFor(domain, dv, &preserve);
    const ToolboxBandPaintPolicy policy =
        ToolboxBandPaintPolicy::resolve(window.window(), controller.unitLedger_.firstBlurUnits(), controller.bandBlur_);
    const ToolboxPaintPassFacts facts(policy, kind == "Proof" ? 0 : &plan);
    ResetSilentButtonMoveCapability();
    toolbox_host::systemVersion = kind == "Closed" ? 0x0755 : 0x0710;
    toolbox_host::systemVersionError = noErr;
    toolbox_host::appearanceError = gestaltUndefSelectorErr;
    MutableState<bool> enabled(kind != "Disabled");
    String label = String::Literal("Follow");
    ToolboxScenePlatformController::ButtonControlBinding *binding = 0;
    bool painted;
    LOKA_VERIFY(controller.ensureButtonControl(
        id, before, label, 0, &enabled, NATIVE_HINT_DEFAULT, &context, binding, &painted));
    ControlRef control = binding->control;
    control->contrlVis = kind == "Hidden" ? 0 : 0x7f;
    if (kind == "NativeRect")
      --control->rect.top;
    if (kind == "OldClip")
    {
      Rect clipped = before;
      ++clipped.top;
      binding->placement = ToolboxNativePlacement(before, clipped);
    }
    if (kind == "NewClip")
      controller.projectionClip.top = after.top + 1;
    if (kind == "Label" || kind == "NoScratch")
      label = String::Literal("Changed");
    if (kind == "Hilite")
      control->hilite = 255;
    if (kind == "Transient")
      control->hilite = 1;
    if (kind == "Collection")
      label = String::FromPlatform(Managed<loka::platform::String>::Wrap(new RefusingUtf8()));
    const bool follow = kind == "Up" || kind == "Down" || kind == "Edge" || kind == "Below" || kind == "Disabled";
    const bool changed = kind == "Label" || kind == "NoScratch" || kind == "Hilite" || kind == "Transient";
    control->calls.clear();
    toolbox_host::invalidatedRects.clear();
    window.port.native.update.clear();
    const int draws = controller.debugStats_.totalControlDrawCount;
    Rect empty = Rect();
    ClipRect(&empty); // Changed-face debt must still name the new rectangle.
    const unsigned validations = toolbox_host::validations;
    const RgnHandle scratch = controller.paintSuppressClipRgn_;
    if (kind == "NoScratch")
      controller.paintSuppressClipRgn_ = 0;
    Observe observer;
    {
      SilentButtonMoveScope observe(observer);
      ToolboxScenePlatformController::PaintPassScope scope(controller, facts);
      LOKA_VERIFY(controller.ensureButtonControl(
                      id, after, label, 0, &enabled, NATIVE_HINT_DEFAULT, &context, binding, &painted)
                  == (kind != "Collection"));
    }
    controller.paintSuppressClipRgn_ = scratch;
    if (kind == "NoScratch")
      LOKA_VERIFY(controller.observation_.outsideRefusal().reason == TOOLBOX_PAINT_REFUSAL_CLIP_ALLOCATION);
    LOKA_VERIFY(observer.visits == (follow ? 1u : 0u));
    LOKA_VERIFY(painted == !follow);
    LOKA_VERIFY(controller.debugStats_.totalControlDrawCount == draws + (follow ? 0 : 1));
    LOKA_VERIFY(EqualRect(&binding->placement.full(), &after) && binding->usedThisFrame);
    if (follow)
    {
      LOKA_VERIFY(control->calls.size() == 1 && control->calls[0].operation == "move");
      LOKA_VERIFY(!control->calls[0].painted && control->calls[0].visibility == 0);
      LOKA_VERIFY(window.port.native.update.empty() && toolbox_host::validations == validations);
      // The same real repaint door repairs a partial or a full button intersection.
      context.updateData(label, 0, &enabled, id, 0);
      context.updateRect(after);
      for (unsigned full = 0; full < 2; ++full)
      {
        Rect repair = after;
        if (!full)
          --repair.bottom;
        ClipRect(&repair);
        const unsigned repaints = toolbox_host::controlCalls.draws;
        context.repaint(control, binding->label);
        LOKA_VERIFY(toolbox_host::controlCalls.draws == repaints + 1);
      }
    }
    else
    {
      LOKA_VERIFY(control->calls[0].operation == "hide" && control->calls[1].operation == "move");
      LOKA_VERIFY(control->calls.back().operation == "show");
      if (kind != "Collection")
        LOKA_VERIFY(control->calls[control->calls.size() - 2].operation == "hilite");
    }
    if (changed)
    {
      LOKA_VERIFY(!toolbox_host::invalidatedRects.empty());
      LOKA_VERIFY(EqualRect(&toolbox_host::invalidatedRects.back(), &after));
    }
    controller.clearControls();
  }
} // namespace button_follow
