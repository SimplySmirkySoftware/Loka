#ifndef LOKA_TOOLBOX_PRESENT_BATCH_HPP
#define LOKA_TOOLBOX_PRESENT_BATCH_HPP

#include <Quickdraw.h>
#include <algorithm>
#include <vector>
#include <cassert>

/** Completed bounded repair input. Overflow retains refusal, never a truncated admission. */
class ToolboxPresentRects
{
public:
  ToolboxPresentRects()
      : rects_(),
        count_(0)
  {
  }
  ToolboxPresentRects(const Rect *rects, std::size_t count)
      : rects_(),
        count_(count > 4 ? 5 : count)
  {
    for (unsigned i = 0; i < this->count(); ++i)
      this->rects_[i] = rects[i];
  }
  bool refused() const
  {
    return this->count_ > 4;
  }
  bool requested() const
  {
    return this->count_ != 0;
  }
  unsigned count() const
  {
    return this->refused() ? 0 : static_cast<unsigned>(this->count_);
  }
  const Rect &at(unsigned i) const
  {
    assert(i < this->count());
    return this->rects_[i];
  }

private:
  Rect rects_[4];
  std::size_t count_;
};

/** One complete request set of one window. The window owns the next batch;
    flushInvalidate owns the sealed batch on its stack. A request posted during
    an attempt lands in next. Full retains rectangles as repair input, never
    separate draw work. The controller tick batch (pendingDirtyRects_/pendingFullInvalidate_
    in ToolboxBatchUpdate.cpp) is a deliberate twin with a different owner and
    clock, and stays separate. */
class ToolboxPresentBatch
{
public:
  enum Summary
  {
    RECTS_ONLY,
    FULL_ONLY,
    FULL_WITH_RECTS
  };
  ToolboxPresentBatch()
      : rects_(),
        summary_(RECTS_ONLY)
  {
  }
  void add(const Rect &rect)
  {
    if (this->full())
    {
      this->summary_ = FULL_WITH_RECTS;
    }
    for (std::size_t i = 0; i < this->rects_.size(); ++i)
    {
      Rect &pending = this->rects_[i];
      if (rect.right < pending.left || rect.left > pending.right || rect.bottom < pending.top
          || rect.top > pending.bottom)
      {
        continue;
      }
      if (rect.left < pending.left)
      {
        pending.left = rect.left;
      }
      if (rect.top < pending.top)
      {
        pending.top = rect.top;
      }
      if (rect.right > pending.right)
      {
        pending.right = rect.right;
      }
      if (rect.bottom > pending.bottom)
      {
        pending.bottom = rect.bottom;
      }
      return;
    }
    this->rects_.push_back(rect);
  }
  void addFull()
  {
    if (this->summary_ == RECTS_ONLY)
      this->summary_ = this->rects_.empty() ? FULL_ONLY : FULL_WITH_RECTS;
  }
  bool empty() const
  {
    return this->summary_ == RECTS_ONLY && this->rects_.empty();
  }
  bool full() const
  {
    return this->summary_ != RECTS_ONLY;
  }
  bool rectRequested() const
  {
    return this->summary_ == FULL_WITH_RECTS || (this->summary_ == RECTS_ONLY && !this->rects_.empty());
  }
  Summary summary() const
  {
    return this->summary_;
  }
  const std::vector<Rect> &rects() const
  {
    return this->rects_;
  }
  /** Window before render, once per sealed attempt; copies at most four owner rows. */
  ToolboxPresentRects repairRects() const
  {
    return ToolboxPresentRects(this->rects_.empty() ? 0 : &this->rects_[0], this->rects_.size());
  }
  void swap(ToolboxPresentBatch &other)
  {
    std::swap(this->rects_, other.rects_);
    std::swap(this->summary_, other.summary_);
  }
  void clear()
  {
    this->rects_.clear();
    this->summary_ = RECTS_ONLY;
  }

private:
  std::vector<Rect> rects_;
  Summary summary_;
};

#endif // LOKA_TOOLBOX_PRESENT_BATCH_HPP
