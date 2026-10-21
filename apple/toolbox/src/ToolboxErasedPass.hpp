#ifndef LOKA_TOOLBOX_ERASED_PASS_HPP
#define LOKA_TOOLBOX_ERASED_PASS_HPP
#include <Quickdraw.h>
#include <cassert>

/** Positive erase evidence, borrowed only for one non-nesting render walk. */
class ToolboxErasedPass
{
public:
  struct Descriptor
  {
    const Rect rect;
    explicit Descriptor(const Rect &area)
        : rect(area)
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
    return this->descriptor_ && rect.left < rect.right && rect.top < rect.bottom
           && this->descriptor_->rect.left <= rect.left && this->descriptor_->rect.top <= rect.top
           && this->descriptor_->rect.right >= rect.right && this->descriptor_->rect.bottom >= rect.bottom;
  }
  /** A refused nested guard leaves the outer descriptor intact in all builds. */
  class Guard
  {
  public:
    Guard(ToolboxErasedPass &pass, const Rect &rect)
        : descriptor_(rect),
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
