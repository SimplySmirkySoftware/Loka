#ifndef LOKA_TOOLBOX_ERASED_PASS_HPP
#define LOKA_TOOLBOX_ERASED_PASS_HPP
#include <Quickdraw.h>
#include <cassert>

/** Completed evidence of an entry erase; NONE never admits a capture. */
class ToolboxErasedEvidence
{
public:
  enum Kind
  {
    NONE,
    RECT
  };
  ToolboxErasedEvidence()
      : kind_(NONE),
        rect_()
  {
  }
  explicit ToolboxErasedEvidence(const Rect &rect)
      : kind_(RECT),
        rect_(rect)
  {
  }
  Kind kind() const
  {
    return this->kind_;
  }
  static bool contains(const Rect &area, const Rect &rect)
  {
    return rect.left < rect.right && rect.top < rect.bottom && area.left <= rect.left && area.top <= rect.top
           && area.right >= rect.right && area.bottom >= rect.bottom;
  }
  bool admitsCapture(const Rect &rect) const
  {
    switch (this->kind_)
    {
    case NONE:
      return false;
    case RECT:
      return contains(this->rect_, rect);
    }
    return false;
  }

private:
  Kind kind_;
  Rect rect_;
};

/** Entry promise and actual erase, borrowed for one non-nesting render walk. */
class ToolboxErasedPass
{
public:
  struct Descriptor
  {
    const Rect shown;
    ToolboxErasedEvidence erased;
    explicit Descriptor(const Rect &area)
        : shown(area),
          erased()
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
  bool dischargesDamage(const Rect &rect) const
  {
    return this->descriptor_ && ToolboxErasedEvidence::contains(this->descriptor_->shown, rect);
  }
  bool admitsCapture(const Rect &rect) const
  {
    return this->descriptor_ && this->descriptor_->erased.admitsCapture(rect);
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
    const Rect &shown() const
    {
      return this->descriptor_.shown;
    }
    /** Entry erasers note once. Release builds clip and preserve the first note. */
    void noteErased(const Rect &rect)
    {
      if (!this->owned_)
        return;
      assert(this->descriptor_.erased.kind() == ToolboxErasedEvidence::NONE);
      if (this->descriptor_.erased.kind() != ToolboxErasedEvidence::NONE)
        return;
      assert(rect.left >= rect.right || rect.top >= rect.bottom
             || ToolboxErasedEvidence::contains(this->descriptor_.shown, rect));
      Rect clipped = {0, 0, 0, 0};
      SectRect(&this->descriptor_.shown, &rect, &clipped);
      this->descriptor_.erased = ToolboxErasedEvidence(clipped);
    }

  private:
    Descriptor descriptor_;
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
