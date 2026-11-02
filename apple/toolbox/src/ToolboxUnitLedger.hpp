#ifndef LOKA_TOOLBOX_UNIT_LEDGER_HPP
#define LOKA_TOOLBOX_UNIT_LEDGER_HPP
#include <Quickdraw.h>
#include <vector>
#include "app/nodes/nestable/ZStack.hpp"
namespace loka
{
  namespace app
  {
    class ZStackNode;
  }
} // namespace loka

/** Completed final-row comparison; identities are never dereferenced. */
class ToolboxUnitComparison
{
public:
  ToolboxUnitComparison(unsigned changed = 0, unsigned added = 0, unsigned departed = 0)
      : changed_(changed),
        added_(added),
        departed_(departed)
  {
  }
  bool changed() const
  {
    return this->changed_ || this->added_ || this->departed_;
  }
  unsigned changedRows() const
  {
    return this->changed_;
  }
  unsigned addedRows() const
  {
    return this->added_;
  }
  unsigned departedRows() const
  {
    return this->departed_;
  }

private:
  unsigned changed_, added_, departed_;
};

/** Controller-owned facts rebuilt by layout; render and repair only query them. */
class ToolboxUnitLedger
{
public:
  struct UnitSeat
  {
    loka::app::ZStackNode *stack;
    unsigned index;
    /** The unit's visible part: unit cut by the projection clip. */
    Rect rect;
    /** The projected unit before that cut; its bottom row may be hidden. */
    Rect unit;
  };
  typedef std::vector<UnitSeat>::size_type Mark;
  void clear()
  {
    this->rows_.swap(this->previous_);
    this->rows_.clear();
  }
  /** Controller reset, once; drops both generations without following identities. */
  void reset()
  {
    this->rows_.clear();
    this->previous_.clear();
  }
  /** Controller after publication, once per layout; O(current * previous), own rows only. */
  ToolboxUnitComparison compareFinalRows() const
  {
    unsigned changed = 0, added = 0, departed = 0;
    for (Mark i = 0; i < this->rows_.size(); ++i)
    {
      const UnitSeat &row = this->rows_[i];
      Mark j = 0;
      for (; j < this->previous_.size(); ++j)
        if (row.stack == this->previous_[j].stack && row.index == this->previous_[j].index)
        {
          if (!sameRect(row.rect, this->previous_[j].rect) || !sameRect(row.unit, this->previous_[j].unit))
            ++changed;
          break;
        }
      if (j == this->previous_.size())
        ++added;
    }
    for (Mark i = 0; i < this->previous_.size(); ++i)
    {
      Mark j = 0;
      for (; j < this->rows_.size(); ++j)
        if (this->previous_[i].stack == this->rows_[j].stack && this->previous_[i].index == this->rows_[j].index)
          break;
      if (j == this->rows_.size())
        ++departed;
    }
    return ToolboxUnitComparison(changed, added, departed);
  }
  void push(const UnitSeat &row)
  {
    this->rows_.push_back(row);
  }
  Mark mark() const
  {
    return this->rows_.size();
  }
  void discard(Mark mark)
  {
    this->rows_.resize(mark);
  }
  const UnitSeat *find(loka::app::ZStackNode *stack, unsigned index, Mark &cursor) const
  {
    while (cursor < this->rows_.size())
    {
      const UnitSeat &row = this->rows_[cursor];
      if (row.stack == stack && row.index >= index)
        return row.index == index ? &row : 0;
      ++cursor;
    }
    return 0;
  }
  /** Completed selection, borrowed only until the next layout. */
  struct FirstBlurUnits
  {
    const UnitSeat *staticBlur;
    const UnitSeat *band;
  };
  /** Once per completed layout: one O(rows) scan of this ledger's own rows. */
  FirstBlurUnits firstBlurUnits() const
  {
    FirstBlurUnits result = {0, 0};
    for (Mark i = 0; i < this->rows_.size(); ++i)
    {
      const UnitSeat &row = this->rows_[i];
      if (row.index != 1)
        continue;
      if (!result.staticBlur && row.stack->props.unitGround == loka::app::ZSTACK_UNIT_GROUND_EXP_STATIC_BLUR)
        result.staticBlur = &row;
      if (!result.band && row.stack->props.unitGround == loka::app::ZSTACK_UNIT_GROUND_EXP_BLUR)
        result.band = &row;
    }
    return result;
  }
  const UnitSeat &at(Mark index) const
  {
    return this->rows_[index];
  }
  /** Strict pixel overlap: touching edges do not schedule replay. */
  static bool overlap(const Rect &a, const Rect &b, Rect &out)
  {
    out.left = a.left > b.left ? a.left : b.left;
    out.top = a.top > b.top ? a.top : b.top;
    out.right = a.right < b.right ? a.right : b.right;
    out.bottom = a.bottom < b.bottom ? a.bottom : b.bottom;
    return out.left < out.right && out.top < out.bottom;
  }
  bool nextOverlap(const Rect &rect, Mark &cursor, Rect &out) const
  {
    while (cursor < this->rows_.size())
      if (overlap(rect, this->rows_[cursor++].rect, out))
        return true;
    return false;
  }

private:
  static bool sameRect(const Rect &a, const Rect &b)
  {
    return a.top == b.top && a.left == b.left && a.bottom == b.bottom && a.right == b.right;
  }
  std::vector<UnitSeat> rows_, previous_;
};
#endif
