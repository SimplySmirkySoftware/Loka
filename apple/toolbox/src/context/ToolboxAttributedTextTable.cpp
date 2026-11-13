#include "ToolboxPaintGround.hpp"
#include "app/layout/AlignedLineOffset.hpp"
#include "app/layout/TextSpanBreaks.hpp"
#include "context/ToolboxAttributedTextTable.hpp"
#include "platform/StringUTF8.hpp"
#include "platform/ToolboxPascalText.hpp"
#include <Script.h>
#include <climits>

using namespace loka::app;
namespace
{
  loka::core::LokaAllocationSite BreakSite()
  {
    return loka::core::LokaAllocationSite("ToolboxAttributedText", "Break");
  }
  short Coordinate(int value)
  {
    return static_cast<short>(value > SHRT_MAX ? SHRT_MAX : value < SHRT_MIN ? SHRT_MIN : value);
  }
} // namespace

/** Borrowed only during build's single port transaction. */
class ToolboxAttributedTextTable::WidthSource : public TextWidthSource
{
public:
  WidthSource(const ToolboxAttributedTextTable &table, const ToolboxTextMeasureScope &measure)
      : table_(table),
        measure_(measure)
  {
  }
  virtual bool valid() const
  {
    return this->table_.bytes_.valid();
  }
  virtual std::size_t length() const
  {
    return this->table_.characters_.size();
  }
  virtual const TextBreakCharacter &character(std::size_t i) const
  {
    return this->table_.characters_[i];
  }
  virtual std::size_t spanCount() const
  {
    return this->table_.spans_.size();
  }
  virtual const TextStyleSpan &span(std::size_t i) const
  {
    return this->table_.spans_[i];
  }
  virtual bool width(std::size_t start, std::size_t end, std::size_t span, int &out) const
  {
    if (start > end || end > this->table_.bytes_.size())
      return false;
    if (span >= this->spanCount())
      return false;
    out = this->table_.advances_[end] - this->table_.advances_[start];
    return true;
  }
  virtual TextLineMetrics metrics(const loka::app::TextStyle &style) const
  {
    for (std::size_t i = 0; i < this->spanCount(); ++i)
      if (this->measure_.sameFont(this->table_.fonts_[i], ToolboxTextFontDescriptor(style)))
        return this->table_.metrics_[i].line;
    this->measure_.select(ToolboxTextFontDescriptor(style));
    FontInfo info;
    GetFontInfo(&info);
    return TextLineMetrics(info.ascent, info.descent, info.leading);
  }

private:
  const ToolboxAttributedTextTable &table_;
  const ToolboxTextMeasureScope &measure_;
};

ToolboxAttributedTextTable::ToolboxAttributedTextTable()
    : lines_(0)
{
}
ToolboxAttributedTextTable::~ToolboxAttributedTextTable()
{
  this->clear();
}
void ToolboxAttributedTextTable::invalidateGeometry()
{
  this->measurement_.invalidate();
  loka::core::LokaDelete(this->lines_, BreakSite());
  this->lines_ = 0;
  this->advances_.clear();
  this->metrics_.clear();
}
void ToolboxAttributedTextTable::clear()
{
  this->invalidateGeometry();
  this->snapshot_ = AttributedString();
  this->bytes_.clear();
  this->characters_.clear();
  this->spans_.clear();
  this->fonts_.clear();
}

bool ToolboxAttributedTextTable::matches(const AttributedString &value) const
{
  return this->bytes_.valid() && this->snapshot_.hasSameSegmentValues(value);
}

