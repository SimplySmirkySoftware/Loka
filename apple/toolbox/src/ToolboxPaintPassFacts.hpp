#ifndef LOKA_TOOLBOX_PAINT_PASS_FACTS_HPP
#define LOKA_TOOLBOX_PAINT_PASS_FACTS_HPP
#include "ToolboxBandPaintPolicy.hpp"
#include "ToolboxPresentObservation.hpp"

/** Render owns these completed facts; the lexical paint borrow expires before the plan. */
struct ToolboxPaintPassFacts
{
  const ToolboxBandPaintPolicy &bandPolicy;
  const ToolboxPresentPlan *const copiedPlan;
  ToolboxPaintPassFacts(const ToolboxBandPaintPolicy &policy, const ToolboxPresentPlan *copy)
      : bandPolicy(policy),
        copiedPlan(copy)
  {
  }
};
#endif
