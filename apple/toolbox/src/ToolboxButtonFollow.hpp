#ifndef LOKA_TOOLBOX_BUTTON_FOLLOW_HPP
#define LOKA_TOOLBOX_BUTTON_FOLLOW_HPP
#include "ToolboxPaintPassFacts.hpp"
#include "ToolboxNativePlacement.hpp"
/** Pure transport check. One moved button visit, at most two plan-owned PRESERVE units. */
inline bool ToolboxButtonFollowsCopy(const ToolboxNativePlacement &before,
                                     const ToolboxNativePlacement &after,
                                     const ToolboxPaintPassFacts *facts)
{
  if (!facts || !facts->copiedPlan)
    return false;
  const ToolboxPresentPlan &plan = *facts->copiedPlan;
  const Rect &oldRect = before.full();
  const Rect &newRect = after.full();
  if (oldRect.left != newRect.left || oldRect.right != newRect.right
      || static_cast<long>(oldRect.top) + plan.dv != newRect.top
      || static_cast<long>(oldRect.bottom) + plan.dv != newRect.bottom)
    return false;
  if (!EqualRect(&before.visible(), &oldRect) || !EqualRect(&after.visible(), &newRect))
    return false;
  if (!ToolboxErasedEvidence::contains(plan.copyDomain, oldRect)
      || !ToolboxErasedEvidence::contains(plan.copyDomain, newRect))
    return false;
  for (unsigned i = 0; i < plan.foreground.preservedCount(); ++i)
  {
    const Rect &preserved = plan.foreground.preservedUnit(i);
    Rect overlap;
    if (SectRect(&oldRect, &preserved, &overlap) || SectRect(&newRect, &preserved, &overlap))
      return false;
  }
  return true;
}
#endif