bool ToolboxAttributedTextTable::reconcileProjection(const AttributedString &value)
{
  if (this->matches(value))
    return true;
  this->clear();
  if (!value.valid())
    return false;
  const bool roman = GetScriptManagerVariable(smSysScript) == smRoman;
  std::size_t count = 0, spanCount = 0;
  // Sizing and filling below use the same segment-local traversal and
  // nonempty span predicate (the synthetic rail keeps its own
  // decoder in SyntheticTextWidthSource). Neither pass joins input segments.
  const AttributedString::Segment *previous = 0;
  for (std::size_t s = 0; s < value.segmentCount(); ++s)
  {
    const AttributedString::Segment &segment = value.segment(s);
    std::string utf8;
    if (!loka::platform::CollectUtf8(segment.text, utf8))
      return false;
    if (utf8.empty())
      continue;
    if (TextSpanBreaks(previous, segment))
      ++spanCount;
    previous = &segment;
    for (std::size_t input = 0; input < utf8.size(); ++count)
    {
      if (count == (std::numeric_limits<std::size_t>::max)() - 1)
        return false;
      input += ToolboxNextTextUnit(utf8.data() + input, utf8.size() - input, roman).consumed;
    }
  }
  // T1 emits one native byte per strict unit. Character endpoints below are
  // the seam for variable-length native units; consumers never decode bytes.
  if (!this->bytes_.allocate(count) || !this->characters_.allocate(count)
      || !this->spans_.allocate(spanCount) || !this->fonts_.allocate(spanCount))
    return false;
  std::size_t character = 0, offset = 0, filledSpans = 0;
  previous = 0;
  for (std::size_t s = 0; s < value.segmentCount(); ++s)
  {
    const AttributedString::Segment &segment = value.segment(s);
    std::string utf8;
    if (!loka::platform::CollectUtf8(segment.text, utf8))
      return false;
    if (utf8.empty())
      continue;
    const bool starts = TextSpanBreaks(previous, segment);
    const std::size_t span = starts ? filledSpans : filledSpans - 1;
    // Fonts share the descriptor count; this also covers continuation end.
    if (span >= this->spans_.size())
      return false;
    if (starts)
    {
      TextStyleSpan &descriptor = this->spans_[span];
      descriptor.source = TextSegmentRange(s);
      descriptor.start = character;
      descriptor.style = segment.style;
      this->fonts_[span] = ToolboxTextFontDescriptor(segment.style);
      ++filledSpans;
    }
    for (std::size_t input = 0; input < utf8.size();)
    {
      const ToolboxTextUnit unit = ToolboxNextTextUnit(utf8.data() + input, utf8.size() - input, roman);
      if (character >= this->characters_.size())
        return false;
      TextBreakCharacter &row = this->characters_[character++];
      row.value = static_cast<unsigned int>(unit.scalar);
      row.span = span;
      row.offset = offset;
      this->bytes_[offset++] = static_cast<char>(unit.native);
      row.end = offset;
      input += unit.consumed;
    }
    // Deliberate contributor-range twin: SyntheticTextWidthSource::append in common/app/layout/TextLineBreaker.cpp.
    this->spans_[span].end = character;
    this->spans_[span].source = this->spans_[span].source.through(s);
    previous = &segment;
  }
  if (character != this->characters_.size() || filledSpans != this->spans_.size())
    return false;
  this->snapshot_ = value;
  return true;
}

bool ToolboxAttributedTextTable::build(const AttributedString &value,
                                       const BlockStyle &block,
                                       short width,
                                       const ToolboxScenePlatformController &controller)
{
  if (!this->reconcileProjection(value))
  {
    this->clear();
    return false;
  }
  this->invalidateGeometry();
  const std::size_t count = this->bytes_.size();
  detail::TextMeasureTable<short> positions;
  if (!positions.allocate((count < SHRT_MAX ? count : SHRT_MAX) + 1)
      || !this->advances_.allocate(count + 1) || !this->metrics_.allocate(this->spans_.size()))
  {
    this->clear();
    return false;
  }
  ToolboxTextMeasureScope measure(controller, this->spans_.size() ? &this->fonts_[0] : 0, this->spans_.size());
  if (!measure.valid())
  {
    this->clear();
    return false;
  }
  this->advances_[0] = 0;
  for (std::size_t i = 0; i < this->spans_.size(); ++i)
  {
    measure.select(this->fonts_[i]);
    std::size_t previous = 0;
    while (previous < i && !measure.sameFont(this->fonts_[previous], this->fonts_[i]))
      ++previous;
    if (previous < i)
      this->metrics_[i] = this->metrics_[previous];
    else
    {
      FontInfo info;
      GetFontInfo(&info);
      this->metrics_[i].line = TextLineMetrics(info.ascent, info.descent, info.leading);
      this->metrics_[i].maxAdvance = info.widMax > 0 ? info.widMax : 1;
    }
    std::size_t start = this->characters_[this->spans_[i].start].offset;
    const std::size_t end = this->characters_[this->spans_[i].end - 1].end;
    while (start < end)
    {
      const std::size_t next = this->rangeEnd(start, end, i);
      if (next == start)
      {
        this->clear();
        return false;
      }
      // MeasureText returns signed-short positions including the final edge.
      // Native batches bound both byte count and pixels; probes use only the
      // completed wide prefix, so WORD lookahead never re-enters Font Manager.
      MeasureText(static_cast<short>(next - start), &this->bytes_[start], &positions[0]);
      const int base = this->advances_[start];
      for (std::size_t byte = 1; byte <= next - start; ++byte)
      {
        if (positions[byte] < 0 || positions[byte] > INT_MAX - base)
        {
          this->clear();
          return false;
        }
        this->advances_[start + byte] = base + positions[byte];
      }
      start = next;
    }
  }
  const WidthSource source(*this, measure);
  this->lines_ = loka::core::LokaNew<TextLineBreaker>(BreakSite(), source, block, width);
  if (!this->lines_ || !this->lines_->valid())
  {
    this->clear();
    return false;
  }
  this->snapshot_ = value;
  int height = 0;
  for (std::size_t i = 0; i < this->lines_->lineCount(); ++i)
  {
    const TextLineMetrics &metrics = this->lines_->line(i).metrics;
    height = Coordinate(height + metrics.ascent + metrics.descent + metrics.leading);
  }
  this->measurement_.commit(width, Extent(static_cast<short>(this->lines_->width()), static_cast<short>(height)));
  return true;
}

