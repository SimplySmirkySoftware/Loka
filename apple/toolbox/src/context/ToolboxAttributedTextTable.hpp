#ifndef LOKA_TOOLBOX_ATTRIBUTED_TEXT_TABLE_HPP
#define LOKA_TOOLBOX_ATTRIBUTED_TEXT_TABLE_HPP
#ifdef TEST_BUILD
namespace loka
{
  namespace testing
  {
    class ToolboxAttributedTextTableAccess;
    void failToolboxAttributedDraws(unsigned count);
    bool refuseToolboxAttributedDraw();
  } // namespace testing
} // namespace loka
#endif

#include "app/layout/TextLineBreaker.hpp"
#include "app/layout/MeasurementResult.hpp"
#include "context/ToolboxLayoutUtil.hpp"

/** Deliberate twin of Win32 LinkHit/HitAnswer in Win32AttributedTextTable.hpp.
    Completed batch span contribution against one logical Toolbox seat. */
struct ToolboxLinkHit
{
  std::size_t firstSegment, lastSegment;
  Rect seat;
};
enum ToolboxHitAnswer
{
  TOOLBOX_HIT_LINK,
  TOOLBOX_HIT_NONE,
  TOOLBOX_HIT_REFUSED
};

/** Context-owned completed PER_RUN projection. A null break is unknown; clear
    releases every derived row together. No port transaction survives build. */
class ToolboxAttributedTextTable
{
public:
  ToolboxAttributedTextTable();
  ~ToolboxAttributedTextTable();
  void clear();
  /** Drop geometry while retaining the complete logical/native projection. */
  void invalidateGeometry();
  bool build(const loka::app::AttributedString &value,
             const loka::app::BlockStyle &block,
             short width,
             const ToolboxScenePlatformController &controller);
  /** Projection identity includes segment boundaries: malformed units cannot
      be joined even when logical AttributedString equality coalesces runs. */
  bool matches(const loka::app::AttributedString &value) const;
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
    assert(this->valid());
    return this->snapshot_;
  }
  bool reusable(short width) const
  {
    return this->measurement_.reusable(width);
  }
  /** The width constraint the completed geometry was built for, if any. */
  bool queryConstraint(short &width) const
  {
    return this->valid() && this->measurement_.queryConstraint(width);
  }
  short height() const
  {
    return this->valid() ? this->measurement_.extent().height : 0;
  }
  short width() const
  {
    return this->valid() ? this->measurement_.extent().width : 0;
  }
  /** Walk accepted advance cells; native measurement is restored on return. */
  ToolboxHitAnswer hitAt(const Rect &seat,
                         const ToolboxScenePlatformController &controller,
                         const loka::app::BlockStyle &block,
                         Point local,
                         ToolboxLinkHit &out) const;
  bool draw(short x,
            short top,
            const ToolboxScenePlatformController &controller,
            const loka::app::BlockStyle &block,
            short availableWidth) const;

private:
#ifdef TEST_BUILD
  friend class loka::testing::ToolboxAttributedTextTableAccess;
#endif
  /** Completed scalar geometry; line/fragment geometry remains in lines_. */
  struct Extent
  {
    short width, height;
    Extent(short w = 0, short h = 0)
        : width(w),
          height(h)
    {
    }
  };
  loka::app::MeasurementResult<short, Extent> measurement_;
  class WidthSource;
  class FragmentWalker;
  struct FontMeasurement
  {
    loka::app::TextLineMetrics line;
    short maxAdvance;
    FontMeasurement()
        : maxAdvance(1)
    {
    }
  };
  enum Ellipsis
  {
    ELLIPSIS_NONE,
    ELLIPSIS_DRAWN,
    ELLIPSIS_LINE_OMITTED
  };
  /** Completed seat-relative fitting result, consumed only after success. */
  struct FittedLine
  {
    std::size_t paintedEnd;
    int offset;
    int paintedWidth;
    Ellipsis ellipsis;
    std::size_t dotsSpan;
  };
  bool fitLine(const loka::app::TextLineRecord &line,
               short seatWidth,
               const loka::app::BlockStyle &block,
               const ToolboxTextMeasureScope &measure,
               FittedLine &out) const;
  bool reconcileProjection(const loka::app::AttributedString &value);
  std::size_t rangeEnd(std::size_t start, std::size_t end, std::size_t span) const;
  loka::app::detail::TextMeasureTable<char> bytes_;
  loka::app::detail::TextMeasureTable<int> advances_;
  loka::app::detail::TextMeasureTable<loka::app::TextBreakCharacter> characters_;
  loka::app::detail::TextMeasureTable<loka::app::TextStyleSpan> spans_;
  loka::app::detail::TextMeasureTable<ToolboxTextFontDescriptor> fonts_;
  loka::app::detail::TextMeasureTable<FontMeasurement> metrics_;
  loka::app::TextLineBreaker *lines_;
  loka::app::AttributedString snapshot_;
  ToolboxAttributedTextTable(const ToolboxAttributedTextTable &);
  ToolboxAttributedTextTable &operator=(const ToolboxAttributedTextTable &);
};
#endif
