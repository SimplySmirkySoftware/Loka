#ifndef LOKA_TOOLBOX_NATIVE_DOOR_SCOPE_HPP
#define LOKA_TOOLBOX_NATIVE_DOOR_SCOPE_HPP

#include "ToolboxPortScope.hpp"

/** Select the owner port and intersect its current clip with the visible rect.
    Owns its saved region; allocation refusal leaves the owner clip unchanged.
    Restores the owner clip and previous port without invalidating. This narrows
    drawing only, not the pointer movement accepted by TrackControl. With
    intersect=false, select the owner only and leave its clip untouched. */
class ToolboxNativeDoorScope
{
public:
  ToolboxNativeDoorScope(GrafPtr owner, const Rect &visible, bool intersect = true)
      : port_(owner),
        owner_(owner),
        savedClip_(intersect ? NewRgn() : 0)
  {
    if (this->savedClip_)
    {
      GetClip(this->savedClip_);
      ClipRect(&visible);
      SectRgn(this->savedClip_, owner->clipRgn, owner->clipRgn);
    }
  }

  ~ToolboxNativeDoorScope()
  {
    if (this->savedClip_)
    {
      SetPort(this->owner_);
      SetClip(this->savedClip_);
      DisposeRgn(this->savedClip_);
    }
  }

private:
  ToolboxNativeDoorScope(const ToolboxNativeDoorScope &);
  ToolboxNativeDoorScope &operator=(const ToolboxNativeDoorScope &);
  ToolboxPortScope port_;
  GrafPtr const owner_;
  RgnHandle const savedClip_;
};

#endif // LOKA_TOOLBOX_NATIVE_DOOR_SCOPE_HPP