std::size_t ToolboxAttributedTextTable::rangeEnd(std::size_t start, std::size_t end, std::size_t span) const
{
  // Both native count and native pixel result are signed shorts. WORD lookahead
  // may exceed either even though its eventual wrapped fragments are small.
  const std::size_t limit = SHRT_MAX / this->metrics_[span].maxAdvance;
  if (end - start <= limit)
    return end;
  const std::size_t bound = start + limit;
  // Search recorded native endpoints, never UTF-8 continuation bits. This
  // remains character-safe when the projection acquires multibyte units.
  std::size_t low = 0, high = this->characters_.size();
  while (low < high)
  {
    const std::size_t mid = low + (high - low) / 2;
    if (this->characters_[mid].end <= bound)
      low = mid + 1;
    else
      high = mid;
  }
  return low && this->characters_[low - 1].end > start ? this->characters_[low - 1].end : start;
}

// Twin: win32/src/context/Win32AttributedTextTable.cpp fitLine.
// Toolbox uses bytes/TextWidth, omits narrower-than-dots lines, searches from
// a lower bound, and fails on probe refusal; Win32 uses UTF-16/GDI, clips those
// dots, scans from zero, and treats probe refusal as a cutoff.
bool ToolboxAttributedTextTable::fitLine(const TextLineRecord &line,
                                         short seatWidth,
                                         const BlockStyle &block,
                                         const ToolboxTextMeasureScope &measure,
                                         FittedLine &out) const
{
  const WidthSource source(*this, measure);
  const bool ellipsis = (!block.hasWrap_ || block.wrap_ == TEXT_WRAP_NONE) && block.hasTruncation_
                        && block.truncation_ == TEXT_TRUNCATION_ELLIPSIS && seatWidth > 0 && line.width > seatWidth
                        && line.fragmentCount > 0;
  const std::size_t lastSpan =
      line.fragmentCount ? this->lines_->fragment(line.firstFragment + line.fragmentCount - 1).span : 0;
  int budget = seatWidth;
  int dotsWidth = 0;
  if (ellipsis)
  {
    // Null defines overflow/extent, but no glyph descriptor. Follow Text's
    // measured three-dot prefix rule, with the terminal run's face for dots.
    measure.select(this->fonts_[lastSpan]);
    dotsWidth = TextWidth("...", 0, 3);
    budget -= dotsWidth;
    if (budget < 0)
      budget = 0;
  }
  // Twin: ToolboxTextContext.cpp PlainDisplay emits nothing when even dots
  // exceed a positive width. Keep this line's advance even when it is empty.
  const bool omitLine = ellipsis && dotsWidth > seatWidth;
  std::size_t paintedEnd =
      line.fragmentCount ? this->lines_->fragment(line.firstFragment + line.fragmentCount - 1).end : 0;
  int paintedWidth = omitLine ? 0 : line.width;
  if (ellipsis && !omitLine)
  {
    paintedWidth = dotsWidth;
    for (std::size_t f = 0; f < line.fragmentCount; ++f)
    {
      const TextFragment &fragment = this->lines_->fragment(line.firstFragment + f);
      std::size_t end = fragment.end;
      if (fragment.width > budget)
      {
        end = fragment.start;
        std::size_t low = 0, high = this->characters_.size();
        while (low < high)
        {
          const std::size_t mid = low + (high - low) / 2;
          if (this->characters_[mid].offset < fragment.start)
            low = mid + 1;
          else
            high = mid;
        }
        for (; low < this->characters_.size() && this->characters_[low].end <= fragment.end; ++low)
        {
          int width = 0;
          if (!source.width(fragment.start, this->characters_[low].end, fragment.span, width))
            return false;
          if (width > budget)
            break;
          end = this->characters_[low].end;
        }
      }
      int advance = 0;
      if (!source.width(fragment.start, end, fragment.span, advance))
        return false;
      paintedWidth += advance;
      budget -= advance;
      paintedEnd = end;
      if (end != fragment.end)
        break;
    }
  }
  // Twin: ToolboxTextContext's plain Text painter aligns its own painted
  // lines. Both use AlignedLineOffset; this path reuses table prefix widths.
  const int offset = AlignedLineOffset(seatWidth, paintedWidth, block.hasAlign_ ? block.align_ : TEXT_ALIGN_LEFT);
  out.paintedEnd = paintedEnd;
  out.offset = offset;
  out.paintedWidth = paintedWidth;
  out.ellipsis = omitLine ? ELLIPSIS_LINE_OMITTED : ellipsis ? ELLIPSIS_DRAWN : ELLIPSIS_NONE;
  out.dotsSpan = lastSpan;
  return true;
}

