#include "ToolboxPaintGround.hpp"
#include "app/layout/AlignedLineOffset.hpp"
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
  if (!this->bytes_.valid() || !value.valid() || this->snapshot_.segmentCount() != value.segmentCount())
    return false;
  for (std::size_t s = 0; s < value.segmentCount(); ++s)
  {
    const AttributedString::Segment &a = this->snapshot_.segment(s);
    const AttributedString::Segment &b = value.segment(s);
    if (&a == &b)
      return true; // Shared immutable storage owns every segment.
    if (a.style != b.style || !a.text.equals(b.text))
      return false;
  }
  return true;
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
  // nonempty/equal-style coalescing rule (the synthetic rail keeps its own
  // decoder in SyntheticTextWidthSource). Neither pass joins input segments.
  const loka::app::TextStyle *previous = 0;
  for (std::size_t s = 0; s < value.segmentCount(); ++s)
  {
    const AttributedString::Segment &segment = value.segment(s);
    std::string utf8;
    if (!loka::platform::CollectUtf8(segment.text, utf8))
      return false;
    if (utf8.empty())
      continue;
    if (!previous || *previous != segment.style)
      ++spanCount;
    previous = &segment.style;
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
  std::size_t character = 0, offset = 0, span = 0;
  previous = 0;
  for (std::size_t s = 0; s < value.segmentCount(); ++s)
  {
    const AttributedString::Segment &segment = value.segment(s);
    std::string utf8;
    if (!loka::platform::CollectUtf8(segment.text, utf8))
      return false;
    if (utf8.empty())
      continue;
    if (!previous || *previous != segment.style)
    {
      if (previous)
        ++span;
      TextStyleSpan &descriptor = this->spans_[span];
      descriptor.segment = s;
      descriptor.start = character;
      descriptor.style = segment.style;
      this->fonts_[span] = ToolboxTextFontDescriptor(segment.style);
    }
    previous = &segment.style;
    for (std::size_t input = 0; input < utf8.size();)
    {
      const ToolboxTextUnit unit = ToolboxNextTextUnit(utf8.data() + input, utf8.size() - input, roman);
      TextBreakCharacter &row = this->characters_[character++];
      row.value = static_cast<unsigned int>(unit.scalar);
      row.span = span;
      row.offset = offset;
      this->bytes_[offset++] = static_cast<char>(unit.native);
      row.end = offset;
      input += unit.consumed;
    }
    this->spans_[span].end = character;
  }
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
  const WidthSource source(*this, measure);
  int y = top;
  for (std::size_t i = 0; i < this->lines_->lineCount(); ++i)
  {
    const TextLineRecord &line = this->lines_->line(i);
    const bool ellipsis = (!block.hasWrap_ || block.wrap_ == TEXT_WRAP_NONE) && block.hasTruncation_
                          && block.truncation_ == TEXT_TRUNCATION_ELLIPSIS && availableWidth > 0
                          && line.width > availableWidth && line.fragmentCount > 0;
    const std::size_t lastSpan =
        line.fragmentCount ? this->lines_->fragment(line.firstFragment + line.fragmentCount - 1).span : 0;
    int budget = availableWidth;
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
    std::size_t paintedEnd = line.fragmentCount
        ? this->lines_->fragment(line.firstFragment + line.fragmentCount - 1).end : 0;
    int paintedWidth = line.width;
    if (ellipsis)
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
    int cursor = x + AlignedLineOffset(availableWidth, paintedWidth,
        block.hasAlign_ ? block.align_ : TEXT_ALIGN_LEFT);
    for (std::size_t f = 0; f < line.fragmentCount; ++f)
    {
      const TextFragment &fragment = this->lines_->fragment(line.firstFragment + f);
      const std::size_t end = fragment.end < paintedEnd ? fragment.end : paintedEnd;
      measure.select(this->fonts_[fragment.span]);
      loka::toolbox::ToolboxTextInkScope ink(
          loka::toolbox::ToolboxTextInk(this->spans_[fragment.span].style.color_, SURFACE_GROUND_WINDOW));
      std::size_t start = fragment.start;
      while (start < end)
      {
        const std::size_t next = this->rangeEnd(start, end, fragment.span);
        int width = fragment.width;
        if (next == start)
          return false;
        if ((start != fragment.start || next != fragment.end) && !source.width(start, next, fragment.span, width))
          return false;
        MoveTo(Coordinate(cursor), Coordinate(y + line.metrics.ascent));
        DrawText(&this->bytes_[start], 0, static_cast<short>(next - start));
        cursor = cursor > INT_MAX - width ? INT_MAX : cursor + width;
        start = next;
      }
      if (end != fragment.end)
        break;
    }
    if (ellipsis)
    {
      measure.select(this->fonts_[lastSpan]);
      MoveTo(Coordinate(cursor), Coordinate(y + line.metrics.ascent));
      loka::toolbox::ToolboxTextInkScope ink(
          loka::toolbox::ToolboxTextInk(this->spans_[lastSpan].style.color_, SURFACE_GROUND_WINDOW));
      DrawText("...", 0, 3);
    }
    y = Coordinate(y + line.metrics.ascent + line.metrics.descent + line.metrics.leading);
  }
  return true;
}
