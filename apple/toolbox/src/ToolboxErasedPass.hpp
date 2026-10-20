#ifndef LOKA_TOOLBOX_ERASED_PASS_HPP
#define LOKA_TOOLBOX_ERASED_PASS_HPP
#include <Quickdraw.h>
#include <cassert>

/** Positive erase evidence, borrowed only for one non-nesting render walk. */
class ToolboxErasedPass
{
public:
  enum Origin
  {
    FULL_DRAW,
    COMPOSITION_REPLAY
  };
  struct Descriptor
  {
    const Origin origin;
    const Rect rect;
    Descriptor(Origin source, const Rect &area)
        : origin(source),
          rect(area)
    {
    }
  };
  ToolboxErasedPass()
      : descriptor_(0)
  {
  }
  bool isOpen() const
  {
    return this->descriptor_ != 0;
  }
  bool admitsCapture(const Rect &rect) const
  {
    return this->descriptor_ && this->descriptor_->origin == COMPOSITION_REPLAY
           && this->descriptor_->rect.left <= rect.left && this->descriptor_->rect.top <= rect.top
           && this->descriptor_->rect.right >= rect.right && this->descriptor_->rect.bottom >= rect.bottom;
  }
  /** A refused nested guard leaves the outer descriptor intact in all builds. */
  class Guard
  {
  public:
    Guard(ToolboxErasedPass &pass, Origin origin, const Rect &rect)
        : descriptor_(origin, rect),
          owned_(pass.isOpen() ? 0 : &pass)
    {
      assert(!pass.isOpen());
      if (this->owned_)
        this->owned_->descriptor_ = &this->descriptor_;
    }
    ~Guard()
    {
      if (this->owned_)
        this->owned_->descriptor_ = 0;
    }

  private:
    const Descriptor descriptor_;
    ToolboxErasedPass *owned_;
    Guard(const Guard &);
    Guard &operator=(const Guard &);
  };

private:
  const Descriptor *descriptor_;
  ToolboxErasedPass(const ToolboxErasedPass &);
  ToolboxErasedPass &operator=(const ToolboxErasedPass &);
};
#endif
