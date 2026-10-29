#ifndef LOKA_TOOLBOX_OWN_CONTROL_MOVE_FOOTPRINT_HPP
#define LOKA_TOOLBOX_OWN_CONTROL_MOVE_FOOTPRINT_HPP
#include <Quickdraw.h>

/** Whole before, after, and old-size-at-new-origin rectangles admitted independently.
    Only their in-port intersections need capture evidence; off-port portions
    cannot contain visible window damage. A refused rectangle is empty. */
class ToolboxOwnControlMoveFootprint
{
public:
  ToolboxOwnControlMoveFootprint()
      : before_(),
        after_(),
        intermediate_()
  {
  }
  ToolboxOwnControlMoveFootprint(const Rect &before, const Rect &after, const Rect &intermediate)
      : before_(before),
        after_(after),
        intermediate_(intermediate)
  {
  }
  bool empty() const
  {
    return EmptyRect(&this->before_) && EmptyRect(&this->after_) && EmptyRect(&this->intermediate_);
  }
  const Rect &before() const
  {
    return this->before_;
  }
  const Rect &after() const
  {
    return this->after_;
  }
  const Rect &intermediate() const
  {
    return this->intermediate_;
  }

private:
  Rect before_;
  Rect after_;
  Rect intermediate_;
};
#endif
