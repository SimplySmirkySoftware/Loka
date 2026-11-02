#ifndef LOKA_TOOLBOX_PRESENT_BATCH_HPP
#define LOKA_TOOLBOX_PRESENT_BATCH_HPP

#include <Quickdraw.h>
#include <algorithm>
#include <vector>

/** One complete request set of one window. The window owns the next batch;
    flushInvalidate owns the sealed batch on its stack. A request posted during
    an attempt lands in next. Full covers all rectangles (full implies empty
    rects). The controller tick batch (pendingDirtyRects_/pendingFullInvalidate_
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
      return;
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
    this->rects_.clear();
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
