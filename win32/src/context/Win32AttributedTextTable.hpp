#ifndef LOKA_WIN32_ATTRIBUTED_TEXT_TABLE_HPP
#define LOKA_WIN32_ATTRIBUTED_TEXT_TABLE_HPP

#include <windows.h>
#include <string>
#include "app/layout/TextLineBreaker.hpp"
#include "app/layout/MeasurementResult.hpp"

class Win32ScenePlatformController;
namespace loka
{
  namespace testing
  {
    class Win32AttributedTextAccess;
  }
} // namespace loka

/** Completed PER_RUN snapshot. Owns native units and derived rows; HFONTs are
    borrowed from the controller and revoked by its WM_SETFONT broadcast. */
class Win32AttributedTextTable
{
public:
  Win32AttributedTextTable();
  ~Win32AttributedTextTable();
  void clear();
  bool build(const loka::app::AttributedString &,
             const loka::app::BlockStyle &,
             int width,
             HDC,
             const Win32ScenePlatformController &);
  bool reusable(int width) const { return this->measurement_.reusable(width); }
  int width() const { return this->valid() ? this->measurement_.extent().width : 0; }
  int height() const { return this->valid() ? this->measurement_.extent().height : 0; }
  bool valid() const
  {
    return this->lines_ != 0;
  }
  const loka::app::TextLineBreaker &lines() const
  {
    assert(this->lines_);
    return *this->lines_;
  }
  const loka::app::AttributedString &value() const
  {
    return this->snapshot_;
  }
  /** Serializes all completed lines, including empty lines, with LF separators. */
  bool joinLines(std::wstring &out) const;
  bool draw(HDC, const RECT &, const loka::app::BlockStyle &) const;

private:
  friend class loka::testing::Win32AttributedTextAccess;
  struct Extent
  {
    Extent(int w = 0, int h = 0) : width(w), height(h) {}
    int width, height;
  };
  loka::app::MeasurementResult<int, Extent> measurement_;
  class WidthSource;
  class FontSelection;
  enum Ellipsis
  {
    ELLIPSIS_NONE,
    ELLIPSIS_DRAWN
  };
  /** Completed seat-relative fitting facts, consumed only on success. */
  struct FittedLine
  {
    std::size_t paintedEnd;
    int offset;
    int paintedWidth;
    Ellipsis ellipsis;
    std::size_t dotsSpan;
  };
  bool fitLine(const loka::app::TextLineRecord &line,
               int seatWidth,
               const loka::app::BlockStyle &block,
               const WidthSource &source,
               FontSelection &measure,
               FittedLine &out) const;
  bool buildRows(const loka::app::AttributedString &,
                 const loka::app::BlockStyle &,
                 int,
                 HDC,
                 const Win32ScenePlatformController &);
  loka::app::detail::TextMeasureTable<WCHAR> units_;
  loka::app::detail::TextMeasureTable<int> advances_;
  loka::app::detail::TextMeasureTable<loka::app::TextBreakCharacter> characters_;
  loka::app::detail::TextMeasureTable<loka::app::TextStyleSpan> spans_;
  loka::app::detail::TextMeasureTable<HFONT> fonts_;
  loka::app::detail::TextMeasureTable<loka::app::TextLineMetrics> metrics_;
  loka::app::TextLineBreaker *lines_;
  loka::app::AttributedString snapshot_;
  Win32AttributedTextTable(const Win32AttributedTextTable &);
  Win32AttributedTextTable &operator=(const Win32AttributedTextTable &);
};
#endif
