#ifndef LOKA_TOOLBOX_NATIVE_PLACEMENT_HPP
#define LOKA_TOOLBOX_NATIVE_PLACEMENT_HPP

#include <Quickdraw.h>

/** The full and visible rectangles last applied to one native control row. */
class ToolboxNativePlacement
{
public:
  ToolboxNativePlacement()
      : full_(),
        visible_()
  {
  }
  ToolboxNativePlacement(const Rect &full, const Rect &visible)
      : full_(full),
        visible_(visible)
  {
  }

  const Rect &full() const
  {
    return this->full_;
  }

  /** The interaction rectangle, including an empty applied placement. */
  const Rect &visible() const
  {
    return this->visible_;
  }

  /** Answer the visible rectangle only when it is nonempty. */
  bool queryVisible(Rect &out) const
  {
    if (EmptyRect(&this->visible_))
      return false;
    out = this->visible_;
    return true;
  }

private:
  Rect full_;
  Rect visible_;
};

#endif // LOKA_TOOLBOX_NATIVE_PLACEMENT_HPP
