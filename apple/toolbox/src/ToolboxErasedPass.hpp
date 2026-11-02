#ifndef LOKA_TOOLBOX_ERASED_PASS_HPP
#define LOKA_TOOLBOX_ERASED_PASS_HPP
#include <Quickdraw.h>
#include <cassert>
#include "ToolboxRegionCheck.hpp"

/** Completed evidence of an entry erase; NONE never admits a capture. */
class ToolboxErasedEvidence
{
public:
  enum Kind
  {
    NONE,
    RECT,
    REGION
  };
  ToolboxErasedEvidence()
      : kind_(NONE),
        rect_(),
        region_(0),
        scratch_(0)
  {
  }
  explicit ToolboxErasedEvidence(const Rect &rect)
      : kind_(RECT),
        rect_(rect),
        region_(0),
        scratch_(0)
  {
  }
  ToolboxErasedEvidence(RgnHandle region, RgnHandle scratch)
      : kind_(REGION),
        rect_(),
        region_(region),
        scratch_(scratch)
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
    case REGION:
      if (!ToolboxRegionOK(this->region_) || !ToolboxRegionOK(this->scratch_))
        return false;
      RectRgn(this->scratch_, &rect);
      if (!ToolboxRegionOK(this->scratch_))
        return false;
      DiffRgn(this->scratch_, this->region_, this->scratch_);
      if (!ToolboxRegionOK(this->scratch_))
        return false;
      return EmptyRgn(this->scratch_) && ToolboxRegionOK(this->scratch_);
    }
    return false;
  }

private:
  Kind kind_;
  Rect rect_;
  RgnHandle region_, scratch_;
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
    /** Presenter-owned regions remain borrowed only while this guard is open. */
    void noteErased(RgnHandle region, RgnHandle scratch)
    {
      if (!this->owned_)
        return;
      assert(this->descriptor_.erased.kind() == ToolboxErasedEvidence::NONE);
      if (this->descriptor_.erased.kind() != ToolboxErasedEvidence::NONE)
        return;
#ifdef LOKA_LIFECYCLE_AUDIT
      assert(region && ToolboxErasedEvidence::contains(this->descriptor_.shown, (**region).rgnBBox));
#endif
      this->descriptor_.erased = ToolboxErasedEvidence(region, scratch);
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
#ifdef TEST_BUILD
  friend class ToolboxErasedPassInspection;
#endif
  const Descriptor *descriptor_;
  ToolboxErasedPass(const ToolboxErasedPass &);
  ToolboxErasedPass &operator=(const ToolboxErasedPass &);
};
#endif
