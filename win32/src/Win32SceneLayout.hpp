#ifndef LOKA_WIN32_SCENE_LAYOUT_HPP
#define LOKA_WIN32_SCENE_LAYOUT_HPP

/** Win32 scene spacing ruled by #950, page 2: kRootInset is the outer
    margin on every side; kGap is the distance between layout seats on either
    axis. Both are logical units projected through spaceScale. */
struct Win32SceneLayout
{
  enum
  {
    kRootInset = 10,
    kGap = 6
  };
};

#endif // LOKA_WIN32_SCENE_LAYOUT_HPP