/** Stack-local nested walk. The consumer owns ink scopes; this walk owns all
    line, fragment and batch geometry, including entry into empty cut fragments. */
class ToolboxAttributedTextTable::FragmentWalker
{
public:
  FragmentWalker(const ToolboxAttributedTextTable &table, const ToolboxTextMeasureScope &measure)
      : table_(table),
        measure_(measure)
  {
  }

  class Line
  {
  public:
    Line(FragmentWalker &walk, const TextLineRecord &line, const FittedLine &fitted, int x, int top)
        : walk_(walk),
          line_(line),
          fitted_(fitted),
          cursor_(x + fitted.offset),
          top_(top),
          fragmentIndex_(0)
    {
    }
    short baseline() const
    {
      return Coordinate(this->top_ + this->line_.metrics.ascent);
    }
    int nextTop() const
    {
      return Coordinate(this->top_ + this->line_.metrics.ascent + this->line_.metrics.descent
                        + this->line_.metrics.leading);
    }
    bool containsY(Point point) const
    {
      return point.v >= this->top_
             && static_cast<int>(point.v) - this->top_ < this->line_.metrics.ascent + this->line_.metrics.descent;
    }
    bool nextFragment(const TextFragment *&out)
    {
      if (this->fitted_.ellipsis == ELLIPSIS_LINE_OMITTED || this->fragmentIndex_ >= this->line_.fragmentCount)
        return false;
      if (this->fragmentIndex_
          && this->walk_.table_.lines_->fragment(this->line_.firstFragment + this->fragmentIndex_ - 1).end
                 > this->fitted_.paintedEnd)
        return false;
      out = &this->walk_.table_.lines_->fragment(this->line_.firstFragment + this->fragmentIndex_++);
      return true;
    }
    class Fragment
    {
    public:
      Fragment(Line &line, const TextFragment &fragment)
          : line_(line),
            fragment_(fragment),
            end_(fragment.end < line.fitted_.paintedEnd ? fragment.end : line.fitted_.paintedEnd),
            start_(fragment.start)
      {
        this->line_.walk_.measure_.select(this->line_.walk_.table_.fonts_[fragment.span]);
      }
      bool pending() const
      {
        return this->start_ < this->end_;
      }
      struct Batch
      {
        std::size_t start, next;
        int x, width;
      };
      bool next(Batch &out)
      {
        const ToolboxAttributedTextTable &table = this->line_.walk_.table_;
        const std::size_t next = table.rangeEnd(this->start_, this->end_, this->fragment_.span);
        int width = this->fragment_.width;
        if (next == this->start_)
          return false;
        const WidthSource source(table, this->line_.walk_.measure_);
        if ((this->start_ != this->fragment_.start || next != this->fragment_.end)
            && !source.width(this->start_, next, this->fragment_.span, width))
          return false;
        const Batch batch = {this->start_, next, this->line_.cursor_, width};
        out = batch;
        this->line_.cursor_ = this->line_.cursor_ > INT_MAX - width ? INT_MAX : this->line_.cursor_ + width;
        this->start_ = next;
        return true;
      }

    private:
      Line &line_;
      const TextFragment &fragment_;
      const std::size_t end_;
      std::size_t start_;
    };
    int cursor() const
    {
      return this->cursor_;
    }

