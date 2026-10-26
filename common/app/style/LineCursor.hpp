#ifndef LOKA_APP_LINE_CURSOR_HPP
#define LOKA_APP_LINE_CURSOR_HPP
#include "core/ObservableList.hpp"
namespace loka
{
  namespace app
  {
    /** Attachment-scoped line identity and column; none has no caret. */
    struct LineCursor
    {
      /** UTF-8 byte offset on a code point boundary; scoped to avoid the Column layout DSL. */
      typedef int Column;
      core::ItemId line;
      Column column;
      LineCursor(core::ItemId id = core::ItemId::none(), Column col = 0)
          : line(id),
            column(col)
      {
      }
      static LineCursor None()
      {
        return LineCursor();
      }
      bool isNone() const
      {
        return this->line.isNone();
      }
      bool operator==(const LineCursor &other) const
      {
        return this->line == other.line && this->column == other.column;
      }
      bool operator!=(const LineCursor &other) const
      {
        return !(*this == other);
      }
    };
    /** Post-replacement row and UTF-8 byte column on a code point boundary; None publishes no caret. */
    struct RowCursor
    {
      unsigned short row;
      LineCursor::Column column;
      RowCursor(unsigned short r, LineCursor::Column col)
          : row(r),
            column(col)
      {
      }
      static RowCursor None()
      {
        return RowCursor(static_cast<unsigned short>(-1), 0);
      }
      bool isNone() const
      {
        return this->row == static_cast<unsigned short>(-1);
      }
    };
  } // namespace app
} // namespace loka
#endif
