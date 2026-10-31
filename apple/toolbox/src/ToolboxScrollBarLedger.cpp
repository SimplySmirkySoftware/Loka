#include "ToolboxScrollBarLedger.hpp"
#include "ToolboxPaintSuppressScope.hpp"
#include "ToolboxScenePlatformController.hpp"
#include "ToolboxScrollViewDecisions.hpp"
#include "ToolboxWindow.hpp"
#include "app/nodes/controls/ScrollBar.hpp"
#include "app/nodes/nestable/ScrollView.hpp"
#include "core/util/StateTrackerGuard.hpp"
#include <Controls.h>

namespace
{
  // The CDEF id and its part codes live in ControlDefinitions.h, which this
  // toolchain's Controls.h does not pull in. Same Universal Interfaces values.
#if !defined(scrollBarProc) && !defined(LOKA_TOOLBOX_MULTIVERSAL_INTERFACES)
  enum
  {
    scrollBarProc = 16
  };
#endif
#if !defined(kControlUpButtonPart)
  enum
  {
    kControlUpButtonPart = 20,
    kControlDownButtonPart = 21,
    kControlPageUpPart = 22,
    kControlPageDownPart = 23
  };
#endif
} // namespace

namespace
{
  // The action proc runs inside TrackControl and has no user-data slot, so
  // the step sizes of the bar being tracked are parked here for the duration
  // of the loop. Classic is single-threaded and TrackControl does not nest,
  // so exactly one bar is ever being tracked.
  int gActiveScrollBarLineStep = 1;
  int gActiveScrollBarPageStep = 1;
  ControlActionUPP gScrollBarActionUPP = 0;

  static pascal void ScrollBarActionProc(ControlRef control, ControlPartCode part)
  {
    if (!control)
    {
      return;
    }
    const int minimum = static_cast<int>(GetControlMinimum(control));
    const int maximum = static_cast<int>(GetControlMaximum(control));
    int value = static_cast<int>(GetControlValue(control));
    switch (part)
    {
    case kControlUpButtonPart:
      value -= gActiveScrollBarLineStep;
      break;
    case kControlDownButtonPart:
      value += gActiveScrollBarLineStep;
      break;
    case kControlPageUpPart:
      value -= gActiveScrollBarPageStep;
      break;
    case kControlPageDownPart:
      value += gActiveScrollBarPageStep;
      break;
    default:
      return;
    }
    value = loka::app::ScrollBarClampValue(value, minimum, maximum);
    // Visual only. Nothing crosses into Loka from inside the loop: a held
    // arrow must not publish one value per tick (ruling 1).
    SetControlValue(control, static_cast<short>(value));
  }

  ControlActionUPP ScrollBarActionUPP()
  {
    if (!gScrollBarActionUPP)
    {
      gScrollBarActionUPP = NewControlActionUPP(ScrollBarActionProc);
    }
    return gScrollBarActionUPP;
  }
} // namespace

ToolboxOwnControlMoveFootprint ToolboxScenePlatformController::admitOwnControlMove(const Rect &before,
                                                                                   const Rect &after) const
{
  // MoveControl places the old size at the new origin before SizeControl runs.
  Rect areas[3] = {before,
                   after,
                   {after.top,
                    after.left,
                    static_cast<short>(after.top + before.bottom - before.top),
                    static_cast<short>(after.left + before.right - before.left)}};
  if (!this->window_ || !this->window_->window())
    return ToolboxOwnControlMoveFootprint();
  const Rect portRect = this->window_->window()->portRect;
  for (unsigned i = 0; i < 3; ++i)
  {
    Rect clipped;
    // Only the in-port intersection needs capture evidence. Keep the whole
    // rectangle when admitted: the Control Manager also invalidates its
    // off-port part, which cannot contain visible damage of this window.
    // An empty intersection is pure off-window damage, safe even without a
    // captured pass; admitsCapture deliberately refuses empty rectangles.
    if (SectRect(&areas[i], &portRect, &clipped) && !this->erasedPass_.admitsCapture(clipped))
      SetRect(&areas[i], 0, 0, 0, 0);
  }
  return ToolboxOwnControlMoveFootprint(areas[0], areas[1], areas[2]);
}

