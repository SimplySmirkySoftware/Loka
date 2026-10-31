#ifndef LOKA_TOOLBOX_PAINT_RESULT_TEXT_HPP
#define LOKA_TOOLBOX_PAINT_RESULT_TEXT_HPP
#include "ToolboxPresentObservation.hpp"
/** Diagnostic readers only: one selected, finalized observation; O(1) fields,
    plus the length of its source-location string. No per-pass history. */
inline std::string ToolboxPaintResultText(const ToolboxPresentObservation &observation)
{
  const unsigned rule = observation.firstRefusal().refused()               ? 1
                        : !observation.paintFinished()                     ? 2
                        : observation.eligibility() == TOOLBOX_PAINT_CLEAN ? 3
                                                                           : 4;
  std::string result("paint_result=");
  result += ToolboxPaintEligibilityName(observation.eligibility());
  result += " rule=";
  result += static_cast<char>('0' + rule);
  result += " first=";
  result += ToolboxPaintRefusalName(observation.firstRefusal().reason);
  result += " site=";
  result +=
      observation.firstRefusal().refused() && observation.firstRefusal().site ? observation.firstRefusal().site : "-";
  result += " coverage=";
  result += rule == 3 ? "window" : rule == 4 ? "not_window" : "-";
  return result;
}
#endif
