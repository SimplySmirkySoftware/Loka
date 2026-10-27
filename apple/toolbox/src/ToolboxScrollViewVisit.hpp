#ifndef LOKA_TOOLBOX_SCROLL_VIEW_VISIT_HPP
#define LOKA_TOOLBOX_SCROLL_VIEW_VISIT_HPP
#include "app/nodes/nestable/ScrollViewRequests.hpp"
#include <cstddef>

/** Rows ensured and shown in this visit. Indices survive subsequent ensures. */
class ToolboxViewportRows
{
public:
  static std::size_t absent()
  {
    return static_cast<std::size_t>(-1);
  }
  ToolboxViewportRows(int offset = 0, std::size_t x = absent(), std::size_t y = absent())
      : offset_(offset),
        x_(x),
        y_(y)
  {
  }
  int verticalOffset() const
  {
    return this->offset_;
  }
  bool queryRow(loka::app::ScrollViewVisit::Axis axis, std::size_t &index) const
  {
    const std::size_t row = axis == loka::app::ScrollViewVisit::X ? this->x_ : this->y_;
    if (row == absent())
      return false;
    index = row;
    return true;
  }
  loka::app::ScrollViewVisit visit() const
  {
    std::size_t index;
    const bool x = this->queryRow(loka::app::ScrollViewVisit::X, index);
    const bool y = this->queryRow(loka::app::ScrollViewVisit::Y, index);
    return loka::app::ScrollViewVisit::succeededWithNativeAxes(x, y);
  }

private:
  int offset_;
  std::size_t x_, y_;
};

/** Completed layout and the native reach committed by that layout. */
struct ToolboxScrollViewLayoutResult
{
  const short width;
  const loka::app::ScrollViewVisit visit;
  const ToolboxViewportRows rows;
  explicit ToolboxScrollViewLayoutResult(short value = 0)
      : width(value),
        visit(loka::app::ScrollViewVisit::refused()),
        rows()
  {
  }
  ToolboxScrollViewLayoutResult(short value, const ToolboxViewportRows &committed)
      : width(value),
        visit(committed.visit()),
        rows(committed)
  {
  }
};
#endif