void ToolboxScenePlatformController::validateOwnControlMove(const ToolboxOwnControlMoveFootprint &footprint)
{
  // Discharge each kept rectangle whole, including its safe off-port damage.
  // No region allocation or bounding box: corners can contain foreign damage.
  if (!EmptyRect(&footprint.before()))
    ValidRect(&footprint.before());
  if (!EmptyRect(&footprint.after()))
    ValidRect(&footprint.after());
  if (!EmptyRect(&footprint.intermediate()))
    ValidRect(&footprint.intermediate());
}

void ToolboxScenePlatformController::validateOwnControlMove(const Rect &before, const Rect &after)
{
  this->validateOwnControlMove(this->admitOwnControlMove(before, after));
}

bool ToolboxScenePlatformController::ensureScrollBarBinding(short resourceId,
                                                            const Rect &rect,
                                                            int minimum,
                                                            int maximum,
                                                            int lineStep,
                                                            int pageStep,
                                                            loka::app::scene::NativeLifetimeHint lifetimeHint,
                                                            ScrollBarControlBinding *&binding,
                                                            Rect *movedFrom)
{
  if (movedFrom)
    *movedFrom = Rect();
  binding = 0;
  if (!window_ || !window_->window() || resourceId <= 0)
  {
    return false;
  }
  Rect visible;
  const Rect &controlRect = rect;
  if (!this->intersectWithProjectionClip(rect, visible))
  {
    return true;
  }
  for (size_t i = 0; i < scrollBarLedger_.scrollBarControls_.size(); ++i)
  {
    if (scrollBarLedger_.scrollBarControls_[i].resourceId == resourceId)
    {
      binding = &scrollBarLedger_.scrollBarControls_[i];
      break;
    }
  }
  bool created = false;
  if (!binding)
  {
    ControlRef control = 0;
    if (!scrollBarLedger_.scrollBarBucket_.tryAcquire(control))
    {
      Rect rectCopy = controlRect;
      Str255 title;
      title[0] = 0;
      // scrollBarProc is the pre-Appearance standard CDEF: available on
      // every system this arm targets, and the one the Classic look expects.
      control = NewControl(window_->window(), &rectCopy, title, false, 0, 0, 1, scrollBarProc, 0);
      if (!control)
      {
        return false;
      }
      HideControl(control);
    }
    ScrollBarControlBinding entry;
    entry.ownerContext = 0;
    entry.resourceId = resourceId;
    entry.control = control;
    entry.value = 0;
    entry.onChange = 0;
    entry.enabled = 0;
    entry.minimum = 0;
    entry.maximum = 0;
    entry.lineStep = 1;
    entry.pageStep = 1;
    entry.appliedValue = 0;
    entry.active = false;
    entry.usedThisFrame = true;
    entry.placement = ToolboxNativePlacement(controlRect, visible);
    entry.lifetimeHint = lifetimeHint;
    scrollBarLedger_.scrollBarControls_.push_back(entry);
    binding = &scrollBarLedger_.scrollBarControls_.back();
    created = true;
  }
  binding->minimum = minimum;
  binding->maximum = maximum;
  binding->lineStep = lineStep;
  binding->pageStep = pageStep;
  binding->lifetimeHint = lifetimeHint;
  binding->usedThisFrame = true;
  if (created || binding->placement.full().left != controlRect.left || binding->placement.full().top != controlRect.top
      || binding->placement.full().right != controlRect.right || binding->placement.full().bottom != controlRect.bottom)
  {
    MoveControl(binding->control, controlRect.left, controlRect.top);
    if (created
        || binding->placement.full().right - binding->placement.full().left != controlRect.right - controlRect.left
        || binding->placement.full().bottom - binding->placement.full().top != controlRect.bottom - controlRect.top)
      SizeControl(binding->control, controlRect.right - controlRect.left, controlRect.bottom - controlRect.top);
    if (movedFrom)
      *movedFrom = binding->placement.full();
  }
  binding->placement = ToolboxNativePlacement(controlRect, visible);
  SetControlMinimum(binding->control, static_cast<short>(minimum));
  SetControlMaximum(binding->control, static_cast<short>(maximum));
  return true;
}