  private:
    FragmentWalker &walk_;
    const TextLineRecord &line_;
    const FittedLine &fitted_;
    int cursor_;
    const int top_;
    std::size_t fragmentIndex_;
  };

private:
  const ToolboxAttributedTextTable &table_;
  const ToolboxTextMeasureScope &measure_;
};

bool ToolboxAttributedTextTable::draw(short x,
                                      short top,
                                      const ToolboxScenePlatformController &controller,
                                      const BlockStyle &block,
                                      short availableWidth) const
{
#ifdef TEST_BUILD
  if (loka::testing::refuseToolboxAttributedDraw())
    return false;
#endif
  if (!this->valid())
    return false;
  ToolboxTextMeasureScope measure(controller, this->fonts_.size() ? &this->fonts_[0] : 0, this->fonts_.size());
  FragmentWalker walk(*this, measure);
  int y = top;
  for (std::size_t i = 0; i < this->lines_->lineCount(); ++i)
  {
    const TextLineRecord &line = this->lines_->line(i);
    FittedLine fitted;
    if (!this->fitLine(line, availableWidth, block, measure, fitted))
      return false;
    FragmentWalker::Line row(walk, line, fitted, x, y);
    const TextFragment *nextFragment = 0;
    while (row.nextFragment(nextFragment))
    {
      const TextFragment &fragment = *nextFragment;
      FragmentWalker::Line::Fragment fragments(row, fragment);
      loka::toolbox::ToolboxTextInkScope ink(
          loka::toolbox::ToolboxTextInk(this->spans_[fragment.span].style.color_, SURFACE_GROUND_WINDOW));
      while (fragments.pending())
      {
        FragmentWalker::Line::Fragment::Batch batch;
        if (!fragments.next(batch))
          return false;
        MoveTo(Coordinate(batch.x), row.baseline());
        DrawText(&this->bytes_[batch.start], 0, static_cast<short>(batch.next - batch.start));
      }
    }
    if (fitted.ellipsis == ELLIPSIS_DRAWN)
    {
      measure.select(this->fonts_[fitted.dotsSpan]);
      MoveTo(Coordinate(row.cursor()), row.baseline());
      loka::toolbox::ToolboxTextInkScope ink(
          loka::toolbox::ToolboxTextInk(this->spans_[fitted.dotsSpan].style.color_, SURFACE_GROUND_WINDOW));
      DrawText("...", 0, 3);
    }
    y = row.nextTop();
  }
  return true;
}

ToolboxHitAnswer ToolboxAttributedTextTable::hitAt(const Rect &seat,
                                                   const ToolboxScenePlatformController &controller,
                                                   const BlockStyle &block,
                                                   Point local,
                                                   ToolboxLinkHit &out) const
{
  if (!this->valid())
    return TOOLBOX_HIT_REFUSED;
  if (!PtInRect(local, &seat))
    return TOOLBOX_HIT_NONE;
  ToolboxTextMeasureScope measure(controller, this->fonts_.size() ? &this->fonts_[0] : 0, this->fonts_.size());
  FragmentWalker walk(*this, measure);
  int y = seat.top;
  for (std::size_t i = 0; i < this->lines_->lineCount(); ++i)
  {
    const TextLineRecord &line = this->lines_->line(i);
    FittedLine fitted;
    if (!this->fitLine(line, static_cast<short>(seat.right - seat.left), block, measure, fitted))
      return TOOLBOX_HIT_REFUSED;
    FragmentWalker::Line row(walk, line, fitted, seat.left, y);
    const TextFragment *nextFragment = 0;
    while (row.nextFragment(nextFragment))
    {
      const TextFragment &fragment = *nextFragment;
      FragmentWalker::Line::Fragment fragments(row, fragment);
      while (fragments.pending())
      {
        FragmentWalker::Line::Fragment::Batch batch;
        if (!fragments.next(batch))
          return TOOLBOX_HIT_REFUSED;
        if (row.containsY(local) && local.h >= batch.x && static_cast<int>(local.h) - batch.x < batch.width)
        {
          const TextSegmentRange &range = this->spans_[fragment.span].source;
          if (this->snapshot_.segment(range.first()).link.isNone())
            return TOOLBOX_HIT_NONE;
          const ToolboxLinkHit hit = {range.first(), range.last(), seat};
          out = hit;
          return TOOLBOX_HIT_LINK;
        }
      }
    }
    y = row.nextTop();
    if (local.v < y)
      return TOOLBOX_HIT_NONE;
  }
  return TOOLBOX_HIT_NONE;
}
