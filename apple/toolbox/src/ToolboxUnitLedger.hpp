#ifndef LOKA_TOOLBOX_UNIT_LEDGER_HPP
#define LOKA_TOOLBOX_UNIT_LEDGER_HPP
#include <Quickdraw.h>
#include <vector>
namespace loka
{
  namespace app
  {
    class ZStackNode;
  }
} // namespace loka

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
    this->rows_.clear();
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
  std::vector<UnitSeat> rows_;
};
#endif