bool ToolboxScenePlatformController::ensureScrollBarControl(short resourceId,
                                                            const Rect &rect,
                                                            const loka::app::ScrollBarProps &props,
                                                            loka::app::scene::NodeContext *ownerContext,
                                                            loka::app::scene::NativeLifetimeHint lifetimeHint)
{
  ToolboxPaintSuppressScope suppress(this->paintSuppressClipRgn_);
  ScrollBarControlBinding *binding = 0;
  Rect movedFrom;
  if (!this->ensureScrollBarBinding(resourceId,
                                    rect,
                                    props.min_,
                                    props.max_,
                                    props.lineStep_,
                                    props.pageStep_,
                                    lifetimeHint,
                                    binding,
                                    &movedFrom))
  {
    return false;
  }
  if (!binding)
  {
    return true;
  }
  if (!this->paintSuppressClipRgn_)
    LOKA_PAINT_REFUSE(this->observation_, TOOLBOX_PAINT_REFUSAL_CLIP_ALLOCATION);
  binding->ownerContext = ownerContext;
  binding->value = props.value_.state();
  binding->valueSeat = props.value_;
  binding->onChange = props.onChange_;
  binding->enabled = props.enabled_;
  bindEnabledState(props.enabled_);
  const int bound = props.value_.isValid() ? props.value_.state()->get() : props.min_;
  const int shown = loka::app::ScrollBarClampValue(bound, props.min_, props.max_);
  SetControlValue(binding->control, static_cast<short>(shown));
  binding->appliedValue = shown;

  const bool enabledNow = !props.enabled_ || props.enabled_->get();
  binding->active = enabledNow && loka::app::ScrollBarIsScrollable(props.min_, props.max_);
  // Classic's one presentation for "cannot be used right now", whether the
  // reason is a disabled binding or a range with nowhere to go.
  HiliteControl(binding->control, binding->active ? 0 : 255);
  ShowControl(binding->control);
  if (!EmptyRect(&movedFrom))
    this->validateOwnControlMove(movedFrom, binding->placement.full());
  return true;
}

void ToolboxScenePlatformController::destroyScrollBarControl(short resourceId,
                                                              loka::app::scene::NativeLifetimeHint lifetimeHint)
{
  for (size_t i = 0; i < scrollBarLedger_.scrollBarControls_.size(); ++i)
  {
    ScrollBarControlBinding &binding = scrollBarLedger_.scrollBarControls_[i];
    if (binding.resourceId != resourceId)
    {
      continue;
    }
    loka::core::State<bool> *enabled = binding.enabled;
    this->retireScrollBarBinding(binding, lifetimeHint);
    binding.control = 0;
    binding.value = 0;
    binding.valueSeat = loka::app::scene::WriteSeat<int>();
    binding.onChange = 0;
    binding.enabled = 0;
    this->pressLedger_.forget(PressRow::NATIVE_BAR, i);
    scrollBarLedger_.scrollBarControls_.erase(scrollBarLedger_.scrollBarControls_.begin() + i);
    controlIds_.release(resourceId);
    // The scroll bar is not hit-list based, so the enabled unbind that
    // releaseNodeContexts performs for buttons and popups happens here
    // instead -- same rule: the observer goes only when no live binding of
    // any kind still needs it.
    if (enabled && !hasLiveBinding(enabled))
    {
      unbindEnabledState(enabled);
    }
    return;
  }
  // An auto id can exist before NewControl succeeds. The viewport ledger
  // still owns that id and must return it when its node retires.
  controlIds_.release(resourceId);
}

#include "ToolboxViewportScrollBar.cpp"

#include "ToolboxEditorScrollBar.cpp"

#include "ToolboxControlInput.cpp"
