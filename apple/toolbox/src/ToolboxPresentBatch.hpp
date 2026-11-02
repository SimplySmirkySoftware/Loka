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
  ToolboxPresentBatch()
      : rects_(),
        full_(false)
  {
  }
  void add(const Rect &rect)
  {
    if (this->full_)
      return;
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
    this->full_ = true;
    this->rects_.clear();
  }
  bool empty() const
  {
    return !this->full_ && this->rects_.empty();
  }
  bool full() const
  {
    return this->full_;
  }
  const std::vector<Rect> &rects() const
  {
    return this->rects_;
  }
  void swap(ToolboxPresentBatch &other)
  {
    std::swap(this->rects_, other.rects_);
    std::swap(this->full_, other.full_);
  }
  void clear()
  {
    this->rects_.clear();
    this->full_ = false;
  }

private:
  std::vector<Rect> rects_;
  bool full_;
};

#endif // LOKA_TOOLBOX_PRESENT_BATCH_HPP
