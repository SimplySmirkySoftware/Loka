#ifndef LOKA_MAC_SCENE_LAYOUT_HPP
#define LOKA_MAC_SCENE_LAYOUT_HPP

#include "app/layout/RailSpacing.hpp"

/** macOS scene spacing ruled by #950, page 2: kRootInset is the outer
    margin on every side; kGap is the distance between layout seats on either
    axis. Both are logical units projected through spaceScale. */
struct MacSceneLayout
{
  enum
  {
    kRootInset = 16,
    kGap = 6
  };
  static const loka::app::layout::RailSpacing &spacing()
  {
    static const loka::app::layout::RailSpacing value(kGap, kGap, kRootInset);
    return value;
  }
};

#endif // LOKA_MAC_SCENE_LAYOUT_HPP
